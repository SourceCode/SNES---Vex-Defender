/*---------------------------------------------------------------------------------
    VEX DEFENDER - Game State Header
    Master game state structure and state machine definitions.

    This header is included by nearly every module in the project. It pulls in
    <snes.h> (PVSnesLib types: u8, u16, s16, etc.) and config.h (VRAM layout,
    palette slots, zone IDs, fixed-point macros, screen constants).

    The GameState struct is the single source of truth for overall game
    progression: which state the game loop is in, which zone the player is
    flying through, how far the story has advanced, and how long the player
    has been playing.  It is defined in game_state.c and declared extern here
    so any module can read (and, where appropriate, write) it.
---------------------------------------------------------------------------------*/
#ifndef GAME_H
#define GAME_H

#include <snes.h>
#include "config.h"

/*=== Game States ===*/
/* These values drive the main loop's state machine dispatch in main.c.
 * Only one state is active at a time; transitions are performed by the
 * gs*Enter() family of functions in game_state.c. */
#define STATE_BOOT       0   /* Initial power-on / reset (transient, never loops) */
#define STATE_TITLE      1   /* Title screen with NEW GAME / CONTINUE menu */
#define STATE_FLIGHT     2   /* Side-scrolling flight gameplay (shoot-em-up) */
#define STATE_BATTLE     3   /* Turn-based RPG battle overlay */
#define STATE_DIALOG     4   /* Story dialog overlay (typewriter text on BG1) */
#define STATE_MENU       5   /* Reserved for future in-game menu */
#define STATE_ZONE_TRANS 6   /* Reserved for zone transition animation */
#define STATE_GAMEOVER   7   /* Game-over screen with RETRY / TITLE menu */
#define STATE_VICTORY    8   /* Victory screen with mission stats and count-up */

/*=== Story Flags (bitfield) ===*/
/* Packed into g_game.story_flags (u16). The lower byte holds game-progress
 * flags; the upper byte holds dialog-trigger flags (SFLAG_* in story.c)
 * that prevent replaying already-seen cutscenes after loading a save. */
#define STORY_ZONE1_CLEAR    0x01  /* Zone 1 boss defeated */
#define STORY_ZONE2_CLEAR    0x02  /* Zone 2 boss defeated */
#define STORY_TWIST_SEEN     0x04  /* Player saw the Zone 3 story twist */
#define STORY_CHOSE_TRUTH    0x08  /* Player chose the "truth" ending path */
#define STORY_CHOSE_LOYALTY  0x10  /* Player chose the "loyalty" ending path */
#define STORY_BOSS_DEFEATED  0x20  /* Final boss was defeated (victory state) */

/*=== Master Game State ===*/
/* Single global instance (g_game) tracks everything needed to resume or
 * save the game at any point.  Kept small (~10 bytes) so the save system
 * can pack it cheaply into SRAM. */
typedef struct {
    u8 current_state;       /* Active STATE_* value driving main-loop dispatch */
    u8 previous_state;      /* Last state before the current one (for return) */
    u8 current_zone;        /* ZONE_DEBRIS / ZONE_ASTEROID / ZONE_FLAGSHIP */
    u8 zones_cleared;       /* How many zones the player has completed (0-3) */
    u8 paused;              /* 1 = game paused (flight only); dims screen to brightness 8 */
    u16 story_flags;        /* Bitmask of STORY_* and SFLAG_* flags */
    u16 frame_counter;      /* Counts 0-59 within the current second (for play-time) */
    u16 play_time_seconds;  /* Total play time in seconds (saved to SRAM) */
    u8  zone_no_damage;     /* 1 = no damage taken in current zone (#155) */
    u16 zone_start_score;   /* Score at start of zone for rank calc (#162) */
    u8  zone_ranks[3];      /* Per-zone rank 0=D,1=C,2=B,3=A,4=S (#199, replaces zone_rank) */
    u8  max_combo;          /* Best combo achieved across playthrough (#174) */
} GameState;

/* The single global instance, defined in game_state.c */
extern GameState g_game;

#endif /* GAME_H */
