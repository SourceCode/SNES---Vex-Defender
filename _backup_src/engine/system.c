/*==============================================================================
 * System Initialization
 * Configures SNES hardware: PPU Mode 1, BG layers, sprites, scroll.
 *============================================================================*/

#include "engine/system.h"
#include "engine/vblank.h"

void systemInit(void)
{
    /* PVSnesLib core init: force blank, clears hardware */
    consoleInit();

    /* Set Mode 1: BG1=4bpp, BG2=4bpp, BG3=2bpp, 8x8 tiles */
    setMode(BG_MODE1, 0);

    /* Configure sprite size: small=16x16, large=32x32 */
    oamInitGfxAttr(VRAM_OBJ_GFX, OBJ_SIZE16_L32);

    /* Initialize OAM - hide all 128 sprites */
    oamInit();
    oamClear(0, 0);

    /* Set BG tile and map addresses per VRAM layout in config.h */
    bgSetGfxPtr(0, VRAM_BG1_GFX);
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);

    bgSetGfxPtr(1, VRAM_BG2_GFX);
    bgSetMapPtr(1, VRAM_BG2_MAP, SC_32x32);

    bgSetGfxPtr(2, VRAM_BG3_GFX);
    bgSetMapPtr(2, VRAM_BG3_MAP, SC_32x32);

    /* Clear all BG scroll registers */
    bgSetScroll(0, 0, 0);
    bgSetScroll(1, 0, 0);
    bgSetScroll(2, 0, 0);

    /* Disable all BGs initially */
    bgSetDisable(0);
    bgSetDisable(1);
    bgSetDisable(2);

    /* Set brightness to 0 for fade-in capability */
    setBrightness(0);

    /* Initialize VBlank framework */
    vblankInit();
}

void systemResetVideo(void)
{
    /* Enter force blank for safe VRAM access */
    setScreenOff();

    /* Clear all VRAM */
    dmaClearVram();

    /* Hide all sprites */
    oamClear(0, 0);

    /* Reset scroll */
    bgSetScroll(0, 0, 0);
    bgSetScroll(1, 0, 0);
    bgSetScroll(2, 0, 0);
}

void systemWaitFrames(u16 count)
{
    u16 i;
    for (i = 0; i < count; i++) {
        WaitForVBlank();
    }
}
