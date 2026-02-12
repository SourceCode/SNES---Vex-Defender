/*==============================================================================
 * Brightness Fade Engine
 *
 * Uses frame-counted easing with a 16-entry ease-in-out lookup table
 * for smoother visual transitions than linear brightness ramping.
 *
 * The SNES PPU brightness register (INIDISP / $2100) controls master
 * screen brightness in 16 levels:
 *   0  = completely black (all pixels output as black)
 *   15 = full brightness (normal display)
 * Intermediate values scale all pixel colors proportionally, producing
 * a smooth fade without modifying palette data.
 *
 * The ease-in-out curve is stored as a 16-entry lookup table that maps
 * linear progress (0-15) to perceptual brightness. The curve starts slow
 * (ease-in), accelerates through the middle, and slows again at the end
 * (ease-out), producing a natural-feeling transition.
 *
 * Division avoidance:
 *   The fade progress index requires computing (frame * 15 / total_frames).
 *   Since the 65816 has no hardware divide instruction, this would normally
 *   require a slow software division loop. Three optimizations are used:
 *     1. total=15: index = frame (direct, no math needed)
 *     2. total=20: index = frame * 3 / 4 = (frame + frame<<1) >> 2
 *     3. total=40: index = frame * 3 / 8 = (frame + frame<<1) >> 3
 *   Other values fall back to repeated subtraction (while-loop division).
 *============================================================================*/

#include "engine/fade.h"

/* Fade state machine variables */
static u8 fade_active;      /* 1 if a fade is currently in progress */
static u8 fade_direction;   /* 1 = fading in (black->bright), 2 = fading out (bright->black) */
static u8 fade_frame;       /* Current frame within the fade (0 to fade_total) */
static u8 fade_total;       /* Total number of frames for this fade */

/*
 * Ease-in-out brightness curve: maps linear progress index [0..15] to
 * eased brightness value [0..15].
 *
 * The curve is:
 *   Index: 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
 *   Value: 0  1  1  2  4  6  8 10 11 12 13 14 14 15 15 15
 *
 * Notice:
 *   - Slow start (indices 0-3 map to brightness 0-2): ease-in
 *   - Fast middle (indices 4-7 jump from 4 to 10): main transition
 *   - Slow end (indices 12-15 all hover near 15): ease-out
 *
 * This produces a smooth S-curve that avoids the "popping" effect of
 * linear brightness transitions, where the initial and final steps
 * appear disproportionately large to human perception.
 */
static const u8 fade_ease[16] = {
    0, 1, 1, 2, 4, 6, 8, 10, 11, 12, 13, 14, 14, 15, 15, 15
};

/*
 * fadeIn - Start a non-blocking fade from black to full brightness.
 *
 * Sets brightness to 0 (black) immediately, then fadeUpdate() will
 * advance through the ease curve each frame.
 *
 * If frames == 0, brightness jumps to 15 instantly (no fade).
 *
 * Parameters:
 *   frames - Duration of the fade in frames (60fps). Common: 15, 20, 40.
 */
void fadeIn(u8 frames)
{
    if (frames == 0) {
        /* Instant: skip to full brightness */
        setBrightness(15);
        fade_active = 0;
        return;
    }
    fade_active = 1;
    fade_direction = 1;   /* Fading in: brightness increases */
    fade_frame = 0;
    fade_total = frames;
    setBrightness(0);     /* Start from black */
}

/*
 * fadeOut - Start a non-blocking fade from full brightness to black.
 *
 * Sets brightness to 15 (full) immediately, then fadeUpdate() will
 * decrease through the reverse ease curve each frame.
 *
 * If frames == 0, brightness jumps to 0 instantly (no fade).
 *
 * Parameters:
 *   frames - Duration of the fade in frames.
 */
void fadeOut(u8 frames)
{
    if (frames == 0) {
        /* Instant: skip to black */
        setBrightness(0);
        fade_active = 0;
        return;
    }
    fade_active = 1;
    fade_direction = 2;   /* Fading out: brightness decreases */
    fade_frame = 0;
    fade_total = frames;
    setBrightness(15);    /* Start from full brightness */
}

/*
 * fadeUpdate - Advance the fade by one frame (non-blocking tick).
 *
 * Algorithm:
 *   1. Compute linear progress: index = (fade_frame * 15) / fade_total
 *      This maps the current frame to a 0-15 range for the LUT.
 *
 *   2. Look up eased brightness from the LUT:
 *      - Fade in:  brightness = fade_ease[index]       (0->15)
 *      - Fade out: brightness = fade_ease[15 - index]  (15->0)
 *
 *   3. Write brightness to PPU via setBrightness().
 *
 *   4. On completion, snap to final brightness (0 or 15) to ensure
 *      exact endpoint regardless of rounding.
 *
 * Division optimization (#108):
 *   The index = frame*15/total computation requires division. Since the
 *   65816 has no DIV instruction, common durations get special-cased:
 *     - total=15: index = frame (trivial, no division)
 *     - total=20: index = frame*3/4 using shift-add: (f + f<<1) >> 2
 *     - total=40: index = frame*3/8 using shift-add: (f + f<<1) >> 3
 *   Uncommon durations use a subtraction loop (repeated subtraction = division).
 *
 * Returns: 1 if fade is still active, 0 when complete.
 */
u8 fadeUpdate(void)
{
    u8 bright;
    u16 product;   /* fade_frame * 15 (fits u16: max 255 * 15 = 3825) */
    u16 index;     /* Progress index into fade_ease LUT (0-15) */
    u16 accum;     /* Accumulator for subtraction-loop division */

    if (!fade_active) return 0;  /* No fade in progress */

    fade_frame++;

    /* Compute progress index: index = (fade_frame * 15) / fade_total.
     * #108: Special-case common fade durations to avoid the division loop. */
    product = (u16)fade_frame * 15;

    if (fade_total == 15) {
        /* index = frame * 15 / 15 = frame. No math needed. */
        index = (u16)fade_frame;
    } else if (fade_total == 20) {
        /* index = frame * 15 / 20 = frame * 3 / 4.
         * Multiply by 3: val + (val << 1). This compiles to LDA + ASL + ADC.
         * Divide by 4: >> 2. This compiles to LSR LSR. */
        index = ((u16)fade_frame + ((u16)fade_frame << 1)) >> 2;
    } else if (fade_total == 40) {
        /* index = frame * 15 / 40 = frame * 3 / 8.
         * Same multiply-by-3 trick, divide by 8 instead of 4. */
        index = ((u16)fade_frame + ((u16)fade_frame << 1)) >> 3;
    } else {
        /* Fallback: subtraction loop for arbitrary fade_total values.
         * This performs division by counting how many times fade_total
         * fits into the product. Worst case: ~15 iterations for index=15.
         * Acceptable since non-standard fade durations are rare. */
        accum = 0;
        index = 0;
        while (accum + fade_total <= product) {
            accum += fade_total;
            index++;
        }
    }

    /* Clamp index to LUT range [0, 15] */
    if (index > 15) index = 15;

    /* Look up eased brightness value from the curve table */
    if (fade_direction == 1) {
        /* Fading in: read LUT forward (index 0->15 = dark->bright) */
        bright = fade_ease[index];
    } else {
        /* Fading out: read LUT backward (15-index: starts bright, ends dark) */
        bright = fade_ease[15 - index];
    }

    /* Write to SNES PPU brightness register (INIDISP / $2100).
     * setBrightness() writes the low 4 bits to control screen brightness. */
    setBrightness(bright);

    /* Check if the fade has completed */
    if (fade_frame >= fade_total) {
        fade_active = 0;
        /* Snap to exact final brightness to avoid off-by-one from rounding.
         * Without this, the final frame might land on brightness 14 instead of 15. */
        if (fade_direction == 1) {
            setBrightness(15);  /* Fade in complete: full brightness */
        } else {
            setBrightness(0);   /* Fade out complete: fully black */
        }
    }

    return fade_active;
}

/*
 * fadeInBlocking - Perform a complete fade-in, blocking until done.
 *
 * Starts the fade with fadeIn(), then loops calling WaitForVBlank()
 * and fadeUpdate() every frame until the fade completes. No game
 * logic runs during this time - suitable for scene transitions.
 *
 * WaitForVBlank() halts the CPU until the next VBlank interrupt,
 * ensuring exactly one brightness update per frame (60fps timing).
 *
 * Parameters:
 *   frames - Duration of the fade in frames.
 */
void fadeInBlocking(u8 frames)
{
    fadeIn(frames);
    while (fadeUpdate()) {
        WaitForVBlank();  /* Sync to 60fps, allow NMI handler to run */
    }
}

/*
 * fadeOutBlocking - Perform a complete fade-out, blocking until done.
 *
 * Same blocking pattern as fadeInBlocking but fades from bright to black.
 *
 * Parameters:
 *   frames - Duration of the fade in frames.
 */
void fadeOutBlocking(u8 frames)
{
    fadeOut(frames);
    while (fadeUpdate()) {
        WaitForVBlank();  /* Sync to 60fps, allow NMI handler to run */
    }
}
