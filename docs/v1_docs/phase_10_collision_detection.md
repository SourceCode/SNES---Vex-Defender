# Phase 10: Collision Detection System

## Objective
Implement axis-aligned bounding box (AABB) collision detection between all game objects: player bullets vs. enemies, enemy bullets vs. player, player vs. items, and player vs. enemies (contact damage). This ties together the shooter gameplay loop.

## Prerequisites
- Phase 5 (Player Ship) complete
- Phase 8 (Bullet System) complete
- Phase 9 (Enemy System) complete

## Detailed Tasks

### 1. Define Collision Hitboxes
Each object type has a hitbox smaller than its sprite (more forgiving gameplay).

### 2. Implement AABB Collision Check
Core function that tests overlap between two rectangles.

### 3. Create Collision Dispatch System
Check all relevant object pairs each frame.

### 4. Implement Collision Responses
- Player bullet hits enemy → damage enemy, destroy bullet
- Enemy bullet hits player → damage player, destroy bullet, invincibility frames
- Player touches enemy → contact damage, knockback
- Player touches item → collect item, apply effect

### 5. Optimize with Broad-Phase Culling
Skip checks for objects that are obviously far apart.

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/collision.h` | CREATE | Collision system header |
| `src/collision.c` | CREATE | Collision system implementation |
| `src/main.c` | MODIFY | Call collision check in main loop |
| `Makefile` | MODIFY | Add collision.obj |
| `data/linkfile` | MODIFY | Add collision.obj |

## Technical Specifications

### Hitbox Definitions
```c
/* Hitbox: smaller than sprite for forgiving gameplay */
typedef struct {
    s16 x, y;       /* Top-left corner (absolute screen coords) */
    u8  w, h;       /* Width, height */
} HitBox;

/* Hitbox sizes per object type (offset from sprite position) */
/* Player ship (32x32 sprite): hitbox is center 16x20 */
#define PLAYER_HB_OFFSET_X  8
#define PLAYER_HB_OFFSET_Y  6
#define PLAYER_HB_WIDTH    16
#define PLAYER_HB_HEIGHT   20

/* Enemy ship (32x32 sprite): hitbox is center 24x24 */
#define ENEMY_HB_OFFSET_X   4
#define ENEMY_HB_OFFSET_Y   4
#define ENEMY_HB_WIDTH     24
#define ENEMY_HB_HEIGHT    24

/* Bullet (8x8 or 16x16): hitbox is full sprite */
#define BULLET_SM_HB_W     6
#define BULLET_SM_HB_H     6
#define BULLET_LG_HB_W    12
#define BULLET_LG_HB_H    12

/* Item (16x16): hitbox is full sprite */
#define ITEM_HB_WIDTH      16
#define ITEM_HB_HEIGHT     16
```

### collision.h
```c
#ifndef COLLISION_H
#define COLLISION_H

#include <snes.h>
#include "config.h"

typedef struct {
    s16 x, y;
    u8  w, h;
} HitBox;

/* Collision flags (bitfield for what was hit this frame) */
#define COL_NONE              0x00
#define COL_PLAYER_HIT        0x01
#define COL_ENEMY_KILLED      0x02
#define COL_ITEM_COLLECTED    0x04
#define COL_PLAYER_CONTACT    0x08

/*--- Functions ---*/
void collision_init(void);
void collision_check_all(void);
u8   collision_test_aabb(HitBox *a, HitBox *b);

/* Result of last collision check frame */
extern u8 g_collision_flags;

#endif /* COLLISION_H */
```

### collision.c
```c
#include "collision.h"
#include "player.h"
#include "bullet.h"
#include "enemy.h"

u8 g_collision_flags;

void collision_init(void) {
    g_collision_flags = COL_NONE;
}

/* Core AABB overlap test */
u8 collision_test_aabb(HitBox *a, HitBox *b) {
    /* No overlap if any side is outside */
    if (a->x + a->w <= b->x) return 0;  /* A is left of B */
    if (b->x + b->w <= a->x) return 0;  /* B is left of A */
    if (a->y + a->h <= b->y) return 0;  /* A is above B */
    if (b->y + b->h <= a->y) return 0;  /* B is above A */
    return 1; /* Overlap! */
}

/* Helper: build hitbox for player */
static void get_player_hitbox(HitBox *hb) {
    hb->x = g_player.x + PLAYER_HB_OFFSET_X;
    hb->y = g_player.y + PLAYER_HB_OFFSET_Y;
    hb->w = PLAYER_HB_WIDTH;
    hb->h = PLAYER_HB_HEIGHT;
}

/* Helper: build hitbox for enemy */
static void get_enemy_hitbox(Enemy *e, HitBox *hb) {
    hb->x = e->x + ENEMY_HB_OFFSET_X;
    hb->y = e->y + ENEMY_HB_OFFSET_Y;
    hb->w = ENEMY_HB_WIDTH;
    hb->h = ENEMY_HB_HEIGHT;
}

/* Helper: build hitbox for bullet */
static void get_bullet_hitbox(Bullet *b, HitBox *hb) {
    hb->x = b->x + 2;
    hb->y = b->y + 2;
    hb->w = BULLET_SM_HB_W;
    hb->h = BULLET_SM_HB_H;
}

void collision_check_all(void) {
    u8 i, j;
    HitBox hb_player, hb_enemy, hb_bullet;

    g_collision_flags = COL_NONE;

    /* Skip collision during invincibility or if player is dead */
    u8 player_vulnerable = (g_player.invincible == 0 && g_player.visible);

    /* Get player hitbox (used in multiple checks) */
    get_player_hitbox(&hb_player);

    /*=== Check 1: Player bullets vs. Enemies ===*/
    for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
        Bullet *b = &g_bullets.pool[i];
        if (!b->active || b->owner != BULLET_OWNER_PLAYER) continue;

        get_bullet_hitbox(b, &hb_bullet);

        for (j = 0; j < MAX_ENEMIES; j++) {
            Enemy *e = &g_enemies.pool[j];
            if (!e->active) continue;

            get_enemy_hitbox(e, &hb_enemy);

            if (collision_test_aabb(&hb_bullet, &hb_enemy)) {
                /* Hit! Damage enemy, destroy bullet */
                enemy_damage(e, b->damage);
                b->active = 0;
                g_collision_flags |= COL_ENEMY_KILLED;

                /* Play hit sound (Phase 17) */
                /* sound_play_sfx(SFX_HIT); */

                break; /* This bullet is done, check next bullet */
            }
        }
    }

    /*=== Check 2: Enemy bullets vs. Player ===*/
    if (player_vulnerable) {
        for (i = MAX_PLAYER_BULLETS; i < MAX_BULLETS; i++) {
            Bullet *b = &g_bullets.pool[i];
            if (!b->active || b->owner != BULLET_OWNER_ENEMY) continue;

            get_bullet_hitbox(b, &hb_bullet);

            if (collision_test_aabb(&hb_bullet, &hb_player)) {
                /* Player hit! */
                g_player.hp -= b->damage;
                if (g_player.hp > 60000) g_player.hp = 0; /* Unsigned underflow check */
                b->active = 0;
                player_flash(); /* Invincibility frames */
                g_collision_flags |= COL_PLAYER_HIT;

                /* Play damage sound (Phase 17) */
                /* sound_play_sfx(SFX_PLAYER_HIT); */

                if (g_player.hp == 0) {
                    /* Player defeated - trigger game over or battle */
                    g_game.current_state = STATE_GAMEOVER;
                }
                break; /* Only one hit per frame */
            }
        }
    }

    /*=== Check 3: Player vs. Enemies (contact damage) ===*/
    if (player_vulnerable) {
        for (j = 0; j < MAX_ENEMIES; j++) {
            Enemy *e = &g_enemies.pool[j];
            if (!e->active) continue;

            get_enemy_hitbox(e, &hb_enemy);

            if (collision_test_aabb(&hb_player, &hb_enemy)) {
                /* Contact damage! Both take damage */
                g_player.hp -= 15; /* Contact hurts more */
                if (g_player.hp > 60000) g_player.hp = 0;
                enemy_damage(e, 20); /* Player ramming does damage */
                player_flash();
                g_collision_flags |= COL_PLAYER_CONTACT;

                if (g_player.hp == 0) {
                    g_game.current_state = STATE_GAMEOVER;
                }
                break;
            }
        }
    }
}
```

### Main Loop Integration
```c
/* In the main loop, after movement updates but before rendering: */
if (g_game.current_state == STATE_FLIGHT) {
    collision_check_all();

    /* React to collision results */
    if (g_collision_flags & COL_PLAYER_HIT) {
        /* Screen flash effect, sound, etc. */
    }
}
```

## Performance Considerations
```
Worst case collision checks per frame:
  Player bullets vs enemies: 8 bullets x 8 enemies = 64 AABB tests
  Enemy bullets vs player:   12 bullets x 1 player = 12 AABB tests
  Player vs enemies:         1 player x 8 enemies  = 8 AABB tests
  Total: ~84 AABB tests maximum

Each AABB test: 4 comparisons = ~20 CPU cycles
Total: ~1,680 cycles = negligible on 3.58MHz CPU (~0.05% of frame time)
```

## Acceptance Criteria
1. Player bullets destroy enemies on contact
2. Enemy bullets damage the player on contact
3. Player-enemy contact deals mutual damage
4. Destroyed bullets disappear immediately
5. Player gets invincibility frames after being hit (no instant multi-hits)
6. Player HP reaching 0 triggers game over state
7. Collision hitboxes are smaller than sprites (forgiving gameplay)
8. No false positives when objects are far apart
9. Performance is smooth with maximum objects on screen

## SNES-Specific Constraints
- All math is integer (s16) - no floating point
- Unsigned underflow: subtracting from u16 HP wraps to ~65535, must check
- Avoid division in collision code - AABB only needs addition and comparison
- Frame budget: collision should use < 5% of CPU time per frame

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~42KB | 256KB    | ~214KB    |
| WRAM     | ~720B | 128KB   | ~127KB    |
| VRAM     | ~10KB | 64KB    | ~54KB     |
| CGRAM    | 128B  | 512B    | 384B      |

## Estimated Complexity
**Medium** - AABB collision is simple math. The main complexity is the dispatch system and handling all the different collision responses correctly.

## Agent Instructions
1. Create `src/collision.h` and `src/collision.c`
2. Update Makefile and linkfile
3. Call `collision_init()` in game_init()
4. Call `collision_check_all()` in main loop AFTER movement updates, BEFORE rendering
5. Test: fire at an enemy, verify it takes damage and dies
6. Test: let an enemy bullet hit the player, verify HP decreases and invincibility activates
7. Test: ram into an enemy, verify mutual damage
8. Test: reduce player HP to 0, verify game over state triggers
9. Verify no crashes with max objects (8 enemies + 20 bullets + player)
