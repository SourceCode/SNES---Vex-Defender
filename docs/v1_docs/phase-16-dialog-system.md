# Phase 16: Story & Dialog System

## Objective
Implement a dialog/text-box system for delivering the game's storyline during flight mode. Dialog boxes appear as overlays on BG3 with character portraits, typewriter-style text reveal, and player-driven page advancement. Story events are triggered by scroll position (Phase 7) and gate certain game progression moments: mission briefings before each zone, mid-zone radio chatter, and pre-boss warnings.

## Prerequisites
- Phase 7 (Scroll triggers fire dialog events), Phase 15 (Game state machine handles DIALOG overlay), Phase 6 (Input system for advancing text).

## Detailed Tasks

1. Create `src/ui/dialog.c` - Dialog box renderer and text sequencer.
2. Define a dialog script format: an array of DialogLine structs, each containing a speaker ID, portrait index, and text string pointer.
3. Implement the dialog box visual: a 28x6 tile rectangle on BG3, using a simple border drawn with ASCII characters. Speaker name on top-left, portrait sprite on the left side of the box.
4. Implement typewriter text reveal: characters appear one at a time at a configurable speed (2 frames per character default). Pressing A immediately reveals the full line.
5. Implement multi-page dialog: when the current page finishes, a blinking prompt appears. Pressing A advances to the next page. After the last page, the dialog closes.
6. Implement portrait display using an OAM sprite (slot 68) that shows a 32x32 character portrait next to the text box.
7. Create the story script data: all dialog lines for the 3-zone storyline stored as ROM constant arrays.
8. Wire scroll triggers to start dialog sequences at the correct story moments.
9. Integrate with game state machine: dialog pushes GS_DIALOG overlay state, pausing flight action while text plays.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/ui/dialog.h
```c
#ifndef DIALOG_H
#define DIALOG_H

#include "game.h"

/* Maximum characters per dialog line (must fit in 26-tile-wide text area) */
#define DIALOG_LINE_MAX     52  /* 26 chars per row, 2 rows */
#define DIALOG_CHARS_PER_ROW 26

/* Speaker IDs */
#define SPEAKER_NONE        0
#define SPEAKER_VEX         1   /* Player character */
#define SPEAKER_COMMANDER   2   /* Ark commander */
#define SPEAKER_ENGINEER    3   /* Ship engineer */
#define SPEAKER_ENEMY       4   /* Enemy transmissions */
#define SPEAKER_SYSTEM      5   /* Ship computer */
#define SPEAKER_COUNT       6

/* Dialog line (ROM data) */
typedef struct {
    u8 speaker;             /* SPEAKER_* ID */
    u8 portrait;            /* Portrait sprite index (0=no portrait) */
    const char *text;       /* Text to display (null-terminated, max 52 chars) */
} DialogLine;

/* Dialog script: a sequence of lines terminated by speaker=SPEAKER_NONE */
typedef struct {
    const DialogLine *lines;  /* Array of dialog lines */
    u8 line_count;            /* Number of lines in this script */
} DialogScript;

/* Text reveal speed (frames per character) */
#define TEXT_SPEED_FAST     1
#define TEXT_SPEED_NORMAL   2
#define TEXT_SPEED_SLOW     4

/* Dialog box layout on BG3 (tile positions) */
#define DBOX_X              1   /* Left edge of box */
#define DBOX_Y              20  /* Top edge of box (bottom of screen) */
#define DBOX_W              30  /* Width in tiles */
#define DBOX_H              6   /* Height in tiles */
#define DBOX_TEXT_X         5   /* Text area left (after portrait space) */
#define DBOX_TEXT_Y         21  /* First text row */
#define DBOX_TEXT_Y2        22  /* Second text row */
#define DBOX_NAME_X         5   /* Speaker name X */
#define DBOX_NAME_Y         20  /* Speaker name row (top of box) */

/* Portrait OAM slot */
#define DIALOG_PORTRAIT_OAM 68

/* Initialize dialog system */
void dialogInit(void);

/* Start a dialog script. Returns immediately; dialog plays over subsequent frames. */
void dialogStart(const DialogScript *script);

/* Update dialog each frame (typewriter, input handling).
 * Returns 1 while dialog is active, 0 when finished. */
u8 dialogUpdate(void);

/* Render dialog box (call after dialogUpdate) */
void dialogRender(void);

/* Is a dialog currently active? */
u8 dialogIsActive(void);

/* Force-close any active dialog */
void dialogClose(void);

/* Set text reveal speed */
void dialogSetSpeed(u8 framesPerChar);

#endif
```

### J:/code/snes/snes-rpg-test/src/ui/dialog.c
```c
/*==============================================================================
 * Dialog / Story Text System
 *
 * Visual layout (on BG3):
 *   +------------------------------+
 *   |[PRT] COMMANDER               |  <- row 20: speaker name
 *   |[PRT] The debris field ahead  |  <- row 21: text line 1
 *   |[PRT] is full of hostiles.    |  <- row 22: text line 2
 *   |                           [>]|  <- row 23: advance prompt
 *   +------------------------------+
 *
 * [PRT] = 32x32 portrait sprite in OAM
 * [>] = blinking advance indicator
 *============================================================================*/

#include "ui/dialog.h"
#include "engine/input.h"
#include "engine/sprites.h"

/* Dialog state */
static const DialogScript *current_script;
static u8 current_line;         /* Index into script->lines */
static u8 reveal_pos;           /* Characters revealed so far */
static u8 text_length;          /* Total length of current text */
static u8 reveal_timer;         /* Countdown to next character */
static u8 text_speed;           /* Frames per character */
static u8 page_complete;        /* 1 when all chars revealed */
static u8 dialog_active;        /* 1 while dialog system is running */
static u8 prompt_blink;         /* Blink counter for advance prompt */

/* Speaker name strings (ROM data) */
static const char *speaker_names[SPEAKER_COUNT] = {
    "",
    "VEX",
    "COMMANDER",
    "ENGINEER",
    "ENEMY",
    "SYSTEM"
};

/* Portrait tile offsets in OBJ VRAM (loaded during zone setup) */
/* Portrait 0 = no portrait, 1 = Vex, 2 = Commander, etc. */
static const u16 portrait_tiles[SPEAKER_COUNT] = {
    0,          /* NONE */
    0,          /* VEX: uses player ship tile temporarily */
    0,          /* COMMANDER: loaded from npc_commander asset */
    0,          /* ENGINEER: shares commander palette */
    0,          /* ENEMY: uses enemy tile */
    0           /* SYSTEM: no portrait */
};

/* Internal helpers */
static u8 charStrLen(const char *s)
{
    u8 len = 0;
    while (s[len] && len < DIALOG_LINE_MAX) len++;
    return len;
}

void dialogInit(void)
{
    dialog_active = 0;
    current_script = (const DialogScript *)0;
    text_speed = TEXT_SPEED_NORMAL;
}

void dialogStart(const DialogScript *script)
{
    if (!script || script->line_count == 0) return;

    current_script = script;
    current_line = 0;
    reveal_pos = 0;
    text_length = charStrLen(script->lines[0].text);
    reveal_timer = text_speed;
    page_complete = 0;
    dialog_active = 1;
    prompt_blink = 0;
}

static void drawBox(void)
{
    u8 x, y;

    /* Draw top border */
    consoleDrawText(DBOX_X, DBOX_Y, "+");
    for (x = 1; x < DBOX_W - 1; x++) {
        consoleDrawText(DBOX_X + x, DBOX_Y, "-");
    }
    consoleDrawText(DBOX_X + DBOX_W - 1, DBOX_Y, "+");

    /* Draw middle rows */
    for (y = 1; y < DBOX_H - 1; y++) {
        consoleDrawText(DBOX_X, DBOX_Y + y, "|");
        consoleDrawText(DBOX_X + DBOX_W - 1, DBOX_Y + y, "|");
    }

    /* Draw bottom border */
    consoleDrawText(DBOX_X, DBOX_Y + DBOX_H - 1, "+");
    for (x = 1; x < DBOX_W - 1; x++) {
        consoleDrawText(DBOX_X + x, DBOX_Y + DBOX_H - 1, "-");
    }
    consoleDrawText(DBOX_X + DBOX_W - 1, DBOX_Y + DBOX_H - 1, "+");
}

static void clearTextArea(void)
{
    u8 x;
    /* Clear speaker name line */
    for (x = DBOX_TEXT_X; x < DBOX_X + DBOX_W - 1; x++) {
        consoleDrawText(x, DBOX_NAME_Y, " ");
    }
    /* Clear text line 1 */
    for (x = DBOX_TEXT_X; x < DBOX_X + DBOX_W - 1; x++) {
        consoleDrawText(x, DBOX_TEXT_Y, " ");
    }
    /* Clear text line 2 */
    for (x = DBOX_TEXT_X; x < DBOX_X + DBOX_W - 1; x++) {
        consoleDrawText(x, DBOX_TEXT_Y2, " ");
    }
    /* Clear prompt line */
    for (x = DBOX_X + 1; x < DBOX_X + DBOX_W - 1; x++) {
        consoleDrawText(x, DBOX_Y + DBOX_H - 2, " ");
    }
}

static void showPortrait(u8 speaker)
{
    if (speaker == SPEAKER_NONE || speaker == SPEAKER_SYSTEM) {
        oamSetVisible(DIALOG_PORTRAIT_OAM * 4, OBJ_HIDE);
        return;
    }
    /* Place portrait sprite at left side of dialog box */
    oamSet(DIALOG_PORTRAIT_OAM * 4,
           (DBOX_X + 1) * 8,       /* X: tile position * 8 pixels */
           (DBOX_TEXT_Y) * 8,       /* Y: aligned with text */
           3,                       /* Priority: on top */
           0, 0,                    /* No flip */
           portrait_tiles[speaker] >> 4,  /* Tile number */
           6);                      /* Palette 6: NPC portraits */
    oamSetEx(DIALOG_PORTRAIT_OAM * 4, OBJ_LARGE, OBJ_SHOW);
}

static void startLine(u8 lineIndex)
{
    const DialogLine *line;
    if (lineIndex >= current_script->line_count) return;

    line = &current_script->lines[lineIndex];
    current_line = lineIndex;
    reveal_pos = 0;
    text_length = charStrLen(line->text);
    reveal_timer = text_speed;
    page_complete = 0;
    prompt_blink = 0;

    clearTextArea();

    /* Draw speaker name */
    if (line->speaker > 0 && line->speaker < SPEAKER_COUNT) {
        consoleDrawText(DBOX_NAME_X, DBOX_NAME_Y,
                        "%s", speaker_names[line->speaker]);
    }

    /* Show portrait */
    showPortrait(line->speaker);
}

u8 dialogUpdate(void)
{
    u16 pressed;
    const DialogLine *line;

    if (!dialog_active) return 0;

    pressed = inputPressed();
    line = &current_script->lines[current_line];

    if (!page_complete) {
        /* Typewriter reveal */
        reveal_timer--;
        if (reveal_timer == 0) {
            reveal_pos++;
            reveal_timer = text_speed;
            if (reveal_pos >= text_length) {
                page_complete = 1;
            }
        }

        /* A button: instant reveal */
        if (pressed & ACTION_CONFIRM) {
            reveal_pos = text_length;
            page_complete = 1;
        }
    } else {
        /* Page complete: blink prompt, wait for A */
        prompt_blink++;

        if (pressed & ACTION_CONFIRM) {
            /* Advance to next line */
            if (current_line + 1 < current_script->line_count) {
                startLine(current_line + 1);
            } else {
                /* Dialog complete */
                dialogClose();
                return 0;
            }
        }
    }

    return 1;
}

void dialogRender(void)
{
    const DialogLine *line;
    u8 i;
    char row1[28];
    char row2[28];

    if (!dialog_active) return;

    line = &current_script->lines[current_line];

    /* Draw the box frame */
    drawBox();

    /* Build revealed text strings for row 1 and row 2 */
    /* Row 1 = characters 0..25, Row 2 = characters 26..51 */
    for (i = 0; i < DIALOG_CHARS_PER_ROW && i < reveal_pos; i++) {
        if (i < text_length) {
            row1[i] = line->text[i];
        } else {
            row1[i] = ' ';
        }
    }
    /* Pad remainder with spaces */
    for (; i < DIALOG_CHARS_PER_ROW; i++) {
        row1[i] = ' ';
    }
    row1[DIALOG_CHARS_PER_ROW] = 0;

    /* Row 2: characters 26+ */
    if (reveal_pos > DIALOG_CHARS_PER_ROW) {
        u8 row2_reveal = reveal_pos - DIALOG_CHARS_PER_ROW;
        for (i = 0; i < DIALOG_CHARS_PER_ROW && i < row2_reveal; i++) {
            u8 src_idx = DIALOG_CHARS_PER_ROW + i;
            if (src_idx < text_length) {
                row2[i] = line->text[src_idx];
            } else {
                row2[i] = ' ';
            }
        }
        for (; i < DIALOG_CHARS_PER_ROW; i++) {
            row2[i] = ' ';
        }
        row2[DIALOG_CHARS_PER_ROW] = 0;
    } else {
        for (i = 0; i < DIALOG_CHARS_PER_ROW; i++) row2[i] = ' ';
        row2[DIALOG_CHARS_PER_ROW] = 0;
    }

    consoleDrawText(DBOX_TEXT_X, DBOX_TEXT_Y, "%s", row1);
    consoleDrawText(DBOX_TEXT_X, DBOX_TEXT_Y2, "%s", row2);

    /* Blinking advance prompt */
    if (page_complete && (prompt_blink & 0x10)) {
        consoleDrawText(DBOX_X + DBOX_W - 3, DBOX_Y + DBOX_H - 2, ">");
    } else if (page_complete) {
        consoleDrawText(DBOX_X + DBOX_W - 3, DBOX_Y + DBOX_H - 2, " ");
    }
}

u8 dialogIsActive(void)
{
    return dialog_active;
}

void dialogClose(void)
{
    u8 x, y;
    dialog_active = 0;
    current_script = (const DialogScript *)0;

    /* Clear the entire dialog box area */
    for (y = 0; y < DBOX_H; y++) {
        for (x = 0; x < DBOX_W; x++) {
            consoleDrawText(DBOX_X + x, DBOX_Y + y, " ");
        }
    }

    /* Hide portrait */
    oamSetVisible(DIALOG_PORTRAIT_OAM * 4, OBJ_HIDE);
}

void dialogSetSpeed(u8 framesPerChar)
{
    text_speed = framesPerChar;
    if (text_speed < 1) text_speed = 1;
}
```

### J:/code/snes/snes-rpg-test/include/game/story.h
```c
#ifndef STORY_H
#define STORY_H

#include "game.h"
#include "ui/dialog.h"

/* Story flags (bitfield) - track which events have occurred */
#define STORY_FLAG_NONE         0x0000
#define STORY_FLAG_INTRO_DONE   0x0001  /* Opening briefing complete */
#define STORY_FLAG_ZONE1_MID    0x0002  /* Zone 1 mid-point dialog */
#define STORY_FLAG_ZONE1_BOSS   0x0004  /* Zone 1 pre-boss warning */
#define STORY_FLAG_ZONE2_ENTER  0x0008  /* Zone 2 entry briefing */
#define STORY_FLAG_ZONE2_MID    0x0010  /* Zone 2 mid-point */
#define STORY_FLAG_ZONE2_BOSS   0x0020  /* Zone 2 pre-boss */
#define STORY_FLAG_ZONE3_ENTER  0x0040  /* Zone 3 entry - final approach */
#define STORY_FLAG_ZONE3_MID    0x0080  /* Zone 3 mid-point */
#define STORY_FLAG_ZONE3_BOSS   0x0100  /* Zone 3 pre-final-boss */
#define STORY_FLAG_VICTORY      0x0200  /* Game won */

extern u16 story_flags;

/* Dialog scripts for each story moment */
extern const DialogScript script_intro;
extern const DialogScript script_zone1_mid;
extern const DialogScript script_zone1_preboss;
extern const DialogScript script_zone2_enter;
extern const DialogScript script_zone2_mid;
extern const DialogScript script_zone2_preboss;
extern const DialogScript script_zone3_enter;
extern const DialogScript script_zone3_mid;
extern const DialogScript script_zone3_preboss;
extern const DialogScript script_victory;

/* Initialize story state */
void storyInit(void);

/* Check and set story flag. Returns 1 if flag was newly set. */
u8 storySetFlag(u16 flag);

/* Check if flag is already set */
u8 storyCheckFlag(u16 flag);

/* Trigger a story dialog. Pushes GS_DIALOG state. */
void storyTriggerDialog(const DialogScript *script, u16 flag);

/* Register scroll triggers for a zone's story events */
void storyRegisterZoneTriggers(u8 zoneId);

#endif
```

### J:/code/snes/snes-rpg-test/src/game/story.c
```c
/*==============================================================================
 * Story Script Data & Event Triggers
 *
 * All dialog text is stored as ROM constants.
 * Story events are fired by scroll triggers registered per zone.
 *============================================================================*/

#include "game/story.h"
#include "game/game_state.h"
#include "engine/scroll.h"

u16 story_flags;

/* ====================================================================
 * ZONE 1: DEBRIS FIELD - Story Dialogs
 * ==================================================================== */

static const DialogLine intro_lines[] = {
    { SPEAKER_COMMANDER, 2, "Cadet Vex, this is Commander  Hale. The Ark needs you." },
    { SPEAKER_COMMANDER, 2, "Our sensors show an alien     armada approaching fast." },
    { SPEAKER_VEX, 1,       "Understood Commander. My      fighter is armed and ready." },
    { SPEAKER_COMMANDER, 2, "Fly through the debris field  and clear a path for us." },
    { SPEAKER_COMMANDER, 2, "Engage hostiles on sight.     Good luck out there." },
};

const DialogScript script_intro = {
    intro_lines, 5
};

static const DialogLine zone1_mid_lines[] = {
    { SPEAKER_SYSTEM, 0,    "WARNING: Multiple contacts    detected ahead." },
    { SPEAKER_VEX, 1,       "I see them. Switching to      combat formation." },
    { SPEAKER_COMMANDER, 2, "Stay sharp Vex. These scouts  are just the vanguard." },
};

const DialogScript script_zone1_mid = {
    zone1_mid_lines, 3
};

static const DialogLine zone1_preboss_lines[] = {
    { SPEAKER_ENGINEER, 3,  "Vex, picking up a large       signature ahead." },
    { SPEAKER_VEX, 1,       "Looks like a command ship.    I am engaging." },
    { SPEAKER_COMMANDER, 2, "That is their scout leader.   Take it down!" },
};

const DialogScript script_zone1_preboss = {
    zone1_preboss_lines, 3
};

/* ====================================================================
 * ZONE 2: ASTEROID BELT - Story Dialogs
 * ==================================================================== */

static const DialogLine zone2_enter_lines[] = {
    { SPEAKER_COMMANDER, 2, "Good work clearing the debris field Vex." },
    { SPEAKER_COMMANDER, 2, "Now you are entering the      asteroid belt." },
    { SPEAKER_ENGINEER, 3,  "Watch the rocks. Your shields cannot take many hits." },
    { SPEAKER_VEX, 1,       "Copy that. Navigating through now." },
};

const DialogScript script_zone2_enter = {
    zone2_enter_lines, 4
};

static const DialogLine zone2_mid_lines[] = {
    { SPEAKER_ENEMY, 4,     "Human vessel. You cannot      stop what is coming." },
    { SPEAKER_VEX, 1,       "We will see about that." },
    { SPEAKER_COMMANDER, 2, "Ignore their transmissions.   Focus on your mission." },
};

const DialogScript script_zone2_mid = {
    zone2_mid_lines, 3
};

static const DialogLine zone2_preboss_lines[] = {
    { SPEAKER_ENGINEER, 3,  "Massive energy spike ahead!   It is their cruiser!" },
    { SPEAKER_COMMANDER, 2, "That cruiser is blocking our  route. Destroy it Vex." },
    { SPEAKER_VEX, 1,       "Weapons hot. Moving to        intercept." },
};

const DialogScript script_zone2_preboss = {
    zone2_preboss_lines, 3
};

/* ====================================================================
 * ZONE 3: FLAGSHIP APPROACH - Story Dialogs
 * ==================================================================== */

static const DialogLine zone3_enter_lines[] = {
    { SPEAKER_COMMANDER, 2, "Vex, you have reached the     enemy flagship." },
    { SPEAKER_COMMANDER, 2, "This is it. Destroy the       command vessel." },
    { SPEAKER_ENGINEER, 3,  "I am routing all power to     your weapons systems." },
    { SPEAKER_VEX, 1,       "For the Ark. Lets finish      this." },
};

const DialogScript script_zone3_enter = {
    zone3_enter_lines, 4
};

static const DialogLine zone3_mid_lines[] = {
    { SPEAKER_ENEMY, 4,     "You are persistent. But our   elite guard will stop you." },
    { SPEAKER_VEX, 1,       "Nothing will stop me from     protecting the Ark." },
};

const DialogScript script_zone3_mid = {
    zone3_mid_lines, 2
};

static const DialogLine zone3_preboss_lines[] = {
    { SPEAKER_ENGINEER, 3,  "Vex, the flagship core is     exposed! Hit it now!" },
    { SPEAKER_COMMANDER, 2, "Everything depends on this.   Give it everything you have." },
    { SPEAKER_VEX, 1,       "Targeting the core. This ends now." },
};

const DialogScript script_zone3_preboss = {
    zone3_preboss_lines, 3
};

/* ====================================================================
 * VICTORY
 * ==================================================================== */

static const DialogLine victory_lines[] = {
    { SPEAKER_COMMANDER, 2, "The flagship is destroyed!    You did it Vex!" },
    { SPEAKER_ENGINEER, 3,  "Enemy fleet is in full        retreat!" },
    { SPEAKER_VEX, 1,       "The Ark is safe. Mission      complete." },
    { SPEAKER_COMMANDER, 2, "You have saved every soul on  this ship. Welcome home." },
};

const DialogScript script_victory = {
    victory_lines, 4
};

/* ====================================================================
 * Story State Management
 * ==================================================================== */

void storyInit(void)
{
    story_flags = STORY_FLAG_NONE;
}

u8 storySetFlag(u16 flag)
{
    if (story_flags & flag) return 0;  /* Already set */
    story_flags |= flag;
    return 1;
}

u8 storyCheckFlag(u16 flag)
{
    return (story_flags & flag) ? 1 : 0;
}

void storyTriggerDialog(const DialogScript *script, u16 flag)
{
    if (storyCheckFlag(flag)) return;  /* Don't replay */
    storySetFlag(flag);

    dialogStart(script);
    gameStatePush(GS_DIALOG);
}

/* ====================================================================
 * Scroll Trigger Callbacks
 * ==================================================================== */

static void triggerIntro(void)
{
    storyTriggerDialog(&script_intro, STORY_FLAG_INTRO_DONE);
}

static void triggerZone1Mid(void)
{
    storyTriggerDialog(&script_zone1_mid, STORY_FLAG_ZONE1_MID);
}

static void triggerZone1PreBoss(void)
{
    storyTriggerDialog(&script_zone1_preboss, STORY_FLAG_ZONE1_BOSS);
}

static void triggerZone2Enter(void)
{
    storyTriggerDialog(&script_zone2_enter, STORY_FLAG_ZONE2_ENTER);
}

static void triggerZone2Mid(void)
{
    storyTriggerDialog(&script_zone2_mid, STORY_FLAG_ZONE2_MID);
}

static void triggerZone2PreBoss(void)
{
    storyTriggerDialog(&script_zone2_preboss, STORY_FLAG_ZONE2_BOSS);
}

static void triggerZone3Enter(void)
{
    storyTriggerDialog(&script_zone3_enter, STORY_FLAG_ZONE3_ENTER);
}

static void triggerZone3Mid(void)
{
    storyTriggerDialog(&script_zone3_mid, STORY_FLAG_ZONE3_MID);
}

static void triggerZone3PreBoss(void)
{
    storyTriggerDialog(&script_zone3_preboss, STORY_FLAG_ZONE3_BOSS);
}

void storyRegisterZoneTriggers(u8 zoneId)
{
    scrollClearTriggers();

    switch (zoneId) {
        case 0: /* ZONE_DEBRIS */
            scrollAddTrigger(100, triggerIntro);
            scrollAddTrigger(2500, triggerZone1Mid);
            scrollAddTrigger(4800, triggerZone1PreBoss);
            break;

        case 1: /* ZONE_ASTEROID */
            scrollAddTrigger(200, triggerZone2Enter);
            scrollAddTrigger(2500, triggerZone2Mid);
            scrollAddTrigger(4800, triggerZone2PreBoss);
            break;

        case 2: /* ZONE_FLAGSHIP */
            scrollAddTrigger(200, triggerZone3Enter);
            scrollAddTrigger(2500, triggerZone3Mid);
            scrollAddTrigger(4800, triggerZone3PreBoss);
            break;
    }
}
```

### Updates to game_state.c (Phase 15)

The game state machine must handle GS_DIALOG as an overlay state, similar to GS_PAUSE.

```c
/* Add to the overlay handling in gameStateUpdate(): */

/* In the overlay switch statement, add: */
case GS_DIALOG:
    if (!dialogUpdate()) {
        /* Dialog finished */
        dialogClose();
        gameStatePop();
    } else {
        dialogRender();
    }
    break;
```

## Technical Specifications

### Dialog Text Formatting
```
Each DialogLine.text field is a fixed-width string padded to fit
two rows of 26 characters each (52 total). The typewriter reveals
characters left-to-right, row 1 then row 2.

Text must be manually line-broken by the author at character 26
(pad with spaces to exactly 26 characters for row 1, then row 2
starts at character 27).

Example:
  "Cadet Vex, this is        Commander Hale."
   |---- 26 chars row 1 ----|---- row 2 ---|

This avoids runtime word-wrap logic (expensive on 65c816).
```

### Dialog Box BG3 Tile Budget
```
Dialog box: 30 x 6 = 180 tile writes per frame.
consoleDrawText calls: ~10 per render cycle.
BG3 has 32x32 = 1024 tiles total.
Dialog box uses rows 20-25 (bottom 6 rows), columns 1-30.

This leaves rows 0-19 free for HUD text (HP display, etc.).
No conflict with the pause overlay text (rows 10-17).
```

### Portrait Sprite Integration
```
Portrait uses OAM slot 68 (allocated in Phase 1 for UI).
Position: (16, 168) pixels = (2 tiles from left, row 21 * 8).
Size: 32x32 (OBJ_LARGE).
Palette: slot 6 (NPC palette).

Portrait tiles are loaded during zone setup (Phase 18).
For zones without unique portraits, the player ship tile
can be reused or the portrait can be hidden.
```

### Story Event Timing (Scroll Positions)
```
Zone 1 (Debris Field) - scroll distance 0 to ~5400:
  100   Opening briefing (5 lines)
  2500  Mid-zone warning (3 lines)
  4800  Pre-boss encounter (3 lines)

Zone 2 (Asteroid Belt) - scroll distance 0 to ~5400:
  200   Zone entry briefing (4 lines)
  2500  Enemy taunt (3 lines)
  4800  Pre-boss alert (3 lines)

Zone 3 (Flagship) - scroll distance 0 to ~5400:
  200   Final approach briefing (4 lines)
  2500  Enemy elite warning (2 lines)
  4800  Pre-final-boss dialog (3 lines)

Each dialog takes ~3-8 seconds depending on line count and
player read speed. Scrolling pauses during dialog (GS_DIALOG
overlay stops flight updates).
```

### Memory Budget
```
Dialog system state: ~32 bytes WRAM
Story flags: 2 bytes WRAM
Dialog scripts (ROM): ~50 lines * 60 bytes avg = ~3KB ROM
Total WRAM: 34 bytes
Total ROM: ~3KB
```

## Acceptance Criteria
1. Calling dialogStart() with a valid script opens the dialog box on BG3.
2. Text appears one character at a time (typewriter effect) at the configured speed.
3. Pressing A during text reveal instantly shows the full line.
4. After text is fully revealed, a blinking ">" prompt appears.
5. Pressing A advances to the next line. After the last line, dialog closes.
6. Speaker name appears at the top of the dialog box.
7. Portrait sprite appears for speakers with a portrait defined.
8. Game state overlay (GS_DIALOG) pauses flight mode during dialog.
9. After dialog closes, flight mode resumes exactly where it left off.
10. Scroll triggers fire story dialogs at the correct distances.
11. Story flags prevent the same dialog from replaying.
12. All dialog text is readable (fits within the 26-char-per-row layout).

## SNES-Specific Constraints
- consoleDrawText writes to a RAM buffer that is DMA'd during VBlank. Drawing the full 180-tile dialog box in one frame is safe because the buffer transfer is handled by PVSnesLib's VBlank ISR.
- Character-by-character reveal must not call consoleDrawText for every character individually (too slow). Instead, build a complete row string and draw once per row per frame.
- Dialog box text uses BG3 (2bpp, 4 colors). The border characters use the same font as consoleDrawText's built-in ASCII font.
- Portrait sprite at OAM slot 68 does not conflict with any flight-mode sprites (player=0, enemies=4-11, bullets=12-35, particles=36-43).
- String literals in const arrays are placed in ROM by the compiler. No WRAM cost for dialog text.

## Estimated Complexity
**Medium** - Typewriter text reveal and multi-page sequencing require state management. The story data is boilerplate. Portrait integration is straightforward.
