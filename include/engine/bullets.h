/*==============================================================================
 * Bullet/Projectile System
 *
 * Manages a pool of bullet entities for player and enemy projectiles.
 * Bullets use dedicated OAM slots (separate from the sprite engine pool)
 * to avoid contention with the general-purpose sprite allocator.
 *
 * Pool layout:
 *   Indices 0 to MAX_PLAYER_BULLETS-1     -> Player bullets (OAM slots 4-19)
 *   Indices MAX_PLAYER_BULLETS to MAX-1   -> Enemy bullets  (OAM slots 40-47)
 *
 * This split allows O(1) allocation scans within each owner's region and
 * ensures player and enemy bullets never compete for the same pool slots.
 *
 * All velocities use 8.8 signed fixed-point format:
 *   - High byte = integer pixels per frame (signed)
 *   - Low byte  = fractional sub-pixel accumulator
 *   - Negative Y = upward movement (SNES screen Y increases downward)
 *
 * Supports three player weapon types (single, spread, laser) with
 * per-weapon fire rates, damage values, and trajectory patterns.
 * Weapon cycling is triggered by L/R shoulder buttons.
 *============================================================================*/

#ifndef BULLETS_H
#define BULLETS_H

#include "game.h"

/*=== Pool Sizes ===*/
/* 16 player bullets allows dense spread-shot patterns without exhaustion.
 * 8 enemy bullets is sufficient for the enemy density in this game. */
#define MAX_PLAYER_BULLETS  16
#define MAX_ENEMY_BULLETS    8
#define MAX_BULLETS         (MAX_PLAYER_BULLETS + MAX_ENEMY_BULLETS)  /* 24 total */

/*=== Bullet Owner ===*/
/* Used to select the correct pool region during allocation and to choose
 * the appropriate tile/palette for rendering. */
#define BULLET_OWNER_PLAYER  0
#define BULLET_OWNER_ENEMY   1

/*=== Bullet Types ===*/
/* Each type determines the visual appearance and hitbox size used
 * during collision detection. */
#define BULLET_TYPE_SINGLE      0   /* Fast, straight up - basic player shot */
#define BULLET_TYPE_SPREAD      1   /* Medium speed, 3-way angled fan pattern */
#define BULLET_TYPE_LASER       2   /* Slow, powerful - uses larger hitbox in collision */
#define BULLET_TYPE_ENEMY_BASIC 3   /* Straight downward enemy fire */
#define BULLET_TYPE_ENEMY_AIMED 4   /* Aimed at player position using vector normalization */

/*=== Player Weapon Types ===*/
/* Index into weapon behavior tables. Cycled with L/R shoulder buttons. */
#define WEAPON_SINGLE   0   /* High fire rate, single projectile */
#define WEAPON_SPREAD   1   /* Medium fire rate, 3 projectiles per shot */
#define WEAPON_LASER    2   /* Low fire rate, high damage per hit */
#define WEAPON_COUNT    3   /* Total number of weapon types for cycling */

/*=== Bullet Entity ===*/
/* Each bullet occupies one slot in the pool and corresponds to one OAM entry.
 * The struct is kept small to minimize per-frame iteration cost when
 * updating and rendering all 24 potential bullets. */
typedef struct {
    s16 x;          /* Screen X position in pixels */
    s16 y;          /* Screen Y position in pixels */
    s16 vx;         /* Horizontal velocity in 8.8 signed fixed-point */
    s16 vy;         /* Vertical velocity in 8.8 signed fixed-point */
    u8  type;       /* BULLET_TYPE_* - determines hitbox and visual */
    u8  owner;      /* BULLET_OWNER_PLAYER or BULLET_OWNER_ENEMY */
    u8  active;     /* ENTITY_ACTIVE or ENTITY_INACTIVE */
    u8  damage;     /* Damage dealt on collision (passed to enemyDamage/player hit) */
    u16 oam_id;     /* OAM byte offset for this bullet's OAM entry (slot_index * 4) */
    u16 tile_num;   /* SNES OBJ character name number for oamSet() */
    u8  palette;    /* OBJ palette index (0-7) passed to oamSet() */
} Bullet;

/*=== Player Weapon State ===*/
/* Tracks the currently selected weapon and its fire rate cooldown.
 * Global so the HUD can display the current weapon type. */
typedef struct {
    u8 weapon_type;      /* Current weapon: WEAPON_SINGLE / SPREAD / LASER */
    u8 fire_cooldown;    /* Frames remaining until next shot is allowed.
                          * Decremented each frame in bulletUpdateAll(). */
} WeaponState;

/* Global weapon state (accessible by HUD for display) */
extern WeaponState g_weapon;

/* Count of currently active bullets (updated each frame by bulletUpdateAll).
 * Used by bulletRenderAll for fast-path when no bullets are alive. */
extern u8 g_bullet_active_count;

/*
 * bulletInit - Initialize the bullet pool and weapon state.
 * Sets all bullets to inactive, assigns OAM slot IDs based on pool index,
 * and resets the weapon to WEAPON_SINGLE with no cooldown.
 */
void bulletInit(void);

/*
 * bulletLoadGraphics - Load bullet tile and palette data into VRAM/CGRAM.
 *
 * Loads player and enemy bullet graphics using spriteLoadTiles16() for
 * the 16x16 tile data (with proper SNES OBJ VRAM row spacing), and
 * spriteLoadPalette() for palette data into OBJ CGRAM slots.
 *
 * Must be called during force blank (screen off) since it writes to VRAM.
 */
void bulletLoadGraphics(void);

/*
 * bulletPlayerFire - Fire a player projectile based on the current weapon type.
 *
 * Spawns one or more bullets from the player's position, applying the
 * weapon-specific velocity, damage, and fire pattern. Respects the
 * fire_cooldown timer to enforce fire rate limits.
 *
 * Parameters:
 *   playerX, playerY - Top-left pixel position of the 32x32 player ship.
 *                      Bullet spawn is offset to center-top of the ship.
 */
void bulletPlayerFire(s16 playerX, s16 playerY);

/*
 * bulletEnemyFireDown - Fire a simple downward enemy bullet.
 *
 * Parameters:
 *   ex, ey - Enemy position. Bullet spawns at (ex, ey+8) moving straight down.
 */
void bulletEnemyFireDown(s16 ex, s16 ey);

/*
 * bulletEnemyFire - Fire an aimed enemy bullet toward a target position.
 *
 * Uses a reciprocal lookup table to approximate vector normalization without
 * hardware division (the 65816 has no divide instruction). The direction
 * vector (dx, dy) is scaled to the desired speed using LUT-based
 * multiplication and bit shifting.
 *
 * Parameters:
 *   ex, ey         - Enemy firing position (bullet origin)
 *   targetX, targetY - Target position (typically the player)
 *   type           - BULLET_TYPE_ENEMY_AIMED or BULLET_TYPE_ENEMY_BASIC
 */
void bulletEnemyFire(s16 ex, s16 ey, s16 targetX, s16 targetY, u8 type);

/*
 * bulletUpdateAll - Per-frame update for all bullets.
 *
 * For each active bullet:
 *   1. Applies velocity to position (integer part of 8.8 fixed-point)
 *   2. Deactivates bullets that move off-screen (with margin for sprite size)
 *
 * Also decrements the player weapon fire cooldown timer.
 * Updates g_bullet_active_count for the render fast-path.
 */
void bulletUpdateAll(void);

/*
 * bulletRenderAll - Write all bullet OAM data for the current frame.
 *
 * IMPORTANT: Must be called AFTER spriteRenderAll(), because the sprite
 * engine hides all OAM slots by default, including the bullet slots.
 * This function overwrites those slots with actual bullet positions.
 *
 * When no bullets are active, uses an optimized do-while loop with
 * additive OAM stride to hide all bullet slots without per-iteration
 * multiply (saves cycles on the 65816).
 */
void bulletRenderAll(void);

/*
 * bulletClearAll - Deactivate all bullets immediately.
 * Used during scene transitions (e.g., entering battle mode) to remove
 * all projectiles from the screen.
 */
void bulletClearAll(void);

/*
 * bulletNextWeapon / bulletPrevWeapon - Cycle the player weapon type.
 * Wraps around (LASER -> SINGLE or SINGLE -> LASER).
 * Resets fire cooldown to allow immediate firing with the new weapon.
 */
void bulletNextWeapon(void);
void bulletPrevWeapon(void);

/*
 * bulletGetPool - Get a pointer to the bullet pool array.
 * Returns: Pointer to bullet_pool[0]. Used by the collision system
 *          to iterate over bullets for hit detection.
 */
Bullet* bulletGetPool(void);

/*
 * bulletGetMasteryBonus - Get damage bonus for a weapon type based on kills.
 * Returns 0/1/2/3 for <10/<25/<50/>=50 kills respectively. (#150)
 */
u8 bulletGetMasteryBonus(u8 weapon_type);

/*
 * bulletAddWeaponKill - Increment kill count for the current weapon type.
 * Called from collision.c on enemy kill. (#150)
 */
void bulletAddWeaponKill(void);

/*
 * bulletResetMomentum - Reset the rapid fire hold counter.
 * Called from player.c when fire button is released. (#151)
 */
void bulletResetMomentum(void);

/* Per-weapon-type kill counts for mastery system (#150) */
extern u16 g_weapon_kills[3];

/* Consecutive frames fire button held, for momentum bonus (#151) */
extern u8 g_fire_hold_frames;

#endif /* BULLETS_H */
