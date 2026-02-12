# Phase 9: Enemy Ship System & AI Patterns

## Objective
Implement an enemy entity pool with configurable AI movement patterns and behaviors. Each enemy has a type (scout, fighter, heavy, elite) with different stats, movement patterns, and firing behaviors. Enemies are spawned by scroll triggers (Phase 7) and destroyed by bullet collisions (Phase 10).

## Prerequisites
- Phase 5 (Sprite Engine), Phase 7 (Scrolling/Triggers), Phase 8 (Bullets).

## Detailed Tasks

1. Create `src/game/enemies.c` - Enemy pool manager with up to 8 simultaneous enemies.
2. Define enemy types with stats (HP, speed, fire rate, score value, tile offset, size).
3. Implement 5 AI movement patterns:
   - LINEAR: Move straight down at constant speed
   - SINE_WAVE: Move down while oscillating horizontally (sine wave)
   - SWOOP: Enter from side, curve toward player, exit opposite side
   - HOVER: Move to a Y position, stop, strafe left/right while firing
   - CHASE: Slowly track toward the player's X position while descending
4. Implement enemy firing logic: each enemy type has a fire_timer that counts down, spawning enemy bullets.
5. Implement enemy spawn function called from scroll triggers.
6. Load enemy sprite graphics into OBJ VRAM during zone setup.
7. Create wave spawn helper that spawns N enemies in a formation pattern.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/game/enemies.h
```c
#ifndef ENEMIES_H
#define ENEMIES_H

#include "game.h"
#include "engine/sprites.h"

#define MAX_ENEMIES 8

/* Enemy types */
#define ENEMY_TYPE_SCOUT    0  /* Zone 1: weak, fast, linear */
#define ENEMY_TYPE_FIGHTER  1  /* Zone 2: medium, sine wave */
#define ENEMY_TYPE_HEAVY    2  /* Zone 2-3: tough, hover+fire */
#define ENEMY_TYPE_ELITE    3  /* Zone 3: fast, chase player */
#define ENEMY_TYPE_COUNT    4

/* AI movement patterns */
#define AI_LINEAR     0
#define AI_SINE_WAVE  1
#define AI_SWOOP      2
#define AI_HOVER      3
#define AI_CHASE      4

/* Enemy type definition (ROM data) */
typedef struct {
    u8  max_hp;         /* Hit points */
    u8  speed;          /* Movement speed (8.8 high byte) */
    u8  fire_rate;      /* Frames between shots (0=no fire) */
    u8  ai_pattern;     /* AI_LINEAR, AI_SINE_WAVE, etc. */
    u8  sprite_size;    /* OBJ_SMALL or OBJ_LARGE */
    u8  palette;        /* OBJ palette slot */
    u16 tile_offset;    /* Tile number in OBJ VRAM */
    u16 score_value;    /* Points awarded when destroyed */
    u8  damage;         /* Contact damage to player */
    u8  bullet_damage;  /* Bullet damage */
} EnemyTypeDef;

/* Enemy instance */
typedef struct {
    s16 x, y;              /* Screen position (pixels) */
    fixed8_8 fx, fy;       /* Sub-pixel position */
    s16 vx, vy;            /* Velocity (8.8 fixed) */
    u8  type;              /* ENEMY_TYPE_* index */
    u8  hp;                /* Current HP */
    u8  active;            /* ENTITY_ACTIVE/INACTIVE/DYING */
    u8  oam_slot;          /* OAM slot index (4-11) */
    u8  fire_timer;        /* Countdown to next shot */
    u8  ai_state;          /* Pattern-specific state variable */
    u16 ai_timer;          /* Pattern-specific timer */
    s16 ai_param1;         /* Pattern-specific parameter */
    s16 ai_param2;         /* Pattern-specific parameter */
    u8  flash_timer;       /* Damage flash effect (palette swap) */
} Enemy;

/* Initialize enemy system */
void enemyInit(void);

/* Load enemy graphics for a zone */
void enemyLoadZoneGraphics(u8 zoneId);

/* Spawn a single enemy at position with type */
Enemy* enemySpawn(u8 type, s16 x, s16 y);

/* Spawn a wave of enemies in formation */
void enemySpawnWave(u8 type, u8 count, s16 startX, s16 startY, s16 spacingX, s16 spacingY);

/* Spawn enemy entering from left side */
void enemySpawnFromLeft(u8 type, s16 y);

/* Spawn enemy entering from right side */
void enemySpawnFromRight(u8 type, s16 y);

/* Update all enemies (AI, movement, firing) */
void enemyUpdateAll(void);

/* Render all enemies to OAM */
void enemyRenderAll(void);

/* Damage an enemy, returns 1 if destroyed */
u8 enemyDamage(Enemy *e, u8 damage);

/* Kill all active enemies (zone transition) */
void enemyKillAll(void);

/* Get pointer to enemy pool for collision checks */
Enemy* enemyGetPool(void);

/* Get enemy type definition */
const EnemyTypeDef* enemyGetTypeDef(u8 type);

#endif
```

### J:/code/snes/snes-rpg-test/src/game/enemies.c
```c
/*==============================================================================
 * Enemy Ship System & AI
 *============================================================================*/

#include "game/enemies.h"
#include "engine/bullets.h"
#include "game/player.h"

/* Extern asset labels */
extern char enemy_scout_tiles, enemy_scout_tiles_end;
extern char enemy_scout_pal, enemy_scout_pal_end;

/* Enemy type definitions (ROM constant data) */
static const EnemyTypeDef enemy_types[ENEMY_TYPE_COUNT] = {
    /* SCOUT: weak, fast, linear path, small sprite */
    { 10, 2, 60, AI_LINEAR, OBJ_SMALL, 1, 0, 100, 10, 10 },
    /* FIGHTER: medium, sine wave movement, large sprite */
    { 25, 1, 45, AI_SINE_WAVE, OBJ_LARGE, 1, 0, 200, 15, 12 },
    /* HEAVY: tough, hovers and fires, large sprite */
    { 50, 1, 30, AI_HOVER, OBJ_LARGE, 2, 0, 350, 20, 15 },
    /* ELITE: fast, chases player, large sprite */
    { 35, 3, 40, AI_CHASE, OBJ_LARGE, 2, 0, 500, 20, 18 },
};

static Enemy enemy_pool[MAX_ENEMIES];

/* Sine lookup (abbreviated - 16 entries for AI oscillation) */
static const s8 ai_sine[16] = {
    0, 3, 5, 7, 7, 7, 5, 3, 0, -3, -5, -7, -7, -7, -5, -3
};

/* OAM slots for enemies: slots 4-11 */
#define ENEMY_OAM_START 4

void enemyInit(void)
{
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        enemy_pool[i].active = ENTITY_INACTIVE;
        enemy_pool[i].oam_slot = ENEMY_OAM_START + i;
    }
}

void enemyLoadZoneGraphics(u8 zoneId)
{
    /* Load zone-appropriate enemy tiles into OBJ VRAM */
    /* For now, load scout tiles for all zones */
    spriteLoadTiles(&enemy_scout_tiles,
                    &enemy_scout_tiles_end - &enemy_scout_tiles,
                    OBJ_ENEMY_OFFSET);
    spriteLoadPalette(&enemy_scout_pal,
                      &enemy_scout_pal_end - &enemy_scout_pal,
                      1);
    /* TODO: Load additional enemy types per zone in Phase 18 */
}

Enemy* enemySpawn(u8 type, s16 x, s16 y)
{
    u8 i;
    const EnemyTypeDef *def;

    for (i = 0; i < MAX_ENEMIES; i++) {
        if (enemy_pool[i].active == ENTITY_INACTIVE) {
            def = &enemy_types[type];
            enemy_pool[i].active = ENTITY_ACTIVE;
            enemy_pool[i].type = type;
            enemy_pool[i].x = x;
            enemy_pool[i].y = y;
            enemy_pool[i].fx = TO_FIXED8(x);
            enemy_pool[i].fy = TO_FIXED8(y);
            enemy_pool[i].vx = 0;
            enemy_pool[i].vy = TO_FIXED8(def->speed);
            enemy_pool[i].hp = def->max_hp;
            enemy_pool[i].fire_timer = def->fire_rate;
            enemy_pool[i].ai_state = 0;
            enemy_pool[i].ai_timer = 0;
            enemy_pool[i].ai_param1 = 0;
            enemy_pool[i].ai_param2 = 0;
            enemy_pool[i].flash_timer = 0;
            return &enemy_pool[i];
        }
    }
    return (Enemy *)0; /* Pool full */
}

void enemySpawnWave(u8 type, u8 count, s16 startX, s16 startY,
                    s16 spacingX, s16 spacingY)
{
    u8 i;
    for (i = 0; i < count; i++) {
        enemySpawn(type,
                   startX + (s16)i * spacingX,
                   startY + (s16)i * spacingY);
    }
}

void enemySpawnFromLeft(u8 type, s16 y)
{
    Enemy *e = enemySpawn(type, -32, y);
    if (e) {
        e->vx = TO_FIXED8(1);  /* Move right */
        e->vy = TO_FIXED8(1);  /* And down */
    }
}

void enemySpawnFromRight(u8 type, s16 y)
{
    Enemy *e = enemySpawn(type, 256, y);
    if (e) {
        e->vx = -TO_FIXED8(1);  /* Move left */
        e->vy = TO_FIXED8(1);   /* And down */
    }
}

static void aiUpdate(Enemy *e)
{
    const EnemyTypeDef *def = &enemy_types[e->type];

    switch (def->ai_pattern) {
        case AI_LINEAR:
            /* Straight down */
            e->fy += e->vy;
            break;

        case AI_SINE_WAVE:
            /* Down + horizontal oscillation */
            e->fy += e->vy;
            e->ai_timer++;
            e->fx += (s16)ai_sine[e->ai_timer & 0x0F] << 4;
            break;

        case AI_SWOOP:
            /* Enter from side, curve, exit */
            e->ai_timer++;
            e->fx += e->vx;
            /* Curve: gradually reduce horizontal, increase vertical */
            if (e->ai_timer > 30) {
                e->fy += e->vy;
                if (e->vx > 0) e->vx -= 4;
                if (e->vx < 0) e->vx += 4;
            }
            break;

        case AI_HOVER:
            /* Move to hover position, then strafe */
            if (e->ai_state == 0) {
                /* Phase 0: descend to hover Y */
                e->fy += e->vy;
                if (e->y >= 60) {
                    e->ai_state = 1;
                    e->vx = TO_FIXED8(1);
                }
            } else {
                /* Phase 1: strafe left/right */
                e->fx += e->vx;
                if (e->x <= 16 || e->x >= 224) {
                    e->vx = -e->vx;
                }
            }
            break;

        case AI_CHASE:
            /* Track player X while descending */
            e->fy += e->vy;
            if (player.x > e->x + 4) {
                e->fx += 0x0080; /* Move right 0.5 px/frame */
            } else if (player.x < e->x - 4) {
                e->fx -= 0x0080; /* Move left 0.5 px/frame */
            }
            break;
    }

    /* Convert fixed to integer */
    e->x = (s16)FROM_FIXED8(e->fx);
    e->y = (s16)FROM_FIXED8(e->fy);
}

void enemyUpdateAll(void)
{
    u8 i;
    Enemy *e;
    const EnemyTypeDef *def;

    for (i = 0; i < MAX_ENEMIES; i++) {
        e = &enemy_pool[i];
        if (e->active != ENTITY_ACTIVE) continue;

        /* AI movement */
        aiUpdate(e);

        /* Off-screen removal */
        if (e->y > 240 || e->y < -64 || e->x < -64 || e->x > 300) {
            e->active = ENTITY_INACTIVE;
            continue;
        }

        /* Firing logic */
        def = &enemy_types[e->type];
        if (def->fire_rate > 0) {
            e->fire_timer--;
            if (e->fire_timer == 0) {
                e->fire_timer = def->fire_rate;
                /* Fire toward player for aimed, or straight down */
                if (def->ai_pattern == AI_CHASE || def->ai_pattern == AI_HOVER) {
                    bulletEnemyFire(e->x + 8, e->y + 16,
                                   player.x + 16, player.y + 16,
                                   BULLET_TYPE_ENEMY_AIMED);
                } else {
                    bulletEnemyFireDown(e->x + 8, e->y + 16);
                }
            }
        }

        /* Damage flash countdown */
        if (e->flash_timer > 0) e->flash_timer--;
    }
}

void enemyRenderAll(void)
{
    u8 i;
    Enemy *e;
    u16 oam_id;
    const EnemyTypeDef *def;
    u8 pal;

    for (i = 0; i < MAX_ENEMIES; i++) {
        e = &enemy_pool[i];
        oam_id = e->oam_slot * 4;

        if (e->active != ENTITY_ACTIVE) {
            oamSetVisible(oam_id, OBJ_HIDE);
            continue;
        }

        def = &enemy_types[e->type];

        /* Flash white on hit (swap to palette 7 briefly) */
        pal = (e->flash_timer > 0) ? 7 : def->palette;

        oamSet(oam_id,
               (u16)e->x, (u16)e->y,
               2, /* priority */
               0, 0, /* no flip */
               def->tile_offset + (OBJ_ENEMY_OFFSET >> 4),
               pal);
        oamSetEx(oam_id, def->sprite_size, OBJ_SHOW);
    }
}

u8 enemyDamage(Enemy *e, u8 damage)
{
    if (e->hp <= damage) {
        e->hp = 0;
        e->active = ENTITY_DYING;
        return 1; /* Destroyed */
    }
    e->hp -= damage;
    e->flash_timer = 4; /* Flash for 4 frames */
    return 0;
}

void enemyKillAll(void)
{
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        enemy_pool[i].active = ENTITY_INACTIVE;
    }
}

Enemy* enemyGetPool(void) { return enemy_pool; }
const EnemyTypeDef* enemyGetTypeDef(u8 type) { return &enemy_types[type]; }
```

## Technical Specifications

### Enemy Type Stats Table
```
Type     HP  Speed  FireRate  AI Pattern   Sprite  Score
------   --  -----  --------  ----------   ------  -----
Scout    10  2.0    60 frm    LINEAR       16x16   100
Fighter  25  1.0    45 frm    SINE_WAVE    32x32   200
Heavy    50  1.0    30 frm    HOVER        32x32   350
Elite    35  3.0    40 frm    CHASE        32x32   500
```

### AI Pattern Details
```
LINEAR:     vy = speed (constant), vx = 0. Straight line down.
SINE_WAVE:  vy = speed, vx = sine_table[timer & 0x0F] * scale.
            Oscillation period: 16 frames. Amplitude: ~7 pixels.
SWOOP:      Enter with vx = +/-1.0. After 30 frames, vx decelerates,
            vy increases. Creates a curved swoop path.
HOVER:      State 0: descend to y=60. State 1: strafe left/right
            between x=16 and x=224, bouncing off edges.
CHASE:      vy = speed. vx = +/-0.5 toward player.x.
            Slowly homes in but doesn't match exactly (escapable).
```

## Acceptance Criteria
1. enemySpawn() creates a visible enemy at the specified position.
2. Scout enemies move straight down and exit the screen.
3. Sine wave enemies oscillate horizontally while descending.
4. Hover enemies stop at y=60 and strafe horizontally.
5. Chase enemies track the player's X position.
6. Enemies fire bullets at their defined fire rate.
7. enemyDamage() reduces HP; destroying an enemy sets it to DYING state.
8. Hit flash effect briefly changes palette on damage.
9. Maximum 8 enemies visible simultaneously.
10. Off-screen enemies are automatically deactivated.

## SNES-Specific Constraints
- 8 enemies at 32x32 = 8 OAM large entries. Within 128 OAM limit.
- 32 sprites/scanline: if all 8 enemies are on the same scanline (unlikely but possible during horizontal formations), some may flicker. Stagger spawn Y positions.
- Enemy AI calculations must be cheap. No division, no floating point. Use lookup tables and shifts.
- The ai_sine table uses s8 values (-7 to +7) for minimal memory.

## Estimated Complexity
**Medium** - Multiple AI patterns require testing each one individually. State machine for HOVER and SWOOP patterns adds complexity.
