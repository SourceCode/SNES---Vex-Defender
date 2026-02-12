/*==============================================================================
 * Brightness Fade Engine
 * Uses 8.8 fixed-point accumulator for smooth brightness transitions.
 * setBrightness(0) = black, setBrightness(15) = full bright.
 *============================================================================*/

#include "engine/fade.h"

static u8 fade_active;
static u8 fade_direction;   /* 1 = fading in, 2 = fading out */
static u16 fade_brightness; /* 8.8 fixed point: high byte = actual brightness */
static s16 fade_step;       /* 8.8 fixed point step per frame */

void fadeIn(u8 frames)
{
    if (frames == 0) {
        setBrightness(15);
        fade_active = 0;
        return;
    }
    fade_active = 1;
    fade_direction = 1;
    fade_brightness = 0;
    /* Step = (15 << 8) / frames */
    fade_step = (s16)((15 * 256) / frames);
    if (fade_step < 1) fade_step = 1;
    setBrightness(0);
}

void fadeOut(u8 frames)
{
    if (frames == 0) {
        setBrightness(0);
        fade_active = 0;
        return;
    }
    fade_active = 1;
    fade_direction = 2;
    fade_brightness = (u16)(15 << 8);
    fade_step = (s16)((15 * 256) / frames);
    if (fade_step < 1) fade_step = 1;
    setBrightness(15);
}

u8 fadeUpdate(void)
{
    u8 bright;
    if (!fade_active) return 0;

    if (fade_direction == 1) {
        /* Fading in */
        fade_brightness += fade_step;
        if (fade_brightness >= (u16)(15 << 8)) {
            fade_brightness = (u16)(15 << 8);
            fade_active = 0;
        }
    } else {
        /* Fading out */
        if (fade_brightness <= (u16)fade_step) {
            fade_brightness = 0;
            fade_active = 0;
        } else {
            fade_brightness -= fade_step;
        }
    }

    bright = (u8)(fade_brightness >> 8);
    if (bright > 15) bright = 15;
    setBrightness(bright);

    return fade_active;
}

void fadeInBlocking(u8 frames)
{
    fadeIn(frames);
    while (fadeUpdate()) {
        WaitForVBlank();
    }
}

void fadeOutBlocking(u8 frames)
{
    fadeOut(frames);
    while (fadeUpdate()) {
        WaitForVBlank();
    }
}
