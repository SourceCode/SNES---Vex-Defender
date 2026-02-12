/*==============================================================================
 * Dialog System - Phase 16
 *
 * Scroll-triggered story dialog with typewriter text reveal on the SNES.
 *
 * Implementation pattern:
 *   When a dialog trigger fires (via scroll distance callback), it sets
 *   g_dialog_pending to point at the desired DialogScript.  The main loop
 *   detects this and transitions STATE_FLIGHT -> STATE_DIALOG, calling
 *   dlgOpen() which performs a blocking fade-out, disables flight systems,
 *   initializes BG1 as a 4bpp text layer (PVSnesLib font), draws the dialog
 *   box frame, and fades back in.
 *
 * Text rendering:
 *   Uses PVSnesLib's consoleInitText() / consoleDrawText() on BG1.
 *   The 4bpp font tiles are loaded at VRAM_TEXT_GFX ($3000), which is
 *   tile offset 0x100 from the BG1 char base at VRAM_BG1_GFX ($2000).
 *   The text tilemap shares the BG1 map at VRAM_BG1_MAP ($6800).
 *   When dialog closes, bgLoadZone() reloads the game's BG1 tiles/map,
 *   restoring the flight-mode background graphics.
 *
 * Dialog lines use two separate 26-char strings (top/bottom row within
 * the dialog box).  The speaker name (e.g., "[COMMANDER]") is drawn on
 * a row above the text lines.
 *
 * Typewriter reveal runs at DLG_TYPE_SPEED frames per character, with
 * O(1) per-frame rendering (only the newly-revealed character is drawn).
 * Pressing A instantly fills the rest of the page.  A blinking ">" prompt
 * appears when a page is fully revealed; pressing A advances to the next
 * page or closes the dialog after the last page.
 *
 * Story scripts are ROM-resident data arrays defined in story.c and
 * triggered by scroll distance callbacks registered in storyRegisterTriggers().
 *============================================================================*/

#ifndef DIALOG_H
#define DIALOG_H

#include "game.h"

/*=== Speaker IDs ===*/
/* Index into the speaker_names[] array in dialog.c.  Used to display
 * the speaker's name above the dialog text (e.g., "[VEX]"). */
#define SPEAKER_NONE      0   /* No speaker label shown */
#define SPEAKER_VEX       1   /* Player character */
#define SPEAKER_COMMANDER 2   /* Mission commander (ally) */
#define SPEAKER_ENGINEER  3   /* Ship's engineer (ally) */
#define SPEAKER_ENEMY     4   /* Hostile alien speaker */
#define SPEAKER_SYSTEM    5   /* Narrator / system message */

/*=== Dialog Text Layout Constants ===*/
/* These define tile-row and tile-column positions on the BG1 tilemap
 * (32x32 tiles, 8x8 pixels each).  The dialog box occupies the bottom
 * portion of the screen (rows 19-24 out of 28 visible rows). */
#define DLG_LINE_MAX      26   /* Max characters per line (fits columns 2-27) */
#define DLG_BOX_TOP       19   /* Tile row: top border of the dialog box */
#define DLG_BOX_BOTTOM    24   /* Tile row: bottom border of the dialog box */
#define DLG_TEXT_ROW1     21   /* Tile row: first line of dialog text */
#define DLG_TEXT_ROW2     22   /* Tile row: second line of dialog text */
#define DLG_NAME_ROW      20   /* Tile row: speaker name label "[NAME]" */
#define DLG_TEXT_COL       2   /* Tile column: left edge of text area */
#define DLG_PROMPT_COL    28   /* Tile column: ">" advance prompt position */
#define DLG_PROMPT_ROW    22   /* Tile row: prompt position (same as line 2) */

/*=== Typewriter Speed ===*/
/* Number of VBlank frames between each character reveal.
 * At 60fps, DLG_TYPE_SPEED=2 means ~30 characters per second. */
#define DLG_TYPE_SPEED     2

/*=== DialogLine: A Single Page of Dialog ===*/
/* Each "page" shows a speaker name plus up to two lines of text.
 * The player must press A to advance past each page. */
typedef struct {
    u8 speaker;             /* SPEAKER_* ID for the name label */
    char *line1;            /* Top text line (up to 26 chars), or NULL for blank */
    char *line2;            /* Bottom text line (up to 26 chars), or NULL for blank */
} DialogLine;

/*=== DialogScript: A Sequence of Pages ===*/
/* ROM-resident array of DialogLines that together form one conversation.
 * Played sequentially from page 0 to line_count-1. */
typedef struct {
    DialogLine *lines;      /* Pointer to array of dialog lines (ROM data) */
    u8 line_count;          /* Number of pages in this script */
} DialogScript;

/*=== Dialog Engine States ===*/
/* Internal state machine for the dialog engine, tracked in s_state (dialog.c). */
#define DSTATE_INACTIVE    0   /* No dialog active; engine is dormant */
#define DSTATE_OPEN        1   /* Transition-in phase (fade, BG1 setup) */
#define DSTATE_TYPING      2   /* Typewriter is revealing text char by char */
#define DSTATE_WAIT        3   /* Full page shown; blinking ">" prompt, waiting for A */
#define DSTATE_CLOSE       4   /* Transition-out phase (restore BG1, fade in) */

/*=== Dialog Pending Trigger ===*/
/* Set by scroll trigger callbacks in story.c. The main loop checks this
 * pointer each frame during STATE_FLIGHT.  If non-NULL, the game transitions
 * to STATE_DIALOG and calls dlgOpen().  This is the same deferred-trigger
 * pattern used by g_battle_trigger for battle encounters. */
extern DialogScript *g_dialog_pending;

/*--- Dialog Engine API ---*/

/*
 * dlgInit - Initialize dialog system to idle state.
 * Clears internal state and pending trigger. Called once during gsInit().
 */
void dlgInit(void);

/*
 * dlgOpen - Begin playing a dialog script.
 * Performs a blocking transition: fades out the flight screen, stops scrolling,
 * hides all sprites, enters force-blank, sets up BG1 as a text layer with
 * the PVSnesLib 4bpp font, draws the dialog box borders, and fades in.
 * Then starts the typewriter on the first page.
 *
 * script: Pointer to a ROM-resident DialogScript (must not be NULL).
 */
void dlgOpen(DialogScript *script);

/*
 * dlgUpdate - Per-frame dialog update, called from the main loop.
 * Handles typewriter character reveal, A-button fast-fill, page advance,
 * blinking prompt, and auto-close after the last page.
 *
 * pad_pressed: Edge-triggered controller input from inputPressed().
 * Returns 1 while dialog is active (game should stay in STATE_DIALOG),
 *         0 when dialog has finished (game should return to STATE_FLIGHT).
 */
u8 dlgUpdate(u16 pad_pressed);

/*
 * dlgIsActive - Check whether the dialog engine is currently running.
 * Returns 1 if any dialog is in progress, 0 if idle.
 */
u8 dlgIsActive(void);

/*--- Story Script API (implemented in story.c) ---*/

/*
 * storyInit - Initialize story system.
 * Story flags live in g_game.story_flags (cleared by gsInit).
 * Called once during gsInit() after dlgInit().
 */
void storyInit(void);

/*
 * storyRegisterTriggers - Register scroll-distance callbacks for story dialogs.
 * Must be called AFTER enemySetupZoneTriggers() because it appends to the
 * existing trigger list (does NOT call scrollClearTriggers).
 *
 * zoneId: ZONE_DEBRIS / ZONE_ASTEROID / ZONE_FLAGSHIP.
 */
void storyRegisterTriggers(u8 zoneId);

/*
 * storyHasFlag - Check if a story flag is set in g_game.story_flags.
 * Returns 1 if the flag bit(s) are set, 0 otherwise.
 */
u8 storyHasFlag(u16 flag);

/*
 * storySetFlag - Set a story flag bit in g_game.story_flags.
 * Bits are OR'd in and never cleared during a playthrough.
 */
void storySetFlag(u16 flag);

#endif /* DIALOG_H */
