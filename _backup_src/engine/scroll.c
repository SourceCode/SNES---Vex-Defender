/*==============================================================================
 * Vertical Scrolling Engine
 *
 * Continuous downward scrolling with dual-layer parallax.
 * BG1 scrolls at the set speed, BG2 at half speed.
 * 8.8 fixed-point accumulator for sub-pixel smooth scrolling.
 *
 * Scroll register writes happen in scrollVBlankUpdate() to avoid tearing.
 * The 32x32 tilemap wraps seamlessly at 256 pixels vertically.
 *============================================================================*/

#include "engine/scroll.h"

/*--- Scroll state ---*/
static u16 scroll_y_fp;        /* BG1 position in 8.8 fixed-point */
static u16 parallax_y_fp;      /* BG2 position in 8.8 fixed-point */
static u16 scroll_speed;       /* Current speed (8.8 fixed-point) */
static u16 target_speed;       /* Target speed for transitions */
static s16 speed_step;         /* Speed change per frame during transition */
static u8  transitioning;      /* 1 if speed transition is active */

/* Total cumulative distance in 8.8 fixed-point */
static u16 total_dist_fp;

/* Hardware scroll values (written during VBlank) */
static u16 hw_bg1_y;
static u16 hw_bg2_y;
static u8  scroll_dirty;

/*--- Scroll triggers ---*/
static ScrollTrigger triggers[MAX_SCROLL_TRIGGERS];
static u8 trigger_count;

void scrollInit(void)
{
    scroll_y_fp = 0;
    parallax_y_fp = 0;
    scroll_speed = SCROLL_SPEED_STOP;
    target_speed = SCROLL_SPEED_STOP;
    speed_step = 0;
    transitioning = 0;
    total_dist_fp = 0;
    hw_bg1_y = 0;
    hw_bg2_y = 0;
    scroll_dirty = 1;
    trigger_count = 0;
}

void scrollSetSpeed(u16 speed)
{
    scroll_speed = speed;
    target_speed = speed;
    transitioning = 0;
}

u16 scrollGetSpeed(void)
{
    return scroll_speed;
}

void scrollTransitionSpeed(u16 targetSpd, u8 frames)
{
    if (frames == 0) {
        scrollSetSpeed(targetSpd);
        return;
    }
    target_speed = targetSpd;
    speed_step = ((s16)targetSpd - (s16)scroll_speed) / (s16)frames;
    if (speed_step == 0) {
        speed_step = (targetSpd > scroll_speed) ? 1 : -1;
    }
    transitioning = 1;
}

void scrollUpdate(void)
{
    u8 i;
    u16 dist_pixels;

    /* Handle speed transition */
    if (transitioning) {
        scroll_speed += speed_step;
        if ((speed_step > 0 && scroll_speed >= target_speed) ||
            (speed_step < 0 && scroll_speed <= target_speed)) {
            scroll_speed = target_speed;
            transitioning = 0;
        }
    }

    if (scroll_speed == 0) return;

    /* Advance scroll positions */
    scroll_y_fp += scroll_speed;

    /* BG2 parallax at half speed */
    parallax_y_fp += (scroll_speed >> 1);

    /* Track total distance */
    total_dist_fp += scroll_speed;

    /* Extract integer pixel values for hardware registers */
    hw_bg1_y = FP8_INT(scroll_y_fp);
    hw_bg2_y = FP8_INT(parallax_y_fp);
    scroll_dirty = 1;

    /* Check scroll triggers against integer distance */
    dist_pixels = FP8_INT(total_dist_fp);
    for (i = 0; i < trigger_count; i++) {
        if (!triggers[i].fired && triggers[i].callback) {
            if (dist_pixels >= triggers[i].distance) {
                triggers[i].fired = 1;
                triggers[i].callback();
            }
        }
    }
}

u16 scrollGetY(void)
{
    return hw_bg1_y;
}

u16 scrollGetDistance(void)
{
    return FP8_INT(total_dist_fp);
}

void scrollAddTrigger(u16 distPixels, ScrollTriggerFn callback)
{
    if (trigger_count >= MAX_SCROLL_TRIGGERS) return;
    triggers[trigger_count].distance = distPixels;
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
        bgSetScroll(0, 0, hw_bg1_y);
        bgSetScroll(1, 0, hw_bg2_y);
        scroll_dirty = 0;
    }
}
