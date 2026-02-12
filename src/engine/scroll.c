/*==============================================================================
 * Vertical Scrolling Engine
 *
 * Continuous downward scrolling with dual-layer parallax.
 * BG1 scrolls at the set speed, BG2 at half speed for depth.
 * 8.8 fixed-point accumulator for sub-pixel smooth scrolling.
 *
 * The SNES PPU uses 10-bit scroll registers (BG1HOFS/BG1VOFS at $210D/$210E).
 * For 32x32 tilemaps (256x256 pixels), the PPU wraps automatically at
 * the 256-pixel boundary, producing seamless infinite vertical scrolling
 * without any tilemap manipulation.
 *
 * Scroll register writes happen in scrollVBlankUpdate() because the PPU
 * latches scroll values on the first visible scanline. Writing mid-frame
 * would cause the top and bottom portions of the screen to scroll
 * different amounts, producing visible tearing artifacts.
 *
 * Speed transitions use a simple ease-out curve: each frame moves 25%
 * of the remaining distance to the target speed. This produces a natural
 * deceleration feel without requiring sine/cosine tables.
 *============================================================================*/

#include "engine/scroll.h"

/*--- Scroll state ---*/
/* All positions use 8.8 fixed-point: high byte = pixel, low byte = sub-pixel.
 * Sub-pixel accumulation allows speeds like 0.5 px/frame to produce smooth
 * 1-pixel steps every 2 frames, avoiding the stuttery look of pure integer motion. */
static u16 scroll_y_fp;        /* BG1 Y position in 8.8 fixed-point */
static u16 parallax_y_fp;      /* BG2 Y position in 8.8 fixed-point (half speed of BG1) */
static u16 scroll_speed;       /* Current speed (8.8 fixed-point pixels/frame) */
static u16 target_speed;       /* Target speed for smooth transitions */
static u8  transitioning;      /* 1 if a speed transition is currently active */

/* Total cumulative distance scrolled, split into integer pixels and sub-pixel.
 * Split tracking avoids the overflow problem of a single u16 in 8.8 FP format
 * (which would wrap at 255 pixels -- far too short for trigger distances of
 * 300-4800 pixels).  With a separate u16 for pixels, we can track up to
 * 65535 pixels, which is more than enough for all zones. */
static u16 total_dist_pixels;  /* Integer pixel distance (0-65535) */
static u8  total_dist_sub;     /* Sub-pixel accumulator (0-255, carries into pixels) */

/* Hardware scroll values to be written during VBlank.
 * These are the integer pixel positions extracted from the FP accumulators.
 * Buffered here so scrollVBlankUpdate() can write them atomically. */
static u16 hw_bg1_y;
static u16 hw_bg2_y;

/* Dirty flag: set to 1 when scroll positions change, cleared after
 * writing to hardware. Avoids redundant register writes when scroll
 * is stopped (speed = 0). */
static u8  scroll_dirty;

/*--- Scroll triggers ---*/
/* Fixed-size array of distance-based triggers. Each trigger fires its
 * callback once when cumulative scroll distance reaches the threshold.
 * Triggers are checked in order every frame; fired triggers are skipped. */
static ScrollTrigger triggers[MAX_SCROLL_TRIGGERS];
static u8 trigger_count;        /* Number of registered triggers */
static u8 triggers_remaining;   /* Unfired triggers (for early exit optimization) */

/*
 * scrollInit - Reset the scroll system to initial state.
 *
 * Zeroes all positions, speeds, and distances. Sets scroll_dirty = 1
 * to ensure the first VBlank update writes initial zero positions to
 * the PPU scroll registers (in case they held stale values from a
 * previous scene).
 */
void scrollInit(void)
{
    scroll_y_fp = 0;
    parallax_y_fp = 0;
    scroll_speed = SCROLL_SPEED_STOP;
    target_speed = SCROLL_SPEED_STOP;
    transitioning = 0;
    total_dist_pixels = 0;
    total_dist_sub = 0;
    hw_bg1_y = 0;
    hw_bg2_y = 0;
    scroll_dirty = 1;   /* Force initial register write */
    trigger_count = 0;
    triggers_remaining = 0;
}

/*
 * scrollSetSpeed - Set scroll speed immediately without transition.
 *
 * Also sets target_speed to match and clears the transitioning flag,
 * so any in-progress transition is cancelled.
 *
 * Parameters:
 *   speed - New speed in 8.8 fixed-point (use SCROLL_SPEED_* constants)
 */
void scrollSetSpeed(u16 speed)
{
    scroll_speed = speed;
    target_speed = speed;
    transitioning = 0;
}

/*
 * scrollGetSpeed - Return the current scroll speed.
 * Returns: Speed in 8.8 fixed-point.
 */
u16 scrollGetSpeed(void)
{
    return scroll_speed;
}

/*
 * scrollTransitionSpeed - Begin a smooth speed transition.
 *
 * The actual transition uses ease-out (25% of remaining distance per frame)
 * in scrollUpdate(). The 'frames' parameter is used only for the instant
 * case (frames == 0); otherwise the ease-out curve determines actual duration.
 *
 * Parameters:
 *   targetSpd - Desired final speed (8.8 fixed-point)
 *   frames    - If 0, transition is instant. Otherwise, ease-out begins.
 */
void scrollTransitionSpeed(u16 targetSpd, u8 frames)
{
    if (frames == 0) {
        /* Instant transition: skip the ease-out entirely */
        scrollSetSpeed(targetSpd);
        return;
    }
    target_speed = targetSpd;
    transitioning = 1;
}

/*
 * scrollUpdate - Per-frame scroll position and trigger update.
 *
 * Called once per frame from the main loop. Performs:
 *   1. Speed transition (ease-out interpolation if active)
 *   2. Position advancement (BG1 at full speed, BG2 at half)
 *   3. Distance tracking for trigger system
 *   4. Trigger checking (fires callbacks when distance thresholds are met)
 *
 * The ease-out transition works by moving 25% (>>2) of the remaining
 * speed difference each frame. This produces an exponential decay curve
 * that starts fast and slows as it approaches the target, giving a
 * natural feel. A minimum step of 1 ensures convergence (prevents
 * infinite asymptotic approach).
 */
void scrollUpdate(void)
{
    u8 i;
    u16 dist_pixels;

    /* Handle speed transition with ease-out (25% of remaining distance per frame) */
    if (transitioning) {
        s16 diff;   /* Signed difference: target - current */
        s16 step;   /* Amount to change this frame */

        diff = (s16)target_speed - (s16)scroll_speed;

        if (diff > 0) {
            /* Accelerating: move 25% of remaining distance toward target */
            step = diff >> 2;       /* 25% of remaining difference */
            if (step < 1) step = 1; /* Minimum step to guarantee convergence */
            scroll_speed += step;
            /* Clamp to target to prevent overshoot from minimum step */
            if ((s16)scroll_speed >= (s16)target_speed) {
                scroll_speed = target_speed;
                transitioning = 0;
            }
        } else if (diff < 0) {
            /* Decelerating: same 25% approach from the other direction */
            step = (-diff) >> 2;
            if (step < 1) step = 1;
            scroll_speed -= step;
            if ((s16)scroll_speed <= (s16)target_speed) {
                scroll_speed = target_speed;
                transitioning = 0;
            }
        } else {
            /* Already at target speed */
            transitioning = 0;
        }
    }

    /* Nothing to do if scroll is stopped */
    if (scroll_speed == 0) return;

    /* Advance BG1 scroll position by current speed.
     * Subtract to scroll downward: decreasing BGVOFS moves the visible
     * window down through the tilemap, making the background scroll
     * downward on screen (the correct direction for a vertical shmup
     * where the player flies "upward" into space). */
    scroll_y_fp -= scroll_speed;

    /* BG2 parallax at half speed (>>1).
     * The half-speed creates the illusion that BG2 (stars) is further
     * away than BG1 (main background), a classic parallax depth cue. */
    parallax_y_fp -= (scroll_speed >> 1);

    /* Track total cumulative distance for trigger system.
     * Accumulate sub-pixel fractional part separately; when it overflows
     * past 255, carry into the integer pixel counter. This avoids the
     * 8.8 FP overflow that would cap distance tracking at 255 pixels. */
    {
        u16 new_sub;
        new_sub = (u16)total_dist_sub + FP8_FRAC(scroll_speed);
        total_dist_sub = (u8)(new_sub & 0xFF);
        total_dist_pixels += FP8_INT(scroll_speed) + (u8)(new_sub >> 8);
    }

    /* Extract integer pixel values from 8.8 fixed-point for hardware registers.
     * FP8_INT(x) = x >> 8, discarding the sub-pixel fractional byte.
     * These values wrap naturally at 256, matching the 32x32 tilemap. */
    hw_bg1_y = FP8_INT(scroll_y_fp);
    hw_bg2_y = FP8_INT(parallax_y_fp);
    scroll_dirty = 1;  /* Mark that hardware registers need updating */

    /* Check scroll triggers against integer distance.
     * triggers_remaining provides an early exit: once all unfired triggers
     * have been processed, the loop terminates even if trigger_count is larger. */
    dist_pixels = total_dist_pixels;
    for (i = 0; i < trigger_count && triggers_remaining > 0; i++) {
        if (!triggers[i].fired && triggers[i].callback) {
            if (dist_pixels >= triggers[i].distance) {
                triggers[i].fired = 1;
                triggers_remaining--;
                /* Fire the callback. This may spawn enemies, change speed,
                 * start dialog, etc. The callback runs in main loop context
                 * so it can safely call any game function. */
                triggers[i].callback();
            }
        }
    }
}

/*
 * scrollGetY - Get the current BG1 scroll Y position as integer pixels.
 * Returns: Pixel offset (0-255, wraps with the 32x32 tilemap).
 */
u16 scrollGetY(void)
{
    return hw_bg1_y;
}

/*
 * scrollGetDistance - Get total cumulative distance scrolled in integer pixels.
 * Returns: Total pixels scrolled since scrollInit(). May wrap for very long sessions.
 */
u16 scrollGetDistance(void)
{
    return total_dist_pixels;
}

/*
 * scrollAddTrigger - Register a new distance-based scroll trigger.
 *
 * Triggers are stored in insertion order. When cumulative scroll distance
 * reaches distPixels, the callback fires once. Ideally triggers should be
 * added in ascending distance order for efficiency (earlier triggers fire
 * and are skipped sooner), but this is not required.
 *
 * Parameters:
 *   distPixels - Distance threshold in pixels
 *   callback   - Function to call when threshold is reached
 *
 * Silently drops the trigger if MAX_SCROLL_TRIGGERS is reached.
 */
/* #132: Changed return type from void to u8 to report overflow.
 * Returns 1 on success, 0 if trigger array is full (overflow). */
u8 scrollAddTrigger(u16 distPixels, ScrollTriggerFn callback)
{
    if (trigger_count >= MAX_SCROLL_TRIGGERS) return 0;  /* #132: Overflow flag */
    triggers[trigger_count].distance = distPixels;
    triggers[trigger_count].callback = callback;
    triggers[trigger_count].fired = 0;
    trigger_count++;
    triggers_remaining++;
    return 1;  /* Success */
}

/*
 * scrollClearTriggers - Remove all scroll triggers.
 * Used when loading a new zone to discard the previous zone's trigger table.
 */
void scrollClearTriggers(void)
{
    trigger_count = 0;
    triggers_remaining = 0;
}

/*
 * scrollResetTriggers - Reset all triggers' fired flags without removing them.
 * Allows the same triggers to fire again. Used for zone restart / retry
 * scenarios where the player replays the same section.
 */
void scrollResetTriggers(void)
{
    u8 i;
    for (i = 0; i < trigger_count; i++) {
        triggers[i].fired = 0;
    }
    triggers_remaining = trigger_count;
}

/*
 * scrollVBlankUpdate - Write buffered scroll positions to PPU hardware registers.
 *
 * This function MUST be called during VBlank (immediately after WaitForVBlank()
 * returns). The SNES PPU latches BG scroll register values (BG1VOFS at $210E,
 * BG2VOFS at $2110) on the transition from VBlank to active display. Writing
 * these registers during active rendering causes mid-frame scroll changes
 * that appear as horizontal tearing lines.
 *
 * bgSetScroll() is a PVSnesLib wrapper that writes the scroll register pair
 * (each scroll register requires two sequential byte writes for the 10-bit value).
 *
 * The dirty flag avoids unnecessary register writes when scroll is stopped,
 * which saves a few cycles per frame (meaningful on the 65816's tight budget).
 */
void scrollVBlankUpdate(void)
{
    if (scroll_dirty) {
        bgSetScroll(0, 0, hw_bg1_y);  /* BG1: no horizontal scroll, vertical = hw_bg1_y */
        bgSetScroll(1, 0, hw_bg2_y);  /* BG2: no horizontal scroll, vertical = hw_bg2_y */
        scroll_dirty = 0;
    }
}
