/*==============================================================================
 * Player Ship
 * Player entity with sprite rendering, input-driven movement, and banking.
 *
 * The player ship is the central game entity. It owns one SpriteEntity from
 * the sprite engine pool (typically slot 0) and renders as a 32x32 OBJ sprite.
 * Movement is driven by D-pad input with optional slow-mode (focus mode)
 * for precise dodging. The ship visually banks (horizontal flip) when moving
 * left/right, with a configurable delay before returning to idle pose.
 *
 * The player's invincibility system uses a frame-countdown timer. While
 * invincible, the sprite blinks on a 4-frame cycle (2 frames visible,
 * 2 frames hidden) to give visual feedback without fully obscuring the ship.
 *
 * This header defines the PlayerShip struct and all player-related functions.
 * The actual RPG stats (HP, ATK, DEF, etc.) are stored separately in
 * rpg_stats.h; this module only handles the physical ship on screen.
 *============================================================================*/

#ifndef PLAYER_H
#define PLAYER_H

#include "game.h"
#include "engine/sprites.h"

/* Player ship start position: centered horizontally near the bottom of
 * the 256x224 screen. Leaves room below for the ship's 32px height. */
#define PLAYER_START_X  112     /* (SCREEN_W - 32) / 2 = (256 - 32) / 2 */
#define PLAYER_START_Y  176

/*=== Movement Constants ===*/
/* Movement speeds in whole pixels per frame. At 60fps:
 *   NORMAL = 2 px/frame = 120 px/sec (crosses screen in ~2 sec)
 *   SLOW   = 1 px/frame = 60 px/sec  (focus mode for dodging) */
#define PLAYER_SPEED_NORMAL  2
#define PLAYER_SPEED_SLOW    1

/* Screen bounds for player movement clamping.
 * The ship sprite is 32x32, so MAX values account for sprite width/height. */
#define PLAYER_MIN_X    0       /* Left edge of screen */
#define PLAYER_MAX_X    224     /* SCREEN_W(256) - sprite_width(32) = 224 */
#define PLAYER_MIN_Y    16      /* Top 16px reserved for future HUD overlay */
#define PLAYER_MAX_Y    192     /* SCREEN_H(224) - sprite_height(32) = 192 */

/* Number of frames to hold the banking (tilted) animation after the player
 * releases the horizontal D-pad direction. Prevents visual jitter from
 * brief taps and makes the banking feel smoother. */
#define BANK_RETURN_DELAY  4

/* Player animation states - determine the ship's visual orientation.
 * Banking is implemented via horizontal flip (hflip) on the sprite. */
#define PLAYER_ANIM_IDLE    0   /* Ship faces forward (no flip) */
#define PLAYER_ANIM_LEFT    1   /* Ship banks left (hflip = 1, mirror image) */
#define PLAYER_ANIM_RIGHT   2   /* Ship banks right (hflip = 0, same as idle visually,
                                 * but tracked as a separate state for bank_timer logic) */

/* Player data structure - runtime state for the player ship.
 * Separate from RPG stats (rpg_stats.h) which track HP/ATK/DEF/etc.
 * This struct handles purely the ship's physical presence on screen. */
typedef struct {
    SpriteEntity *sprite;       /* Pointer to allocated OAM sprite entity from engine pool.
                                 * NULL if allocation failed (should never happen in practice). */
    s16 x;                      /* Ship X position in screen pixels (top-left of 32x32 sprite) */
    s16 y;                      /* Ship Y position in screen pixels (top-left of 32x32 sprite) */
    u8 anim_state;              /* Current PLAYER_ANIM_* state (idle/left/right) */
    u8 invincible_timer;        /* Frames of invincibility remaining. Set to 120 (2 sec)
                                 * after taking damage or exiting battle. While > 0, the
                                 * sprite blinks and collisions are ignored. */
    u8 visible;                 /* 1 = sprite shown on screen, 0 = hidden.
                                 * Toggled by invincibility blink and playerShow/Hide. */
    u8 bank_timer;              /* Countdown frames before banking returns to idle.
                                 * Reset to BANK_RETURN_DELAY on each horizontal input.
                                 * When it reaches 0 with no horizontal input, banking
                                 * reverts to PLAYER_ANIM_IDLE. */
    u8 combo_flash;             /* Frames of combo palette flash remaining (#234).
                                 * Set to 6 when killing an enemy at 2x+ combo.
                                 * While > 0, player sprite uses alternate palette
                                 * on even frames for a flash effect. */
} PlayerShip;

/* Global singleton player instance. Only one player ship exists at a time. */
extern PlayerShip g_player;

/* Initialize player: load 32x32 ship tiles + palette into VRAM via DMA,
 * allocate a SpriteEntity from the engine pool, and set the ship at its
 * starting position. Must be called after spriteSystemInit() and while the
 * screen is in force blank (since it writes VRAM). */
void playerInit(void);

/* Process input actions and move the player ship.
 * 'held' is the result of inputHeld() - a bitmask of currently-pressed buttons.
 * Handles D-pad movement (4 directions), slow mode (ACTION_SLOW = shoulder button),
 * and banking animation state with return delay. Clamps position to screen bounds. */
void playerHandleInput(u16 held);

/* Per-frame update: syncs the SpriteEntity position to match g_player.x/y,
 * handles invincibility blink animation (alternating show/hide on 4-frame
 * cycle), and manages the thrust trail flicker effect behind the ship. */
void playerUpdate(void);

/* Set the ship's visual banking state. Changes the hflip property on the
 * sprite entity. PLAYER_ANIM_LEFT sets hflip=1 (mirror), all others set
 * hflip=0 (normal). Does nothing if sprite is NULL. */
void playerSetBanking(u8 state);

/* Set player position directly (bypasses input handling and clamping).
 * Used for scripted sequences like respawn positioning. */
void playerSetPosition(s16 x, s16 y);

/* Make the player ship visible by setting the OAM show flag.
 * Used when transitioning back from battle or menu screens. */
void playerShow(void);

/* Hide the player ship by setting the OAM hide flag.
 * Used during battle transitions and game over sequences. */
void playerHide(void);

#endif /* PLAYER_H */
