/*==============================================================================
 * Input System
 *
 * Reads SNES controller each frame and maps hardware buttons to game actions.
 * Provides held/pressed/released edge detection for all mapped actions.
 *
 * The SNES controller has 12 buttons, each mapped to a bit in a 16-bit word.
 * PVSnesLib reads the controller state via the auto-joypad read feature
 * during VBlank and stores it in a buffer. padsCurrent(0) returns this
 * buffered state for controller port 1.
 *
 * This module introduces an indirection layer (ACTION_* flags) so game
 * code never references hardware KEY_* constants directly, making it
 * trivial to remap controls later.
 *
 * Edge detection (pressed/released) uses XOR + AND masking between the
 * current and previous frame states. This allows single-frame triggers
 * for menu navigation, weapon switching, and other discrete inputs.
 *============================================================================*/

#ifndef INPUT_H
#define INPUT_H

#include "game.h"

/*=== Game Action Flags ===*/
/* Each flag occupies one bit in a u16 bitmask. Multiple actions can be
 * active simultaneously (e.g., ACTION_UP | ACTION_FIRE for moving + shooting).
 * The values are chosen as powers of 2 for efficient bitwise testing. */
#define ACTION_UP       0x0001  /* D-pad up: move ship upward */
#define ACTION_DOWN     0x0002  /* D-pad down: move ship downward */
#define ACTION_LEFT     0x0004  /* D-pad left: move ship left */
#define ACTION_RIGHT    0x0008  /* D-pad right: move ship right */
#define ACTION_FIRE     0x0010  /* Y button: fire current weapon */
#define ACTION_SLOW     0x0020  /* B button: focus/slow movement mode */
#define ACTION_CONFIRM  0x0040  /* A button: confirm menu selection */
#define ACTION_CANCEL   0x0080  /* Select button: cancel/back in menus */
#define ACTION_MENU     0x0100  /* X button: open pause/status menu */
#define ACTION_PAUSE    0x0200  /* Start button: pause game */
#define ACTION_PREV_WPN 0x0400  /* L shoulder: cycle to previous weapon */
#define ACTION_NEXT_WPN 0x0800  /* R shoulder: cycle to next weapon */

/*
 * inputInit - Initialize the input system.
 * Zeroes all state (held, pressed, released, previous frame, raw pad).
 * Call once at boot before the main game loop.
 */
void inputInit(void);

/*
 * inputUpdate - Read the controller and update all action states.
 *
 * Reads the raw hardware pad state via padsCurrent(0), maps it through
 * the pad-to-action lookup table, then computes edge detection:
 *   pressed  = currently held AND NOT held last frame
 *   released = NOT currently held AND held last frame
 *
 * Must be called exactly once per frame, after WaitForVBlank() returns
 * (so padsCurrent reflects the latest joypad auto-read).
 */
void inputUpdate(void);

/*
 * inputHeld - Get all actions currently held down this frame.
 * Returns: Bitmask of ACTION_* flags that are currently pressed.
 *          Nonzero for as long as the button is held.
 */
u16 inputHeld(void);

/*
 * inputPressed - Get actions that transitioned from released to held this frame.
 * Returns: Bitmask of ACTION_* flags that were just pressed (edge trigger).
 *          Only nonzero for one frame per button press.
 */
u16 inputPressed(void);

/*
 * inputReleased - Get actions that transitioned from held to released this frame.
 * Returns: Bitmask of ACTION_* flags that were just released (edge trigger).
 *          Only nonzero for one frame per button release.
 */
u16 inputReleased(void);

/*
 * inputRawPad - Get the raw hardware pad state (KEY_* bitmask from PVSnesLib).
 * Returns: The unprocessed 16-bit joypad register value.
 *          Useful for debugging or bypass scenarios.
 */
u16 inputRawPad(void);

#endif /* INPUT_H */
