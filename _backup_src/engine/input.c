/*==============================================================================
 * Input System
 * Reads SNES controller via PVSnesLib's padsCurrent() and maps hardware
 * buttons to game action flags. Computes edge detection (pressed/released)
 * from frame-to-frame state changes.
 *
 * PVSnesLib reads controllers during VBlank ISR automatically.
 * padsCurrent(0) returns the state buffered during the last VBlank.
 *============================================================================*/

#include "engine/input.h"

static u16 actions_held;
static u16 actions_pressed;
static u16 actions_released;
static u16 actions_prev;
static u16 raw_pad;

static u16 mapPadToActions(u16 pad)
{
    u16 actions = 0;

    if (pad & KEY_UP)     actions |= ACTION_UP;
    if (pad & KEY_DOWN)   actions |= ACTION_DOWN;
    if (pad & KEY_LEFT)   actions |= ACTION_LEFT;
    if (pad & KEY_RIGHT)  actions |= ACTION_RIGHT;
    if (pad & KEY_Y)      actions |= ACTION_FIRE;
    if (pad & KEY_B)      actions |= ACTION_SLOW;
    if (pad & KEY_A)      actions |= ACTION_CONFIRM;
    if (pad & KEY_X)      actions |= ACTION_MENU;
    if (pad & KEY_START)  actions |= ACTION_PAUSE;
    if (pad & KEY_SELECT) actions |= ACTION_CANCEL;
    if (pad & KEY_L)      actions |= ACTION_PREV_WPN;
    if (pad & KEY_R)      actions |= ACTION_NEXT_WPN;

    return actions;
}

void inputInit(void)
{
    actions_held = 0;
    actions_pressed = 0;
    actions_released = 0;
    actions_prev = 0;
    raw_pad = 0;
}

void inputUpdate(void)
{
    raw_pad = padsCurrent(0);
    actions_held = mapPadToActions(raw_pad);
    actions_pressed = actions_held & ~actions_prev;
    actions_released = ~actions_held & actions_prev;
    actions_prev = actions_held;
}

u16 inputHeld(void)     { return actions_held; }
u16 inputPressed(void)  { return actions_pressed; }
u16 inputReleased(void) { return actions_released; }
u16 inputRawPad(void)   { return raw_pad; }
