/*==============================================================================
 * Background Rendering System
 *
 * SNES Mode 1 layer usage during flight gameplay:
 *   BG1 (4bpp, 16 colors): Primary space background, scrolls vertically.
 *       Tiles at VRAM $2000, tilemap at VRAM $6800 (shared with text).
 *   BG2 (4bpp, 16 colors): Parallax star dots layer, scrolls at half speed.
 *       Tiles at VRAM $5000, tilemap at VRAM $7400.
 *       Procedurally generated star map with palette-cycling twinkle effect.
 *   BG3 (2bpp, 4 colors): HUD text overlay (fixed position, transparent BG).
 *
 * The star parallax layer (BG2) uses a procedural tilemap generated at
 * load time with a seeded xorshift16 PRNG. This saves ROM space compared
 * to storing a pre-made tilemap. Three star dot tiles (bright/medium/dim)
 * are palette-cycled every N frames to create a twinkling effect.
 *
 * Palette cycling updates are deferred to VBlank via bgVBlankUpdate()
 * to avoid CGRAM write conflicts with active display. The SNES PPU only
 * allows CGRAM writes during VBlank or force blank.
 *
 * Zone backgrounds are loaded during force blank since VRAM is only
 * writable when the PPU is not actively rendering.
 *============================================================================*/

#ifndef BACKGROUND_H
#define BACKGROUND_H

#include "game.h"

/*
 * Sentinel value indicating no background zone is currently loaded.
 * Uses 0xFF since valid zone IDs are 0, 1, 2.
 */
#define BG_ZONE_NONE  0xFF

/*
 * bgSystemInit - Initialize the background system state.
 * Sets current zone to BG_ZONE_NONE, resets twinkle timer and star
 * palette cycle buffer to initial brightness values.
 * Call once after systemInit() during the boot sequence.
 */
void bgSystemInit(void);

/*
 * bgLoadZone - Load a zone's complete background into VRAM.
 *
 * Performs the following during force blank (setScreenOff):
 *   1. Loads BG1 tileset, palette, and tilemap for the specified zone
 *   2. Generates the procedural star map for BG2 (xorshift16 PRNG)
 *   3. Uploads star tiles and palette to BG2 VRAM/CGRAM
 *   4. Enables BG1 and BG2, resets scroll positions
 *   5. Resets the star twinkle animation state
 *
 * Returns with the screen still in force blank. The caller must call
 * setScreenOn() or fadeIn() when ready to display.
 *
 * Parameters:
 *   zoneId - ZONE_DEBRIS (0), ZONE_ASTEROID (1), or ZONE_FLAGSHIP (2)
 *            from config.h. Invalid values cause early return.
 *
 * Zone-specific behavior:
 *   - Each zone has different twinkle speeds (8/6/4 frames) for
 *     increasingly intense visual activity in later zones.
 */
void bgLoadZone(u8 zoneId);

/*
 * bgLoadStarsOnly - Load only the BG2 star parallax layer.
 *
 * Loads the procedural star map, tiles, and palette into BG2 without
 * touching BG1. Used by screens that want animated star backgrounds
 * behind text (e.g., title screen). Sets current_zone to ZONE_DEBRIS
 * so bgUpdate() twinkle animation works.
 *
 * Must be called during force blank (setScreenOff).
 */
void bgLoadStarsOnly(void);

/*
 * bgUpdate - Per-frame background update for palette cycling effects.
 *
 * Rotates the three star brightness colors (bright -> medium -> dim -> bright)
 * every N frames, where N depends on the zone and scroll speed.
 * During fast scroll, twinkle speed increases to 2 frames for more
 * energetic visuals.
 *
 * Does nothing if no zone is loaded or if scrolling is stopped
 * (e.g., during pause or scene transitions).
 *
 * Sets a dirty flag when the palette changes, consumed by bgVBlankUpdate().
 */
void bgUpdate(void);

/*
 * bgVBlankUpdate - Upload modified star palette to CGRAM during VBlank.
 *
 * If the palette was changed by bgUpdate(), DMAs the 3 cycling star
 * colors (6 bytes) to CGRAM entries 17-19 (BG palette 1, colors 1-3).
 *
 * Must be called during VBlank (immediately after WaitForVBlank) because
 * CGRAM is only safely writable during the vertical blanking period.
 * Writing to CGRAM during active display causes color glitches.
 */
void bgVBlankUpdate(void);

/*
 * bgSetParallaxVisible - Toggle the BG2 star parallax layer on or off.
 *
 * Parameters:
 *   visible - 1 to enable BG2, 0 to disable BG2.
 *             Useful for scenes that don't want the star layer
 *             (e.g., battle screen, menus).
 */
void bgSetParallaxVisible(u8 visible);

/*
 * bgGetCurrentZone - Get the currently loaded zone ID.
 * Returns: Zone ID (ZONE_DEBRIS, ZONE_ASTEROID, ZONE_FLAGSHIP) or
 *          BG_ZONE_NONE (0xFF) if no zone is loaded.
 */
u8 bgGetCurrentZone(void);

#endif /* BACKGROUND_H */
