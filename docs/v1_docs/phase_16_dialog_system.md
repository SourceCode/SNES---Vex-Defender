# Phase 16: Story/Dialog System

## Objective
Implement the story and dialog system that presents narrative text with character portraits, player choices, and story progression flags. This system delivers the game's storyline including the CRITICAL PLOT TWIST in Zone 3.

## Prerequisites
- Phase 12 (Battle UI / Text Rendering) complete
- Phase 15 (Game State Machine) complete

## Detailed Tasks

### 1. Create Dialog Data Format
Story text stored in ROM as structured dialog sequences.

### 2. Implement Dialog Box Renderer
Text box with portrait, name tag, and auto-advancing text.

### 3. Implement Player Choice System
Binary choices that set story flags (critical for the twist).

### 4. Write Complete Game Script
All dialog for intro, zone transitions, twist, and ending.

### 5. Integrate Dialog Triggers with Game Flow

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/dialog.h` | CREATE | Dialog system header |
| `src/dialog.c` | CREATE | Dialog system implementation |
| `src/game.c` | MODIFY | Add dialog state handling |
| `include/story_data.h` | CREATE | All game dialog text |

## Technical Specifications

### Dialog Data Format
```c
/* Dialog entry - one text box display */
typedef struct {
    u8  portrait_id;       /* 0=none, 1=Vex, 2=Admiral, 3=Alien, 4=System */
    u8  flags;             /* DIALOG_FLAG_* */
    const char *name;      /* Speaker name */
    const char *text_line1; /* Line 1 (max 28 chars) */
    const char *text_line2; /* Line 2 (max 28 chars) */
} DialogEntry;

/* Dialog flags */
#define DIALOG_FLAG_NONE      0x00
#define DIALOG_FLAG_CHOICE    0x01  /* Player must make a choice after this */
#define DIALOG_FLAG_END       0x02  /* Last entry in sequence */
#define DIALOG_FLAG_SET_FLAG  0x04  /* Sets a story flag */
#define DIALOG_FLAG_WAIT      0x08  /* Longer pause before continuing */

/* Dialog choice */
typedef struct {
    const char *option_a;   /* Choice A text (max 20 chars) */
    const char *option_b;   /* Choice B text (max 20 chars) */
    u8  flag_a;             /* Story flag to set if A chosen */
    u8  flag_b;             /* Story flag to set if B chosen */
} DialogChoice;

/* Dialog sequence - a series of entries */
typedef struct {
    const DialogEntry *entries;
    u8 entry_count;
    const DialogChoice *choice;  /* NULL if no choice */
} DialogSequence;
```

### Dialog System State
```c
typedef struct {
    u8  active;                /* Dialog currently showing */
    u8  current_sequence;      /* Which sequence is playing */
    u8  current_entry;         /* Which entry in the sequence */
    u8  char_index;            /* Character reveal position (typewriter) */
    u8  char_timer;            /* Timer for typewriter effect */
    u8  waiting_for_input;     /* Waiting for player to press A */
    u8  showing_choice;        /* Choice menu is displayed */
    u8  choice_cursor;         /* 0=option A, 1=option B */
    u8  text_speed;            /* Frames per character (2 = fast) */
} DialogState;

extern DialogState g_dialog;
```

### Dialog Box Layout
```
Screen: 256x224 (32x28 tiles)

Dialog box (bottom of screen):
+----------------------------------+  Row 0
|  [Portrait]  SPEAKER NAME        |  Row 20
|              Line 1 of dialog    |  Row 21
|              text goes here...   |  Row 22
|              Line 2 continues... |  Row 23
|                          [A]     |  Row 24 (press A indicator)
+----------------------------------+  Row 25

Portrait: 32x32 sprite at (8, 160)
Name:     Text at tile (6, 20)
Line 1:   Text at tile (6, 22)
Line 2:   Text at tile (6, 23)

Choice overlay (replaces lines):
  > CHOICE A TEXT
    CHOICE B TEXT
```

### dialog.c (Core Implementation)
```c
#include "dialog.h"
#include "input.h"
#include "ui.h"
#include "game.h"

DialogState g_dialog;

/* Dialog sequences defined in story_data.h */
extern const DialogSequence dialog_sequences[];

void dialog_init(void) {
    g_dialog.active = 0;
    g_dialog.text_speed = 2;
}

void dialog_start(u8 sequence_id) {
    g_dialog.active = 1;
    g_dialog.current_sequence = sequence_id;
    g_dialog.current_entry = 0;
    g_dialog.char_index = 0;
    g_dialog.char_timer = 0;
    g_dialog.waiting_for_input = 0;
    g_dialog.showing_choice = 0;
    g_dialog.choice_cursor = 0;

    g_game.previous_state = g_game.current_state;
    g_game.current_state = STATE_DIALOG;

    /* Draw dialog box frame */
    ui_draw_box(0, 19, 32, 7);

    /* Show first entry */
    dialog_show_entry();
}

void dialog_show_entry(void) {
    const DialogSequence *seq = &dialog_sequences[g_dialog.current_sequence];
    const DialogEntry *entry = &seq->entries[g_dialog.current_entry];

    /* Clear text area */
    ui_clear_area(1, 20, 30, 5);

    /* Draw speaker name */
    if (entry->name) {
        ui_draw_text(6, 20, entry->name);
    }

    /* Draw portrait sprite */
    if (entry->portrait_id > 0) {
        /* Set portrait sprite (OAM slot 30) */
        /* Portrait sprites loaded based on portrait_id */
        /* oamSet(30*4, 8, 160, 3, 0, 0, portrait_tile, portrait_pal); */
    }

    /* Reset typewriter effect */
    g_dialog.char_index = 0;
    g_dialog.char_timer = 0;
    g_dialog.waiting_for_input = 0;
}

void dialog_update(void) {
    if (!g_dialog.active) return;

    const DialogSequence *seq = &dialog_sequences[g_dialog.current_sequence];
    const DialogEntry *entry = &seq->entries[g_dialog.current_entry];

    if (g_dialog.showing_choice) {
        /* Handle choice input */
        if (input_is_pressed(KEY_UP) || input_is_pressed(KEY_DOWN)) {
            g_dialog.choice_cursor = !g_dialog.choice_cursor;
            dialog_draw_choice();
        }
        if (input_is_pressed(KEY_A)) {
            /* Apply choice */
            if (g_dialog.choice_cursor == 0) {
                g_game.story_flags |= seq->choice->flag_a;
            } else {
                g_game.story_flags |= seq->choice->flag_b;
            }
            g_dialog.showing_choice = 0;
            dialog_advance();
        }
        return;
    }

    if (!g_dialog.waiting_for_input) {
        /* Typewriter text reveal */
        g_dialog.char_timer++;
        if (g_dialog.char_timer >= g_dialog.text_speed) {
            g_dialog.char_timer = 0;
            g_dialog.char_index++;

            /* Render revealed text */
            dialog_render_text(entry);

            /* Check if all text revealed */
            u8 total_len = 0;
            if (entry->text_line1) total_len += 28;
            if (entry->text_line2) total_len += 28;

            if (g_dialog.char_index >= total_len) {
                g_dialog.waiting_for_input = 1;
                /* Draw "press A" indicator */
                ui_draw_text(28, 24, ">");
            }
        }

        /* Press A to skip typewriter and show all text instantly */
        if (input_is_pressed(KEY_A)) {
            g_dialog.char_index = 255;
            dialog_render_text(entry);
            g_dialog.waiting_for_input = 1;
            ui_draw_text(28, 24, ">");
        }
    } else {
        /* Waiting for A press to advance */
        /* Blink the indicator */
        if ((g_game.frame_counter & 0x10) == 0) {
            ui_draw_text(28, 24, ">");
        } else {
            ui_draw_text(28, 24, " ");
        }

        if (input_is_pressed(KEY_A)) {
            /* Check for choice */
            if (entry->flags & DIALOG_FLAG_CHOICE) {
                g_dialog.showing_choice = 1;
                g_dialog.choice_cursor = 0;
                dialog_draw_choice();
                return;
            }

            dialog_advance();
        }
    }
}

void dialog_advance(void) {
    const DialogSequence *seq = &dialog_sequences[g_dialog.current_sequence];
    const DialogEntry *entry = &seq->entries[g_dialog.current_entry];

    /* Check if this was the last entry */
    if (entry->flags & DIALOG_FLAG_END) {
        dialog_end();
        return;
    }

    /* Advance to next entry */
    g_dialog.current_entry++;
    if (g_dialog.current_entry >= seq->entry_count) {
        dialog_end();
        return;
    }

    dialog_show_entry();
}

void dialog_end(void) {
    g_dialog.active = 0;

    /* Clear dialog box */
    ui_clear_area(0, 19, 32, 7);

    /* Hide portrait sprite */
    oamSetEx(30 * 4, OBJ_LARGE, OBJ_HIDE);

    /* Return to previous state */
    g_game.current_state = g_game.previous_state;
}

void dialog_render_text(const DialogEntry *entry) {
    /* Render text with typewriter reveal up to char_index */
    char buf[29]; /* 28 chars + null */
    u8 i, limit;

    if (entry->text_line1) {
        limit = g_dialog.char_index;
        if (limit > 28) limit = 28;
        for (i = 0; i < limit && entry->text_line1[i]; i++) {
            buf[i] = entry->text_line1[i];
        }
        buf[i] = 0;
        ui_draw_text(6, 22, buf);
    }

    if (entry->text_line2 && g_dialog.char_index > 28) {
        limit = g_dialog.char_index - 28;
        if (limit > 28) limit = 28;
        for (i = 0; i < limit && entry->text_line2[i]; i++) {
            buf[i] = entry->text_line2[i];
        }
        buf[i] = 0;
        ui_draw_text(6, 23, buf);
    }
}

void dialog_draw_choice(void) {
    const DialogSequence *seq = &dialog_sequences[g_dialog.current_sequence];
    ui_clear_area(6, 22, 24, 2);

    if (g_dialog.choice_cursor == 0) {
        ui_draw_text(5, 22, ">");
    } else {
        ui_draw_text(5, 22, " ");
    }
    ui_draw_text(6, 22, seq->choice->option_a);

    if (g_dialog.choice_cursor == 1) {
        ui_draw_text(5, 23, ">");
    } else {
        ui_draw_text(5, 23, " ");
    }
    ui_draw_text(6, 23, seq->choice->option_b);
}
```

### story_data.h - COMPLETE GAME SCRIPT
```c
#ifndef STORY_DATA_H
#define STORY_DATA_H

/* ========================================================
 * VEX DEFENDER - COMPLETE GAME SCRIPT
 *
 * THE TWIST: "The Ark" colony ship that Vex is defending
 * was actually STOLEN from the alien race. The "aliens"
 * are the original owners trying to reclaim their ship.
 * Admiral Holt knew all along and has been using Vex
 * as a pawn. The alien commander Zyx is actually trying
 * to save their people aboard The Ark who are imprisoned
 * in cryo-chambers the humans repurposed.
 *
 * The player discovers this in Zone 3 and must CHOOSE:
 *   A) Side with the truth (help the aliens, fight Holt)
 *   B) Stay loyal to humanity (fight the alien commander)
 * Both paths lead to a final boss, but with different
 * final dialog and endings.
 * ========================================================*/

/* --- SEQUENCE 0: INTRO --- */
const DialogEntry intro_entries[] = {
    {4, 0, "SYSTEM", "YEAR 2847. THE LAST HUMAN",    "COLONY SHIP 'THE ARK' FLEES"},
    {4, 0, "SYSTEM", "THROUGH DEEP SPACE. AN ALIEN",  "ARMADA PURSUES RELENTLESSLY."},
    {2, 0, "ADMIRAL", "CADET VEX, YOU ARE OUR LAST",  "LINE OF DEFENSE."},
    {2, 0, "ADMIRAL", "THE ARK CARRIES 10000 SOULS.", "YOU MUST PROTECT THEM."},
    {1, 0, "VEX",    "UNDERSTOOD, ADMIRAL HOLT.",     "I WONT LET THEM DOWN."},
    {2, DIALOG_FLAG_END, "ADMIRAL", "GOOD. NOW GET OUT THERE",
                                    "AND CLEAR THE DEBRIS FIELD."},
};

const DialogSequence seq_intro = {intro_entries, 6, 0};

/* --- SEQUENCE 1: ZONE 1 CLEAR --- */
const DialogEntry zone1_clear_entries[] = {
    {2, 0, "ADMIRAL", "DEBRIS FIELD CLEARED.",         "WELL DONE, CADET."},
    {2, 0, "ADMIRAL", "BUT SENSORS SHOW A MASSIVE",    "FORCE IN THE ASTEROID BELT."},
    {1, 0, "VEX",    "IM PICKING UP STRANGE",          "SIGNALS FROM THEIR SHIPS..."},
    {2, 0, "ADMIRAL", "IGNORE THOSE. JUST STATIC.",    "FOCUS ON YOUR MISSION."},
    {1, DIALOG_FLAG_END, "VEX", "...ROGER THAT.",       "HEADING INTO THE BELT."},
};

const DialogSequence seq_zone1_clear = {zone1_clear_entries, 5, 0};

/* --- SEQUENCE 2: ZONE 2 CLEAR (MINI-BOSS DEFEATED) --- */
const DialogEntry zone2_clear_entries[] = {
    {1, 0, "VEX",    "THAT SENTINEL WAS TOUGH.",       "WHATS AHEAD?"},
    {2, 0, "ADMIRAL", "THE ALIEN FLAGSHIP. END THIS.", "DESTROY THEIR COMMANDER."},
    {1, 0, "VEX",    "ADMIRAL... THOSE SIGNALS",       "THEYRE NOT STATIC."},
    {1, 0, "VEX",    "IVE BEEN DECODING THEM.",        "THEYRE... DISTRESS CALLS?"},
    {2, 0, "ADMIRAL", "VEX. THATS AN ORDER.",          "PROCEED TO THE FLAGSHIP."},
    {1, DIALOG_FLAG_END, "VEX", "...SOMETHING DOESNT",
                                 "FEEL RIGHT ABOUT THIS."},
};

const DialogSequence seq_zone2_clear = {zone2_clear_entries, 6, 0};

/* --- SEQUENCE 3: THE TWIST (Zone 3, before final boss) --- */
const DialogEntry twist_entries[] = {
    {3, 0, "???",    "HUMAN PILOT... WAIT.",           "PLEASE. LISTEN TO ME."},
    {1, 0, "VEX",    "YOURE... THE ALIEN COMMANDER?",  "WHY ARENT YOU ATTACKING?"},
    {3, 0, "ZYX",    "MY NAME IS ZYX. THAT SHIP",     "YOU CALL THE ARK..."},
    {3, 0, "ZYX",    "IT IS OURS. YOUR ADMIRAL",      "STOLE IT FROM MY PEOPLE."},
    {3, 0, "ZYX",    "10000 OF MY PEOPLE SLEEP IN",   "THOSE CRYO-CHAMBERS."},
    {3, 0, "ZYX",    "THEY ARE NOT HUMAN COLONISTS.", "THEY ARE MY IMPRISONED KIN."},
    {1, 0, "VEX",    "WHAT?! THATS... THATS NOT...",  "ADMIRAL, IS THIS TRUE?!"},
    {2, 0, "ADMIRAL", "...VEX. IT DOESNT MATTER.",    "WE NEED THAT SHIP."},
    {2, 0, "ADMIRAL", "THEIR TECHNOLOGY WILL SAVE",   "HUMANITY. THE COST IS-"},
    {1, 0, "VEX",    "THE COST IS 10000 LIVES?!",     "YOU LIED TO ME!"},
    {2, 0, "ADMIRAL", "I DID WHAT I HAD TO.",         "NOW FINISH THE MISSION."},
    {3, DIALOG_FLAG_CHOICE, "ZYX", "PLEASE, VEX. HELP US FREE",
                                    "OUR PEOPLE. OR FIGHT ME."},
};

const DialogChoice twist_choice = {
    "HELP ZYX (THE TRUTH)",     /* Option A */
    "OBEY HOLT (LOYALTY)",      /* Option B */
    STORY_CHOSE_TRUTH,          /* Flag A */
    STORY_CHOSE_LOYALTY         /* Flag B */
};

const DialogSequence seq_twist = {twist_entries, 12, &twist_choice};

/* --- SEQUENCE 4: CHOSE TRUTH (Fight Admiral's drones) --- */
const DialogEntry truth_path_entries[] = {
    {3, 0, "ZYX",    "THANK YOU, VEX. BUT HOLT",     "WONT LET US GO EASILY."},
    {2, 0, "ADMIRAL", "TRAITOR! ACTIVATING THE",     "ARKS DEFENSE SYSTEMS!"},
    {1, 0, "VEX",    "BRING IT ON, ADMIRAL.",        "IM DONE BEING YOUR PAWN."},
    {4, DIALOG_FLAG_END, "SYSTEM", "WARNING: ARK DEFENSE",
                                    "TURRETS ACTIVATED!"},
};

const DialogSequence seq_truth_path = {truth_path_entries, 4, 0};

/* --- SEQUENCE 5: CHOSE LOYALTY (Fight Zyx) --- */
const DialogEntry loyalty_path_entries[] = {
    {1, 0, "VEX",    "IM SORRY ZYX. I CANT LET",     "HUMANITYS HOPE DIE."},
    {3, 0, "ZYX",    "THEN I HAVE NO CHOICE.",        "I MUST FIGHT FOR MY PEOPLE."},
    {2, 0, "ADMIRAL", "GOOD SOLDIER. TAKE OUT",      "THEIR COMMANDER."},
    {1, DIALOG_FLAG_END, "VEX", "(IS THIS... REALLY",
                                 "THE RIGHT THING TO DO?)"},
};

const DialogSequence seq_loyalty_path = {loyalty_path_entries, 4, 0};

/* --- SEQUENCE 6: VICTORY (TRUTH ending) --- */
const DialogEntry victory_truth_entries[] = {
    {3, 0, "ZYX",    "THE DEFENSE SYSTEMS ARE DOWN.", "MY PEOPLE... THEYRE WAKING UP."},
    {1, 0, "VEX",    "ZYX... WHAT HAPPENS NOW?",     "TO BOTH OUR PEOPLES?"},
    {3, 0, "ZYX",    "WE SHARE THIS SHIP. TOGETHER.", "THERE IS ROOM FOR ALL OF US."},
    {1, 0, "VEX",    "A NEW BEGINNING. FOR BOTH",    "OUR KINDS."},
    {4, DIALOG_FLAG_END, "SYSTEM", "AND SO, THE ARK SAILED ON",
                                    "CARRYING TWO PEOPLES AS ONE."},
};

const DialogSequence seq_victory_truth = {victory_truth_entries, 5, 0};

/* --- SEQUENCE 7: VICTORY (LOYALTY ending) --- */
const DialogEntry victory_loyalty_entries[] = {
    {2, 0, "ADMIRAL", "ITS DONE. THE ALIEN THREAT",  "IS ELIMINATED."},
    {1, 0, "VEX",    "...AT WHAT COST, ADMIRAL?",    "10000 LIVES?"},
    {2, 0, "ADMIRAL", "ACCEPTABLE LOSSES, CADET.",   "HUMANITYS FUTURE IS-"},
    {1, 0, "VEX",    "NO. WHEN WE ARRIVE...",        "ILL TELL EVERYONE THE TRUTH."},
    {4, DIALOG_FLAG_END, "SYSTEM", "VEX FLEW ON, HAUNTED BY",
                                    "THE WEIGHT OF THEIR CHOICE."},
};

const DialogSequence seq_victory_loyalty = {victory_loyalty_entries, 5, 0};

/* --- Master sequence table --- */
#define SEQ_INTRO         0
#define SEQ_ZONE1_CLEAR   1
#define SEQ_ZONE2_CLEAR   2
#define SEQ_TWIST         3
#define SEQ_TRUTH_PATH    4
#define SEQ_LOYALTY_PATH  5
#define SEQ_VICTORY_TRUTH 6
#define SEQ_VICTORY_LOYAL 7
#define SEQ_COUNT         8

const DialogSequence dialog_sequences[SEQ_COUNT] = {
    seq_intro,
    seq_zone1_clear,
    seq_zone2_clear,
    seq_twist,
    seq_truth_path,
    seq_loyalty_path,
    seq_victory_truth,
    seq_victory_loyalty,
};

#endif /* STORY_DATA_H */
```

## Acceptance Criteria
1. Dialog box appears at bottom of screen with proper border
2. Text reveals character-by-character (typewriter effect)
3. Pressing A skips typewriter and shows full text instantly
4. Pressing A after text is fully revealed advances to next entry
5. Character names display above dialog text
6. Choice menu appears with two options and navigable cursor
7. Choices set correct story flags (STORY_CHOSE_TRUTH or STORY_CHOSE_LOYALTY)
8. Dialog sequences play in correct order through the game
9. The twist dialog successfully presents the moral dilemma
10. Both ending sequences play based on player's twist choice
11. All text fits within 28 characters per line (no overflow)

## SNES-Specific Constraints
- All strings must be uppercase (PVSnesLib font is typically uppercase only)
- Max 28 characters per line at 8x8 font = 224 pixels (within 256px width)
- String constants stored in ROM via `const` - no WRAM allocation
- No dynamic string formatting - all text is pre-written
- Dialog data is ~2KB total in ROM

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~64KB | 256KB    | ~192KB    |
| WRAM     | ~1.15KB| 128KB  | ~127KB    |
| VRAM     | ~14KB | 64KB    | ~50KB     |
| CGRAM    | 192B  | 512B    | 320B      |

## Estimated Complexity
**Medium** - The typewriter effect and choice system are the main challenges. The dialog data is straightforward const ROM arrays.

## Agent Instructions
1. Create `src/dialog.h`, `src/dialog.c`, and `include/story_data.h`
2. Update Makefile and linkfile
3. Call `dialog_init()` in game_init()
4. Call `dialog_start(SEQ_INTRO)` when entering STATE_STORY_INTRO
5. Add `dialog_update()` call in STATE_DIALOG case of game_update()
6. Test: trigger intro sequence, verify all 6 lines display correctly
7. Test: trigger twist sequence, verify choice works and sets flags
8. Test: verify typewriter speed and A-to-skip work
9. Verify all text lines are 28 chars or less
10. Test both endings by choosing different options at the twist
