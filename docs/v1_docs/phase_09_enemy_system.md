# Phase 9: Enemy Ship System & AI Patterns

## Objective
Implement the enemy ship system with an object pool, multiple enemy types, AI movement patterns, and firing behaviors. Enemies spawn based on scroll position triggers and zone configuration, creating the core "shooter" gameplay.

## Prerequisites
- Phase 5 (Sprite Engine) complete
- Phase 7 (Vertical Scrolling) complete - scroll triggers working
- Phase 8 (Bullet System) complete - enemy bullet spawning available

## Detailed Tasks

### 1. Create Enemy Object Pool
Similar to bullets, a fixed-size pool of enemy structs.

### 2. Define Enemy Types
6 enemy types with unique stats, sprites, and behaviors per zone.

### 3. Implement AI Movement Patterns
- **Linear**: Move straight down
- **Sine Wave**: Move down with horizontal oscillation
- **Dive**: Move toward player position
- **Orbit**: Circle around a point
- **Strafe**: Move horizontally, pausing to fire

### 4. Implement Enemy Spawning System
Spawn enemies at configured scroll distances using scroll triggers from Phase 7.

### 5. Connect Enemy Firing to Bullet System

### 6. Create Wave/Formation System
Enemies spawn in predefined formations (V-shape, line, pincer).

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/enemy.h` | CREATE | Enemy system header |
| `src/enemy.c` | CREATE | Enemy system implementation |
| `src/scroll.c` | MODIFY | Wire scroll triggers to enemy spawner |
| `src/main.c` | MODIFY | Call enemy update/render in main loop |
| `Makefile` | MODIFY | Add enemy.obj |
| `data/linkfile` | MODIFY | Add enemy.obj |

## Technical Specifications

### Enemy Type Definitions
```c
/* Enemy type IDs */
#define ENEMY_NONE        0
#define ENEMY_SCOUT       1   /* Zone 1: basic, linear movement */
#define ENEMY_RAIDER      2   /* Zone 1: sine-wave, moderate HP */
#define ENEMY_INTERCEPTOR 3   /* Zone 2: fast dive, low HP */
#define ENEMY_DESTROYER   4   /* Zone 2: slow strafe, high HP, heavy fire */
#define ENEMY_ELITE       5   /* Zone 3: aggressive dive + fire */
#define ENEMY_COMMANDER   6   /* Zone 3: orbit pattern, rapid fire */
#define ENEMY_ASTEROID    7   /* All zones: obstacle, no AI, just drifts */

/* AI pattern IDs */
#define AI_LINEAR      0   /* Straight down */
#define AI_SINE        1   /* Sine wave horizontal */
#define AI_DIVE        2   /* Aim toward player */
#define AI_ORBIT       3   /* Circle pattern */
#define AI_STRAFE      4   /* Horizontal movement + pause to fire */
#define AI_DRIFT       5   /* Slow random drift (asteroids) */

/* Enemy data table (ROM - constant) */
typedef struct {
    u8  type;
    u8  ai_pattern;
    u16 hp;
    u8  attack;
    u8  defense;
    u8  speed;           /* Movement speed */
    u8  fire_rate;       /* Frames between shots (0 = doesn't fire) */
    u8  tile_offset;     /* VRAM tile index */
    u8  palette;         /* Sprite palette */
    u16 xp_reward;       /* XP given on defeat */
    u8  drop_chance;     /* Item drop chance (0-255, where 255=100%) */
    u8  sprite_size;     /* OBJ_SMALL or OBJ_LARGE */
} EnemyTemplate;

/* Runtime enemy instance */
typedef struct {
    s16 x, y;            /* Position */
    s16 vx, vy;          /* Velocity (8.8 fixed) */
    u8  type;            /* Enemy type (index into template table) */
    u8  active;          /* Active flag */
    u16 hp;              /* Current HP */
    u8  ai_pattern;      /* Current AI behavior */
    u8  ai_timer;        /* AI state timer */
    u8  ai_phase;        /* AI sub-state */
    u8  fire_timer;      /* Countdown to next shot */
    u8  oam_id;          /* OAM sprite slot */
    u8  flash_timer;     /* Damage flash counter */
    s16 ai_param1;       /* AI-specific parameter (center X for orbit, etc.) */
    s16 ai_param2;       /* AI-specific parameter */
} Enemy;
```

### enemy.h
```c
#ifndef ENEMY_H
#define ENEMY_H

#include <snes.h>
#include "config.h"

/* ... type definitions from above ... */

#define MAX_ENEMIES 8

typedef struct {
    Enemy pool[MAX_ENEMIES];
    u8 active_count;
    u8 total_killed;     /* For scoring */
} EnemySystem;

extern EnemySystem g_enemies;

/* Template table (in ROM) */
extern const EnemyTemplate enemy_templates[8];

/* External ASM labels */
extern char spr_scout_tiles, spr_scout_pal;
extern char spr_raider_tiles, spr_raider_pal;
extern char spr_interceptor_tiles, spr_interceptor_pal;
extern char spr_destroyer_tiles, spr_destroyer_pal;
extern char spr_elite_tiles, spr_elite_pal;
extern char spr_commander_tiles, spr_commander_pal;

/*--- Functions ---*/
void enemies_init(void);
void enemies_load_sprites(u8 zone);
void enemies_update(void);
void enemies_render(void);
void enemies_clear_all(void);

void enemy_spawn(u8 type, s16 x, s16 y);
void enemy_spawn_wave(u8 type, u8 formation, u8 count);
void enemy_damage(Enemy *e, u16 damage);
void enemy_kill(Enemy *e);

#endif /* ENEMY_H */
```

### enemy.c (Core Implementation)
```c
#include "enemy.h"
#include "bullet.h"
#include "player.h"

EnemySystem g_enemies;

/* Enemy template table - stats for each type */
const EnemyTemplate enemy_templates[8] = {
    /* type,             ai,        hp, atk, def, spd, fire, tile, pal,          xp,  drop, size */
    {ENEMY_NONE,        AI_LINEAR,   0,  0,   0,  0,   0,    0,   0,             0,   0,   OBJ_SMALL},
    {ENEMY_SCOUT,       AI_LINEAR,  15,  5,   2,  2,  60,   16,  PAL_SPR_ENEMY_A, 10, 25,  OBJ_LARGE},
    {ENEMY_RAIDER,      AI_SINE,    25,  8,   3,  1,  45,   20,  PAL_SPR_ENEMY_A, 20, 40,  OBJ_LARGE},
    {ENEMY_INTERCEPTOR, AI_DIVE,    20, 10,   2,  4,  30,   24,  PAL_SPR_ENEMY_B, 30, 30,  OBJ_LARGE},
    {ENEMY_DESTROYER,   AI_STRAFE,  50, 12,   8,  1,  20,   28,  PAL_SPR_ENEMY_B, 50, 60,  OBJ_LARGE},
    {ENEMY_ELITE,       AI_DIVE,    40, 15,   6,  3,  25,   32,  PAL_SPR_ENEMY_A, 60, 50,  OBJ_LARGE},
    {ENEMY_COMMANDER,   AI_ORBIT,   35, 12,   5,  2,  15,   36,  PAL_SPR_ENEMY_B, 75, 70,  OBJ_LARGE},
    {ENEMY_ASTEROID,    AI_DRIFT,   30,  0,  10,  1,   0,   40,  PAL_SPR_ENEMY_A, 5,  15,  OBJ_LARGE},
};

/* OAM slots for enemies: start after bullets */
#define OAM_ENEMY_START 21

void enemies_init(void) {
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        g_enemies.pool[i].active = 0;
        g_enemies.pool[i].oam_id = OAM_ENEMY_START + i;
    }
    g_enemies.active_count = 0;
    g_enemies.total_killed = 0;
}

void enemies_load_sprites(u8 zone) {
    /* Load zone-specific enemy sprites into VRAM */
    /* Only load 2-3 enemy types per zone to save VRAM */
    u16 vram_addr = VRAM_SPR_TILES + 0x400; /* After player + bullet tiles */

    switch(zone) {
        case ZONE_DEBRIS:
            dmaCopyVram(&spr_scout_tiles, vram_addr, 512);
            dmaCopyVram(&spr_raider_tiles, vram_addr + 256, 512);
            dmaCopyCGram(&spr_scout_pal, 128 + (PAL_SPR_ENEMY_A * 16 * 2), 32);
            dmaCopyCGram(&spr_raider_pal, 128 + (PAL_SPR_ENEMY_B * 16 * 2), 32);
            break;
        case ZONE_ASTEROID:
            dmaCopyVram(&spr_interceptor_tiles, vram_addr, 512);
            dmaCopyVram(&spr_destroyer_tiles, vram_addr + 256, 512);
            dmaCopyCGram(&spr_interceptor_pal, 128 + (PAL_SPR_ENEMY_A * 16 * 2), 32);
            dmaCopyCGram(&spr_destroyer_pal, 128 + (PAL_SPR_ENEMY_B * 16 * 2), 32);
            break;
        case ZONE_FLAGSHIP:
            dmaCopyVram(&spr_elite_tiles, vram_addr, 512);
            dmaCopyVram(&spr_commander_tiles, vram_addr + 256, 512);
            dmaCopyCGram(&spr_elite_pal, 128 + (PAL_SPR_ENEMY_A * 16 * 2), 32);
            dmaCopyCGram(&spr_commander_pal, 128 + (PAL_SPR_ENEMY_B * 16 * 2), 32);
            break;
    }
}

void enemy_spawn(u8 type, s16 x, s16 y) {
    u8 i;
    Enemy *e = 0;

    /* Find free slot */
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!g_enemies.pool[i].active) {
            e = &g_enemies.pool[i];
            break;
        }
    }
    if (!e) return; /* No free slots */

    const EnemyTemplate *t = &enemy_templates[type];

    e->x = x;
    e->y = y;
    e->vx = 0;
    e->vy = (s16)(t->speed) << 8; /* Convert to 8.8 fixed, default downward */
    e->type = type;
    e->active = 1;
    e->hp = t->hp;
    e->ai_pattern = t->ai_pattern;
    e->ai_timer = 0;
    e->ai_phase = 0;
    e->fire_timer = t->fire_rate;
    e->flash_timer = 0;
    e->ai_param1 = x;  /* Store initial X for orbit/sine center */
    e->ai_param2 = 0;

    g_enemies.active_count++;
}

/* Spawn a formation of enemies */
void enemy_spawn_wave(u8 type, u8 formation, u8 count) {
    u8 i;
    s16 start_x, spacing;

    switch(formation) {
        case 0: /* Horizontal line */
            spacing = SCREEN_WIDTH / (count + 1);
            for (i = 0; i < count; i++) {
                enemy_spawn(type, spacing * (i + 1) - 16, -32);
            }
            break;

        case 1: /* V-formation */
            start_x = SCREEN_WIDTH / 2 - 16;
            for (i = 0; i < count; i++) {
                s16 offset = (i & 1) ? ((i + 1) / 2 * 40) : (-((i + 1) / 2) * 40);
                s16 y_off = ((i + 1) / 2) * -24;
                enemy_spawn(type, start_x + offset, -32 + y_off);
            }
            break;

        case 2: /* Single column (top) */
            for (i = 0; i < count; i++) {
                enemy_spawn(type, SCREEN_WIDTH / 2 - 16, -32 - (i * 48));
            }
            break;
    }
}

/* AI Update functions */
static void ai_linear(Enemy *e) {
    /* Just move straight down */
    /* vy already set from template */
}

static void ai_sine(Enemy *e) {
    /* Horizontal sine wave while moving down */
    e->ai_timer++;
    /* Use simple approximation: toggle direction every 30 frames */
    if (e->ai_timer >= 30) {
        e->ai_timer = 0;
        e->vx = -e->vx;
        if (e->vx == 0) e->vx = 0x0100; /* Start moving right */
    }
}

static void ai_dive(Enemy *e) {
    /* Phase 0: move down to mid-screen, Phase 1: dive at player */
    if (e->ai_phase == 0) {
        if (e->y > 60) {
            e->ai_phase = 1;
            /* Aim toward player */
            s16 dx = g_player.x - e->x;
            s16 dy = g_player.y - e->y;
            s16 abs_dx = (dx < 0) ? -dx : dx;
            s16 abs_dy = (dy < 0) ? -dy : dy;
            s16 max_d = (abs_dx > abs_dy) ? abs_dx : abs_dy;
            if (max_d > 0) {
                e->vx = (dx * 0x0300) / max_d;
                e->vy = (dy * 0x0300) / max_d;
            }
        }
    }
}

static void ai_strafe(Enemy *e) {
    /* Move to Y=40, then strafe horizontally, stopping to fire */
    if (e->ai_phase == 0) {
        /* Move to position */
        if (e->y >= 40) {
            e->ai_phase = 1;
            e->vy = 0;
            e->vx = 0x0180; /* Strafe right */
        }
    } else {
        /* Bounce off screen edges */
        if (e->x > SCREEN_WIDTH - 40) e->vx = -0x0180;
        if (e->x < 8) e->vx = 0x0180;
    }
}

static void ai_drift(Enemy *e) {
    /* Slow downward drift with slight random wobble */
    e->ai_timer++;
    if (e->ai_timer >= 60) {
        e->ai_timer = 0;
        e->vx = ((rand() & 0xFF) - 128); /* Random horizontal drift */
    }
}

void enemies_update(void) {
    u8 i;
    g_enemies.active_count = 0;

    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g_enemies.pool[i];
        if (!e->active) continue;

        /* Run AI pattern */
        switch(e->ai_pattern) {
            case AI_LINEAR: ai_linear(e); break;
            case AI_SINE:   ai_sine(e);   break;
            case AI_DIVE:   ai_dive(e);   break;
            case AI_STRAFE: ai_strafe(e); break;
            case AI_DRIFT:  ai_drift(e);  break;
        }

        /* Update position */
        e->x += (e->vx >> 8);
        e->y += (e->vy >> 8);

        /* Fire at player */
        if (enemy_templates[e->type].fire_rate > 0) {
            e->fire_timer--;
            if (e->fire_timer == 0) {
                e->fire_timer = enemy_templates[e->type].fire_rate;
                bullet_spawn_enemy_basic(e->x + 12, e->y + 32,
                                        g_player.x + 16, g_player.y + 16);
            }
        }

        /* Damage flash countdown */
        if (e->flash_timer > 0) e->flash_timer--;

        /* Offscreen removal */
        if (e->y > SCREEN_HEIGHT + 48 || e->y < -64 ||
            e->x > SCREEN_WIDTH + 48 || e->x < -48) {
            e->active = 0;
            continue;
        }

        g_enemies.active_count++;
    }
}

void enemies_render(void) {
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g_enemies.pool[i];
        u16 oam_offset = e->oam_id * 4;

        if (!e->active) {
            oamSetEx(oam_offset, OBJ_LARGE, OBJ_HIDE);
            continue;
        }

        /* Skip rendering during flash (damage indicator) */
        if (e->flash_timer & 0x02) {
            oamSetEx(oam_offset, OBJ_LARGE, OBJ_HIDE);
            continue;
        }

        u8 pal = enemy_templates[e->type].palette;
        u8 tile = enemy_templates[e->type].tile_offset;

        oamSet(oam_offset, e->x, e->y, 2, 0, 0, tile, pal);
        oamSetEx(oam_offset, OBJ_LARGE, OBJ_SHOW);
    }
}

void enemy_damage(Enemy *e, u16 damage) {
    u16 actual = damage;
    u8 def = enemy_templates[e->type].defense;

    /* Damage reduction from defense */
    if (actual > def)
        actual -= def;
    else
        actual = 1; /* Minimum 1 damage */

    if (e->hp > actual) {
        e->hp -= actual;
        e->flash_timer = 8; /* Flash for 8 frames */
    } else {
        enemy_kill(e);
    }
}

void enemy_kill(Enemy *e) {
    /* Award XP and score */
    g_player.xp += enemy_templates[e->type].xp_reward;
    g_game.score += enemy_templates[e->type].xp_reward;
    g_enemies.total_killed++;

    /* TODO: Spawn explosion effect (Phase 19) */
    /* TODO: Random item drop (Phase 14) */

    e->active = 0;
    g_enemies.active_count--;
}

void enemies_clear_all(void) {
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        g_enemies.pool[i].active = 0;
    }
    g_enemies.active_count = 0;
}
```

## Acceptance Criteria
1. Enemies spawn at top of screen and move according to their AI pattern
2. All 5 AI patterns work: linear, sine-wave, dive, strafe, drift
3. Enemies fire bullets at the player based on their fire_rate
4. `enemy_damage()` reduces HP with defense calculation, flashes sprite
5. Enemies killed by reducing HP to 0, awarding XP and score
6. Maximum 8 enemies active simultaneously without crashes
7. Wave spawning creates correct formations (line, V, column)
8. Enemies that leave screen are deactivated and their OAM slot freed
9. Zone-specific sprite loading works (different enemies per zone)

## SNES-Specific Constraints
- 128 OAM sprites total - enemy slots (21-28) leave room for other objects
- Division operations in aiming code are expensive - consider lookup tables
- const data (enemy_templates) is stored in ROM, not WRAM
- rand() available from PVSnesLib but limited quality
- Enemy sprite loading per zone swaps VRAM tiles - must be done during VBlank/blank

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~40KB | 256KB    | ~216KB    |
| WRAM     | ~700B | 128KB   | ~127KB    |
| VRAM     | ~10KB | 64KB    | ~54KB     |
| CGRAM    | 128B  | 512B    | 384B      |

## Estimated Complexity
**Complex** - The AI system with multiple patterns, the wave spawning, and the integration with bullet/scroll systems make this one of the more involved phases.

## Agent Instructions
1. Create `src/enemy.h` and `src/enemy.c`
2. Update Makefile and linkfile
3. Add `enemies_init()` and `enemies_load_sprites(ZONE_DEBRIS)` to game_init()
4. Add `enemies_update()` and `enemies_render()` to main loop
5. For testing, manually call `enemy_spawn_wave(ENEMY_SCOUT, 0, 3)` to spawn a test wave
6. Test each AI pattern individually by spawning one enemy of each type
7. Verify enemies fire bullets that travel toward the player
8. Verify killed enemies award XP (check g_player.xp in debugger)
9. Stress test: spawn 8 enemies + maximum bullets and verify no OAM corruption
