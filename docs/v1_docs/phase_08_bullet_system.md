# Phase 8: Bullet/Projectile System

## Objective
Implement a flexible bullet/projectile system that handles player shots, enemy shots, and special attacks. Bullets are sprite-based, use an object pool for memory efficiency, and support different movement patterns (straight, homing, spread).

## Prerequisites
- Phase 5 (Sprite Engine) complete
- Phase 6 (Input System) complete
- Phase 3 (Asset Pipeline) complete - bullet sprites converted

## Detailed Tasks

### 1. Create Bullet Object Pool
Fixed-size array of bullet structs reused via active/inactive flagging.

### 2. Implement Bullet Types
- Player basic shot (straight up, fast)
- Player special attack (spread pattern, uses MP)
- Enemy basic shot (straight down, moderate speed)
- Boss attack (complex pattern)

### 3. Implement Bullet Spawning
Functions to spawn bullets at specific positions with configured properties.

### 4. Implement Bullet Movement Patterns
Each bullet type has a movement function: linear, sine-wave, homing, spread.

### 5. Connect to Player Input
B button fires player basic shot with rate limiting (cooldown).

### 6. Load Bullet Sprites into VRAM

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/bullet.h` | CREATE | Bullet system header |
| `src/bullet.c` | CREATE | Bullet system implementation |
| `src/player.c` | MODIFY | Add shooting logic |
| `src/main.c` | MODIFY | Call bullet update in main loop |
| `Makefile` | MODIFY | Add bullet.obj |
| `data/linkfile` | MODIFY | Add bullet.obj |

## Technical Specifications

### Bullet Data Structure
```c
/* Bullet types */
#define BULLET_NONE          0
#define BULLET_PLAYER_BASIC  1
#define BULLET_PLAYER_SPREAD 2
#define BULLET_PLAYER_HOMING 3
#define BULLET_ENEMY_BASIC   4
#define BULLET_ENEMY_AIMED   5
#define BULLET_BOSS_RING     6

/* Bullet ownership */
#define BULLET_OWNER_PLAYER  0
#define BULLET_OWNER_ENEMY   1

typedef struct {
    s16 x;               /* X position (signed for offscreen checks) */
    s16 y;               /* Y position */
    s16 vx;              /* X velocity (8.8 fixed-point) */
    s16 vy;              /* Y velocity (8.8 fixed-point) */
    u8  type;            /* BULLET_* type */
    u8  owner;           /* BULLET_OWNER_* */
    u8  active;          /* 1 = active, 0 = available for reuse */
    u8  damage;          /* Damage value for collision */
    u8  oam_id;          /* OAM slot for this bullet */
    u8  tile_offset;     /* Tile number in VRAM for this bullet type */
    u8  palette;         /* Palette number */
    u8  lifetime;        /* Frames remaining (0 = infinite until offscreen) */
} Bullet;

#define MAX_PLAYER_BULLETS  8
#define MAX_ENEMY_BULLETS   12
#define MAX_BULLETS         (MAX_PLAYER_BULLETS + MAX_ENEMY_BULLETS)

/* Firing rate cooldowns (in frames) */
#define FIRE_COOLDOWN_BASIC   8    /* ~7.5 shots/sec */
#define FIRE_COOLDOWN_SPREAD  20   /* ~3 shots/sec */
```

### bullet.h
```c
#ifndef BULLET_H
#define BULLET_H

#include <snes.h>
#include "config.h"

/* ... (types and structs from above) ... */

typedef struct {
    Bullet pool[MAX_BULLETS];
    u8 player_cooldown;        /* Frames until player can fire again */
    u8 active_count;           /* Number of active bullets */
} BulletSystem;

extern BulletSystem g_bullets;

/* External ASM labels for bullet sprites */
extern char spr_bullet_player, spr_bullet_special;
extern char spr_bullet_enemy, spr_bullet_boss;
extern char spr_bullet_pal;

/*--- Functions ---*/
void bullets_init(void);
void bullets_load_sprites(void);
void bullets_update(void);
void bullets_render(void);
void bullets_clear_all(void);

/* Spawning */
void bullet_spawn_player_basic(s16 x, s16 y);
void bullet_spawn_player_spread(s16 x, s16 y);
void bullet_spawn_enemy_basic(s16 x, s16 y, s16 target_x, s16 target_y);
void bullet_spawn_boss_ring(s16 x, s16 y, u8 count);

/* Player firing (called from player update) */
void player_fire(void);

#endif /* BULLET_H */
```

### bullet.c (Core Implementation)
```c
#include "bullet.h"
#include "player.h"
#include "input.h"

BulletSystem g_bullets;

/* OAM slots for bullets: slots 1-20 (after player at slot 0) */
#define OAM_BULLET_START  1

void bullets_init(void) {
    u8 i;
    for (i = 0; i < MAX_BULLETS; i++) {
        g_bullets.pool[i].active = 0;
        g_bullets.pool[i].oam_id = (OAM_BULLET_START + i);
    }
    g_bullets.player_cooldown = 0;
    g_bullets.active_count = 0;
}

void bullets_load_sprites(void) {
    /* Upload bullet tile data to VRAM */
    /* Player bullet: 16x16 = 128 bytes at 4bpp */
    dmaCopyVram(&spr_bullet_player, VRAM_SPR_TILES + 256, 128);
    /* Special bullet */
    dmaCopyVram(&spr_bullet_special, VRAM_SPR_TILES + 320, 128);
    /* Enemy bullet */
    dmaCopyVram(&spr_bullet_enemy, VRAM_SPR_TILES + 384, 128);
    /* Boss bullet */
    dmaCopyVram(&spr_bullet_boss, VRAM_SPR_TILES + 448, 128);

    /* Upload bullet palette */
    dmaCopyCGram(&spr_bullet_pal, 128 + (PAL_SPR_BULLET * 16 * 2), 32);
}

/* Find an inactive bullet slot */
static Bullet* bullet_alloc(u8 owner) {
    u8 i;
    u8 start = (owner == BULLET_OWNER_PLAYER) ? 0 : MAX_PLAYER_BULLETS;
    u8 end = (owner == BULLET_OWNER_PLAYER) ? MAX_PLAYER_BULLETS : MAX_BULLETS;

    for (i = start; i < end; i++) {
        if (!g_bullets.pool[i].active) {
            return &g_bullets.pool[i];
        }
    }
    return 0; /* No free slots */
}

void bullet_spawn_player_basic(s16 x, s16 y) {
    Bullet *b = bullet_alloc(BULLET_OWNER_PLAYER);
    if (!b) return;

    b->x = x + 12;          /* Center on ship (32px wide, bullet is 16px) */
    b->y = y - 16;           /* Spawn above ship */
    b->vx = 0;
    b->vy = -0x0400;         /* -4.0 pixels/frame upward (8.8 fixed) */
    b->type = BULLET_PLAYER_BASIC;
    b->owner = BULLET_OWNER_PLAYER;
    b->active = 1;
    b->damage = g_player.attack;
    b->tile_offset = 8;      /* Tile index for player bullet */
    b->palette = PAL_SPR_BULLET;
    b->lifetime = 0;         /* Infinite - deactivate when offscreen */
}

void bullet_spawn_player_spread(s16 x, s16 y) {
    /* Spawn 3 bullets in a spread pattern */
    Bullet *b;
    s16 cx = x + 12;
    s16 cy = y - 16;

    /* Center bullet (straight up) */
    b = bullet_alloc(BULLET_OWNER_PLAYER);
    if (b) {
        b->x = cx; b->y = cy;
        b->vx = 0; b->vy = -0x0400;
        b->type = BULLET_PLAYER_SPREAD;
        b->owner = BULLET_OWNER_PLAYER;
        b->active = 1; b->damage = g_player.attack;
        b->tile_offset = 10; b->palette = PAL_SPR_BULLET;
        b->lifetime = 0;
    }

    /* Left bullet (angled left) */
    b = bullet_alloc(BULLET_OWNER_PLAYER);
    if (b) {
        b->x = cx - 8; b->y = cy;
        b->vx = -0x0100; b->vy = -0x0380;
        b->type = BULLET_PLAYER_SPREAD;
        b->owner = BULLET_OWNER_PLAYER;
        b->active = 1; b->damage = g_player.attack;
        b->tile_offset = 10; b->palette = PAL_SPR_BULLET;
        b->lifetime = 0;
    }

    /* Right bullet (angled right) */
    b = bullet_alloc(BULLET_OWNER_PLAYER);
    if (b) {
        b->x = cx + 8; b->y = cy;
        b->vx = 0x0100; b->vy = -0x0380;
        b->type = BULLET_PLAYER_SPREAD;
        b->owner = BULLET_OWNER_PLAYER;
        b->active = 1; b->damage = g_player.attack;
        b->tile_offset = 10; b->palette = PAL_SPR_BULLET;
        b->lifetime = 0;
    }
}

void bullet_spawn_enemy_basic(s16 x, s16 y, s16 target_x, s16 target_y) {
    Bullet *b = bullet_alloc(BULLET_OWNER_ENEMY);
    if (!b) return;

    b->x = x;
    b->y = y;

    /* Calculate direction toward target (simple approximation) */
    s16 dx = target_x - x;
    s16 dy = target_y - y;

    /* Normalize to ~3 pixels/frame speed (approximate) */
    /* Simple approach: divide by largest component */
    s16 abs_dx = (dx < 0) ? -dx : dx;
    s16 abs_dy = (dy < 0) ? -dy : dy;
    s16 max_d = (abs_dx > abs_dy) ? abs_dx : abs_dy;

    if (max_d > 0) {
        b->vx = (dx * 0x0300) / max_d;  /* 3.0 speed in 8.8 fixed */
        b->vy = (dy * 0x0300) / max_d;
    } else {
        b->vx = 0;
        b->vy = 0x0300; /* Default: straight down */
    }

    b->type = BULLET_ENEMY_BASIC;
    b->owner = BULLET_OWNER_ENEMY;
    b->active = 1;
    b->damage = 10;
    b->tile_offset = 12;
    b->palette = PAL_SPR_BULLET;
    b->lifetime = 120; /* 2 seconds max */
}

void bullets_update(void) {
    u8 i;
    g_bullets.active_count = 0;

    /* Decrement player fire cooldown */
    if (g_bullets.player_cooldown > 0)
        g_bullets.player_cooldown--;

    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &g_bullets.pool[i];
        if (!b->active) continue;

        /* Update position (8.8 fixed-point velocity) */
        b->x += (b->vx >> 8);
        b->y += (b->vy >> 8);

        /* Lifetime check */
        if (b->lifetime > 0) {
            b->lifetime--;
            if (b->lifetime == 0) {
                b->active = 0;
                continue;
            }
        }

        /* Offscreen check */
        if (b->y < -16 || b->y > SCREEN_HEIGHT + 16 ||
            b->x < -16 || b->x > SCREEN_WIDTH + 16) {
            b->active = 0;
            continue;
        }

        g_bullets.active_count++;
    }
}

void bullets_render(void) {
    u8 i;
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &g_bullets.pool[i];
        u16 oam_offset = b->oam_id * 4;

        if (!b->active) {
            /* Hide inactive bullet sprites */
            oamSetEx(oam_offset, OBJ_SMALL, OBJ_HIDE);
            continue;
        }

        /* Set bullet sprite (16x16 = "small" size in OBJ_SIZE8_L32? No...) */
        /* With OBJ_SIZE8_L32: small=8x8, large=32x32 */
        /* Bullets need 16x16... use OBJ_SIZE16_L32 or compose from 8x8? */
        /* Solution: use 8x8 tiles for bullets, or switch to OBJ_SIZE16_L32 */
        /* For simplicity, use 8x8 bullets (small sprites) */
        oamSet(oam_offset, b->x, b->y, 2, 0, 0,
               b->tile_offset, b->palette);
        oamSetEx(oam_offset, OBJ_SMALL, OBJ_SHOW);
    }
}

void bullets_clear_all(void) {
    u8 i;
    for (i = 0; i < MAX_BULLETS; i++) {
        g_bullets.pool[i].active = 0;
    }
    g_bullets.active_count = 0;
}

void player_fire(void) {
    if (g_bullets.player_cooldown > 0) return;

    if (input_is_held(KEY_B)) {
        /* Basic shot */
        bullet_spawn_player_basic(g_player.x, g_player.y);
        g_bullets.player_cooldown = FIRE_COOLDOWN_BASIC;
    }

    if (input_is_pressed(KEY_A) && g_player.mp >= 5) {
        /* Spread shot (uses MP) */
        bullet_spawn_player_spread(g_player.x, g_player.y);
        g_player.mp -= 5;
        g_bullets.player_cooldown = FIRE_COOLDOWN_SPREAD;
    }
}
```

## Acceptance Criteria
1. Pressing B fires a bullet that travels straight up from the player ship
2. Fire rate is limited (~7 shots/sec for basic, ~3 for spread)
3. Bullets disappear when leaving the screen
4. Spread shot fires 3 bullets in a fan pattern
5. Enemy bullets travel toward the player's position at spawn time
6. Object pool properly recycles bullet slots (no memory leaks)
7. No OAM corruption - inactive bullets are hidden, not garbled
8. All 20 bullet slots can be active simultaneously without flickering

## SNES-Specific Constraints
- With OBJ_SIZE8_L32, small sprites are 8x8 - bullets may need to be 8x8
- To support 16x16 bullets AND 32x32 ships, consider OBJ_SIZE16_L32 (small=16x16, large=32x32)
- 32 sprites per scanline limit - avoid spawning many bullets at the same Y coordinate
- Division is VERY slow on 65816 - use lookup tables or approximations for aiming
- Signed 16-bit math: be careful with overflow in velocity calculations

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~36KB | 256KB    | ~220KB    |
| WRAM     | ~500B | 128KB   | ~127KB    |
| VRAM     | ~9KB  | 64KB    | ~55KB     |
| CGRAM    | 96B   | 512B    | 416B      |

## Estimated Complexity
**Medium-Complex** - Object pooling, multiple bullet types, and aiming math require careful implementation. The OAM sprite size mode choice affects the entire sprite system.

## Agent Instructions
1. **Critical Decision**: Choose sprite size mode. Recommended: `OBJ_SIZE16_L32` (16x16 small + 32x32 large) so bullets are 16x16 "small" and ships are 32x32 "large"
2. Update the oamInitGfxSet call in game_init() if changing sprite size mode
3. Create `src/bullet.h` and `src/bullet.c`
4. Add `bullets_init()` and `bullets_load_sprites()` to game_init()
5. Add `player_fire()` call inside player_update_movement()
6. Add `bullets_update()` and `bullets_render()` to main loop
7. Test: press B to fire, verify bullets travel upward
8. Test: press A for spread shot, verify 3 bullets in fan pattern
9. Verify bullet count never exceeds MAX_BULLETS (check active_count)
