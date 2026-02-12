# Phase 12: Battle UI & Menu System

## Objective
Create the complete visual interface for the turn-based battle system: HP/MP bars, action menu with cursor, damage numbers, battle messages, enemy/player sprite positioning on the battle screen, and animated attack effects. Uses BG3 (text layer) and BG2 (battle background) with sprite overlays.

## Prerequisites
- Phase 11 (Battle System Core) complete
- Phase 4 (Background Rendering) complete
- Phase 3 (Asset Pipeline) complete - fonts available

## Detailed Tasks

### 1. Create Font/Text Rendering System
Load a bitmap font and render text strings on BG3 for battle messages and UI.

### 2. Design Battle Screen Layout
```
+----------------------------------+
|  VEX       HP: ███████░░ 75/100  |  <- Top: Player info
|            MP: ████░░░░░  25/50  |
|                                  |
|                                  |
|         [ENEMY SPRITE]           |  <- Center: Enemy display
|                                  |
|                                  |
|  [PLAYER SPRITE]                 |  <- Lower: Player display
|                                  |
|  +----------+  +--------------+  |
|  | > ATTACK |  | Enemy dealt  |  |  <- Bottom: Menu + Messages
|  |   SPECIAL|  | 15 damage!   |  |
|  |   ITEM   |  |              |  |
|  |   DEFEND |  |              |  |
|  +----------+  +--------------+  |
+----------------------------------+
```

### 3. Implement HP/MP Bar Rendering
Graphical bars that fill/deplete based on current values.

### 4. Implement Menu Cursor System
Animated cursor (blinking arrow) that moves with D-pad input.

### 5. Implement Damage Number Display
Show damage dealt as floating numbers that rise and fade.

### 6. Implement Battle Message Log
Text area showing action descriptions ("Vex attacks!", "Enemy takes 15 damage!").

### 7. Create Attack Animations
Simple sprite-based animations for attacks (flash, shake, slide).

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/ui.h` | CREATE | UI system header |
| `src/ui.c` | CREATE | UI rendering implementation |
| `src/battle.c` | MODIFY | Integrate UI calls |
| `Makefile` | MODIFY | Add ui.obj |
| `data/linkfile` | MODIFY | Add ui.obj |

## Technical Specifications

### Font System
```c
/* Font tile layout in VRAM (BG3 at $3000):
 * Standard ASCII font, 8x8 tiles, 2bpp (4 colors)
 * Characters: Space(32) through '~'(126) = 95 chars
 * 95 tiles x 16 bytes (2bpp 8x8) = 1520 bytes in VRAM
 *
 * BG3 tilemap at $6000:
 * 32x32 = 1024 entries, each 2 bytes = 2KB
 * Text is written by setting tilemap entries to tile indices
 */

/* To write text: convert ASCII char to tile index */
/* tile_index = char_value - 32 (space = tile 0, 'A' = tile 33) */
```

### ui.h
```c
#ifndef UI_H
#define UI_H

#include <snes.h>
#include "config.h"

/* UI layout positions (in tile coordinates, 8px per tile) */
/* Screen is 32x28 tiles (256x224 pixels) */

/* Player info area (top of screen) */
#define UI_PLAYER_NAME_X  1
#define UI_PLAYER_NAME_Y  1
#define UI_PLAYER_HP_X    1
#define UI_PLAYER_HP_Y    2
#define UI_PLAYER_MP_X    1
#define UI_PLAYER_MP_Y    3

/* Enemy info area (below player info) */
#define UI_ENEMY_NAME_X   20
#define UI_ENEMY_NAME_Y   1
#define UI_ENEMY_HP_X     20
#define UI_ENEMY_HP_Y     2

/* Battle menu (bottom-left) */
#define UI_MENU_X         2
#define UI_MENU_Y        21
#define UI_MENU_WIDTH     8
#define UI_MENU_HEIGHT    4

/* Message area (bottom-right) */
#define UI_MSG_X         12
#define UI_MSG_Y         21
#define UI_MSG_WIDTH     18
#define UI_MSG_HEIGHT     4

/* HP bar dimensions */
#define HP_BAR_WIDTH     10  /* tiles wide */
#define HP_BAR_CHAR_FULL  0x80  /* Custom tile: full block */
#define HP_BAR_CHAR_EMPTY 0x81  /* Custom tile: empty block */
#define HP_BAR_CHAR_HALF  0x82  /* Custom tile: half block */

/* Cursor character */
#define UI_CURSOR_CHAR   '>'

/* Flight mode HUD */
#define UI_HUD_HP_X       1
#define UI_HUD_HP_Y       0
#define UI_HUD_SCORE_X   20
#define UI_HUD_SCORE_Y    0
#define UI_HUD_ZONE_X    10
#define UI_HUD_ZONE_Y     0

/*--- Functions ---*/
void ui_init(void);
void ui_load_font(void);

/* Text rendering (BG3) */
void ui_draw_text(u8 x, u8 y, const char *text);
void ui_draw_number(u8 x, u8 y, u16 value, u8 digits);
void ui_clear_area(u8 x, u8 y, u8 w, u8 h);
void ui_clear_all(void);

/* Battle UI */
void ui_draw_battle_screen(void);
void ui_draw_battle_menu(u8 cursor_pos);
void ui_draw_hp_bar(u8 x, u8 y, u16 current, u16 max);
void ui_draw_battle_message(const char *msg);
void ui_update_battle_stats(void);

/* Damage number popup */
void ui_show_damage(s16 x, s16 y, u16 damage);
void ui_update_damage_popup(void);

/* Flight HUD */
void ui_draw_flight_hud(void);
void ui_update_flight_hud(void);

/* Window/box drawing */
void ui_draw_box(u8 x, u8 y, u8 w, u8 h);

#endif /* UI_H */
```

### ui.c (Core Implementation)
```c
#include "ui.h"
#include "battle.h"
#include "player.h"

/* Font data - extern from ASM */
extern char tilfont, palfont;

/* Damage popup state */
static s16 dmg_popup_x;
static s16 dmg_popup_y;
static u16 dmg_popup_value;
static u8  dmg_popup_timer;

void ui_init(void) {
    dmg_popup_timer = 0;
    ui_load_font();
}

void ui_load_font(void) {
    /* Use PVSnesLib's built-in font system on BG3 */
    consoleSetTextMapPtr(VRAM_BG3_MAP);
    consoleSetTextGfxPtr(VRAM_BG3_TILES);
    consoleSetTextOffset(0x0100);  /* Tile offset in map entries */
    consoleInitText(2, PAL_BG3_FONT * 2, &tilfont, &palfont);
}

void ui_draw_text(u8 x, u8 y, const char *text) {
    consoleDrawText(x, y, text);
}

void ui_draw_number(u8 x, u8 y, u16 value, u8 digits) {
    char buf[6];
    u8 i;
    /* Manual number-to-string (no sprintf on SNES) */
    for (i = 0; i < digits; i++) {
        buf[digits - 1 - i] = '0' + (value % 10);
        value /= 10;
    }
    buf[digits] = 0;

    /* Skip leading zeros (replace with spaces) */
    for (i = 0; i < digits - 1; i++) {
        if (buf[i] == '0') buf[i] = ' ';
        else break;
    }

    consoleDrawText(x, y, buf);
}

void ui_clear_area(u8 x, u8 y, u8 w, u8 h) {
    u8 row, col;
    char spaces[33]; /* Max 32 chars + null */
    for (col = 0; col < w && col < 32; col++) spaces[col] = ' ';
    spaces[col] = 0;
    for (row = 0; row < h; row++) {
        consoleDrawText(x, y + row, spaces);
    }
}

void ui_clear_all(void) {
    ui_clear_area(0, 0, 32, 28);
}

void ui_draw_box(u8 x, u8 y, u8 w, u8 h) {
    /* Simple box using ASCII-like characters */
    u8 i;
    char line[33];

    /* Top border */
    line[0] = '+';
    for (i = 1; i < w - 1; i++) line[i] = '-';
    line[w - 1] = '+';
    line[w] = 0;
    consoleDrawText(x, y, line);

    /* Sides */
    line[0] = '|';
    for (i = 1; i < w - 1; i++) line[i] = ' ';
    line[w - 1] = '|';
    for (i = 1; i < h - 1; i++) {
        consoleDrawText(x, y + i, line);
    }

    /* Bottom border */
    line[0] = '+';
    for (i = 1; i < w - 1; i++) line[i] = '-';
    line[w - 1] = '+';
    consoleDrawText(x, y + h - 1, line);
}

/* Draw complete battle screen */
void ui_draw_battle_screen(void) {
    ui_clear_all();

    /* Player info */
    ui_draw_text(UI_PLAYER_NAME_X, UI_PLAYER_NAME_Y, g_battle.player.name);
    ui_draw_text(UI_PLAYER_HP_X, UI_PLAYER_HP_Y, "HP:");
    ui_draw_hp_bar(UI_PLAYER_HP_X + 3, UI_PLAYER_HP_Y,
                   g_battle.player.hp, g_battle.player.max_hp);
    ui_draw_text(UI_PLAYER_MP_X, UI_PLAYER_MP_Y, "MP:");
    ui_draw_hp_bar(UI_PLAYER_MP_X + 3, UI_PLAYER_MP_Y,
                   g_battle.player.mp, g_battle.player.max_mp);

    /* Enemy info */
    ui_draw_text(UI_ENEMY_NAME_X, UI_ENEMY_NAME_Y, g_battle.enemy.name);
    ui_draw_text(UI_ENEMY_HP_X, UI_ENEMY_HP_Y, "HP:");
    ui_draw_hp_bar(UI_ENEMY_HP_X + 3, UI_ENEMY_HP_Y,
                   g_battle.enemy.hp, g_battle.enemy.max_hp);

    /* Menu box */
    ui_draw_box(UI_MENU_X - 1, UI_MENU_Y - 1, UI_MENU_WIDTH + 2, UI_MENU_HEIGHT + 2);

    /* Message box */
    ui_draw_box(UI_MSG_X - 1, UI_MSG_Y - 1, UI_MSG_WIDTH + 2, UI_MSG_HEIGHT + 2);
}

void ui_draw_battle_menu(u8 cursor_pos) {
    u8 i;
    for (i = 0; i < BACT_COUNT; i++) {
        /* Draw cursor or space */
        if (i == cursor_pos) {
            ui_draw_text(UI_MENU_X, UI_MENU_Y + i, ">");
        } else {
            ui_draw_text(UI_MENU_X, UI_MENU_Y + i, " ");
        }
        /* Draw action name */
        ui_draw_text(UI_MENU_X + 1, UI_MENU_Y + i, battle_action_names[i]);
    }
}

void ui_draw_hp_bar(u8 x, u8 y, u16 current, u16 max) {
    /* Draw a text-based HP bar: [=====     ] 75/100 */
    char bar[13]; /* 10 chars + brackets + null */
    u8 filled, i;

    if (max == 0) max = 1; /* Avoid divide by zero */

    /* Calculate filled portion (0-10 scale) */
    filled = (u8)((u16)(current * 10) / max);
    if (filled > 10) filled = 10;

    bar[0] = '[';
    for (i = 0; i < 10; i++) {
        bar[i + 1] = (i < filled) ? '=' : ' ';
    }
    bar[11] = ']';
    bar[12] = 0;

    consoleDrawText(x, y, bar);

    /* Numeric value */
    ui_draw_number(x + 13, y, current, 3);
}

void ui_draw_battle_message(const char *msg) {
    /* Clear message area and draw new message */
    ui_clear_area(UI_MSG_X, UI_MSG_Y, UI_MSG_WIDTH, UI_MSG_HEIGHT);
    ui_draw_text(UI_MSG_X, UI_MSG_Y, msg);
}

void ui_update_battle_stats(void) {
    /* Refresh just the changing values (HP/MP bars) */
    ui_draw_hp_bar(UI_PLAYER_HP_X + 3, UI_PLAYER_HP_Y,
                   g_battle.player.hp, g_battle.player.max_hp);
    ui_draw_hp_bar(UI_PLAYER_MP_X + 3, UI_PLAYER_MP_Y,
                   g_battle.player.mp, g_battle.player.max_mp);
    ui_draw_hp_bar(UI_ENEMY_HP_X + 3, UI_ENEMY_HP_Y,
                   g_battle.enemy.hp, g_battle.enemy.max_hp);
}

/* Floating damage number */
void ui_show_damage(s16 x, s16 y, u16 damage) {
    dmg_popup_x = x;
    dmg_popup_y = y;
    dmg_popup_value = damage;
    dmg_popup_timer = 30; /* Display for 30 frames */
}

void ui_update_damage_popup(void) {
    if (dmg_popup_timer > 0) {
        dmg_popup_timer--;
        dmg_popup_y--; /* Float upward */

        /* Draw damage number using sprites or text */
        /* Using text overlay for simplicity */
        u8 tx = (u8)(dmg_popup_x >> 3); /* Convert pixel to tile coords */
        u8 ty = (u8)(dmg_popup_y >> 3);
        if (dmg_popup_timer > 0) {
            ui_draw_number(tx, ty, dmg_popup_value, 3);
        } else {
            /* Clear when done */
            ui_clear_area(tx, ty, 3, 1);
        }
    }
}

/* Flight mode HUD */
void ui_draw_flight_hud(void) {
    ui_draw_text(UI_HUD_HP_X, UI_HUD_HP_Y, "HP:");
    ui_draw_text(UI_HUD_SCORE_X, UI_HUD_SCORE_Y, "SC:");
}

void ui_update_flight_hud(void) {
    /* Update HP display */
    ui_draw_number(UI_HUD_HP_X + 3, UI_HUD_HP_Y, g_player.hp, 3);

    /* Update score */
    ui_draw_number(UI_HUD_SCORE_X + 3, UI_HUD_SCORE_Y, g_game.score, 5);
}
```

## Acceptance Criteria
1. Battle screen displays with correct layout (player info, enemy info, menu, messages)
2. HP bars visually reflect current/max HP ratio
3. Menu cursor moves with D-pad and highlights current selection
4. Battle messages appear in the message area ("Vex attacks!", "15 damage!")
5. Damage numbers appear and float upward when damage is dealt
6. HP/MP bars update in real-time during battle
7. UI boxes render correctly with border characters
8. Text is legible and properly positioned on screen
9. Flight HUD shows HP and score during flight mode
10. UI clears properly when transitioning between states

## SNES-Specific Constraints
- BG3 in Mode 1 is 2bpp (4 colors) - font is limited to 4 colors
- consoleDrawText writes to BG3 tilemap in VRAM - safe during VBlank
- Maximum 1024 unique tiles on BG3 (more than enough for ASCII font)
- Text rendering via tilemap is fast (no per-pixel drawing)
- Division for HP bar calculation is slow - consider pre-computed lookup

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~52KB | 256KB    | ~204KB    |
| WRAM     | ~950B | 128KB   | ~127KB    |
| VRAM     | ~14KB | 64KB    | ~50KB     |
| CGRAM    | 160B  | 512B    | 352B      |

## Estimated Complexity
**Medium-Complex** - Lots of UI layout code, but each piece is simple text rendering. The HP bar and damage popup are the most visually complex parts.

## Agent Instructions
1. Create `src/ui.h` and `src/ui.c`
2. Update Makefile and linkfile
3. Must create/convert a font bitmap (use PVSnesLib's pvsneslibfont.png from examples)
4. Call `ui_init()` after game_init()
5. In battle_update(), call `ui_draw_battle_screen()` on BSTATE_TRANSITION_IN
6. Call `ui_draw_battle_menu()` during BSTATE_PLAYER_TURN
7. Call `ui_update_battle_stats()` after every action
8. Call `ui_draw_battle_message()` with appropriate text for each action
9. Test: verify text is readable at SNES resolution (256x224)
10. Enable BG3 in battle mode: `bgSetEnable(2);`
