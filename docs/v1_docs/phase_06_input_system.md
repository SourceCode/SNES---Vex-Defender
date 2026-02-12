# Phase 6: Input System & Ship Movement

## Objective
Implement the SNES controller input system and connect it to player ship movement. Support D-pad for movement, B button for shooting, A button for special attack, and Start for pause/menu. Create a clean input abstraction layer usable by all game systems.

## Prerequisites
- Phase 5 (Sprite Engine & Player Ship) complete - player ship visible on screen

## Detailed Tasks

### 1. Create Input Manager Module
Read SNES controller state each frame and provide clean button press/held/released detection.

### 2. Connect Input to Player Movement
D-pad moves the player ship within screen bounds.

### 3. Implement Button Mapping
Map SNES buttons to game actions for both flight mode and battle mode.

### 4. Add Movement Smoothing
Gradual acceleration/deceleration for ship movement feel.

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/input.h` | CREATE | Input system header |
| `src/input.c` | CREATE | Input system implementation |
| `src/player.c` | MODIFY | Add movement logic |
| `src/main.c` | MODIFY | Call input_update in main loop |
| `Makefile` | MODIFY | Add input.obj |
| `data/linkfile` | MODIFY | Add input.obj |

## Technical Specifications

### SNES Controller Button Mapping
```
PVSnesLib button masks (from input.h):
  KEY_A       = 0x0080    A button
  KEY_B       = 0x8000    B button
  KEY_X       = 0x0040    X button
  KEY_Y       = 0x4000    Y button
  KEY_L       = 0x0020    L shoulder
  KEY_R       = 0x0010    R shoulder
  KEY_START   = 0x1000    Start
  KEY_SELECT  = 0x2000    Select
  KEY_UP      = 0x0800    D-pad Up
  KEY_DOWN    = 0x0400    D-pad Down
  KEY_LEFT    = 0x0200    D-pad Left
  KEY_RIGHT   = 0x0100    D-pad Right
```

### Game Button Mapping
```
Flight Mode:
  D-pad     → Move ship
  B         → Fire basic shot
  A         → Fire special attack (uses MP)
  Y         → Use item
  Start     → Pause / Open menu
  Select    → Toggle auto-fire

Battle Mode:
  D-pad U/D → Navigate menu options
  A         → Confirm selection
  B         → Cancel / Back
  L/R       → Switch between party members (future)
  Start     → Skip animation
```

### input.h
```c
#ifndef INPUT_H
#define INPUT_H

#include <snes.h>
#include "config.h"

/* Input state structure */
typedef struct {
    u16 held;          /* Buttons currently held down */
    u16 pressed;       /* Buttons pressed this frame (new presses only) */
    u16 released;      /* Buttons released this frame */
    u16 prev;          /* Previous frame's held state */
} InputState;

extern InputState g_input;

/* Game action flags (derived from raw input + game state) */
#define ACTION_MOVE_UP      0x0001
#define ACTION_MOVE_DOWN    0x0002
#define ACTION_MOVE_LEFT    0x0004
#define ACTION_MOVE_RIGHT   0x0008
#define ACTION_FIRE         0x0010
#define ACTION_SPECIAL      0x0020
#define ACTION_USE_ITEM     0x0040
#define ACTION_PAUSE        0x0080
#define ACTION_CONFIRM      0x0100
#define ACTION_CANCEL       0x0200
#define ACTION_MENU_UP      0x0400
#define ACTION_MENU_DOWN    0x0800

/*--- Functions ---*/
void input_init(void);
void input_update(void);
u8   input_is_held(u16 button);
u8   input_is_pressed(u16 button);
u8   input_is_released(u16 button);

#endif /* INPUT_H */
```

### input.c
```c
#include "input.h"

InputState g_input;

void input_init(void) {
    g_input.held = 0;
    g_input.pressed = 0;
    g_input.released = 0;
    g_input.prev = 0;
}

void input_update(void) {
    /* Store previous state */
    g_input.prev = g_input.held;

    /* Read current controller state */
    /* padsCurrent(0) returns player 1 buttons */
    g_input.held = padsCurrent(0);

    /* Calculate edge triggers */
    /* Pressed: buttons that are held NOW but were NOT held before */
    g_input.pressed = g_input.held & ~g_input.prev;

    /* Released: buttons that are NOT held now but WERE held before */
    g_input.released = ~g_input.held & g_input.prev;
}

u8 input_is_held(u16 button) {
    return (g_input.held & button) != 0;
}

u8 input_is_pressed(u16 button) {
    return (g_input.pressed & button) != 0;
}

u8 input_is_released(u16 button) {
    return (g_input.released & button) != 0;
}
```

### Player Movement (add to player.c)
```c
void player_update_movement(void) {
    s16 dx = 0, dy = 0;

    /* Read directional input */
    if (input_is_held(KEY_UP))    dy = -g_player.speed;
    if (input_is_held(KEY_DOWN))  dy =  g_player.speed;
    if (input_is_held(KEY_LEFT))  dx = -g_player.speed;
    if (input_is_held(KEY_RIGHT)) dx =  g_player.speed;

    /* Diagonal movement normalization (approximate) */
    /* If moving diagonally, reduce speed by ~70% (3/4 approximation) */
    if (dx != 0 && dy != 0) {
        dx = (dx * 3) >> 2;  /* multiply by 0.75 */
        dy = (dy * 3) >> 2;
    }

    /* Apply velocity */
    g_player.x += dx;
    g_player.y += dy;

    /* Clamp to screen bounds */
    if (g_player.x < 0) g_player.x = 0;
    if (g_player.x > SCREEN_WIDTH - 32) g_player.x = SCREEN_WIDTH - 32;
    if (g_player.y < 16) g_player.y = 16;  /* Leave room for top UI */
    if (g_player.y > SCREEN_HEIGHT - 32) g_player.y = SCREEN_HEIGHT - 32;

    /* Update animation frame based on horizontal movement */
    if (dx < 0) {
        g_player.anim_frame = PLAYER_FRAME_LEFT;
    } else if (dx > 0) {
        g_player.anim_frame = PLAYER_FRAME_RIGHT;
    } else {
        g_player.anim_frame = PLAYER_FRAME_IDLE;
    }
}
```

### Updated Main Loop
```c
while(1) {
    /* Read controller input */
    input_update();

    /* Handle pause toggle */
    if (input_is_pressed(KEY_START)) {
        g_game.paused = !g_game.paused;
    }

    if (!g_game.paused) {
        /* Update player movement */
        player_update_movement();

        /* Update background scrolling */
        bg_update();
    }

    /* Update sprite OAM data */
    player_update_sprite();

    /* Frame sync */
    g_game.frame_counter++;
    WaitForVBlank();
}
```

## Acceptance Criteria
1. D-pad moves the player ship in all 8 directions (including diagonals)
2. Ship stops immediately when D-pad is released
3. Ship cannot move outside screen bounds (clamped)
4. Start button toggles pause (ship and background stop moving)
5. `input_is_pressed()` fires only once per button press (not repeated)
6. `input_is_held()` returns true every frame the button is held
7. No input lag (movement responds on the exact frame button is pressed)
8. Diagonal movement is slightly slower than cardinal movement

## SNES-Specific Constraints
- `padsCurrent(0)` must be called ONCE per frame (reads hardware registers)
- Controller reads are only valid after VBlank
- Auto-joypad read must be enabled (PVSnesLib does this in consoleInit)
- Maximum 2 controllers on standard SNES port
- The SNES has no analog input - D-pad is digital only

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~34KB | 256KB    | ~222KB    |
| WRAM     | ~60B  | 128KB   | ~128KB    |
| VRAM     | ~8.5KB| 64KB    | ~55.5KB   |
| CGRAM    | 64B   | 512B    | 448B      |

## Estimated Complexity
**Simple** - PVSnesLib provides clean controller reading. The edge detection logic (pressed/released) is the only nuanced part.

## Agent Instructions
1. Create `src/input.h` and `src/input.c`
2. Update Makefile and linkfile
3. Add `input_init()` call in `game_init()`
4. Add `input_update()` as the FIRST call in the main loop
5. Add `player_update_movement()` to player.c
6. Wire movement into the main loop
7. Test: D-pad should move the ship, Start should pause
8. Test diagonal movement is slightly slower than straight
9. Verify button press detection only fires once (tap Start rapidly)
