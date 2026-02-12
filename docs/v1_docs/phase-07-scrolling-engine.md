# Phase 7: Vertical Scrolling Engine

## Objective
Implement continuous vertical scrolling for the space flight sections. BG1 scrolls downward (the player appears to fly upward through space), BG2 scrolls at half speed for parallax. Support variable scroll speed and the ability to stop scrolling for encounters and cutscenes.

## Prerequisites
- Phase 4 (Background System), Phase 6 (Input/Movement).

## Detailed Tasks

1. Create `src/engine/scroll.c` - Vertical scroll manager that updates BG scroll registers each frame.

2. Implement continuous scroll with wrapping: The background is 256x256 pixels (32x32 tiles). As scroll_y increases past 255, it wraps back to 0 naturally (SNES hardware wraps tilemap addressing). This gives infinite vertical scrolling with a single 32x32 tilemap.

3. Implement parallax: BG1 at 1.0x speed, BG2 at 0.5x speed. Scroll values are set via bgSetScroll() during VBlank.

4. Implement scroll speed control: normal speed, slow (approaching encounter), stop (during battle/dialog), fast (post-battle acceleration).

5. Add scroll-based triggers: when scroll_y passes certain thresholds, trigger events (enemy spawns, dialog, boss entrance). The trigger system stores a sorted list of scroll positions and their callbacks.

6. Implement scroll position tracking for the zone progress system (Phase 18).

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/engine/scroll.h
```c
#ifndef SCROLL_H
#define SCROLL_H

#include "game.h"

/* Scroll speed presets (8.8 fixed point, pixels/frame) */
#define SCROLL_SPEED_STOP    0x0000
#define SCROLL_SPEED_SLOW    0x0040  /* 0.25 px/frame */
#define SCROLL_SPEED_NORMAL  0x0080  /* 0.5 px/frame */
#define SCROLL_SPEED_FAST    0x0100  /* 1.0 px/frame */
#define SCROLL_SPEED_RUSH    0x0200  /* 2.0 px/frame */

/* Scroll trigger callback type */
typedef void (*ScrollTriggerFn)(void);

/* Maximum scroll triggers per zone */
#define MAX_SCROLL_TRIGGERS 32

/* Scroll trigger entry */
typedef struct {
    u16 scroll_pos;         /* Scroll Y position that triggers this (in pixels) */
    ScrollTriggerFn callback;  /* Function to call when reached */
    u8 fired;               /* 1 if already triggered */
} ScrollTrigger;

/* Initialize scroll system */
void scrollInit(void);

/* Set the current scroll speed (8.8 fixed point) */
void scrollSetSpeed(u16 speed);

/* Get current scroll speed */
u16 scrollGetSpeed(void);

/* Update scroll position - call once per frame */
void scrollUpdate(void);

/* Get current scroll Y position (integer pixels, wraps at 256) */
u16 scrollGetY(void);

/* Get total distance scrolled (does NOT wrap, tracks full distance) */
u32 scrollGetTotalDistance(void);

/* Smoothly transition to a new speed over N frames */
void scrollTransitionSpeed(u16 targetSpeed, u8 frames);

/* Add a scroll trigger */
void scrollAddTrigger(u16 scrollPos, ScrollTriggerFn callback);

/* Clear all scroll triggers */
void scrollClearTriggers(void);

/* Reset all trigger fired flags (for zone restart) */
void scrollResetTriggers(void);

/* VBlank update: set hardware scroll registers */
void scrollVBlankUpdate(void);

#endif
```

### J:/code/snes/snes-rpg-test/src/engine/scroll.c
```c
/*==============================================================================
 * Vertical Scrolling Engine
 *
 * Manages continuous downward scrolling of space backgrounds.
 * Uses 8.8 fixed point for sub-pixel smooth scrolling.
 * BG1 scrolls at full speed, BG2 at half speed (parallax).
 *============================================================================*/

#include "engine/scroll.h"
#include "engine/background.h"

/* Scroll state */
static fixed8_8 scroll_y_fixed;    /* 8.8 fixed Y position */
static u16 scroll_speed;           /* Current speed (8.8 fixed) */
static u16 scroll_target_speed;    /* Target speed for transitions */
static s16 scroll_speed_step;      /* Speed change per frame during transition */
static u8  scroll_transitioning;   /* 1 if speed transition active */

/* Total distance tracker (32-bit, does not wrap) */
static u16 total_distance_lo;
static u16 total_distance_hi;

/* Hardware scroll values (set during VBlank) */
static u16 hw_bg1_scroll_y;
static u16 hw_bg2_scroll_y;
static u8  scroll_dirty;

/* Scroll triggers */
static ScrollTrigger triggers[MAX_SCROLL_TRIGGERS];
static u8 trigger_count;

void scrollInit(void)
{
    scroll_y_fixed = 0;
    scroll_speed = SCROLL_SPEED_STOP;
    scroll_target_speed = SCROLL_SPEED_STOP;
    scroll_speed_step = 0;
    scroll_transitioning = 0;

    total_distance_lo = 0;
    total_distance_hi = 0;

    hw_bg1_scroll_y = 0;
    hw_bg2_scroll_y = 0;
    scroll_dirty = 0;

    trigger_count = 0;
}

void scrollSetSpeed(u16 speed)
{
    scroll_speed = speed;
    scroll_target_speed = speed;
    scroll_transitioning = 0;
}

u16 scrollGetSpeed(void)
{
    return scroll_speed;
}

void scrollTransitionSpeed(u16 targetSpeed, u8 frames)
{
    if (frames == 0) {
        scrollSetSpeed(targetSpeed);
        return;
    }
    scroll_target_speed = targetSpeed;
    scroll_speed_step = ((s16)targetSpeed - (s16)scroll_speed) / (s16)frames;
    if (scroll_speed_step == 0) {
        scroll_speed_step = (targetSpeed > scroll_speed) ? 1 : -1;
    }
    scroll_transitioning = 1;
}

void scrollUpdate(void)
{
    u16 prev_distance;
    u16 prev_y_int;
    u16 new_y_int;
    u8 i;

    /* Handle speed transition */
    if (scroll_transitioning) {
        scroll_speed += scroll_speed_step;
        /* Check if we've reached or passed the target */
        if ((scroll_speed_step > 0 && scroll_speed >= scroll_target_speed) ||
            (scroll_speed_step < 0 && scroll_speed <= scroll_target_speed)) {
            scroll_speed = scroll_target_speed;
            scroll_transitioning = 0;
        }
    }

    /* Track previous position for trigger checks */
    prev_y_int = total_distance_lo;

    /* Advance scroll position */
    scroll_y_fixed += scroll_speed;

    /* Update total distance (32-bit add) */
    prev_distance = total_distance_lo;
    total_distance_lo += scroll_speed;
    if (total_distance_lo < prev_distance) {
        total_distance_hi++;  /* Carry */
    }

    /* Calculate hardware scroll values */
    /* BG1: full speed (use integer part of scroll_y, wraps at 256) */
    hw_bg1_scroll_y = FROM_FIXED8(scroll_y_fixed);

    /* BG2: half speed (parallax) */
    hw_bg2_scroll_y = FROM_FIXED8(scroll_y_fixed >> 1);

    scroll_dirty = 1;

    /* Check scroll triggers */
    new_y_int = FROM_FIXED8((fixed8_8)total_distance_lo);
    for (i = 0; i < trigger_count; i++) {
        if (!triggers[i].fired && triggers[i].callback) {
            /* Trigger fires when total distance passes the trigger position */
            if (total_distance_lo >= (u16)(triggers[i].scroll_pos << 8)) {
                triggers[i].fired = 1;
                triggers[i].callback();
            }
        }
    }
}

u16 scrollGetY(void)
{
    return hw_bg1_scroll_y;
}

u32 scrollGetTotalDistance(void)
{
    /* Combine hi:lo into pseudo-32-bit
     * Since SNES has no native 32-bit, we return lo only for simplicity.
     * For a 10-minute game at 0.5px/frame, max = 0.5 * 60 * 600 = 18000 pixels.
     * This fits in 16 bits (max 65535). */
    return (u32)total_distance_lo;
}

void scrollAddTrigger(u16 scrollPos, ScrollTriggerFn callback)
{
    if (trigger_count >= MAX_SCROLL_TRIGGERS) return;
    triggers[trigger_count].scroll_pos = scrollPos;
    triggers[trigger_count].callback = callback;
    triggers[trigger_count].fired = 0;
    trigger_count++;
}

void scrollClearTriggers(void)
{
    trigger_count = 0;
}

void scrollResetTriggers(void)
{
    u8 i;
    for (i = 0; i < trigger_count; i++) {
        triggers[i].fired = 0;
    }
}

void scrollVBlankUpdate(void)
{
    if (scroll_dirty) {
        bgSetScroll(0, 0, hw_bg1_scroll_y);
        bgSetScroll(1, 0, hw_bg2_scroll_y);
        scroll_dirty = 0;
    }
}
```

## Technical Specifications

### Scroll Wrapping
```
SNES BG scroll registers are 10-bit (0-1023) but tilemap wraps at:
  32x32 map: wraps every 256 pixels (32 tiles * 8 pixels)
  64x32 map: wraps every 512 pixels horizontally, 256 vertically

Our BG1 uses 32x32 (SC_32x32), so vertical scroll wraps at 256.
This means a 256x256 pixel background seamlessly loops vertically.

The background art (Phase 3) must be designed so that the top edge
tiles-match with the bottom edge for seamless wrapping.
```

### Scroll Speed and Game Duration
```
At SCROLL_SPEED_NORMAL (0.5 px/frame, 60fps):
  Pixels/second: 30
  Pixels/minute: 1800
  10-minute game: ~18000 pixels total

Zone breakdown (approximate):
  Zone 1 (Debris Field):  3 min = ~5400 pixels (scroll pos 0 - 5400)
  Zone 2 (Asteroid Belt): 3 min = ~5400 pixels (scroll pos 5400 - 10800)
  Zone 3 (Flagship):      3 min = ~5400 pixels (scroll pos 10800 - 16200)
  + Boss fights pause scrolling: adds ~1 min of stopped time

Total distance fits in u16 (max 65535).
```

### Scroll Trigger System
```
Triggers are checked by comparing total_distance against threshold values.
Example zone 1 triggers:

  Distance  Event
  --------  -----
  300       First enemy scout spawn
  600       Two scouts spawn
  1000      Tutorial dialog popup
  1500      Scout wave (4 enemies)
  2000      First asteroid debris appears
  3500      Scout commander (tougher enemy)
  5000      Zone 1 boss approach (scroll slows)
  5200      Zone 1 to Zone 2 transition

Each trigger fires exactly once. Callbacks typically:
  - Call enemy spawn functions (Phase 9)
  - Start dialog (Phase 16)
  - Change scroll speed
  - Play sound effects (Phase 17)
```

## Acceptance Criteria
1. Background scrolls smoothly downward at ~0.5 pixels/frame.
2. BG2 star layer scrolls at half the speed of BG1 (visible parallax).
3. Scrolling is seamless - no visible seam at the 256-pixel wrap point.
4. scrollSetSpeed(SCROLL_SPEED_STOP) stops scrolling immediately.
5. scrollTransitionSpeed() smoothly accelerates/decelerates over the specified frames.
6. Scroll triggers fire callbacks at the correct positions.
7. Each trigger fires only once.
8. Scrolling runs at full 60fps with no frame drops.

## SNES-Specific Constraints
- bgSetScroll() writes to write-twice registers (BG1HOFS/VOFS). Must be called during VBlank or force blank.
- Scroll values are signed 13-bit internally but we only need positive values (0-255 for wrapping).
- Setting scroll registers outside VBlank causes visual tearing (one half of screen at old value, other half at new).
- HDMA can also modify scroll registers per-scanline, which would conflict. Phase 4's HDMA gradient uses backdrop color, not scroll, so no conflict.

## Estimated Complexity
**Simple** - Incrementing a counter and writing to scroll registers. The trigger system adds modest complexity.
