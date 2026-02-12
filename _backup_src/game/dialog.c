/*==============================================================================
 * Dialog Engine - Phase 16
 *
 * Manages story dialog presentation using BG3 text overlay.
 * Same transition pattern as battle: disable BG1, init BG3/font,
 * reload BG1 via bgLoadZone when done.
 *
 * Typewriter text reveal at 2 frames/char with A-button fast-fill.
 * Blinking ">" prompt when page is fully revealed.
 * A-button advances to next page; auto-closes after last page.
 *
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#include "game/dialog.h"
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

/*=== Pending dialog trigger (set by scroll callbacks) ===*/
DialogScript *g_dialog_pending;

/*=== Speaker Name Strings ===*/
static char *speaker_names[6] = {
    "",           /* SPEAKER_NONE */
    "VEX",        /* SPEAKER_VEX */
    "COMMANDER",  /* SPEAKER_COMMANDER */
    "ENGINEER",   /* SPEAKER_ENGINEER */
    "ENEMY",      /* SPEAKER_ENEMY */
    "SYSTEM"      /* SPEAKER_SYSTEM */
};

/*=== Internal State ===*/
static u8 s_state;              /* DSTATE_* */
static DialogScript *s_script;  /* Current script being played */
static u8 s_page;               /* Current line index in script */
static u8 s_char_pos;           /* Characters revealed so far (both lines) */
static u8 s_type_timer;         /* Frames until next character */
static u8 s_total_chars;        /* Total chars in current page */
static u8 s_line1_len;          /* Length of current line1 */
static u8 s_line2_len;          /* Length of current line2 */
static u8 s_prompt_blink;       /* Blink counter for ">" prompt */

/*=== Blank line for clearing text rows ===*/
static char s_blank[] = "                          ";  /* 26 spaces */

/*===========================================================================*/
/* Internal Helpers                                                          */
/*===========================================================================*/

/* Get string length (simple, no stdlib) */
static u8 strLen(char *s)
{
    u8 len;
    len = 0;
    if (s == 0) return 0;
    while (s[len] != 0 && len < DLG_LINE_MAX) {
        len++;
    }
    return len;
}

/* Draw the dialog box frame on BG3 */
static void dlgDrawBox(void)
{
    /* Top border: row 19 */
    consoleDrawText(0, DLG_BOX_TOP,
        "------------------------------");

    /* Bottom border: row 24 */
    consoleDrawText(0, DLG_BOX_BOTTOM,
        "------------------------------");

    /* Clear interior rows 20-23 */
    consoleDrawText(DLG_TEXT_COL, DLG_NAME_ROW, s_blank);
    consoleDrawText(DLG_TEXT_COL, DLG_TEXT_ROW1, s_blank);
    consoleDrawText(DLG_TEXT_COL, DLG_TEXT_ROW2, s_blank);
    consoleDrawText(DLG_TEXT_COL, 23, s_blank);
}

/* Draw the speaker name for the current page */
static void dlgDrawSpeaker(void)
{
    u8 spk;
    spk = s_script->lines[s_page].speaker;
    if (spk > SPEAKER_SYSTEM) spk = SPEAKER_NONE;

    /* Clear name row first */
    consoleDrawText(DLG_TEXT_COL, DLG_NAME_ROW, s_blank);

    if (spk != SPEAKER_NONE) {
        consoleDrawText(DLG_TEXT_COL, DLG_NAME_ROW, speaker_names[spk]);
    }
}

/* Set up a new page for typewriter reveal */
static void dlgStartPage(void)
{
    DialogLine *line;

    line = &s_script->lines[s_page];

    s_line1_len = strLen(line->line1);
    s_line2_len = strLen(line->line2);
    s_total_chars = s_line1_len + s_line2_len;
    s_char_pos = 0;
    s_type_timer = 0;
    s_prompt_blink = 0;

    /* Clear text rows */
    consoleDrawText(DLG_TEXT_COL, DLG_TEXT_ROW1, s_blank);
    consoleDrawText(DLG_TEXT_COL, DLG_TEXT_ROW2, s_blank);
    /* Clear prompt area */
    consoleDrawText(DLG_PROMPT_COL, DLG_PROMPT_ROW, " ");

    /* Draw speaker name */
    dlgDrawSpeaker();

    s_state = DSTATE_TYPING;
}

/* Reveal characters up to s_char_pos using partial string draws */
static void dlgRevealText(void)
{
    DialogLine *line;
    u8 show1;
    u8 show2;
    u8 i;
    /* Temp buffer for partial text: 26 chars + null */
    char buf[28];

    line = &s_script->lines[s_page];

    /* How many chars of line1 to show? */
    show1 = s_char_pos;
    if (show1 > s_line1_len) show1 = s_line1_len;

    /* How many chars of line2 to show? */
    show2 = 0;
    if (s_char_pos > s_line1_len) {
        show2 = s_char_pos - s_line1_len;
        if (show2 > s_line2_len) show2 = s_line2_len;
    }

    /* Draw line1 partial */
    if (show1 > 0 && line->line1 != 0) {
        for (i = 0; i < show1; i++) {
            buf[i] = line->line1[i];
        }
        buf[show1] = 0;
        consoleDrawText(DLG_TEXT_COL, DLG_TEXT_ROW1, buf);
    }

    /* Draw line2 partial */
    if (show2 > 0 && line->line2 != 0) {
        for (i = 0; i < show2; i++) {
            buf[i] = line->line2[i];
        }
        buf[show2] = 0;
        consoleDrawText(DLG_TEXT_COL, DLG_TEXT_ROW2, buf);
    }
}

/* Instantly reveal all text on current page */
static void dlgFillPage(void)
{
    DialogLine *line;

    line = &s_script->lines[s_page];

    if (line->line1 != 0) {
        consoleDrawText(DLG_TEXT_COL, DLG_TEXT_ROW1, line->line1);
    }
    if (line->line2 != 0) {
        consoleDrawText(DLG_TEXT_COL, DLG_TEXT_ROW2, line->line2);
    }

    s_char_pos = s_total_chars;
    s_state = DSTATE_WAIT;
}

/*===========================================================================*/
/* Transition In: Fade out flight, set up BG3, draw box, fade in            */
/*===========================================================================*/

static void dlgTransitionIn(void)
{
    /* Fade to black */
    fadeOutBlocking(15);

    /* Stop flight systems (same pattern as battle) */
    scrollSetSpeed(SCROLL_SPEED_STOP);
    bulletClearAll();
    enemyKillAll();
    playerHide();
    spriteHideAll();

    /* Enter force blank for VRAM operations */
    setScreenOff();

    /* Disable BG1 (tiles will be corrupted by font at 0x3000) */
    bgSetDisable(0);

    /* Initialize BG3 text system (loads font to VRAM 0x3000) */
    consoleInitText(0, BG_4COLORS, 0, 0);

    /* Enable BG3 for text display */
    bgSetEnable(2);

    /* Draw dialog box frame */
    dlgDrawBox();

    /* Exit force blank and fade in */
    setScreenOn();
    fadeInBlocking(15);
}

/*===========================================================================*/
/* Transition Out: Fade out, restore BG1, resume flight                     */
/*===========================================================================*/

static void dlgTransitionOut(void)
{
    /* Fade to black */
    fadeOutBlocking(15);

    /* Disable BG3 text */
    bgSetDisable(2);

    /* Reload zone background to fix BG1 tiles corrupted by font.
     * bgLoadZone enters force blank internally and re-enables BG1+BG2. */
    bgLoadZone(g_game.current_zone);

    /* Show player again */
    playerShow();

    /* Exit force blank and fade in */
    setScreenOn();
    fadeInBlocking(15);

    /* Resume flight */
    scrollSetSpeed(SCROLL_SPEED_NORMAL);
    g_player.invincible_timer = 120;
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

void dlgInit(void)
{
    s_state = DSTATE_INACTIVE;
    s_script = 0;
    s_page = 0;
    g_dialog_pending = 0;
}

void dlgOpen(DialogScript *script)
{
    if (script == 0) return;
    if (script->line_count == 0) return;

    s_script = script;
    s_page = 0;

    /* Blocking transition into dialog screen */
    dlgTransitionIn();

    /* Start first page */
    dlgStartPage();
}

u8 dlgUpdate(u16 pad_pressed)
{
    if (s_state == DSTATE_INACTIVE) return 0;

    switch (s_state) {

        case DSTATE_TYPING:
            /* A-button: fast-fill remaining text */
            if (pad_pressed & ACTION_CONFIRM) {
                dlgFillPage();
                return 1;
            }

            /* Typewriter tick */
            s_type_timer++;
            if (s_type_timer >= DLG_TYPE_SPEED) {
                s_type_timer = 0;
                s_char_pos++;
                dlgRevealText();
                soundPlaySFX(SFX_DIALOG_BLIP);

                /* Check if all chars revealed */
                if (s_char_pos >= s_total_chars) {
                    s_state = DSTATE_WAIT;
                }
            }
            return 1;

        case DSTATE_WAIT:
            /* Blink ">" prompt every ~32 frames */
            s_prompt_blink++;
            if ((s_prompt_blink & 0x1F) < 0x10) {
                consoleDrawText(DLG_PROMPT_COL, DLG_PROMPT_ROW, ">");
            } else {
                consoleDrawText(DLG_PROMPT_COL, DLG_PROMPT_ROW, " ");
            }

            /* A-button: advance to next page or close */
            if (pad_pressed & ACTION_CONFIRM) {
                soundPlaySFX(SFX_MENU_SELECT);
                s_page++;
                if (s_page >= s_script->line_count) {
                    /* Script finished, close dialog */
                    s_state = DSTATE_CLOSE;
                } else {
                    /* Next page */
                    dlgStartPage();
                }
            }
            return 1;

        case DSTATE_CLOSE:
            /* Blocking transition back to flight */
            dlgTransitionOut();
            s_state = DSTATE_INACTIVE;
            s_script = 0;
            return 0;
    }

    return 0;
}

u8 dlgIsActive(void)
{
    return (s_state != DSTATE_INACTIVE) ? 1 : 0;
}
