# Phase 10: Collision Detection System

## Objective
Implement axis-aligned bounding box (AABB) collision detection between all game entities: player vs enemy bullets, player bullets vs enemies, and player vs enemies (contact damage). When collisions occur, trigger damage, destruction, score awards, and battle encounters (the RPG twist -- some collisions trigger turn-based battles instead of instant destruction).

## Prerequisites
- Phase 8 (Bullets), Phase 9 (Enemies), Phase 5 (Player).

## Detailed Tasks

1. Create `src/engine/collision.c` - AABB collision check functions optimized for SNES (no multiplication, use shifts).
2. Implement player-bullet vs enemy collision loop: iterate player bullets against all enemies.
3. Implement enemy-bullet vs player collision loop.
4. Implement player vs enemy contact collision (triggers battle or contact damage).
5. Define hitbox sizes per entity type (smaller than visual sprite for fair gameplay).
6. Implement collision response: damage application, bullet destruction, enemy destruction with score.
7. Implement the RPG battle trigger: when a player bullet hits a "strong" enemy (fighter+ types) for the first time, OR when player contacts an enemy, transition to turn-based battle instead of instant kill. Weaker enemies (scouts) die instantly from bullets.
8. Create explosion/particle effect placeholder for enemy destruction.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/engine/collision.h
```c
#ifndef COLLISION_H
#define COLLISION_H

#include "game.h"

/* Hitbox definition (offset from entity position) */
typedef struct {
    s8 x_offset;   /* Left edge offset from sprite x */
    s8 y_offset;   /* Top edge offset from sprite y */
    u8 width;      /* Hitbox width */
    u8 height;     /* Hitbox height */
} Hitbox;

/* Collision result flags */
#define COLL_NONE           0x00
#define COLL_DAMAGE         0x01  /* Apply damage and continue */
#define COLL_DESTROY        0x02  /* Destroy the target */
#define COLL_BATTLE         0x04  /* Trigger turn-based battle */
#define COLL_BOUNCE         0x08  /* Bounce off (unused for now) */

/* Predefined hitboxes for entity types */
/* Player ship 32x32: hitbox is inner 16x16 (generous for the player) */
#define HITBOX_PLAYER       { 8, 8, 16, 16 }

/* Enemy 16x16: hitbox is 12x12 centered */
#define HITBOX_ENEMY_SMALL  { 2, 2, 12, 12 }

/* Enemy 32x32: hitbox is 24x24 centered */
#define HITBOX_ENEMY_LARGE  { 4, 4, 24, 24 }

/* Bullet 8x8 (shown as 16x16): hitbox 6x6 centered */
#define HITBOX_BULLET_SMALL { 5, 5, 6, 6 }

/* Bullet 16x16: hitbox 10x10 centered */
#define HITBOX_BULLET_LARGE { 3, 3, 10, 10 }

/* Check if two AABBs overlap
 * Returns 1 if overlapping, 0 if not
 * Uses only comparisons (no multiply) */
u8 collisionCheckAABB(s16 ax, s16 ay, const Hitbox *ha,
                      s16 bx, s16 by, const Hitbox *hb);

/* Run all collision checks for one frame
 * This is the main entry point called from the game loop */
void collisionCheckAll(void);

/* Initialize collision system */
void collisionInit(void);

/* Set the callback for battle trigger */
typedef void (*BattleTriggerFn)(u8 enemyType);
void collisionSetBattleCallback(BattleTriggerFn callback);

/* Score tracking */
extern u32 player_score;
void scoreAdd(u16 points);

#endif
```

### J:/code/snes/snes-rpg-test/src/engine/collision.c
```c
/*==============================================================================
 * Collision Detection System
 * AABB (Axis-Aligned Bounding Box) checks between entity pools.
 *============================================================================*/

#include "engine/collision.h"
#include "engine/bullets.h"
#include "game/enemies.h"
#include "game/player.h"

/* Hitbox constants */
static const Hitbox hb_player = HITBOX_PLAYER;
static const Hitbox hb_enemy_small = HITBOX_ENEMY_SMALL;
static const Hitbox hb_enemy_large = HITBOX_ENEMY_LARGE;
static const Hitbox hb_bullet_small = HITBOX_BULLET_SMALL;
static const Hitbox hb_bullet_large = HITBOX_BULLET_LARGE;

static BattleTriggerFn battle_callback;
u32 player_score;

/* Enemy types that trigger battles vs instant kill */
/* Scouts are too weak for battle (instant kill). Fighter+ trigger battles. */
#define ENEMY_BATTLE_THRESHOLD ENEMY_TYPE_FIGHTER

void collisionInit(void)
{
    battle_callback = (BattleTriggerFn)0;
    player_score = 0;
}

void collisionSetBattleCallback(BattleTriggerFn callback)
{
    battle_callback = callback;
}

u8 collisionCheckAABB(s16 ax, s16 ay, const Hitbox *ha,
                      s16 bx, s16 by, const Hitbox *hb)
{
    s16 a_left, a_right, a_top, a_bottom;
    s16 b_left, b_right, b_top, b_bottom;

    a_left   = ax + ha->x_offset;
    a_right  = a_left + ha->width;
    a_top    = ay + ha->y_offset;
    a_bottom = a_top + ha->height;

    b_left   = bx + hb->x_offset;
    b_right  = b_left + hb->width;
    b_top    = by + hb->y_offset;
    b_bottom = b_top + hb->height;

    /* No overlap if any gap exists */
    if (a_right <= b_left) return 0;
    if (a_left >= b_right) return 0;
    if (a_bottom <= b_top) return 0;
    if (a_top >= b_bottom) return 0;

    return 1;
}

void scoreAdd(u16 points)
{
    u16 old_lo;
    old_lo = (u16)(player_score & 0xFFFF);
    player_score += points;
    /* Clamp at 9999999 or similar max if needed */
}

static void checkPlayerBulletsVsEnemies(void)
{
    u8 bi, ei;
    Bullet *bullets = bulletGetPool();
    Enemy *enemies = enemyGetPool();
    Bullet *b;
    Enemy *e;
    const EnemyTypeDef *def;
    const Hitbox *bh;
    const Hitbox *eh;

    for (bi = 0; bi < MAX_BULLETS; bi++) {
        b = &bullets[bi];
        if (b->active != ENTITY_ACTIVE) continue;
        if (b->owner != BULLET_OWNER_PLAYER) continue;

        bh = (b->type == BULLET_TYPE_LASER) ? &hb_bullet_large : &hb_bullet_small;

        for (ei = 0; ei < MAX_ENEMIES; ei++) {
            e = &enemies[ei];
            if (e->active != ENTITY_ACTIVE) continue;

            def = enemyGetTypeDef(e->type);
            eh = (def->sprite_size == OBJ_LARGE) ? &hb_enemy_large : &hb_enemy_small;

            if (collisionCheckAABB(b->x, b->y, bh, e->x, e->y, eh)) {
                /* Collision detected! */

                /* Deactivate the bullet */
                b->active = ENTITY_INACTIVE;

                /* Check if this enemy type triggers a battle */
                if (e->type >= ENEMY_BATTLE_THRESHOLD && battle_callback) {
                    /* Trigger turn-based battle */
                    battle_callback(e->type);
                    /* Enemy is removed during battle transition */
                    e->active = ENTITY_INACTIVE;
                    return; /* Exit collision loop during battle */
                }

                /* Normal damage */
                if (enemyDamage(e, b->damage)) {
                    /* Enemy destroyed */
                    scoreAdd(def->score_value);
                    e->active = ENTITY_INACTIVE;
                    /* TODO: spawn explosion particle (Phase 19) */
                }

                break; /* This bullet is consumed, check next bullet */
            }
        }
    }
}

static void checkEnemyBulletsVsPlayer(void)
{
    u8 bi;
    Bullet *bullets = bulletGetPool();
    Bullet *b;

    /* Skip if player is invincible */
    if (player.invincible_timer > 0) return;
    if (!player.visible) return;

    for (bi = 0; bi < MAX_BULLETS; bi++) {
        b = &bullets[bi];
        if (b->active != ENTITY_ACTIVE) continue;
        if (b->owner != BULLET_OWNER_ENEMY) continue;

        if (collisionCheckAABB(b->x, b->y, &hb_bullet_small,
                               player.x, player.y, &hb_player)) {
            /* Player hit by enemy bullet */
            b->active = ENTITY_INACTIVE;

            /* Trigger battle instead of instant damage */
            if (battle_callback) {
                battle_callback(ENEMY_TYPE_SCOUT);  /* Random encounter */
                return;
            }

            /* Fallback: direct damage + invincibility frames */
            player.invincible_timer = 120; /* 2 seconds invincibility */
            break;
        }
    }
}

static void checkPlayerVsEnemies(void)
{
    u8 ei;
    Enemy *enemies = enemyGetPool();
    Enemy *e;
    const EnemyTypeDef *def;
    const Hitbox *eh;

    if (player.invincible_timer > 0) return;
    if (!player.visible) return;

    for (ei = 0; ei < MAX_ENEMIES; ei++) {
        e = &enemies[ei];
        if (e->active != ENTITY_ACTIVE) continue;

        def = enemyGetTypeDef(e->type);
        eh = (def->sprite_size == OBJ_LARGE) ? &hb_enemy_large : &hb_enemy_small;

        if (collisionCheckAABB(player.x, player.y, &hb_player,
                               e->x, e->y, eh)) {
            /* Contact collision - always triggers battle */
            if (battle_callback) {
                battle_callback(e->type);
                e->active = ENTITY_INACTIVE;
                return;
            }

            /* Fallback: contact damage */
            player.invincible_timer = 120;
            e->active = ENTITY_INACTIVE;
            break;
        }
    }
}

void collisionCheckAll(void)
{
    checkPlayerBulletsVsEnemies();
    checkEnemyBulletsVsPlayer();
    checkPlayerVsEnemies();
}
```

## Technical Specifications

### Collision Check Performance
```
Worst case per frame:
  24 player bullets x 8 enemies = 192 AABB checks
  24 enemy bullets x 1 player = 24 AABB checks
  8 enemies x 1 player = 8 AABB checks
  Total: 224 AABB checks

Each AABB check: 4 comparisons + 4 additions = ~20 CPU cycles
Total: 224 * 20 = ~4480 cycles

At 3.58 MHz with ~30000 cycles per visible frame, this is ~15% of frame budget.
Acceptable, but if performance is tight, add early rejection (skip offscreen entities).
```

### Battle Trigger Logic
```
When does a collision trigger a BATTLE vs INSTANT DAMAGE?

1. Player bullet hits SCOUT (type 0):
   -> Instant damage. If scout HP reaches 0, destroyed + score.
   -> Scouts are too weak for full battle.

2. Player bullet hits FIGHTER/HEAVY/ELITE (type 1+):
   -> Trigger turn-based battle.
   -> Enemy is removed from overworld.
   -> Battle outcome determines if enemy is defeated.
   -> After battle, return to flight mode.

3. Enemy bullet hits PLAYER:
   -> Trigger random encounter battle.
   -> Or apply direct damage if battles are disabled in config.

4. Player CONTACTS enemy (any type):
   -> Always triggers battle (unavoidable close encounter).
```

### Hitbox Visualization (for debugging)
```
Player ship (32x32 sprite):
  +--------------------------------+
  |        margin (8px)            |
  |   +--------------------+      |
  |   |   HITBOX 16x16     |      |
  |   |                    |      |
  |   +--------------------+      |
  |                                |
  +--------------------------------+

This makes the player feel "fair" - near-misses don't count.
The hitbox center represents the cockpit of the ship.
```

## Acceptance Criteria
1. Player bullets destroy scout enemies on contact.
2. Player bullets against fighter+ enemies trigger the battle callback.
3. Enemy bullets hitting the player trigger the battle callback.
4. Player ship becomes invincible (blinking) for 2 seconds after being hit when battle callback is not set.
5. Score increases correctly when enemies are destroyed.
6. Destroyed bullets disappear from the screen.
7. No false-positive collisions (hitboxes are smaller than sprites).
8. Performance stays at 60fps with max entities on screen (8 enemies + 24 bullets).

## SNES-Specific Constraints
- No floating point in AABB checks. All integer comparisons.
- Avoid division and multiplication in the hot collision loop.
- The s16 type is critical for correct comparison of negative coordinates (sprites partially off-screen).
- 816-tcc may generate slow code for nested loops. Consider manual loop unrolling if performance is an issue.

## Estimated Complexity
**Medium** - AABB math is trivial, but the interaction between collision types (instant kill vs battle trigger) and proper entity lifecycle management requires careful state handling.
