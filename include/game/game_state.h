/*==============================================================================
 * Game State Machine - Phase 18
 *
 * Manages master game states: TITLE, FLIGHT, BATTLE, GAMEOVER, VICTORY.
 * Provides enter/update functions for each screen and handles transitions
 * with proper VRAM management and fade effects.
 *
 * Phase 18 adds zone advancement (3-zone campaign) and victory screen.
 *
 * Uses STATE_* defines from game.h and the g_game global.
 *============================================================================*/

#ifndef GAME_STATE_H
#define GAME_STATE_H

#include "game.h"

/* Flag set by zone-end scroll trigger; main.c checks this */
extern u8 g_zone_advance;

/* Initialize g_game to default values. Call once at boot. */
void gsInit(void);

/* Enter the title screen (set up BG3 text, fade in).
 * Sets g_game.current_state = STATE_TITLE. */
void gsTitleEnter(void);

/* Per-frame title screen update. Blinks PRESS START text.
 * Transitions to flight on Start button press. */
void gsTitleUpdate(u16 pad_pressed);

/* Enter flight mode: init subsystems, load zone graphics, fade in.
 * Sets g_game.current_state = STATE_FLIGHT. */
void gsFlightEnter(void);

/* Advance to the next zone. Fades out, reloads, fades in.
 * After Zone 3, transitions to victory. */
void gsZoneAdvance(void);

/* Enter victory screen (after Zone 3 completion).
 * Sets g_game.current_state = STATE_VICTORY. */
void gsVictoryEnter(void);

/* Per-frame victory screen update. */
void gsVictoryUpdate(u16 pad_pressed);

/* Enter game over screen (set up BG3 text on dark screen).
 * Called when battle ends in defeat. Screen should already be dark.
 * Sets g_game.current_state = STATE_GAMEOVER. */
void gsGameOverEnter(void);

/* Per-frame game over update. Blinks PRESS START.
 * Transitions to title on Start button press. */
void gsGameOverUpdate(u16 pad_pressed);

/* Toggle pause during flight mode.
 * Dims screen brightness to 8 when paused, restores to 15 when unpaused. */
void gsPauseToggle(void);

#endif /* GAME_STATE_H */
