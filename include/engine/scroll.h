/*==============================================================================
 * Vertical Scrolling Engine
 *
 * Manages continuous downward scrolling of space backgrounds with parallax.
 * BG1 scrolls at full speed, BG2 at half speed for a depth effect.
 *
 * Uses 8.8 fixed-point accumulators for sub-pixel smooth scrolling:
 *   - At 0.5 px/frame (SCROLL_SPEED_NORMAL), the background moves
 *     1 pixel every 2 frames, producing smooth 30fps apparent motion.
 *   - The fractional accumulator prevents speed quantization artifacts
 *     that would occur with pure integer pixel stepping.
 *
 * Scroll register writes happen in scrollVBlankUpdate() to avoid tearing.
 * The SNES PPU latches scroll values on the first non-blanked scanline,
 * so they must be written during VBlank or force blank.
 *
 * The 32x32 tilemap (256x256 pixels) wraps seamlessly because the SNES
 * PPU hardware automatically wraps tilemap reads at the map boundary.
 * This means continuous scrolling requires no tilemap updates at all.
 *
 * Trigger system: distance-based callbacks fire when the cumulative
 * scroll distance reaches specified thresholds. Used for spawning
 * enemies, starting dialog, or changing scroll speed at scripted points.
 *============================================================================*/

#ifndef SCROLL_H
#define SCROLL_H

#include "game.h"
#include "config.h"

/*
 * Scroll trigger callback function pointer.
 * Takes no arguments; the callback is responsible for knowing what
 * to do when triggered (typically captured via closure-like patterns
 * with global state, since C89 has no closures).
 */
typedef void (*ScrollTriggerFn)(void);

/*
 * Scroll trigger entry.
 * Stored in a flat array, checked each frame against cumulative distance.
 * Triggers fire at most once (unless reset via scrollResetTriggers).
 */
typedef struct {
    u16 distance;           /* Fire when cumulative distance >= this value (in pixels) */
    ScrollTriggerFn callback; /* Function to call when triggered */
    u8 fired;               /* 1 if already triggered this run, 0 if pending */
} ScrollTrigger;

/*
 * scrollInit - Initialize the scroll system.
 * Zeroes all positions, speeds, triggers, and distance accumulators.
 * Call once during scene setup before scrolling begins.
 */
void scrollInit(void);

/*
 * scrollSetSpeed - Set the scroll speed immediately (no transition).
 *
 * Parameters:
 *   speed - Scroll speed in 8.8 fixed-point pixels per frame.
 *           Use the SCROLL_SPEED_* constants from config.h:
 *             SCROLL_SPEED_STOP   = 0x0000 (stopped)
 *             SCROLL_SPEED_SLOW   = FP8(0.25) = 0x0040
 *             SCROLL_SPEED_NORMAL = FP8(0.5)  = 0x0080
 *             SCROLL_SPEED_FAST   = FP8(1.0)  = 0x0100
 *             SCROLL_SPEED_RUSH   = FP8(2.0)  = 0x0200
 */
void scrollSetSpeed(u16 speed);

/*
 * scrollGetSpeed - Get the current scroll speed.
 * Returns: Current speed in 8.8 fixed-point.
 */
u16 scrollGetSpeed(void);

/*
 * scrollTransitionSpeed - Smoothly transition to a new speed.
 *
 * Uses ease-out interpolation (25% of remaining distance per frame)
 * for a natural deceleration/acceleration feel. The transition converges
 * asymptotically and snaps to the target when the step would be < 1.
 *
 * Parameters:
 *   targetSpeed - Desired final speed (8.8 fixed-point)
 *   frames      - Hint for transition duration (0 = instant).
 *                 The actual duration depends on the ease-out curve.
 */
void scrollTransitionSpeed(u16 targetSpeed, u8 frames);

/*
 * scrollUpdate - Per-frame scroll update.
 *
 * Advances scroll positions by the current speed, handles speed
 * transitions, tracks cumulative distance, and checks all triggers.
 *
 * BG2 parallax runs at half the BG1 speed, creating a depth illusion
 * where stars appear further away than the main background.
 *
 * Call once per frame from the main loop, before rendering.
 */
void scrollUpdate(void);

/*
 * scrollGetY - Get the current BG1 scroll Y position.
 * Returns: Integer pixel Y offset (wraps within 0-255 for the 32x32 tilemap).
 *          This is the value written to the SNES BG1VOFS register.
 */
u16 scrollGetY(void);

/*
 * scrollGetDistance - Get total cumulative distance scrolled.
 * Returns: Total distance in integer pixels since last scrollInit().
 *          Does not wrap; fits u16 for a ~10 minute game at 0.5 px/frame
 *          (~18000 pixels maximum).
 */
u16 scrollGetDistance(void);

/*
 * scrollAddTrigger - Register a distance-based trigger.
 *
 * Parameters:
 *   distPixels - Cumulative scroll distance (pixels) at which to fire
 *   callback   - Function to call when the distance is reached
 *
 * #132: Returns 1 on success, 0 if MAX_SCROLL_TRIGGERS (24) is reached.
 */
u8 scrollAddTrigger(u16 distPixels, ScrollTriggerFn callback);

/*
 * scrollClearTriggers - Remove all scroll triggers.
 * Used when loading a new zone to discard the previous zone's triggers.
 */
void scrollClearTriggers(void);

/*
 * scrollResetTriggers - Reset all trigger fired flags without removing them.
 * Allows triggers to fire again. Used for zone restart / retry scenarios.
 */
void scrollResetTriggers(void);

/*
 * scrollVBlankUpdate - Write scroll registers to SNES PPU hardware.
 *
 * Calls bgSetScroll() for BG1 and BG2 only if positions have changed
 * (dirty flag optimization to avoid unnecessary register writes).
 *
 * MUST be called during VBlank (immediately after WaitForVBlank returns)
 * because the SNES PPU latches BG scroll registers at the start of
 * active display. Writing mid-frame would cause visible tearing.
 */
void scrollVBlankUpdate(void);

#endif /* SCROLL_H */
