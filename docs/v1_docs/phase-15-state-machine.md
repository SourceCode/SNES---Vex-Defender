# Phase 15: Game State Machine & Scene Management

## Objective
Implement the master game state machine that manages transitions between all game scenes: Title Screen, Flight Mode, Battle Mode, Dialog, Pause Menu, Game Over, and Victory. Each state has its own init/update/render/exit lifecycle. This is the glue that ties all previous systems together into a cohesive game loop.

## Prerequisites
- Phase 2 (System init), Phase 4 (Backgrounds), Phase 5-6 (Player/Input), Phase 7 (Scrolling), Phase 8-10 (Combat), Phase 11-12 (Battle).

## Detailed Tasks

1. Create `src/game/game_state.c` - Master state machine with state stack (for pause overlay).
2. Define game states: TITLE, FLIGHT, BATTLE, DIALOG, PAUSE, GAME_OVER, VICTORY.
3. Implement state transition with proper cleanup: exit old state, init new state.
4. Implement FLIGHT state: the main gameplay loop (scroll, spawn, shoot, collide).
5. Implement BATTLE state: wraps the battle engine and UI from Phases 11-12.
6. Implement PAUSE state: overlay on flight mode, shows stats/inventory.
7. Implement the main() function that delegates to the state machine.
8. Wire the collision battle callback (Phase 10) to trigger FLIGHT->BATTLE transition.
9. Wire battle exit to trigger BATTLE->FLIGHT transition with proper VRAM restore.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/game/game_state.h
```c
#ifndef GAME_STATE_H
#define GAME_STATE_H

#include "game.h"

/* Game states */
#define GS_NONE      0
#define GS_TITLE     1
#define GS_FLIGHT    2
#define GS_BATTLE    3
#define GS_DIALOG    4
#define GS_PAUSE     5
#define GS_GAME_OVER 6
#define GS_VICTORY   7

/* State transition types */
#define TRANS_NONE       0
#define TRANS_FADE       1  /* Fade out -> fade in */
#define TRANS_INSTANT    2  /* Immediate swap */

/* Initialize state machine */
void gameStateInit(void);

/* Set the next state (will transition on next frame) */
void gameStateChange(u8 newState, u8 transitionType);

/* Push an overlay state (e.g., pause) without destroying current */
void gameStatePush(u8 overlayState);

/* Pop overlay state, return to previous */
void gameStatePop(void);

/* Run one frame of the current state
 * This is the main game tick called from the while(1) loop */
void gameStateUpdate(void);

/* Get current active state */
u8 gameStateGetCurrent(void);

/* Is the game in a specific state? */
u8 gameStateIs(u8 state);

/* Store data for state transitions (e.g., which enemy triggered battle) */
void gameStateSetParam(u16 param);
u16 gameStateGetParam(void);

#endif
```

### J:/code/snes/snes-rpg-test/src/game/game_state.c
```c
/*==============================================================================
 * Game State Machine
 *
 * Manages the lifecycle of all game scenes.
 * Each state has: enter(), update(), render(), exit() functions.
 *============================================================================*/

#include "game/game_state.h"
#include "engine/system.h"
#include "engine/input.h"
#include "engine/fade.h"
#include "engine/scroll.h"
#include "engine/background.h"
#include "engine/sprites.h"
#include "engine/bullets.h"
#include "engine/collision.h"
#include "game/player.h"
#include "game/enemies.h"
#include "game/rpg_stats.h"
#include "game/inventory.h"
#include "battle/battle_engine.h"
#include "ui/battle_ui.h"

static u8 current_state;
static u8 pending_state;
static u8 pending_transition;
static u8 overlay_state;       /* For pause menu overlay */
static u8 previous_state;      /* State to return to after overlay */
static u16 state_param;        /* Parameter passed between states */

/* Forward declarations for state functions */
static void enterTitle(void);
static void updateTitle(void);
static void exitTitle(void);

static void enterFlight(void);
static void updateFlight(void);
static void exitFlight(void);

static void enterBattle(void);
static void updateBattle(void);
static void exitBattle(void);

static void enterPause(void);
static void updatePause(void);
static void exitPause(void);

static void enterGameOver(void);
static void updateGameOver(void);

static void enterVictory(void);
static void updateVictory(void);

/* Battle trigger callback (registered with collision system) */
static void onBattleTriggered(u8 enemyType)
{
    state_param = (u16)enemyType;
    gameStateChange(GS_BATTLE, TRANS_FADE);
}

void gameStateInit(void)
{
    current_state = GS_NONE;
    pending_state = GS_NONE;
    pending_transition = TRANS_NONE;
    overlay_state = GS_NONE;
    state_param = 0;

    /* Register the battle trigger callback */
    collisionSetBattleCallback(onBattleTriggered);
}

void gameStateChange(u8 newState, u8 transitionType)
{
    pending_state = newState;
    pending_transition = transitionType;
}

void gameStatePush(u8 overlayState_in)
{
    previous_state = current_state;
    overlay_state = overlayState_in;
}

void gameStatePop(void)
{
    overlay_state = GS_NONE;
    /* Don't re-enter the previous state, just resume updating it */
}

static void executeTransition(void)
{
    if (pending_state == GS_NONE) return;

    /* Exit current state */
    switch (current_state) {
        case GS_TITLE:  exitTitle(); break;
        case GS_FLIGHT: exitFlight(); break;
        case GS_BATTLE: exitBattle(); break;
    }

    /* Transition effect */
    if (pending_transition == TRANS_FADE) {
        fadeOutBlocking(15);
    }

    /* Enter new state */
    current_state = pending_state;
    pending_state = GS_NONE;
    pending_transition = TRANS_NONE;

    switch (current_state) {
        case GS_TITLE:     enterTitle(); break;
        case GS_FLIGHT:    enterFlight(); break;
        case GS_BATTLE:    enterBattle(); break;
        case GS_PAUSE:     enterPause(); break;
        case GS_GAME_OVER: enterGameOver(); break;
        case GS_VICTORY:   enterVictory(); break;
    }

    fadeInBlocking(15);
}

void gameStateUpdate(void)
{
    /* Handle pending state transitions */
    if (pending_state != GS_NONE) {
        executeTransition();
    }

    /* Handle overlay states (pause) */
    if (overlay_state != GS_NONE) {
        switch (overlay_state) {
            case GS_PAUSE: updatePause(); break;
        }
        return; /* Don't update underlying state while overlaid */
    }

    /* Update current state */
    switch (current_state) {
        case GS_TITLE:     updateTitle(); break;
        case GS_FLIGHT:    updateFlight(); break;
        case GS_BATTLE:    updateBattle(); break;
        case GS_GAME_OVER: updateGameOver(); break;
        case GS_VICTORY:   updateVictory(); break;
    }
}

/* === TITLE STATE === */
static void enterTitle(void)
{
    systemResetVideo();
    /* Title screen setup (Phase 20 adds full title) */
    consoleInitText(0, BG_4COLORS, 0, 0);
    bgSetEnable(2);
    consoleDrawText(8, 10, "VOIDRUNNER");
    consoleDrawText(6, 14, "PRESS START");
    setScreenOn();
}

static void updateTitle(void)
{
    if (inputPressed() & ACTION_PAUSE) {
        gameStateChange(GS_FLIGHT, TRANS_FADE);
    }
}

static void exitTitle(void) { }

/* === FLIGHT STATE === */
static void enterFlight(void)
{
    systemResetVideo();
    rpgStatsInit();
    inventoryInit();
    spriteSystemInit();
    bulletInit();
    enemyInit();
    collisionInit();
    scrollInit();

    /* Load zone graphics */
    bgLoadZone(ZONE_DEBRIS);
    enemyLoadZoneGraphics(ZONE_DEBRIS);
    bulletLoadGraphics();
    playerInit();

    /* Start scrolling */
    scrollSetSpeed(SCROLL_SPEED_NORMAL);

    /* Enable display layers */
    bgSetEnable(0);
    bgSetEnable(1);
    setScreenOn();
}

static void updateFlight(void)
{
    u16 held = inputHeld();
    u16 pressed = inputPressed();

    /* Pause check */
    if (pressed & ACTION_PAUSE) {
        gameStatePush(GS_PAUSE);
        enterPause();
        return;
    }

    /* Player movement */
    playerHandleInput(held);

    /* Fire weapon */
    if (held & ACTION_FIRE) {
        bulletPlayerFire(player.x, player.y);
    }

    /* Weapon cycling */
    if (pressed & ACTION_NEXT_WPN) bulletNextWeapon();
    if (pressed & ACTION_PREV_WPN) bulletPrevWeapon();

    /* Update all systems */
    scrollUpdate();
    playerUpdate();
    enemyUpdateAll();
    bulletUpdateAll();
    spriteUpdateAll();

    /* Collision detection (may trigger battle transition) */
    collisionCheckAll();

    /* Render */
    bgUpdate();
    spriteRenderAll();
    enemyRenderAll();
    bulletRenderAll();
}

static void exitFlight(void) {
    scrollSetSpeed(SCROLL_SPEED_STOP);
}

/* === BATTLE STATE === */
static void enterBattle(void)
{
    u8 enemyType = (u8)state_param;
    battleInit();
    battleUIInit();
    battleUIEnter();
    battleStart(enemyType);
}

static void updateBattle(void)
{
    u8 action;

    if (!battleUpdate()) {
        /* Battle ended */
        BattleResult *result = battleGetResult();
        rpgApplyBattleResult(result);

        /* Check for item drop on victory */
        if (result->outcome == 1) {
            u8 drop = inventoryRollDrop(battle.enemy.enemy_type);
            if (drop != ITEM_NONE) {
                inventoryAdd(drop, 1);
            }
        }

        battleUIExit();
        gameStateChange(GS_FLIGHT, TRANS_FADE);
        return;
    }

    /* Handle player turn input */
    if (battle.state == BSTATE_PLAYER_TURN) {
        action = battleUIGetSelectedAction();
        if (action != 0xFF) {
            battleSelectAction(action);
        }
    }

    battleUIUpdate();
    battleUIRender();
}

static void exitBattle(void) { }

/* === PAUSE STATE (overlay) === */
static void enterPause(void)
{
    /* Dim screen slightly or show pause text on BG3 */
    consoleDrawText(10, 12, "= PAUSED =");
    consoleDrawText(4, 15, "HP:%d/%d  SP:%d/%d",
        rpg_stats.current_hp, rpg_stats.max_hp,
        rpg_stats.current_sp, rpg_stats.max_sp);
    consoleDrawText(4, 17, "LV:%d  XP:%d",
        rpg_stats.level, rpg_stats.xp);
}

static void updatePause(void)
{
    if (inputPressed() & ACTION_PAUSE) {
        /* Clear pause text */
        consoleDrawText(10, 12, "          ");
        consoleDrawText(4, 15, "                        ");
        consoleDrawText(4, 17, "                ");
        gameStatePop();
    }
}

static void exitPause(void) { }

/* === GAME OVER / VICTORY (Phase 20 expands) === */
static void enterGameOver(void)
{
    consoleDrawText(8, 12, "GAME OVER");
    consoleDrawText(6, 14, "PRESS START");
}
static void updateGameOver(void)
{
    if (inputPressed() & ACTION_PAUSE)
        gameStateChange(GS_TITLE, TRANS_FADE);
}
static void enterVictory(void)
{
    consoleDrawText(7, 12, "VICTORY!");
    consoleDrawText(4, 14, "THE ARK IS SAVED");
}
static void updateVictory(void)
{
    if (inputPressed() & ACTION_PAUSE)
        gameStateChange(GS_TITLE, TRANS_FADE);
}

u8 gameStateGetCurrent(void) { return current_state; }
u8 gameStateIs(u8 state) { return current_state == state; }
void gameStateSetParam(u16 param) { state_param = param; }
u16 gameStateGetParam(void) { return state_param; }
```

### Updated main.c
```c
#include "game.h"
#include "engine/system.h"
#include "engine/input.h"
#include "engine/vblank.h"
#include "game/game_state.h"

int main(void)
{
    systemInit();
    inputInit();
    gameStateInit();

    /* Start at title screen */
    gameStateChange(GS_TITLE, TRANS_FADE);

    setScreenOn();

    while (1) {
        inputUpdate();
        gameStateUpdate();
        WaitForVBlank();
    }
    return 0;
}
```

## Acceptance Criteria
1. Game boots to title screen, pressing START enters flight mode.
2. Flight mode has scrolling backgrounds, player movement, enemies, and bullets.
3. Hitting a fighter+ enemy transitions to battle mode with fade effect.
4. Battle plays to completion, then returns to flight mode.
5. Pressing START during flight shows pause overlay with stats.
6. Pressing START again unpauses.
7. No VRAM corruption during any state transition.
8. State transitions take <1 second each.

## SNES-Specific Constraints
- State transitions that swap VRAM content MUST enter force blank first.
- fadeOutBlocking/fadeInBlocking call WaitForVBlank internally, which keeps the system responsive.
- The overlay pattern (pause) does NOT re-init video, just draws text on top.

## Estimated Complexity
**Complex** - This phase integrates all previous systems. Edge cases in state transitions (e.g., battle triggered during fade, pause during dialog) require defensive coding.
