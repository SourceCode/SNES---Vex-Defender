# Phase 6: Input System & Ship Movement

## Objective
Create an input abstraction layer that reads SNES controller state and translates it into game actions. Implement smooth player ship movement with D-pad control, screen boundary clamping, and speed modulation (hold B for slow/focus mode). Add visual banking feedback when moving left/right.

## Prerequisites
- Phase 5 (Player Ship sprite is on screen).

## Detailed Tasks

1. Create `src/engine/input.c` - Input manager that wraps PVSnesLib's pad functions, provides edge-detection (just-pressed, just-released), and buffered input state.

2. Implement player movement in `src/game/player.c`:
   - D-pad moves the ship in 8 directions
   - Normal speed: 2 pixels/frame
   - Slow mode (hold B): 1 pixel/frame
   - Screen boundary clamping: x in [0, 224], y in [32, 208]
   - Smooth sub-pixel movement using 8.8 fixed point

3. Implement banking animation:
   - Moving left: sprite flips horizontally (bank left)
   - Moving right: normal orientation (bank right)
   - Not moving horizontally: idle (no flip)
   - Banking has a 4-frame delay before returning to idle (feels responsive)

4. Map buttons to game actions:
   - D-pad: Movement
   - B: Slow/focus mode (while held)
   - Y: Fire weapon (Phase 8)
   - A: Confirm / special ability
   - X: Open menu (Phase 12)
   - Start: Pause
   - L/R: Cycle weapons (Phase 14)

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/engine/input.h
```c
#ifndef INPUT_H
#define INPUT_H

#include "game.h"

/* Game action flags (abstract from hardware buttons) */
#define ACTION_UP       0x0001
#define ACTION_DOWN     0x0002
#define ACTION_LEFT     0x0004
#define ACTION_RIGHT    0x0008
#define ACTION_FIRE     0x0010
#define ACTION_SLOW     0x0020
#define ACTION_CONFIRM  0x0040
#define ACTION_CANCEL   0x0080
#define ACTION_MENU     0x0100
#define ACTION_PAUSE    0x0200
#define ACTION_PREV_WPN 0x0400
#define ACTION_NEXT_WPN 0x0800

/* Initialize input system */
void inputInit(void);

/* Read controller state - call once per frame after WaitForVBlank */
void inputUpdate(void);

/* Current button state (held this frame) */
u16 inputHeld(void);

/* Buttons just pressed this frame (edge detection) */
u16 inputPressed(void);

/* Buttons just released this frame */
u16 inputReleased(void);

/* Get raw pad value for advanced use */
u16 inputRawPad(void);

#endif
```

### J:/code/snes/snes-rpg-test/src/engine/input.c
```c
/*==============================================================================
 * Input System
 * Wraps PVSnesLib pad reading with edge detection and action mapping.
 *============================================================================*/

#include "engine/input.h"

static u16 actions_held;
static u16 actions_pressed;
static u16 actions_released;
static u16 actions_prev;
static u16 raw_pad;

/* Map SNES buttons to game actions */
static u16 mapPadToActions(u16 pad)
{
    u16 actions = 0;

    if (pad & KEY_UP)     actions |= ACTION_UP;
    if (pad & KEY_DOWN)   actions |= ACTION_DOWN;
    if (pad & KEY_LEFT)   actions |= ACTION_LEFT;
    if (pad & KEY_RIGHT)  actions |= ACTION_RIGHT;
    if (pad & KEY_Y)      actions |= ACTION_FIRE;
    if (pad & KEY_B)      actions |= ACTION_SLOW;
    if (pad & KEY_A)      actions |= ACTION_CONFIRM;
    if (pad & KEY_X)      actions |= ACTION_MENU;
    if (pad & KEY_START)  actions |= ACTION_PAUSE;
    if (pad & KEY_SELECT) actions |= ACTION_CANCEL;
    if (pad & KEY_L)      actions |= ACTION_PREV_WPN;
    if (pad & KEY_R)      actions |= ACTION_NEXT_WPN;

    return actions;
}

void inputInit(void)
{
    actions_held = 0;
    actions_pressed = 0;
    actions_released = 0;
    actions_prev = 0;
    raw_pad = 0;
}

void inputUpdate(void)
{
    raw_pad = padsCurrent(0);
    actions_held = mapPadToActions(raw_pad);

    /* Edge detection */
    actions_pressed = actions_held & ~actions_prev;   /* 0->1 transitions */
    actions_released = ~actions_held & actions_prev;   /* 1->0 transitions */

    actions_prev = actions_held;
}

u16 inputHeld(void)
{
    return actions_held;
}

u16 inputPressed(void)
{
    return actions_pressed;
}

u16 inputReleased(void)
{
    return actions_released;
}

u16 inputRawPad(void)
{
    return raw_pad;
}
```

### Updated J:/code/snes/snes-rpg-test/include/game/player.h (additions)
```c
/* Movement speeds (8.8 fixed point) */
#define PLAYER_SPEED_NORMAL  0x0200  /* 2.0 pixels/frame */
#define PLAYER_SPEED_SLOW    0x0100  /* 1.0 pixel/frame */

/* Screen boundaries for clamping (pixels) */
#define PLAYER_MIN_X    0
#define PLAYER_MAX_X    224     /* 256 - 32 (sprite width) */
#define PLAYER_MIN_Y    32      /* Leave room for HUD at top */
#define PLAYER_MAX_Y    192     /* 224 - 32 (sprite height) */

/* Banking return delay (frames before returning to idle) */
#define BANK_RETURN_DELAY  4

/* Additional fields in PlayerShip struct */
/* fixed8_8 fx, fy;   - Sub-pixel position (8.8 fixed) */
/* u8 bank_timer;      - Frames since last horizontal input */
```

### Updated J:/code/snes/snes-rpg-test/src/game/player.c (movement)
```c
/* Add to PlayerShip or use file-scope variables */
static fixed8_8 player_fx;  /* 8.8 fixed X */
static fixed8_8 player_fy;  /* 8.8 fixed Y */
static u8 bank_timer;

void playerInit(void)
{
    /* ... existing init code ... */

    player_fx = TO_FIXED8(PLAYER_START_X);
    player_fy = TO_FIXED8(PLAYER_START_Y);
    bank_timer = 0;
}

void playerHandleInput(u16 held)
{
    sfixed8_8 speed;
    u8 moving_h = 0;

    /* Determine speed */
    speed = (held & ACTION_SLOW) ? PLAYER_SPEED_SLOW : PLAYER_SPEED_NORMAL;

    /* Apply directional movement */
    if (held & ACTION_UP) {
        player_fy -= speed;
    }
    if (held & ACTION_DOWN) {
        player_fy += speed;
    }
    if (held & ACTION_LEFT) {
        player_fx -= speed;
        moving_h = 1;
        playerSetBanking(PLAYER_ANIM_LEFT);
        bank_timer = BANK_RETURN_DELAY;
    }
    if (held & ACTION_RIGHT) {
        player_fx += speed;
        moving_h = 1;
        playerSetBanking(PLAYER_ANIM_RIGHT);
        bank_timer = BANK_RETURN_DELAY;
    }

    /* Return to idle banking when not moving horizontally */
    if (!moving_h) {
        if (bank_timer > 0) {
            bank_timer--;
        } else {
            playerSetBanking(PLAYER_ANIM_IDLE);
        }
    }

    /* Clamp to screen boundaries */
    if (player_fx < TO_FIXED8(PLAYER_MIN_X)) player_fx = TO_FIXED8(PLAYER_MIN_X);
    if (player_fx > TO_FIXED8(PLAYER_MAX_X)) player_fx = TO_FIXED8(PLAYER_MAX_X);
    if (player_fy < TO_FIXED8(PLAYER_MIN_Y)) player_fy = TO_FIXED8(PLAYER_MIN_Y);
    if (player_fy > TO_FIXED8(PLAYER_MAX_Y)) player_fy = TO_FIXED8(PLAYER_MAX_Y);

    /* Convert fixed-point back to integer pixel position */
    player.x = FROM_FIXED8(player_fx);
    player.y = FROM_FIXED8(player_fy);
}
```

### Updated main.c main loop
```c
/* In main game loop: */
while (1) {
    /* Read input */
    inputUpdate();

    /* Handle player movement */
    playerHandleInput(inputHeld());

    /* Update sprites */
    playerUpdate();
    spriteUpdateAll();
    spriteRenderAll();

    /* Update backgrounds */
    bgUpdate();

    /* Sync */
    WaitForVBlank();
}
```

## Technical Specifications

### Movement Math (8.8 Fixed Point)
```
Position stored as u16 in 8.8 format:
  High byte = pixel position (0-255)
  Low byte = sub-pixel fraction (0-255)

Speed 0x0200 = 2.0 pixels/frame = 120 pixels/second at 60fps
Speed 0x0100 = 1.0 pixel/frame = 60 pixels/second at 60fps
Speed 0x0180 = 1.5 pixels/frame = 90 pixels/second

Diagonal movement: same speed per axis, so diagonal is ~1.41x faster.
This is acceptable for a shooter (precise diagonal control feels good).
True normalization would require sqrt which is too expensive.
```

### Input Timing
```
PVSnesLib reads controllers during VBlank ISR automatically.
padsCurrent(0) returns the state read during the last VBlank.

Input pipeline per frame:
  1. WaitForVBlank() returns (VBlank ISR has already read pads)
  2. inputUpdate() reads padsCurrent(0), computes edges
  3. playerHandleInput() uses held/pressed state
  4. Movement applied, sprites rendered
  5. Next WaitForVBlank()

Input latency: 1-2 frames (standard for SNES games).
```

### Button Mapping Reference
```
SNES Button  Game Action       Flight Mode       Battle Mode
-----------  ---------------   ---------------   ---------------
D-Pad        Move              Ship movement     Menu navigation
Y            Fire              Shoot             Attack
B            Slow/Cancel       Focus mode        Back/Cancel
A            Confirm/Special   Special ability   Confirm
X            Menu              Open menu         Item
Start        Pause             Pause game        Pause
Select       Cancel            (unused)          (unused)
L            Prev Weapon       Cycle weapon      (unused)
R            Next Weapon       Cycle weapon      (unused)
```

## Acceptance Criteria
1. D-pad moves the player ship smoothly in all 8 directions.
2. Ship cannot move outside the defined screen boundaries.
3. Holding B reduces movement speed noticeably.
4. Moving left flips the sprite; moving right shows normal orientation.
5. Releasing horizontal input returns to idle animation after ~4 frames.
6. No input lag perceptible (responsive controls).
7. All buttons mapped correctly (test with emulator input display).
8. Movement is smooth at 60fps with no visible jitter.

## SNES-Specific Constraints
- PVSnesLib reads pad data during VBlank ISR. Do not call scanPads() manually.
- padsCurrent(0) returns a u16 with hardware button bit layout, not necessarily matching the KEY_ defines order.
- Do not read pads inside the NMI handler; use the values read by the ISR.
- The SNES controller can report impossible combinations (left+right simultaneously on some controllers).

## Estimated Complexity
**Simple** - Input reading and movement are standard. Fixed-point math is basic shift operations.
