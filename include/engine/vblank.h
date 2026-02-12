/*==============================================================================
 * VBlank Handler Framework
 *
 * Provides per-frame callback registration and frame counting.
 * Callbacks run in the main loop after WaitForVBlank() completes,
 * keeping PVSnesLib's default NMI handler intact for OAM DMA,
 * joypad reads, and text buffer transfers.
 *
 * Architecture note: On the SNES, the VBlank (Vertical Blank) period is the
 * only safe window to write to PPU registers (VRAM, CGRAM, OAM) without
 * causing visual artifacts. PVSnesLib's NMI ISR handles the critical DMA
 * transfers during this window. Our callbacks run AFTER the ISR completes,
 * in the main loop context, so they can safely do game logic without
 * worrying about PPU timing constraints.
 *
 * The callback system uses a fixed-size slot array rather than a linked list
 * to avoid dynamic memory allocation on the 65816's limited RAM (128KB WRAM).
 *============================================================================*/

#ifndef VBLANK_H
#define VBLANK_H

#include "game.h"

/*
 * VBlank callback function pointer type.
 * Callbacks take no arguments and return nothing. They are invoked
 * once per frame in the main loop after WaitForVBlank() returns.
 */
typedef void (*VBlankCallback)(void);

/*
 * Maximum number of post-VBlank callbacks that can be registered.
 * Kept small (4) since each slot is scanned every frame even if empty,
 * and game logic only needs a few persistent per-frame hooks
 * (e.g., scroll register writes, palette updates).
 */
#define MAX_VBLANK_CALLBACKS 4

/*
 * vblankInit - Initialize the VBlank handler system.
 * Zeroes the frame counter and clears all callback slots.
 * Call once during boot, before the main game loop begins.
 */
void vblankInit(void);

/*
 * vblankRegisterCallback - Register a callback to run each frame after VBlank.
 *
 * Scans for the first empty slot in the callback array.
 *
 * Parameters:
 *   cb - Function pointer to the callback (must not be NULL)
 *
 * Returns:
 *   Slot index (0 to MAX_VBLANK_CALLBACKS-1) on success, or 0xFF if
 *   all slots are occupied.
 */
u8 vblankRegisterCallback(VBlankCallback cb);

/*
 * vblankRemoveCallback - Remove a previously registered callback by slot index.
 *
 * Parameters:
 *   slot - The slot index returned by vblankRegisterCallback().
 *          Out-of-range or already-empty slots are silently ignored.
 */
void vblankRemoveCallback(u8 slot);

/*
 * vblankClearCallbacks - Remove all registered callbacks.
 * Used during scene transitions to ensure no stale callbacks persist.
 */
void vblankClearCallbacks(void);

/*
 * vblankProcessCallbacks - Execute all registered callbacks and tick the frame counter.
 *
 * Call exactly once per frame in the main loop, typically right after
 * WaitForVBlank() returns. Uses an early-exit counter so iteration
 * stops as soon as all known callbacks have been invoked, avoiding
 * unnecessary scanning of empty trailing slots.
 */
void vblankProcessCallbacks(void);

/*
 * Global frame counter. Incremented by vblankProcessCallbacks() each frame.
 * Wraps at 65535 (u16 max). Useful for animation timing, periodic events,
 * and as a simple entropy source for PRNG seeding.
 * At 60 fps, wraps approximately every 18.2 minutes.
 */
extern u16 g_frame_count;

#endif /* VBLANK_H */
