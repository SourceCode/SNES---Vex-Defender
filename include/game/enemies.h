/*==============================================================================
 * Enemy Ship System
 * Manages enemy entity pool with AI movement patterns and firing behaviors.
 * Enemies spawn via scroll triggers and are destroyed by bullet collisions.
 *
 * Enemies use dedicated OAM slots (20-27) separate from the sprite engine
 * pool. enemyRenderAll() must be called AFTER spriteRenderAll() to overwrite
 * the sprite engine's default hiding of these OAM slots.
 *
 * Architecture overview:
 *   - Pool-based allocation: 8 enemy slots are statically allocated and
 *     recycled. No heap allocation is used (the 65816 has no malloc).
 *   - ROM-based type definitions (EnemyTypeDef): stat templates stored in
 *     ROM are read at spawn time to initialize each enemy instance.
 *   - Dedicated OAM slots: enemies bypass the sprite engine pool and write
 *     directly to OAM slots 20-27 during render. This avoids contention with
 *     the sprite pool allocator and ensures deterministic OAM ordering.
 *   - Zone-relative VRAM: each zone loads exactly 2 enemy sprite tilesets
 *     into VRAM slots A and B. The enemy type -> VRAM slot mapping changes
 *     per zone (e.g., SCOUT uses slot A in Zone 1, FIGHTER uses slot A in
 *     Zone 2), so the renderer uses a LUT rather than a direct type lookup.
 *============================================================================*/

#ifndef ENEMIES_H
#define ENEMIES_H

#include "game.h"

/* Maximum number of simultaneously active enemies on screen.
 * Limited to 8 to match the allocated OAM range (slots 20-27).
 * This also keeps the per-frame update loop cost predictable
 * on the 65816's ~3.58MHz clock. */
#define MAX_ENEMIES 8

/* Enemy types - index into the EnemyTypeDef table.
 * Each zone uses 2 of these types for its spawner callbacks. */
#define ENEMY_TYPE_SCOUT    0   /* Linear, fast, low HP - basic fodder enemy */
#define ENEMY_TYPE_FIGHTER  1   /* Sine wave, medium HP - weaving mid-tier */
#define ENEMY_TYPE_HEAVY    2   /* Hover + strafe, high HP - tanky mini-boss */
#define ENEMY_TYPE_ELITE    3   /* Chase player, high speed - aggressive pursuer */
#define ENEMY_TYPE_COUNT    4

/* AI movement patterns - assigned per enemy type in EnemyTypeDef.
 * These determine how aiUpdate() moves the enemy each frame. */
#define AI_LINEAR     0   /* Straight down (+ optional lateral from side-spawns) */
#define AI_SINE_WAVE  1   /* Descend + horizontal oscillation using sine LUT */
#define AI_SWOOP      2   /* Enter from side, curve, exit (future use - reserved) */
#define AI_HOVER      3   /* Descend to y=60, then strafe left/right bouncing off edges */
#define AI_CHASE      4   /* Track player X position while descending */

/* Enemy type definition (ROM data) - stat template for each enemy type.
 * Stored as const in ROM to save precious WRAM space on the SNES.
 * One entry per ENEMY_TYPE_* value. */
typedef struct {
    u8  max_hp;         /* Starting hit points when spawned */
    u8  speed;          /* Downward movement speed in whole pixels/frame;
                         * converted to 8.8 fixed-point at spawn time */
    u8  fire_rate;      /* Frames between shots; 0 = enemy never fires.
                         * At 60fps, a value of 60 = one shot per second */
    u8  ai_pattern;     /* AI_* movement pattern constant */
    u16 score_value;    /* Points awarded to g_score when destroyed */
    u8  damage;         /* Contact damage dealt to player on collision */
} EnemyTypeDef;

/* Enemy instance - runtime state for one active enemy.
 * Allocated from the static pool in enemy_pool[MAX_ENEMIES].
 * Uses 8.8 fixed-point for velocity to allow sub-pixel movement
 * on the 65816 without floating-point math. */
typedef struct {
    s16 x, y;           /* Screen position in whole pixels (top-left corner of 32x32 sprite) */
    s16 vx, vy;         /* Velocity in 8.8 fixed-point: high byte = pixels, low byte = fraction.
                         * e.g., 0x0180 = 1.5 pixels/frame. Extracted via >>8 each frame. */
    u8  type;           /* ENEMY_TYPE_* index into enemy_types[] table */
    u8  hp;             /* Current HP; when <= 0 from enemyDamage(), transitions to DYING */
    u8  active;         /* ENTITY_INACTIVE(0) / ENTITY_ACTIVE(1) / ENTITY_DYING(2) state */
    u8  fire_timer;     /* Countdown frames until next shot; reset to fire_rate on fire */
    u8  ai_state;       /* Pattern-specific sub-state (e.g., 0=descending, 1=strafing for HOVER) */
    u8  ai_timer;       /* Pattern-specific frame counter for timed behaviors.
                         * For SINE_WAVE: index into sine LUT is (ai_timer >> 2) & 0x0F */
    s16 ai_param1;      /* Pattern-specific parameter: initial X for SINE_WAVE center of
                         * oscillation. Set at spawn time and remains constant. */
    u8  flash_timer;    /* Damage blink countdown: when > 0, sprite blinks (hidden on odd frames).
                         * For DYING state, counts down from 10; at 0, enemy becomes INACTIVE */
    u16 oam_id;         /* OAM byte offset = (OAM_ENEMIES + pool_index) * 4.
                         * Used directly with oamSet() / oamSetEx() / oamSetVisible(). */
    u8  age;            /* Frames since spawn, capped at 255 (#146 speed kill bonus) */
    u8  is_golden;      /* 1 = rare golden variant with 2x HP, 3x score (#147) */
    u8  shield;         /* 1-hit shield absorbs first bullet without damage (#181) */
    u8  is_hazard;      /* 1 = environmental hazard, invulnerable to bullets (#186) */
} Enemy;

/* Initialize enemy system: clears all pool slots to INACTIVE and assigns
 * OAM byte offsets. Call once at game startup before any spawning. */
void enemyInit(void);

/* Load enemy graphics for the specified zone into VRAM. Loads 2 enemy
 * sprite tilesets (32x32, 4bpp) into OBJ VRAM slots A and B, plus their
 * palettes into OBJ CGRAM. Must be called during force blank (screen off)
 * since it writes to VRAM via DMA. Also populates the gfx_lut[] for fast
 * rendering. */
void enemyLoadGraphics(u8 zoneId);

/* Register scroll-distance triggers for enemy wave spawning in the given zone.
 * Clears existing triggers and adds zone-specific callbacks that spawn enemies
 * at predetermined scroll distances (e.g., 300px, 600px, ...). Each trigger
 * fires exactly once when the accumulated scroll distance crosses its threshold. */
void enemySetupZoneTriggers(u8 zoneId);

/* Spawn a single enemy at the given screen position with the given type.
 * Searches the pool for an INACTIVE slot and initializes it from the
 * EnemyTypeDef table. Returns pointer to the new enemy, or NULL (0) if
 * the pool is full or the type is invalid. */
Enemy* enemySpawn(u8 type, s16 x, s16 y);

/* Spawn a horizontal wave of enemies with uniform spacing.
 * count enemies are placed starting at (startX, startY), each offset by
 * (spacingX, spacingY). Use spacingY != 0 for diagonal formations.
 * Useful for scripted wave patterns in zone trigger callbacks. */
void enemySpawnWave(u8 type, u8 count, s16 startX, s16 startY, s16 spacingX, s16 spacingY);

/* Spawn an enemy entering from the left side of the screen.
 * For LINEAR AI, spawns off-screen at x=-24 with rightward velocity (1.5 px/f).
 * For other AI types, spawns at visible left edge (x=24) and lets AI handle movement. */
void enemySpawnFromLeft(u8 type, s16 y);

/* Spawn an enemy entering from the right side of the screen.
 * For LINEAR AI, spawns off-screen at x=SCREEN_W+8 with leftward velocity.
 * Mirror of enemySpawnFromLeft() for right-side entries. */
void enemySpawnFromRight(u8 type, s16 y);

/* Update all enemies for one frame: runs AI movement, checks off-screen removal,
 * handles firing logic (aimed or straight-down based on AI type), and counts
 * active enemies. Updates g_enemy_active_count as a side effect. */
void enemyUpdateAll(void);

/* Render all enemies to OAM. Writes directly to OAM slots 20-27.
 * Must be called AFTER spriteRenderAll() and bulletRenderAll() because the
 * sprite engine hides unused OAM slots by default - this function overwrites
 * those slots with actual enemy sprite data. Handles damage blink, dying
 * death-shudder animation, and off-screen culling. */
void enemyRenderAll(void);

/* Apply damage to an enemy. If HP drops to 0, transitions to DYING state
 * (10-frame blink-out animation). Returns 1 if the enemy was destroyed,
 * 0 if it survived the hit. flash_timer is set for visual damage feedback. */
u8 enemyDamage(Enemy *e, u8 damage);

/* Immediately deactivate all enemies. Used during zone transitions and
 * battle transitions to clear the field without death animations. */
void enemyKillAll(void);

/* Return pointer to the enemy pool array for external iteration
 * (e.g., collision detection in the collision system). Callers should
 * iterate MAX_ENEMIES entries and check the 'active' field. */
Enemy* enemyGetPool(void);

/* Return pointer to the ROM-based EnemyTypeDef for the given type.
 * Clamps invalid type values to 0 (SCOUT) to prevent out-of-bounds reads. */
const EnemyTypeDef* enemyGetTypeDef(u8 type);

/* Active enemy count, updated each frame by enemyUpdateAll().
 * Used by the render fast-path and external systems (e.g., boss trigger
 * that waits for all enemies to be cleared before spawning boss). */
extern u8 g_enemy_active_count;

/* Spawn a V-formation of 5 enemies centered at (centerX, topY) (#164).
 * Creates a V-shape pattern: 1 point + 2 wings + 2 far wings. */
void enemySpawnVFormation(u8 type, s16 centerX, s16 topY);

#endif /* ENEMIES_H */
