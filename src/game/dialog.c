/*==============================================================================
 * Dialog Engine - Phase 16
 *
 * Manages story dialog presentation using BG1 text overlay (4bpp font).
 * Transition pattern: init BG1 font, reload BG1 game tiles via bgLoadZone
 * when done.
 *
 * Typewriter text reveal at 2 frames/char with A-button fast-fill.
 * Blinking ">" prompt when page is fully revealed.
 * A-button advances to next page; auto-closes after last page.
 *
 * The dialog engine temporarily takes over BG1 for text display.  During
 * the transition-in, all flight systems are halted (scroll stopped, bullets
 * cleared, enemies killed, player hidden) and BG1 is reconfigured with the
 * PVSnesLib 4bpp font.  On close, bgLoadZone() restores the game's BG1
 * background tiles and the flight resumes.
 *
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#include "game/dialog.h"
#include "assets.h"
#include "engine/input.h"
#include "engine/fade.h"
#include "engine/sound.h"
#include "engine/scroll.h"
#include "engine/sprites.h"
#include "engine/background.h"
#include "engine/vblank.h"
#include "game/player.h"
#include "game/enemies.h"
#include "engine/bullets.h"

/*=== Pending dialog trigger (set by scroll callbacks in story.c) ===*/
/* When a scroll trigger fires, it sets this pointer to the desired script.
 * The main loop checks this each frame during STATE_FLIGHT and transitions
 * to STATE_DIALOG if non-NULL. */
DialogScript *g_dialog_pending;

/*=== Speaker Name Strings ===*/
/* Indexed by SPEAKER_* constants.  Displayed in brackets above the text area
 * (e.g., "[COMMANDER]").  Must be non-const for PVSnesLib compatibility. */
static char *speaker_names[6] = {
    "",           /* SPEAKER_NONE: no name shown */
    "VEX",        /* SPEAKER_VEX: player character */
    "COMMANDER",  /* SPEAKER_COMMANDER: mission control (ally) */
    "ENGINEER",   /* SPEAKER_ENGINEER: ship's engineer (ally) */
    "ENEMY",      /* SPEAKER_ENEMY: hostile alien speaker */
    "SYSTEM"      /* SPEAKER_SYSTEM: narrator / system message */
};

/*=== Internal State Machine Variables ===*/
static u8 s_state;              /* Current DSTATE_* value */
static DialogScript *s_script;  /* Pointer to the script currently being played */
static u8 s_page;               /* Index of the current page within the script */
static u8 s_char_pos;           /* Number of characters revealed so far (across both lines) */
static u8 s_type_timer;         /* Countdown timer for typewriter: ticks down each frame */
static u8 s_total_chars;        /* Total characters on the current page (line1 + line2) */
static u8 s_line1_len;          /* Character count of the current page's top line */
static u8 s_line2_len;          /* Character count of the current page's bottom line */
static u8 s_prompt_blink;       /* Frame counter for the blinking ">" prompt animation */

/*=== Blank line for clearing text rows ===*/
/* 26 spaces matches DLG_LINE_MAX, used to overwrite a full row of text */
static char s_blank[] = "                          ";  /* 26 spaces */

/*===========================================================================*/
/* Internal Helpers                                                          */
/*===========================================================================*/

/*
 * strLen - Calculate string length with safety bounds.
 *
 * Walks the string using a pointer rather than indexing to avoid
 * redundant base+offset address calculations on the 65816.
 * Clamps to DLG_LINE_MAX (26) to prevent buffer overruns if a
 * script string is accidentally too long.
 *
 * s:       Pointer to the string (may be NULL).
 * Returns: Length in characters (0 if NULL or empty).
 */
static u8 strLen(char *s)
{
    char *p;
    if (s == 0) return 0;
    p = s;
    while (*p != 0 && (u8)(p - s) < DLG_LINE_MAX) {
        p++;
    }
    return (u8)(p - s);
}

/*
 * dlgDrawBox - Draw the dialog box frame borders on BG1.
 *
 * Renders a top border at row 19, bottom border at row 24, and clears
 * the four interior rows (20-23) for the speaker name and text content.
 * The borders are simple dash characters spanning the full 30-tile width.
 */
static void dlgDrawBox(void)
{
    /* Top border: row 19 (just above the speaker name area) */
    consoleDrawText(0, DLG_BOX_TOP,
        "------------------------------");

    /* Bottom border: row 24 (below the text area) */
    consoleDrawText(0, DLG_BOX_BOTTOM,
        "------------------------------");

    /* Clear interior rows: speaker name (20), text line 1 (21),
     * text line 2 (22), and an extra row (23) for visual padding */
    consoleDrawText(DLG_TEXT_COL, DLG_NAME_ROW, s_blank);
    consoleDrawText(DLG_TEXT_COL, DLG_TEXT_ROW1, s_blank);
    consoleDrawText(DLG_TEXT_COL, DLG_TEXT_ROW2, s_blank);
    consoleDrawText(DLG_TEXT_COL, 23, s_blank);
}

/*
 * dlgDrawSpeaker - Render the speaker name label for the current page.
 *
 * Formats the name in brackets (e.g., "[COMMANDER]") and draws it on
 * the name row above the text area.  If the speaker is SPEAKER_NONE,
 * the row is simply cleared.  The speaker ID is clamped to prevent
 * out-of-bounds access into the speaker_names array.
 */
static void dlgDrawSpeaker(void)
{
    u8 spk;
    char name_buf[16];  /* Enough for "[" + 12-char name + "]" + null */
    u8 len;
    u8 k;

    spk = s_script->lines[s_page].speaker;
    if (spk > SPEAKER_SYSTEM) spk = SPEAKER_NONE;  /* Bounds clamp */

    /* Clear the name row first to remove the previous speaker's name */
    consoleDrawText(DLG_TEXT_COL, DLG_NAME_ROW, s_blank);

    if (spk != SPEAKER_NONE) {
        /* Build "[SPEAKER_NAME]" string in a local buffer */
        len = 0;
        name_buf[len++] = '[';
        for (k = 0; speaker_names[spk][k] != 0 && k < 12; k++) {
            name_buf[len++] = speaker_names[spk][k];
        }
        name_buf[len++] = ']';
        name_buf[len] = 0;  /* Null terminate */
        consoleDrawText(DLG_TEXT_COL, DLG_NAME_ROW, name_buf);
    }
}

/*
 * dlgStartPage - Initialize a new page for typewriter reveal.
 *
 * Computes the lengths of both text lines, resets the reveal position
 * to zero, clears the text area, draws the speaker name, and sets the
 * engine state to DSTATE_TYPING to begin the typewriter effect.
 */
static void dlgStartPage(void)
{
    DialogLine *line;

    line = &s_script->lines[s_page];

    /* Compute character counts for the current page's two lines */
    s_line1_len = strLen(line->line1);
    s_line2_len = strLen(line->line2);
    s_total_chars = s_line1_len + s_line2_len;
    s_char_pos = 0;     /* No characters revealed yet */
    s_type_timer = 0;   /* Fire immediately on first frame */
    s_prompt_blink = 0; /* Reset prompt animation */

    /* Clear the two text rows to remove previous page content */
    consoleDrawText(DLG_TEXT_COL, DLG_TEXT_ROW1, s_blank);
    consoleDrawText(DLG_TEXT_COL, DLG_TEXT_ROW2, s_blank);
    /* Clear the advance prompt area */
    consoleDrawText(DLG_PROMPT_COL, DLG_PROMPT_ROW, " ");

    /* Draw speaker name for this page */
    dlgDrawSpeaker();

    s_state = DSTATE_TYPING;
}

/*
 * dlgRevealText - Draw the single newly-revealed character.
 *
 * This is an O(1)-per-frame optimization: instead of redrawing all
 * revealed text each frame (O(n) where n is the number of revealed
 * characters), we only draw the one character at position s_char_pos.
 *
 * Characters 1..s_line1_len go on text row 1; characters beyond that
 * go on text row 2.  The function determines which line and column
 * based on s_char_pos and draws a single-character string.
 *
 * dl: Pointer to the current DialogLine (for line1/line2 strings).
 */
static void dlgRevealText(const DialogLine *dl)
{
    char ch[2];  /* Single character + null terminator */
    u8 col;
    u8 idx;

    if (s_char_pos == 0) return;  /* Nothing to reveal yet */

    /* Determine which character to draw based on current position */
    if (s_char_pos <= s_line1_len) {
        /* Character is on the first text line */
        if (dl->line1 == 0) return;
        ch[0] = dl->line1[s_char_pos - 1];  /* s_char_pos is 1-based */
        col = DLG_TEXT_COL + s_char_pos - 1;
        ch[1] = 0;
        consoleDrawText(col, DLG_TEXT_ROW1, ch);
    } else {
        /* Character is on the second text line */
        idx = s_char_pos - s_line1_len - 1;
        if (dl->line2 == 0 || idx >= s_line2_len) return;
        ch[0] = dl->line2[idx];
        col = DLG_TEXT_COL + idx;
        ch[1] = 0;
        consoleDrawText(col, DLG_TEXT_ROW2, ch);
    }
}

/*
 * dlgFillPage - Instantly reveal all remaining text on the current page.
 *
 * Called when the player presses A during typewriter reveal to skip ahead.
 * Draws both full lines at once using consoleDrawText (which handles
 * entire strings efficiently) and transitions to DSTATE_WAIT.
 */
static void dlgFillPage(void)
{
    DialogLine *line;

    line = &s_script->lines[s_page];

    /* Draw both full text lines if they exist */
    if (line->line1 != 0) {
        consoleDrawText(DLG_TEXT_COL, DLG_TEXT_ROW1, line->line1);
    }
    if (line->line2 != 0) {
        consoleDrawText(DLG_TEXT_COL, DLG_TEXT_ROW2, line->line2);
    }

    /* Mark all characters as revealed and wait for player input */
    s_char_pos = s_total_chars;
    s_state = DSTATE_WAIT;
}

/*===========================================================================*/
/* Transition In: Fade out flight, set up BG1, draw box, fade in            */
/*===========================================================================*/

/*
 * dlgTransitionIn - Blocking transition from flight mode to dialog mode.
 *
 * Sequence:
 *   1. Fade the flight screen to black (15-frame fade)
 *   2. Stop all flight systems: scroll, bullets, enemies, player sprite
 *   3. Enter force-blank for safe VRAM modification
 *   4. Reconfigure BG1 for text display with PVSnesLib 4bpp font
 *   5. Draw the dialog box borders
 *   6. Exit force-blank and fade the dialog screen in (15-frame fade)
 *
 * The flight systems are stopped (not paused) because the dialog may
 * display for an indefinite time and we don't want enemies or bullets
 * drifting off-screen in the background.
 */
static void dlgTransitionIn(void)
{
    /* Fade to black over 15 frames */
    fadeOutBlocking(15);

    /* Halt all flight-mode systems (same shutdown pattern as battle entry) */
    scrollSetSpeed(SCROLL_SPEED_STOP);
    bulletClearAll();
    enemyKillAll();
    playerHide();
    spriteHideAll();

    /* Enter force-blank: PPU output disabled, VRAM writable at any time.
     * Must be in force-blank before modifying character data or tilemaps. */
    setScreenOff();

    /* Reconfigure BG1 for text display.
     * The font is 4bpp and loaded at VRAM_TEXT_GFX ($2000, tile offset
     * 0x100 from the BG1 char base at $1000).  The text tilemap reuses
     * the BG1 map at VRAM_TEXT_MAP ($0000), overwriting the game
     * background tilemap temporarily. */
    consoleSetTextMapPtr(VRAM_TEXT_MAP);
    consoleSetTextGfxPtr(VRAM_TEXT_GFX);
    consoleSetTextOffset(0x0100);
    consoleInitText(0, 16 * 2, &snesfont, &snespal);
    bgSetGfxPtr(0, VRAM_BG1_GFX);
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);

    /* Enable BG1 for text rendering (it was the game BG, now it's text) */
    bgSetEnable(0);

    /* Draw the dialog box frame (borders + clear interior) */
    dlgDrawBox();

    /* Exit force-blank and fade in to show the dialog */
    setScreenOn();
    fadeInBlocking(15);
}

/*===========================================================================*/
/* Transition Out: Fade out, restore BG1, resume flight                     */
/*===========================================================================*/

/*
 * dlgTransitionOut - Blocking transition from dialog mode back to flight.
 *
 * Sequence:
 *   1. Fade the dialog screen to black (15-frame fade)
 *   2. Disable BG1 text layer
 *   3. Reload the zone background via bgLoadZone() which restores BG1
 *      tile data, palette, and tilemap from ROM assets (enters/exits
 *      force-blank internally and re-enables BG1+BG2)
 *   4. Re-show the player sprite
 *   5. Exit force-blank and fade in (15-frame fade)
 *   6. Resume scroll at normal speed
 *   7. Grant 2 seconds of invincibility (120 frames) so the player
 *      isn't immediately hit by enemies that spawn during the transition
 */
static void dlgTransitionOut(void)
{
    /* Fade to black */
    fadeOutBlocking(15);

    /* Disable BG1 text rendering */
    bgSetDisable(0);

    /* Reload zone background to restore BG1 game tiles.
     * bgLoadZone() enters force blank internally, loads tiles/palette/map
     * from ROM, and re-enables BG1+BG2 before returning. */
    bgLoadZone(g_game.current_zone);

    /* Make the player ship sprite visible again */
    playerShow();

    /* Exit force blank and fade in */
    setScreenOn();
    fadeInBlocking(15);

    /* Resume flight: restore zone-appropriate scroll speed and brief invincibility.
     * Zone 3 (Flagship) uses SCROLL_SPEED_FAST for increased intensity. */
    if (g_game.current_zone == ZONE_FLAGSHIP) {
        scrollSetSpeed(SCROLL_SPEED_FAST);
    } else {
        scrollSetSpeed(SCROLL_SPEED_NORMAL);
    }
    g_player.invincible_timer = 120;  /* 2 seconds of post-dialog invincibility */
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

/*
 * dlgInit - Initialize the dialog engine to its idle state.
 * Clears the state machine, script pointer, page counter, and pending trigger.
 * Called once during gsInit() at game startup.
 */
void dlgInit(void)
{
    s_state = DSTATE_INACTIVE;
    s_script = 0;
    s_page = 0;
    g_dialog_pending = 0;
}

/*
 * dlgOpen - Begin playing a dialog script.
 *
 * Validates the script pointer, performs the blocking transition-in
 * (fade out flight, set up BG1 text, draw box, fade in), then starts
 * the typewriter on the first page.
 *
 * script: Pointer to a DialogScript.  Must not be NULL and must have
 *         at least one line.
 */
void dlgOpen(DialogScript *script)
{
    if (script == 0) return;        /* Null guard */
    if (script->line_count == 0) return;  /* Empty script guard */

    s_script = script;
    s_page = 0;

    /* Blocking transition: fades out flight, sets up BG1 text, fades in */
    dlgTransitionIn();

    /* Begin typewriter reveal on the first page */
    dlgStartPage();
}

/*
 * dlgUpdate - Per-frame dialog engine update.
 *
 * Called from the main loop every frame while in STATE_DIALOG.
 * Handles three internal states:
 *
 * DSTATE_TYPING: Typewriter reveal.
 *   - A-button: instantly fills the remaining text (fast-fill).
 *   - Otherwise: advances one character every DLG_TYPE_SPEED frames.
 *   - Plays SFX_DIALOG_BLIP for each character revealed.
 *   - Automatically skips space characters (no delay for whitespace).
 *   - Transitions to DSTATE_WAIT when all characters are revealed.
 *
 * DSTATE_WAIT: Page fully shown, waiting for player input.
 *   - Blinks the ">" prompt every ~32 frames (0x1F mask).
 *   - A-button: advances to next page, or closes if this was the last page.
 *
 * DSTATE_CLOSE: Script finished, perform transition-out.
 *   - Calls dlgTransitionOut() (blocking fade, restore BG1, resume flight).
 *   - Returns to DSTATE_INACTIVE.
 *
 * pad_pressed: Edge-triggered input (ACTION_CONFIRM for A-button).
 * Returns:     1 while dialog is active, 0 when finished and closed.
 */
u8 dlgUpdate(u16 pad_pressed)
{
    if (s_state == DSTATE_INACTIVE) return 0;

    switch (s_state) {

        case DSTATE_TYPING:
            /* A-button: skip typewriter, fill all remaining text instantly */
            if (pad_pressed & ACTION_CONFIRM) {
                dlgFillPage();
                return 1;
            }

            /* Typewriter tick: count frames, reveal one char per DLG_TYPE_SPEED frames */
            s_type_timer++;
            if (s_type_timer >= DLG_TYPE_SPEED) {
                /* Cache the current line pointer to avoid re-dereferencing
                 * the script struct on every character access. */
                DialogLine *dl;
                char ch;
                dl = &s_script->lines[s_page];

                s_type_timer = 0;
                s_char_pos++;
                dlgRevealText(dl);
                soundPlaySFX(SFX_DIALOG_BLIP);  /* Audible tick for each character */

                /* Fast-skip spaces: instantly advance past whitespace so pauses
                 * only occur on visible characters.  This makes the typewriter
                 * feel more natural by not pausing on empty space. */
                while (s_char_pos < s_total_chars) {
                    if (s_char_pos < s_line1_len) {
                        ch = dl->line1[s_char_pos];
                    } else if (dl->line2 != 0) {
                        ch = dl->line2[s_char_pos - s_line1_len];
                    } else {
                        break;
                    }
                    if (ch != ' ') break;  /* Stop at next visible character */
                    s_char_pos++;
                    dlgRevealText(dl);  /* Draw the space (for correct cursor position) */
                }

                /* Check if all characters on this page have been revealed */
                if (s_char_pos >= s_total_chars) {
                    s_state = DSTATE_WAIT;
                }
            }
            return 1;

        case DSTATE_WAIT:
            /* Blink the ">" advance prompt on a ~32-frame cycle.
             * Visible for the first 16 frames (0x00-0x0F), hidden for
             * the next 16 frames (0x10-0x1F).  Uses bitwise AND for
             * efficient modulo on the 65816. */
            s_prompt_blink++;
            if ((s_prompt_blink & 0x1F) < 0x10) {
                consoleDrawText(DLG_PROMPT_COL, DLG_PROMPT_ROW, ">");
            } else {
                consoleDrawText(DLG_PROMPT_COL, DLG_PROMPT_ROW, " ");
            }

            /* A-button: advance to next page or close dialog */
            if (pad_pressed & ACTION_CONFIRM) {
                soundPlaySFX(SFX_MENU_SELECT);
                s_page++;
                if (s_page >= s_script->line_count) {
                    /* All pages shown: initiate close sequence */
                    s_state = DSTATE_CLOSE;
                } else {
                    /* Start typewriter on the next page */
                    dlgStartPage();
                }
            }
            return 1;

        case DSTATE_CLOSE:
            /* Blocking transition back to flight mode.
             * Restores BG1 game tiles, re-shows player, resumes scroll. */
            dlgTransitionOut();
            s_state = DSTATE_INACTIVE;
            s_script = 0;
            return 0;  /* Dialog is finished */
    }

    return 0;
}

/*
 * dlgIsActive - Query whether the dialog engine is currently running.
 * Returns 1 if any dialog state is active (typing, waiting, closing),
 *         0 if the engine is idle (DSTATE_INACTIVE).
 */
u8 dlgIsActive(void)
{
    return (s_state != DSTATE_INACTIVE) ? 1 : 0;
}
