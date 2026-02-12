/*==============================================================================
 * VBlank Handler Framework
 * Manages per-frame callbacks and frame counting.
 * Callbacks execute in the main loop after WaitForVBlank().
 *============================================================================*/

#include "engine/vblank.h"

u16 g_frame_count;

static VBlankCallback vblank_callbacks[MAX_VBLANK_CALLBACKS];
static u8 vblank_callback_count;

void vblankInit(void)
{
    u8 i;
    g_frame_count = 0;
    vblank_callback_count = 0;
    for (i = 0; i < MAX_VBLANK_CALLBACKS; i++) {
        vblank_callbacks[i] = (VBlankCallback)0;
    }
}

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
    return 0xFF;
}

void vblankRemoveCallback(u8 slot)
{
    if (slot < MAX_VBLANK_CALLBACKS && vblank_callbacks[slot]) {
        vblank_callbacks[slot] = (VBlankCallback)0;
        vblank_callback_count--;
    }
}

void vblankClearCallbacks(void)
{
    u8 i;
    for (i = 0; i < MAX_VBLANK_CALLBACKS; i++) {
        vblank_callbacks[i] = (VBlankCallback)0;
    }
    vblank_callback_count = 0;
}

void vblankProcessCallbacks(void)
{
    u8 i;
    for (i = 0; i < MAX_VBLANK_CALLBACKS; i++) {
        if (vblank_callbacks[i]) {
            vblank_callbacks[i]();
        }
    }
    g_frame_count++;
}
