/*==============================================================================
 * Input System
 *
 * Reads SNES controller via PVSnesLib's padsCurrent() and maps hardware
 * buttons to game action flags. Computes edge detection (pressed/released)
 * from frame-to-frame state changes.
 *
 * PVSnesLib reads controllers during VBlank ISR automatically using the
 * SNES auto-joypad read feature (enabled via NMITIMEN register $4200).
 * The hardware reads the serial shift register from each controller port
 * during VBlank and stores the result in registers $4218-$421F.
 * padsCurrent(0) returns the buffered state for controller port 1.
 *
 * The indirection layer (KEY_* -> ACTION_*) serves two purposes:
 *   1. Decouples game code from hardware button assignments
 *   2. Allows future button remapping without changing game logic
 *
 * Edge detection algorithm:
 *   pressed  = held & ~prev   (bits that are 1 now but were 0 last frame)
 *   released = ~held & prev   (bits that are 0 now but were 1 last frame)
 *   This is computed with only AND, NOT, and no branching.
 *============================================================================*/

#include "engine/input.h"

/* Current frame action state (all buttons currently held) */
static u16 actions_held;

/* Actions that transitioned to held THIS frame (rising edge) */
static u16 actions_pressed;

/* Actions that transitioned to released THIS frame (falling edge) */
static u16 actions_released;

/* Previous frame's held state, used for edge detection */
static u16 actions_prev;

/* Raw hardware pad state from padsCurrent(), stored for debug access */
static u16 raw_pad;

/*
 * Button-to-action mapping table.
 *
 * Each entry pairs a PVSnesLib KEY_* hardware bitmask with the
 * corresponding game ACTION_* flag. The table is const (stored in ROM)
 * which is preferable on the 65816: ROM reads are fast, and this avoids
 * wasting limited WRAM on data that never changes.
 *
 * Using a table + loop instead of 12 inline if-statements produces
 * smaller generated code on the 65816, which has a small instruction
 * cache and benefits from compact loops.
 */
static const u16 pad_action_map[12][2] = {
    { KEY_UP,     ACTION_UP },
    { KEY_DOWN,   ACTION_DOWN },
    { KEY_LEFT,   ACTION_LEFT },
    { KEY_RIGHT,  ACTION_RIGHT },
    { KEY_Y,      ACTION_FIRE },
    { KEY_B,      ACTION_SLOW },
    { KEY_A,      ACTION_CONFIRM },
    { KEY_X,      ACTION_MENU },
    { KEY_START,  ACTION_PAUSE },
    { KEY_SELECT, ACTION_CANCEL },
    { KEY_L,      ACTION_PREV_WPN },
    { KEY_R,      ACTION_NEXT_WPN },
};

/*
 * mapPadToActions - Convert raw hardware pad bits to game action flags.
 *
 * Iterates through all 12 mapping entries, testing each KEY_* bit in
 * the pad state and OR-ing the corresponding ACTION_* flag into the result.
 *
 * Parameters:
 *   pad - Raw 16-bit joypad state from padsCurrent()
 *
 * Returns:
 *   Bitmask of ACTION_* flags corresponding to pressed hardware buttons.
 */
static u16 mapPadToActions(u16 pad)
{
    u16 actions;
    u8 i;
    actions = 0;
    for (i = 0; i < 12; i++) {
        if (pad & pad_action_map[i][0]) {
            actions |= pad_action_map[i][1];
        }
    }
    return actions;
}

/*
 * inputInit - Reset all input state to zero.
 * Called once at boot. After this, inputUpdate() must be called each
 * frame to begin tracking controller state.
 */
void inputInit(void)
{
    actions_held = 0;
    actions_pressed = 0;
    actions_released = 0;
    actions_prev = 0;
    raw_pad = 0;
}

/*
 * inputUpdate - Read the controller and compute all action states.
 *
 * Called once per frame after WaitForVBlank() returns, which ensures
 * padsCurrent() reflects the auto-joypad read that completed during
 * the most recent VBlank.
 *
 * The edge detection is computed as:
 *   pressed  = held & ~prev  (newly pressed buttons)
 *   released = ~held & prev  (newly released buttons)
 *
 * Then prev is updated to current for the next frame's comparison.
 * This is a standard digital edge detection pattern used in many
 * game input systems.
 */
void inputUpdate(void)
{
    raw_pad = padsCurrent(0);                /* Read controller port 1 */
    actions_held = mapPadToActions(raw_pad);  /* Map hardware -> game actions */
    actions_pressed = actions_held & ~actions_prev;   /* Rising edges */
    actions_released = ~actions_held & actions_prev;  /* Falling edges */
    actions_prev = actions_held;              /* Store for next frame */
}

/* Accessor functions - return the current frame's action state.
 * These are trivial getters but encapsulate the static module state,
 * allowing the internal representation to change without affecting callers. */
u16 inputHeld(void)     { return actions_held; }
u16 inputPressed(void)  { return actions_pressed; }
u16 inputReleased(void) { return actions_released; }
u16 inputRawPad(void)   { return raw_pad; }
