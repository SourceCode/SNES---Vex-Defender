/*==============================================================================
 * Brightness Fade Engine
 *
 * Smooth screen brightness transitions using the SNES PPU's master
 * brightness register (INIDISP / $2100). This register controls the
 * overall screen brightness in 16 levels (0 = black, 15 = full bright).
 *
 * The fade engine uses a 16-entry ease-in-out lookup table to map
 * linear progress to perceptually smooth brightness curves, avoiding
 * the jarring linear ramp that would otherwise be visible.
 *
 * Two modes of operation:
 *   - Non-blocking: fadeIn()/fadeOut() start the fade, fadeUpdate()
 *     advances it one step per frame. Game logic continues running.
 *   - Blocking: fadeInBlocking()/fadeOutBlocking() loop internally
 *     with WaitForVBlank() until complete. Used during scene transitions
 *     where no game logic needs to run.
 *
 * Division-free index computation:
 *   The progress index is computed as (frame * 15 / total_frames).
 *   Since the 65816 has no hardware divide, common total_frames values
 *   (15, 20, 40) use shift-add equivalents. Other values fall back to
 *   a subtraction loop (repeated subtraction as division).
 *============================================================================*/

#ifndef FADE_H
#define FADE_H

#include "game.h"

/*
 * fadeIn - Start a non-blocking fade from black to full brightness.
 *
 * Parameters:
 *   frames - Duration of the fade in frames (at 60fps).
 *            Common values: 15 (~0.25s), 20 (~0.33s), 40 (~0.67s).
 *            If 0, brightness jumps immediately to 15 (instant).
 *
 * Sets the screen to black immediately, then fadeUpdate() will
 * advance the brightness each frame until fully bright.
 */
void fadeIn(u8 frames);

/*
 * fadeOut - Start a non-blocking fade from full brightness to black.
 *
 * Parameters:
 *   frames - Duration of the fade in frames.
 *            If 0, brightness jumps immediately to 0 (instant).
 *
 * Sets the screen to full bright immediately, then fadeUpdate() will
 * decrease the brightness each frame until fully black.
 */
void fadeOut(u8 frames);

/*
 * fadeUpdate - Advance the fade by one frame (non-blocking tick).
 *
 * Computes the current brightness from the ease-in-out LUT based on
 * the elapsed frame count and total duration. Writes the brightness
 * via setBrightness() (which writes to SNES register INIDISP / $2100).
 *
 * Returns: 1 while the fade is still in progress, 0 when complete.
 *          Caller should call this every frame and can use the return
 *          value to detect fade completion.
 */
u8 fadeUpdate(void);

/*
 * fadeInBlocking - Perform a complete fade-in, blocking until done.
 *
 * Internally calls fadeIn() then loops with WaitForVBlank() + fadeUpdate()
 * until the fade completes. No game logic runs during this time.
 *
 * Parameters:
 *   frames - Duration of the fade in frames.
 */
void fadeInBlocking(u8 frames);

/*
 * fadeOutBlocking - Perform a complete fade-out, blocking until done.
 *
 * Internally calls fadeOut() then loops with WaitForVBlank() + fadeUpdate()
 * until the fade completes. No game logic runs during this time.
 *
 * Parameters:
 *   frames - Duration of the fade in frames.
 */
void fadeOutBlocking(u8 frames);

#endif /* FADE_H */
