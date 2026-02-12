/*==============================================================================
 * System Initialization
 *
 * Provides the top-level hardware initialization sequence and utility
 * functions for the SNES 65816 / PPU / SPC700 platform.
 *
 * systemInit() is called once from bootSequence() in main.c.  It assumes
 * that crt0_snes.asm has already run PVSnesLib's consoleInit(), which
 * clears VRAM, initializes OAM, enables NMI + joypad auto-read, and
 * installs consoleVblank as the NMI handler.  systemInit() then configures
 * PPU Mode 1, BG layer addresses, sprite size, and sets brightness to 0
 * so the first scene can fade in cleanly.
 *
 * systemResetVideo() is available for scene transitions that need to
 * tear down and rebuild the entire video state (clear VRAM, reset OAM,
 * reset scroll registers) while in force-blank.
 *
 * systemWaitFrames() is a simple blocking delay used for splash screens
 * and hardware settle time after power-on.
 *============================================================================*/

#ifndef SYSTEM_H
#define SYSTEM_H

#include "game.h"

/*
 * systemInit - Full hardware initialization.
 * Configures PPU Mode 1 (BG1=4bpp, BG2=4bpp, BG3=2bpp), sets BG tile/map
 * VRAM addresses per config.h layout, configures sprite size to 16x16/32x32,
 * hides all 128 OAM entries, clears scroll registers, disables all BG layers,
 * sets brightness to 0, and initializes the VBlank callback framework.
 * Call once at boot, after crt0 has finished.
 */
void systemInit(void);

/*
 * systemResetVideo - Re-initialize video hardware for a clean scene transition.
 * Enters force-blank (setScreenOff), DMA-clears all 64KB of VRAM, hides all
 * OAM sprites, and resets all three BG scroll registers to (0, 0).
 * The caller is responsible for exiting force-blank (setScreenOn) after
 * loading new graphics.
 */
void systemResetVideo(void);

/*
 * systemWaitFrames - Block for a specified number of VBlank frames.
 * Uses WaitForVBlank() in a loop.  At 60fps (NTSC), 60 frames = 1 second.
 * Used for splash screen hold times and post-boot hardware settle delay.
 *
 * count: Number of frames to wait.
 */
void systemWaitFrames(u16 count);

#endif /* SYSTEM_H */
