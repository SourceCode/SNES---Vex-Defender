/*==============================================================================
 * System Initialization
 *
 * Configures SNES hardware: PPU Mode 1, BG layers, sprites, scroll.
 *
 * The SNES PPU (Picture Processing Unit) renders up to 4 background layers
 * and 128 sprites.  In Mode 1 the layers are:
 *   BG1: 4 bits per pixel (16 colors from a 16-color sub-palette)
 *   BG2: 4 bits per pixel
 *   BG3: 2 bits per pixel (4 colors, used for simple overlays)
 *
 * This game uses BG1 for the main game background AND text (they share
 * the same tilemap at VRAM $0000 but use different character tiles).
 * BG2 is used for the star parallax layer.  BG3 is unused because the
 * PVSnesLib console font is 4bpp and cannot be displayed on a 2bpp layer.
 *
 * VRAM layout is defined in config.h. All addresses are 16-bit word
 * addresses (the PPU uses word addressing, so $2000 = byte address $4000).
 *============================================================================*/

#include "engine/system.h"
#include "engine/vblank.h"
#include "assets.h"
#include "config.h"

/*
 * systemInit - One-time hardware setup at boot.
 *
 * IMPORTANT: crt0_snes.asm calls consoleInit() before main(), which handles:
 *   - Clearing all 64KB of VRAM via DMA
 *   - Initializing OAM (Object Attribute Memory) for 128 sprites
 *   - Enabling NMI (VBlank interrupt) and joypad auto-read
 *   - Installing consoleVblank as the NMI handler
 * We must NOT call consoleInit() again here as it would be redundant and
 * could cause visible glitches.
 */
void systemInit(void)
{
    /* NOTE: crt0_snes.asm already calls consoleInit() before main().
     * Do NOT call consoleInit() again here - it is redundant.
     * consoleInit sets up: VRAM clear, OAM init, pad init,
     * consoleVblank as NMI handler, NMI+joypad auto-read enabled. */

    /* Configure text rendering on BG1 (4bpp).
     * PVSnesLib's consoleInitText() loads the 4bpp font tiles into VRAM at
     * VRAM_TEXT_GFX and uses VRAM_TEXT_MAP for the text tilemap.
     * The text offset 0x0100 means font tiles start at tile index 256 within
     * BG1's character set, leaving tiles 0-255 free for game background art.
     * Palette slot 16*2 = palette 2 of the BG palettes (CGRAM offset 32). */
    consoleSetTextMapPtr(VRAM_TEXT_MAP);
    consoleSetTextGfxPtr(VRAM_TEXT_GFX);
    consoleSetTextOffset(0x0100);
    consoleInitText(0, 16 * 2, &snesfont, &snespal);

    /* BG1: Main game background layer.
     * Character (tile) data at VRAM_BG1_GFX, tilemap at VRAM_BG1_MAP.
     * Shares the tilemap with the text system -- game tiles use tile indices
     * 0-255, font tiles use 256+.  bgLoadZone() later overwrites tiles 0-255
     * with zone-specific background art. */
    bgSetGfxPtr(0, VRAM_BG1_GFX);
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);

    /* BG2: Star parallax background layer.
     * Separate tile data at $5000 and tilemap at $7400.
     * This layer scrolls at a different speed than BG1 to create a
     * depth-of-field parallax effect during flight mode. */
    bgSetGfxPtr(1, VRAM_BG2_GFX);
    bgSetMapPtr(1, VRAM_BG2_MAP, SC_32x32);

    /* Set PPU to Mode 1: BG1=4bpp, BG2=4bpp, BG3=2bpp, all 8x8 tiles.
     * Second parameter 0 means no BG3 priority bit. */
    setMode(BG_MODE1, 0);

    /* Configure sprite (OBJ) rendering.
     * Sprite tiles start at VRAM_OBJ_GFX.
     * OBJ_SIZE16_L32 means: small sprites = 16x16, large sprites = 32x32.
     * Enemy ships use large (32x32) sprites; bullets use small (16x16). */
    oamInitGfxAttr(VRAM_OBJ_GFX, OBJ_SIZE16_L32);

    /* Initialize OAM table: set all 128 sprite entries to hidden.
     * oamInit() sets up the OAM mirror buffer in WRAM.
     * oamClear(0, 0) hides sprites starting at slot 0, count 0 means "all". */
    oamInit();
    oamClear(0, 0);

    /* Clear all BG scroll registers to (0, 0).
     * Each BG has independent H/V scroll registers ($210D-$2114).
     * Starting at zero ensures no leftover scroll from a previous state. */
    bgSetScroll(0, 0, 0);
    bgSetScroll(1, 0, 0);
    bgSetScroll(2, 0, 0);

    /* Disable all BG layers initially.
     * Layers are selectively enabled by each game state's enter function
     * (e.g., gsFlightEnter enables BG1+BG2, gsTitleEnter enables BG1 only). */
    bgSetDisable(0);
    bgSetDisable(1);
    bgSetDisable(2);

    /* Set master brightness to 0 (fully dark).
     * This allows the first scene (title screen) to fade in smoothly
     * from black using fadeInBlocking(). */
    setBrightness(0);

    /* Initialize the VBlank callback framework.
     * This sets up the deferred-callback queue processed at the end of
     * each main loop iteration via vblankProcessCallbacks(). */
    vblankInit();
}

/*
 * systemResetVideo - Tear down video state for a clean scene transition.
 *
 * Enters force-blank mode (PPU output disabled, VRAM freely writable),
 * clears all VRAM via DMA, hides all sprites, and zeros scroll registers.
 * After calling this, the caller should load new scene graphics into VRAM
 * and then call setScreenOn() to exit force-blank.
 */
void systemResetVideo(void)
{
    /* Enter force blank: sets bit 7 of register $2100.
     * While force-blank is active, the PPU outputs no picture and VRAM
     * can be written at any time (not just during VBlank). */
    setScreenOff();

    /* Clear all 64KB of VRAM using DMA channel.
     * This ensures no stale tile data or tilemaps from the previous scene
     * bleed through when new graphics are loaded. */
    dmaClearVram();

    /* Hide all 128 sprites by writing to the OAM mirror buffer.
     * The actual OAM hardware registers are updated during the next VBlank
     * by the NMI handler (consoleVblank). */
    oamClear(0, 0);

    /* Reset scroll positions to origin for all three BG layers */
    bgSetScroll(0, 0, 0);
    bgSetScroll(1, 0, 0);
    bgSetScroll(2, 0, 0);
}

/*
 * systemWaitFrames - Blocking delay for a specified number of VBlank periods.
 *
 * Each call to WaitForVBlank() halts the CPU (via WAI instruction) until
 * the next NMI fires, then returns.  At 60fps (NTSC) or 50fps (PAL),
 * this provides frame-accurate timing for splash screens and settle delays.
 *
 * count: Number of VBlank frames to wait.
 */
void systemWaitFrames(u16 count)
{
    u16 i;
    for (i = 0; i < count; i++) {
        WaitForVBlank();
    }
}
