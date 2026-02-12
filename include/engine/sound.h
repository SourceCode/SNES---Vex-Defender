/*==============================================================================
 * Sound Engine - Phase 17
 *
 * Wraps PVSnesLib's SPC700 API for game-level sound management.
 * Supports BRR sound effects and music modules (IT format via smconv).
 *
 * Architecture:
 *   The SNES has a dedicated Sony SPC700 sound processor with 64KB of
 *   Audio RAM (ARAM). Communication between the 65816 CPU and SPC700
 *   happens through 4 I/O ports ($2140-$2143). PVSnesLib provides a
 *   driver that runs on the SPC700 and accepts commands from the CPU side.
 *
 * SFX Pipeline:
 *   1. BRR (Bit Rate Reduction) samples are converted from WAV at build time
 *   2. soundInit() boots the SPC700 driver, allocates ARAM, and uploads
 *      all BRR samples via spcSetSoundEntry()
 *   3. soundPlaySFX() triggers playback via spcPlaySound()
 *   4. soundUpdate() calls spcProcess() each frame to keep the driver alive
 *
 * IMPORTANT: spcPlaySound uses REVERSE index ordering from load order.
 *   Index 0 = LAST loaded sample, Index N-1 = FIRST loaded sample.
 *   Sounds are loaded in reverse SFX_* ID order to compensate, so that
 *   the final spc_index = sfxId - 1 maps correctly.
 *
 * Music: Stub functions for future IT module support. When IT modules are
 *        provided and converted via smconv, enable music playback by
 *        uncommenting the spcSetBank/spcLoad/spcPlay calls in sound.c.
 *
 * spcProcess() MUST be called every frame to keep the SPC700 driver alive.
 * Missing frames can cause audio glitches or driver crashes.
 *============================================================================*/

#ifndef SOUND_H
#define SOUND_H

#include "game.h"

/*=== Sound Effect IDs ===*/
/* These IDs are used as indices into the SFX definition array and for
 * the reverse-index mapping to spcPlaySound(). SFX_NONE (0) is a
 * sentinel that causes soundPlaySFX() to no-op. */
#define SFX_NONE            0   /* No sound (sentinel value) */
#define SFX_PLAYER_SHOOT    1   /* Player bullet fired - short blip */
#define SFX_ENEMY_SHOOT     2   /* Enemy bullet fired - lower pitch blip */
#define SFX_EXPLOSION       3   /* Enemy destroyed - noise burst */
#define SFX_HIT             4   /* Bullet hits target - impact thud */
#define SFX_MENU_SELECT     5   /* Menu confirm (A button) - bright chime */
#define SFX_MENU_MOVE       6   /* Menu cursor move - soft tick */
#define SFX_DIALOG_BLIP     7   /* Typewriter text character - tiny blip */
#define SFX_LEVEL_UP        8   /* Level up jingle - ascending tone */
#define SFX_HEAL            9   /* HP/SP restore - shimmering tone */
#define SFX_COUNT           10  /* Total number of SFX IDs (including SFX_NONE) */

/*=== Music Track IDs ===*/
/* Stub IDs for future IT module support. Currently no music is played;
 * these are defined so game code can reference tracks in preparation
 * for when music assets are available. */
#define MUSIC_NONE          0   /* No music (sentinel / stopped) */
#define MUSIC_TITLE         1   /* Title screen theme */
#define MUSIC_FLIGHT_ZONE1  2   /* Zone 1: Debris Field - tense ambient */
#define MUSIC_FLIGHT_ZONE2  3   /* Zone 2: Asteroid Belt - faster tempo */
#define MUSIC_FLIGHT_ZONE3  4   /* Zone 3: Flagship Approach - dramatic */
#define MUSIC_BATTLE        5   /* Turn-based battle - intense */
#define MUSIC_BOSS          6   /* Boss encounter - epic */
#define MUSIC_VICTORY       7   /* Victory fanfare - triumphant */
#define MUSIC_GAME_OVER     8   /* Game over - somber */
#define MUSIC_COUNT         9   /* Total number of music track IDs */

/*--- Sound Engine API ---*/

/*
 * soundInit - Initialize the sound system.
 *
 * Performs the complete audio setup sequence:
 *   1. Boots the SPC700 processor (loads the PVSnesLib driver into ARAM)
 *   2. Allocates 56 blocks (14336 bytes) of ARAM for BRR sample storage
 *   3. Loads all 9 BRR sound effects into ARAM in reverse order
 *      (to compensate for PVSnesLib's reverse playback indexing)
 *
 * Total BRR data is approximately 13.2KB, fitting within the 14KB allocation.
 * Call once after systemInit() during the boot sequence.
 */
void soundInit(void);

/*
 * soundPlaySFX - Play a sound effect by ID.
 *
 * Parameters:
 *   sfxId - One of the SFX_* constants (1-9). SFX_NONE (0) and
 *           out-of-range values are silently ignored.
 *
 * The ID is mapped to the SPC700 play index as: spc_index = sfxId - 1.
 * This works because sounds were loaded in reverse order during init.
 */
void soundPlaySFX(u8 sfxId);

/*
 * soundUpdate - Per-frame sound system update.
 *
 * Calls spcProcess() to service the SPC700 driver. This handles
 * pending sample uploads, command processing, and driver keepalive.
 *
 * CRITICAL: Must be called every frame without exception. Skipping
 * frames can cause the SPC700 driver to lose sync, resulting in
 * audio corruption or silence.
 */
void soundUpdate(void);

/*--- Music API (stubs for future IT module support) ---*/

/*
 * soundPlayMusic - Start playing a music track.
 *
 * Parameters:
 *   trackId - One of the MUSIC_* constants.
 *
 * Currently a stub (no-op). When IT modules are available:
 *   1. Add smconv conversion to the Makefile
 *   2. Include the soundbank binary in data.asm
 *   3. Call spcSetBank(&soundbank) in soundInit()
 *   4. Implement: spcLoad(module_index), spcPlay(0)
 */
void soundPlayMusic(u8 trackId);

/*
 * soundStopMusic - Stop the currently playing music track.
 * Calls spcStop() if music is active, resets state to MUSIC_NONE.
 */
void soundStopMusic(void);

/*
 * soundPauseMusic - Pause the current music (e.g., for pause menu).
 * Calls spcPauseMusic() if music is active. Resume with soundResumeMusic().
 */
void soundPauseMusic(void);

/*
 * soundResumeMusic - Resume previously paused music.
 * Calls spcResumeMusic() if music is active.
 */
void soundResumeMusic(void);

/*
 * soundGetCurrentMusic - Get the ID of the currently playing music track.
 * Returns: MUSIC_* constant, or MUSIC_NONE if no music is playing.
 */
u8 soundGetCurrentMusic(void);

#endif /* SOUND_H */
