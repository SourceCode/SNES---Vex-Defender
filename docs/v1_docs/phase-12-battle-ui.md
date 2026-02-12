# Phase 12: Battle UI & Menu System

## Objective
Create the visual battle interface that displays during turn-based combat. This includes: battle background, player/enemy HP bars, action menu (ATTACK/DEFEND/SPECIAL/ITEM), damage number popups, and battle message text. The UI is rendered on BG2 (tilemap-based menus) and BG3 (text), with combatant sprites on OBJ layer.

## Prerequisites
- Phase 11 (Battle Engine logic), Phase 4 (Background system for battle bg swap).

## Detailed Tasks

1. Create `src/ui/battle_ui.c` - Battle scene visual manager.
2. Implement battle scene transition: fade out flight mode, load battle background into BG1, load UI tiles into BG2, show battle sprites.
3. Implement HP bars for player and enemy using BG2 tiles (filled/empty bar segments).
4. Implement the 4-option action menu with cursor navigation (D-pad up/down, A to confirm).
5. Implement battle message display on BG3 (text line at bottom of screen).
6. Implement damage number display (sprite-based or text-based popup).
7. Implement simple attack animations (sprite flash, shake, or slide).
8. Handle battle exit transition: fade out battle, restore flight graphics, fade in flight mode.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/ui/battle_ui.h
```c
#ifndef BATTLE_UI_H
#define BATTLE_UI_H

#include "game.h"
#include "battle/battle_engine.h"

/* Battle UI layout constants (tile positions on BG2 32x32 tilemap) */

/* Enemy info panel: top of screen */
#define BUI_ENEMY_NAME_X    2
#define BUI_ENEMY_NAME_Y    1
#define BUI_ENEMY_HP_BAR_X  2
#define BUI_ENEMY_HP_BAR_Y  2
#define BUI_ENEMY_HP_LEN    12  /* Bar width in tiles */

/* Player info panel: bottom-right */
#define BUI_PLAYER_NAME_X   16
#define BUI_PLAYER_NAME_Y   20
#define BUI_PLAYER_HP_X     16
#define BUI_PLAYER_HP_Y     21
#define BUI_PLAYER_SP_X     16
#define BUI_PLAYER_SP_Y     22

/* Action menu: bottom-left */
#define BUI_MENU_X          2
#define BUI_MENU_Y          20
#define BUI_MENU_CURSOR_X   1
#define BUI_MENU_ITEM_COUNT 4

/* Message box: very bottom */
#define BUI_MSG_X           1
#define BUI_MSG_Y           25

/* Battle sprite positions */
#define BUI_PLAYER_SPR_X    180
#define BUI_PLAYER_SPR_Y    120
#define BUI_ENEMY_SPR_X     60
#define BUI_ENEMY_SPR_Y     60

/* Initialize battle UI system */
void battleUIInit(void);

/* Transition into battle scene (loads graphics, sets up tilemaps)
 * This enters force blank, swaps VRAM content, and fades in. */
void battleUIEnter(void);

/* Transition out of battle scene (restores flight graphics) */
void battleUIExit(void);

/* Update battle UI each frame (menu navigation, animations) */
void battleUIUpdate(void);

/* Render battle UI elements (HP bars, menu, messages) */
void battleUIRender(void);

/* Get the currently selected menu action */
u8 battleUIGetSelectedAction(void);

/* Show a damage number at the specified combatant */
void battleUIShowDamage(s16 damage, u8 isPlayer);

/* Show battle message text */
void battleUIShowMessage(const char *msg);

/* Is the UI in the "player choosing action" state? */
u8 battleUIIsMenuActive(void);

#endif
```

### J:/code/snes/snes-rpg-test/src/ui/battle_ui.c
```c
/*==============================================================================
 * Battle UI
 *
 * Visual layout:
 *   +---------------------------------+
 *   | ENEMY_NAME      HP [==========] |  <- BG2 tiles / BG3 text
 *   |                                  |
 *   |        [ENEMY SPRITE]            |  <- OAM
 *   |                                  |
 *   |                                  |
 *   |              [PLAYER SPRITE]     |  <- OAM
 *   |                                  |
 *   | > ATTACK    | VEX     HP: 100    |  <- BG2 tiles / BG3 text
 *   |   DEFEND    | LV:1    SP:   3    |
 *   |   SPECIAL   |                    |
 *   |   ITEM      |                    |
 *   +---------------------------------+
 *   | MESSAGE TEXT LINE                |  <- BG3 text
 *   +---------------------------------+
 *============================================================================*/

#include "ui/battle_ui.h"
#include "engine/input.h"
#include "engine/fade.h"
#include "engine/sprites.h"
#include "engine/background.h"

/* UI state */
static u8 menu_cursor;         /* 0-3: which action is highlighted */
static u8 menu_active;         /* 1 if player is choosing */
static u8 menu_confirmed;      /* 1 when player pressed confirm */
static u8 selected_action;     /* The confirmed action */

/* Animation state */
static u8 damage_display_timer;
static s16 damage_display_value;
static u8 damage_display_target;  /* 0=enemy, 1=player */
static u8 shake_timer;
static s8 shake_offset_x;

/* OAM slots for battle sprites */
#define BATTLE_PLAYER_OAM  44  /* OAM slot * 4 = 176 */
#define BATTLE_ENEMY_OAM   48  /* OAM slot * 4 = 192 */

/* Menu option labels */
static const char *menu_labels[4] = {
    "ATTACK", "DEFEND", "SPECIAL", "ITEM"
};

void battleUIInit(void)
{
    menu_cursor = 0;
    menu_active = 0;
    menu_confirmed = 0;
    damage_display_timer = 0;
    shake_timer = 0;
}

void battleUIEnter(void)
{
    /* Fade out current scene */
    fadeOutBlocking(15);

    /* Enter force blank for VRAM changes */
    setScreenOff();
    dmaClearVram();

    /* Load battle background into BG1 */
    /* For now, use the current zone background */
    /* Phase 18 adds dedicated battle backgrounds */
    bgLoadZone(ZONE_DEBRIS);  /* Placeholder */

    /* Set up BG2 for UI panels */
    /* Clear BG2 tilemap to transparent */
    /* In a real implementation, load UI frame tiles here */
    bgSetMapPtr(1, BG2_TILEMAP_VRAM, SC_32x32);
    bgSetEnable(1);

    /* Set up BG3 for text */
    consoleInitText(0, BG_4COLORS, 0, 0);
    bgSetEnable(2);

    /* Disable scrolling for battle */
    bgSetScroll(0, 0, 0);
    bgSetScroll(1, 0, 0);
    bgSetScroll(2, 0, 0);

    /* Hide all flight-mode sprites */
    spriteHideAll();

    /* Show battle combatant sprites */
    /* Player sprite on right side */
    oamSet(BATTLE_PLAYER_OAM * 4,
           BUI_PLAYER_SPR_X, BUI_PLAYER_SPR_Y,
           3, 0, 0, 0, 0);
    oamSetEx(BATTLE_PLAYER_OAM * 4, OBJ_LARGE, OBJ_SHOW);

    /* Enemy sprite on left side */
    oamSet(BATTLE_ENEMY_OAM * 4,
           BUI_ENEMY_SPR_X, BUI_ENEMY_SPR_Y,
           3, 0, 0,
           (OBJ_ENEMY_OFFSET >> 4),
           1);
    oamSetEx(BATTLE_ENEMY_OAM * 4, OBJ_LARGE, OBJ_SHOW);

    /* Draw static UI elements */
    battleUIRender();

    /* Fade in */
    setScreenOn();
    fadeInBlocking(15);

    menu_cursor = 0;
    menu_active = 0;
    menu_confirmed = 0;
}

void battleUIExit(void)
{
    fadeOutBlocking(15);

    /* Restore flight mode graphics */
    setScreenOff();
    dmaClearVram();

    /* The scene manager (Phase 15) handles reloading flight graphics */
    /* For now, just prepare for the transition */

    oamSetVisible(BATTLE_PLAYER_OAM * 4, OBJ_HIDE);
    oamSetVisible(BATTLE_ENEMY_OAM * 4, OBJ_HIDE);
}

void battleUIUpdate(void)
{
    u16 pressed = inputPressed();

    /* Menu navigation */
    if (menu_active) {
        if (pressed & ACTION_UP) {
            if (menu_cursor > 0) menu_cursor--;
        }
        if (pressed & ACTION_DOWN) {
            if (menu_cursor < BUI_MENU_ITEM_COUNT - 1) menu_cursor++;
        }
        if (pressed & ACTION_CONFIRM) {
            selected_action = menu_cursor;
            menu_confirmed = 1;
            menu_active = 0;
        }
    }

    /* Update shake animation */
    if (shake_timer > 0) {
        shake_timer--;
        shake_offset_x = (shake_timer & 2) ? 2 : -2;
        if (shake_timer == 0) shake_offset_x = 0;
    }

    /* Update damage display timer */
    if (damage_display_timer > 0) {
        damage_display_timer--;
    }
}

void battleUIRender(void)
{
    u8 i;
    u8 hp_filled;
    char buf[16];

    /* Draw enemy name and HP bar */
    consoleDrawText(BUI_ENEMY_NAME_X, BUI_ENEMY_NAME_Y,
                    "%s", battle.enemy.name);

    /* HP bar: calculate filled portion (0-12 tiles) */
    if (battle.enemy.max_hp > 0) {
        hp_filled = (u8)((u16)battle.enemy.hp * BUI_ENEMY_HP_LEN / battle.enemy.max_hp);
    } else {
        hp_filled = 0;
    }
    /* Draw HP bar using text characters */
    consoleDrawText(BUI_ENEMY_HP_BAR_X, BUI_ENEMY_HP_BAR_Y, "HP:");
    for (i = 0; i < BUI_ENEMY_HP_LEN; i++) {
        if (i < hp_filled) {
            consoleDrawText(BUI_ENEMY_HP_BAR_X + 3 + i, BUI_ENEMY_HP_BAR_Y, "=");
        } else {
            consoleDrawText(BUI_ENEMY_HP_BAR_X + 3 + i, BUI_ENEMY_HP_BAR_Y, "-");
        }
    }

    /* Draw player info panel */
    consoleDrawText(BUI_PLAYER_NAME_X, BUI_PLAYER_NAME_Y,
                    "%s  LV:%d", battle.player.name, battle.player.level);
    consoleDrawText(BUI_PLAYER_HP_X, BUI_PLAYER_HP_Y,
                    "HP:%d/%d", battle.player.hp, battle.player.max_hp);
    consoleDrawText(BUI_PLAYER_SP_X, BUI_PLAYER_SP_Y,
                    "SP:%d/%d", battle.player.sp, battle.player.max_sp);

    /* Draw action menu */
    for (i = 0; i < BUI_MENU_ITEM_COUNT; i++) {
        if (menu_active && i == menu_cursor) {
            consoleDrawText(BUI_MENU_CURSOR_X, BUI_MENU_Y + i, ">");
        } else {
            consoleDrawText(BUI_MENU_CURSOR_X, BUI_MENU_Y + i, " ");
        }
        consoleDrawText(BUI_MENU_X, BUI_MENU_Y + i, "%s", menu_labels[i]);
    }

    /* Draw battle message */
    {
        const char *msg = battleGetMessage();
        /* Clear message line first */
        consoleDrawText(BUI_MSG_X, BUI_MSG_Y, "                              ");
        if (msg[0] != 0) {
            consoleDrawText(BUI_MSG_X, BUI_MSG_Y, "%s", msg);
        }
    }

    /* Draw damage number if active */
    if (damage_display_timer > 0) {
        u8 dx, dy;
        if (damage_display_target == 0) {
            dx = BUI_ENEMY_SPR_X / 8 + 2;
            dy = BUI_ENEMY_SPR_Y / 8 - 1;
        } else {
            dx = BUI_PLAYER_SPR_X / 8 + 2;
            dy = BUI_PLAYER_SPR_Y / 8 - 1;
        }
        if (damage_display_value < 0) {
            consoleDrawText(dx, dy, "+%d", -damage_display_value);
        } else {
            consoleDrawText(dx, dy, "%d", damage_display_value);
        }
    }

    /* Update battle sprite positions (with shake) */
    if (shake_timer > 0) {
        if (damage_display_target == 0) {
            oamSetXY(BATTLE_ENEMY_OAM * 4,
                     BUI_ENEMY_SPR_X + shake_offset_x, BUI_ENEMY_SPR_Y);
        } else {
            oamSetXY(BATTLE_PLAYER_OAM * 4,
                     BUI_PLAYER_SPR_X + shake_offset_x, BUI_PLAYER_SPR_Y);
        }
    }
}

u8 battleUIGetSelectedAction(void)
{
    if (menu_confirmed) {
        menu_confirmed = 0;
        return selected_action;
    }
    return 0xFF; /* No selection yet */
}

void battleUIShowDamage(s16 damage, u8 isPlayer)
{
    damage_display_value = damage;
    damage_display_target = isPlayer;
    damage_display_timer = 30;  /* Show for 0.5 sec */
    shake_timer = 8;  /* Shake for ~8 frames */
}

void battleUIShowMessage(const char *msg)
{
    consoleDrawText(BUI_MSG_X, BUI_MSG_Y, "                              ");
    consoleDrawText(BUI_MSG_X, BUI_MSG_Y, "%s", msg);
}

u8 battleUIIsMenuActive(void)
{
    return menu_active;
}
```

## Technical Specifications

### Battle Screen Layout (256x224, in tiles of 8x8 = 32x28)
```
Row  Content
---  -------
0    [top border]
1    SCOUT            HP:[============]
2    [empty]
3-6  [enemy sprite area]
7-14 [middle space]
15-18 [player sprite area]
19   [separator]
20   > ATTACK    |  VEX    LV:1
21     DEFEND    |  HP: 100/100
22     SPECIAL   |  SP:   3/  3
23     ITEM      |
24   [separator]
25   ENEMY APPEARED!
26-27 [bottom border/empty]
```

### Tile-Based HP Bar
```
Using text characters for simplicity:
  Full HP:  HP:[============]   (12 '=' characters)
  Half HP:  HP:[======------]
  Low HP:   HP:[==----------]
  Dead:     HP:[-----------]

Each '=' or '-' is one text character on BG3.
For a graphical HP bar with custom tiles, allocate:
  - 3 tiles: bar-start, bar-filled, bar-empty, bar-end
  - Draw on BG2 tilemap with palette color coding (green/yellow/red)
This can be enhanced in Phase 20 (Polish).
```

### Integration with Battle Engine
```
In the main battle loop:

while (battleUpdate()) {
    inputUpdate();
    battleUIUpdate();

    /* Check if engine is waiting for player input */
    if (battle.state == BSTATE_PLAYER_TURN) {
        menu_active = 1;
        u8 action = battleUIGetSelectedAction();
        if (action != 0xFF) {
            battleSelectAction(action);
            menu_active = 0;
        }
    }

    /* Show damage popup when action resolves */
    if (battle.state == BSTATE_RESOLVE && battle.last_damage != 0) {
        u8 target = (battle.turn_number & 1) ? 0 : 1;
        battleUIShowDamage(battle.last_damage, target);
    }

    battleUIRender();
    WaitForVBlank();
}
```

## Acceptance Criteria
1. Battle screen displays with enemy sprite on the left, player sprite on the right.
2. Enemy name and HP bar are visible at the top.
3. Player name, level, HP, and SP are visible at bottom-right.
4. Action menu (ATTACK/DEFEND/SPECIAL/ITEM) appears at bottom-left.
5. D-pad up/down moves the menu cursor with a visible ">" indicator.
6. Pressing A confirms the selected action.
7. Damage numbers appear briefly near the hit combatant.
8. Hit sprites shake briefly on damage.
9. Battle messages appear at the bottom of the screen.
10. Transition in/out of battle uses fade effects with no VRAM corruption.

## SNES-Specific Constraints
- consoleDrawText uses BG3 with PVSnesLib's built-in font. It writes to a RAM buffer that is DMA'd during VBlank. Multiple draws per frame are fine as long as total text fits.
- The BG3 tilemap has limited space (32x32 = 1024 tiles). Text lines should be kept short (under 30 characters).
- sprintf-style formatting in consoleDrawText may be slow. Minimize formatted prints per frame (cache values that don't change).
- Battle transition replaces VRAM content. Must save/restore flight VRAM when entering/exiting battle.

## Estimated Complexity
**Complex** - UI layout, menu navigation, animated damage numbers, and VRAM transitions require careful coordinate math and timing. This is the most visually complex phase.
