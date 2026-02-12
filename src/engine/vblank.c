/*==============================================================================
 * VBlank Handler Framework
 *
 * Manages per-frame callbacks and frame counting. Callbacks execute in
 * the main loop after WaitForVBlank(), not inside the NMI ISR itself.
 *
 * This is important because:
 *   - PVSnesLib's NMI ISR handles time-critical DMA transfers (OAM, VRAM)
 *     that must complete within the ~2200 CPU cycle VBlank window.
 *   - Our callbacks (scroll register writes, palette updates) run after
 *     the ISR returns control to the main loop, where timing is relaxed.
 *   - No interrupt nesting or reentrancy concerns since callbacks run
 *     with interrupts enabled in normal main-loop context.
 *
 * The callback array uses NULL (cast to 0) to mark empty slots, with a
 * separate count for early-exit optimization during iteration.
 *============================================================================*/

#include "engine/vblank.h"

/* Global frame counter, incremented once per frame by vblankProcessCallbacks.
 * Wraps at 65535 (~18 minutes at 60fps). Accessible by all modules. */
u16 g_frame_count;

/* Fixed-size callback slot array. NULL entries are empty/available.
 * Using a static array avoids heap allocation on the memory-constrained SNES. */
static VBlankCallback vblank_callbacks[MAX_VBLANK_CALLBACKS];

/* Number of currently registered (non-NULL) callbacks.
 * Used as an early-exit counter in vblankProcessCallbacks() to avoid
 * scanning empty trailing slots. */
static u8 vblank_callback_count;

/*
 * vblankInit - Reset the VBlank handler system to initial state.
 *
 * Zeroes the frame counter, clears all callback slots to NULL, and
 * resets the callback count. Called once at boot.
 */
void vblankInit(void)
{
    u8 i;
    g_frame_count = 0;
    vblank_callback_count = 0;
    for (i = 0; i < MAX_VBLANK_CALLBACKS; i++) {
        vblank_callbacks[i] = (VBlankCallback)0;
    }
}

/*
 * vblankRegisterCallback - Register a function to be called every frame.
 *
 * Scans the callback array for the first NULL slot and inserts the
 * callback there. Returns the slot index for later removal.
 *
 * Parameters:
 *   cb - Function pointer to register (must not be NULL).
 *
 * Returns:
 *   Slot index (0 to MAX_VBLANK_CALLBACKS-1) on success.
 *   0xFF if all slots are occupied (registration failed).
 */
u8 vblankRegisterCallback(VBlankCallback cb)
{
    u8 i;
    for (i = 0; i < MAX_VBLANK_CALLBACKS; i++) {
        if (vblank_callbacks[i] == (VBlankCallback)0) {
            vblank_callbacks[i] = cb;
            vblank_callback_count++;
            return i;
        }
    }
    return 0xFF;  /* No free slots available */
}

/*
 * vblankRemoveCallback - Unregister a callback by its slot index.
 *
 * Validates the slot index and checks that the slot is occupied before
 * clearing it. Decrements the active callback count.
 *
 * Parameters:
 *   slot - Index returned by vblankRegisterCallback(). Invalid or
 *          already-empty slots are silently ignored (defensive coding
 *          for the limited debugging environment on SNES hardware).
 */
void vblankRemoveCallback(u8 slot)
{
    if (slot < MAX_VBLANK_CALLBACKS && vblank_callbacks[slot]) {
        vblank_callbacks[slot] = (VBlankCallback)0;
        vblank_callback_count--;
    }
}

/*
 * vblankClearCallbacks - Remove all registered callbacks.
 *
 * Used during scene transitions (e.g., flight -> battle) to ensure
 * no stale per-frame hooks from the previous scene persist.
 */
void vblankClearCallbacks(void)
{
    u8 i;
    for (i = 0; i < MAX_VBLANK_CALLBACKS; i++) {
        vblank_callbacks[i] = (VBlankCallback)0;
    }
    vblank_callback_count = 0;
}

/*
 * vblankProcessCallbacks - Execute all registered callbacks and advance frame counter.
 *
 * Iterates through the callback array, invoking each non-NULL callback.
 * Uses a 'remaining' counter that decrements on each invocation, allowing
 * early exit once all known callbacks have been processed. This avoids
 * scanning empty trailing slots when only 1-2 callbacks are registered.
 *
 * Example: If 2 callbacks are in slots 0 and 2, the loop invokes slot 0
 * (remaining=1), skips slot 1, invokes slot 2 (remaining=0), then exits
 * without checking slot 3.
 *
 * The frame counter is incremented unconditionally at the end, even if
 * no callbacks are registered, to maintain consistent timing.
 */
void vblankProcessCallbacks(void)
{
    u8 i;
    u8 remaining;
    remaining = vblank_callback_count;
    for (i = 0; i < MAX_VBLANK_CALLBACKS && remaining > 0; i++) {
        if (vblank_callbacks[i]) {
            vblank_callbacks[i]();
            remaining--;
        }
    }
    g_frame_count++;
}
