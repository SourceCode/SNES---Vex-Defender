# Phase 15: Game State Machine & Scene Management

## Objective
Implement the master game state machine that orchestrates all game states (boot, title, story, flight, battle, menu, game over, victory) with proper transitions between them. This is the central coordinator that ties all previous systems together into a cohesive game flow.

## Prerequisites
- Phase 2 (Hardware Init) complete
- Phase 4 (Background Rendering) complete
- Phase 6 (Input System) complete
- Phase 11 (Battle System) complete
- Phase 12 (Battle UI) complete

## Detailed Tasks

### 1. Implement Master State Machine
Central switch statement in game_update() dispatching to state handlers.

### 2. Define Complete Game Flow
```
BOOT → TITLE → STORY_INTRO →
  ZONE 1 FLIGHT → [battles] → ZONE 1 COMPLETE →
  ZONE 2 FLIGHT → [battles] → MINI-BOSS → ZONE 2 COMPLETE →
  ZONE 3 FLIGHT → [battles] → STORY_TWIST → FINAL BOSS →
  VICTORY (or GAMEOVER at any point)
```

### 3. Implement Scene Transition System
Standardized fade-out/load/fade-in transitions between states.

### 4. Create Title Screen State
Logo, "PRESS START", basic menu.

### 5. Create Game Over State
"GAME OVER" display with retry option.

### 6. Create Victory State
End game celebration.

### 7. Wire All Previous Systems Into State Machine

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/game.h` | MODIFY | Full state machine declarations |
| `src/game.c` | MODIFY | Complete state machine implementation |
| `src/main.c` | MODIFY | Clean main loop calling game_update/render |

## Technical Specifications

### Complete Game State Enum
```c
/* Master game states */
#define STATE_BOOT          0
#define STATE_TITLE         1
#define STATE_STORY_INTRO   2
#define STATE_FLIGHT        3
#define STATE_BATTLE        4
#define STATE_MENU          5
#define STATE_GAMEOVER      6
#define STATE_VICTORY       7
#define STATE_DIALOG        8
#define STATE_ZONE_TRANS    9
#define STATE_STORY_TWIST  10
#define STATE_BOSS_INTRO   11

/* Zone progression sub-states */
#define ZONE_STATE_ENTER    0  /* Loading zone assets */
#define ZONE_STATE_FLYING   1  /* Active flight gameplay */
#define ZONE_STATE_BOSS     2  /* Boss encounter */
#define ZONE_STATE_CLEAR    3  /* Zone cleared sequence */
```

### game.h (Updated)
```c
#ifndef GAME_H
#define GAME_H

#include <snes.h>
#include "config.h"

typedef struct {
    u8  current_state;
    u8  previous_state;
    u8  current_zone;
    u8  zone_state;
    u8  paused;
    u16 frame_counter;
    u16 score;
    u8  story_flags;
    u8  zones_cleared;
    u8  transition_timer;
    u8  transition_target;
} GameState;

extern GameState g_game;

/* Story flags bitfield */
#define STORY_INTRO_SEEN      0x01
#define STORY_ZONE1_CLEAR     0x02
#define STORY_ZONE2_CLEAR     0x04
#define STORY_TWIST_SEEN      0x08
#define STORY_BOSS_DEFEATED   0x10
#define STORY_CHOSE_TRUTH     0x20  /* Twist choice: sided with aliens */
#define STORY_CHOSE_LOYALTY   0x40  /* Twist choice: stayed loyal */

/*--- Functions ---*/
void game_init(void);
void game_update(void);
void game_render(void);
void game_change_state(u8 new_state);
void game_start_zone(u8 zone);

/* State handlers */
void state_title_update(void);
void state_title_render(void);
void state_flight_update(void);
void state_flight_render(void);
void state_battle_update(void);
void state_battle_render(void);
void state_gameover_update(void);
void state_gameover_render(void);
void state_victory_update(void);
void state_victory_render(void);

/* Transitions */
void screen_fade_in(void);
void screen_fade_out(void);

#endif /* GAME_H */
```

### game.c (Full Implementation)
```c
#include "game.h"
#include "player.h"
#include "enemy.h"
#include "bullet.h"
#include "collision.h"
#include "scroll.h"
#include "battle.h"
#include "ui.h"
#include "items.h"
#include "stats.h"
#include "input.h"

GameState g_game;

void game_init(void) {
    /* Zero game state */
    g_game.current_state = STATE_BOOT;
    g_game.previous_state = STATE_BOOT;
    g_game.current_zone = ZONE_DEBRIS;
    g_game.zone_state = ZONE_STATE_ENTER;
    g_game.paused = 0;
    g_game.frame_counter = 0;
    g_game.score = 0;
    g_game.story_flags = 0;
    g_game.zones_cleared = 0;
    g_game.transition_timer = 0;

    /* Init hardware */
    consoleInit();
    setMode(BG_MODE1, 0);

    /* Configure backgrounds */
    bgSetGfxPtr(0, VRAM_BG1_TILES);
    bgSetGfxPtr(1, VRAM_BG2_TILES);
    bgSetGfxPtr(2, VRAM_BG3_TILES);
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);
    bgSetMapPtr(1, VRAM_BG2_MAP, SC_32x32);
    bgSetMapPtr(2, VRAM_BG3_MAP, SC_32x32);

    /* Init OAM */
    oamInit();

    /* Init all subsystems */
    input_init();
    bg_init();
    player_init();
    bullets_init();
    enemies_init();
    collision_init();
    items_init();
    stats_init();
    ui_init();

    /* Go to title screen */
    game_change_state(STATE_TITLE);
}

void game_change_state(u8 new_state) {
    g_game.previous_state = g_game.current_state;
    g_game.current_state = new_state;
    g_game.transition_timer = 0;

    /* State entry logic */
    switch(new_state) {
        case STATE_TITLE:
            /* Load title background */
            bg_load(BG_TITLE);
            bgSetEnable(0);
            bgSetDisable(1);
            bgSetEnable(2);  /* Text layer */
            ui_clear_all();
            ui_draw_text(7, 8, "VEX DEFENDER");
            ui_draw_text(5, 12, "VERTICAL ASSAULT RPG");
            ui_draw_text(8, 18, "PRESS START");
            /* Hide player sprite */
            player_set_visible(0);
            break;

        case STATE_STORY_INTRO:
            ui_clear_all();
            /* Story text set up by dialog system (Phase 16) */
            break;

        case STATE_FLIGHT:
            /* Show game layers */
            bgSetEnable(0);  /* Starfield */
            bgSetDisable(1); /* UI BG off during flight */
            bgSetEnable(2);  /* HUD text */
            player_set_visible(1);
            bg_resume_scroll();
            ui_draw_flight_hud();
            break;

        case STATE_BATTLE:
            /* Battle handles its own init via battle_trigger() */
            bgSetDisable(0); /* Hide starfield */
            bgSetEnable(2);  /* Text layer for battle UI */
            player_set_visible(0);
            break;

        case STATE_GAMEOVER:
            bullets_clear_all();
            enemies_clear_all();
            player_set_visible(0);
            ui_clear_all();
            ui_draw_text(10, 10, "GAME OVER");
            ui_draw_text(6, 14, "PRESS START TO RETRY");
            break;

        case STATE_VICTORY:
            bullets_clear_all();
            enemies_clear_all();
            bg_stop_scroll();
            ui_clear_all();
            ui_draw_text(8, 6, "VICTORY!");
            ui_draw_text(3, 10, "THE ARK IS SAFE... FOR NOW");
            ui_draw_text(5, 14, "FINAL SCORE:");
            ui_draw_number(18, 14, g_game.score, 5);
            ui_draw_text(8, 18, "PRESS START");
            break;

        case STATE_ZONE_TRANS:
            screen_fade_out();
            break;
    }
}

/* Master update dispatcher */
void game_update(void) {
    g_game.frame_counter++;

    switch(g_game.current_state) {
        case STATE_TITLE:
            state_title_update();
            break;

        case STATE_FLIGHT:
            if (!g_game.paused) {
                state_flight_update();
            }
            /* Pause toggle */
            if (input_is_pressed(KEY_START)) {
                g_game.paused = !g_game.paused;
                if (g_game.paused) {
                    ui_draw_text(11, 13, "PAUSED");
                } else {
                    ui_clear_area(11, 13, 6, 1);
                }
            }
            break;

        case STATE_BATTLE:
            state_battle_update();
            break;

        case STATE_GAMEOVER:
            state_gameover_update();
            break;

        case STATE_VICTORY:
            state_victory_update();
            break;

        case STATE_DIALOG:
            /* Handled by dialog system (Phase 16) */
            break;

        case STATE_ZONE_TRANS:
            g_game.transition_timer++;
            if (g_game.transition_timer > 30) {
                game_start_zone(g_game.current_zone + 1);
            }
            break;
    }
}

/* Flight mode update - the shooter gameplay */
void state_flight_update(void) {
    player_update_movement();
    player_fire();
    bullets_update();
    enemies_update();
    bg_update();
    collision_check_all();
    items_update_pickups();
    ui_update_flight_hud();
}

/* Master render dispatcher */
void game_render(void) {
    switch(g_game.current_state) {
        case STATE_FLIGHT:
            player_update_sprite();
            bullets_render();
            enemies_render();
            items_render_pickups();
            break;

        case STATE_BATTLE:
            /* Battle rendering handled inside battle_update */
            break;

        default:
            /* Most states use text-only (BG3) rendering */
            break;
    }
}

void state_title_update(void) {
    /* Blink "PRESS START" text */
    if ((g_game.frame_counter & 0x20) == 0) {
        ui_draw_text(8, 18, "PRESS START");
    } else {
        ui_clear_area(8, 18, 11, 1);
    }

    if (input_is_pressed(KEY_START)) {
        screen_fade_out();
        game_change_state(STATE_STORY_INTRO);
        screen_fade_in();
    }
}

void state_battle_update(void) {
    battle_update();
}

void state_gameover_update(void) {
    if (input_is_pressed(KEY_START)) {
        /* Restart game */
        game_init();
    }
}

void state_victory_update(void) {
    if (input_is_pressed(KEY_START)) {
        /* Return to title */
        game_change_state(STATE_TITLE);
    }
}

void game_start_zone(u8 zone) {
    if (zone >= ZONE_COUNT) {
        game_change_state(STATE_VICTORY);
        return;
    }

    g_game.current_zone = zone;
    g_game.zone_state = ZONE_STATE_ENTER;

    /* Load zone assets */
    bg_load(zone);  /* BG_DEBRIS=0, BG_ASTEROID=1, BG_FLAGSHIP=2 */
    enemies_load_sprites(zone);
    bullets_clear_all();
    enemies_clear_all();

    /* Set up scroll triggers for this zone (Phase 18) */
    /* zone_setup_triggers(zone); */

    game_change_state(STATE_FLIGHT);
    screen_fade_in();
}

/* Screen fade effects */
void screen_fade_in(void) {
    u8 i;
    for (i = 0; i < 16; i++) {
        setBrightness(i);
        WaitForVBlank();
        WaitForVBlank();
    }
}

void screen_fade_out(void) {
    s8 i;
    for (i = 15; i >= 0; i--) {
        setBrightness((u8)i);
        WaitForVBlank();
        WaitForVBlank();
    }
}
```

### Updated main.c (Final Clean Version)
```c
#include <snes.h>
#include "game.h"
#include "input.h"

int main(void) {
    /* Initialize everything */
    game_init();

    /* Turn on screen */
    setScreenOn();

    /* Master game loop */
    while(1) {
        /* Read input */
        input_update();

        /* Update game logic */
        game_update();

        /* Render current state */
        game_render();

        /* Frame sync */
        WaitForVBlank();
    }

    return 0;
}
```

## Acceptance Criteria
1. Game boots to title screen with "VEX DEFENDER" and "PRESS START"
2. "PRESS START" text blinks on and off
3. Pressing Start transitions to story intro (or flight if story not yet implemented)
4. Flight mode has working gameplay (movement, shooting, enemies, scrolling)
5. Battles transition correctly from flight and back
6. Game over screen shows when HP reaches 0, Start restarts
7. Victory screen shows after final boss (or after Zone 3 for now)
8. Zone transitions fade out, load new assets, fade in
9. Pause works during flight mode (Start button)
10. State changes don't leak resources (bullets/enemies cleared on transitions)
11. Score accumulates across zones
12. All previous systems work together without conflicts

## SNES-Specific Constraints
- State transitions that load VRAM must happen during Force Blank or VBlank
- screen_fade_out sets brightness to 0 (force blank equivalent)
- Cannot load large amounts of VRAM data between frames - use blank period
- The main loop must complete within one frame (~16.6ms at 60fps)
- If game logic takes too long, WaitForVBlank will still sync to next frame (frame skip)

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~60KB | 256KB    | ~196KB    |
| WRAM     | ~1.1KB| 128KB   | ~127KB    |
| VRAM     | ~14KB | 64KB    | ~50KB     |
| CGRAM    | 192B  | 512B    | 320B      |

## Estimated Complexity
**Complex** - This phase integrates ALL previous systems. The state machine itself is simple, but ensuring clean transitions between states without resource leaks or graphical glitches requires careful testing.

## Agent Instructions
1. This phase is primarily about modifying `src/game.h` and `src/game.c`
2. Update main.c to the clean 3-call loop: input, update, render
3. Update the Makefile OFILES to include ALL .obj files from all phases
4. Update linkfile to include ALL .obj files
5. Test: verify boot → title → flight → battle → flight loop works
6. Test: verify game over → restart works
7. Test: verify zone transition (manually trigger zone change)
8. Test: verify pause/unpause during flight
9. **Critical**: verify no VRAM corruption during state transitions
10. This is the integration phase - expect to fix bugs from earlier phases
