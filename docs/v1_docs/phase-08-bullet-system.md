# Phase 8: Bullet/Projectile System

## Objective
Implement a bullet pool system for both player and enemy projectiles. Player fires bullets upward with the Y button; enemies fire bullets downward. Support multiple bullet types (single shot, spread shot, laser) with different speeds, sizes, and patterns. All bullets use OAM sprites from the reserved bullet pool slots.

## Prerequisites
- Phase 5 (Sprite Engine), Phase 6 (Input), Phase 3 (bullet asset conversion).

## Detailed Tasks

1. Create `src/engine/bullets.c` - Bullet pool manager with pre-allocated array of bullet entities.
2. Implement bullet spawning with configurable velocity, size, type, and owner (player/enemy).
3. Implement per-frame bullet movement and automatic deactivation when off-screen.
4. Implement player shooting: press Y to fire. Auto-fire when held (fire every N frames).
5. Implement 3 player weapon types (cycled with L/R):
   - Single Shot: one 8x8 bullet, fast, straight up
   - Spread Shot: three 8x8 bullets (left-angled, straight, right-angled)
   - Laser: one 16x16 bullet, slower but more powerful visually
6. Load bullet tile data into OBJ VRAM at the bullet offset.
7. Render active bullets each frame using sprite pool.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/engine/bullets.h
```c
#ifndef BULLETS_H
#define BULLETS_H

#include "game.h"

/* Bullet pool size */
#define MAX_BULLETS 24

/* Bullet owner */
#define BULLET_OWNER_PLAYER  0
#define BULLET_OWNER_ENEMY   1

/* Bullet types */
#define BULLET_TYPE_SINGLE   0  /* 8x8, fast, straight */
#define BULLET_TYPE_SPREAD   1  /* 8x8, medium, angled */
#define BULLET_TYPE_LASER    2  /* 16x16, slow, powerful */
#define BULLET_TYPE_ENEMY_BASIC  3  /* 8x8, enemy standard */
#define BULLET_TYPE_ENEMY_AIMED  4  /* 8x8, aimed at player */

/* Bullet entity */
typedef struct {
    s16 x;          /* Screen X (pixels) */
    s16 y;          /* Screen Y (pixels) */
    s16 vx;         /* X velocity (8.8 fixed) */
    s16 vy;         /* Y velocity (8.8 fixed) */
    fixed8_8 fx;    /* Sub-pixel X (8.8 fixed) */
    fixed8_8 fy;    /* Sub-pixel Y (8.8 fixed) */
    u8 type;        /* BULLET_TYPE_* */
    u8 owner;       /* BULLET_OWNER_PLAYER or BULLET_OWNER_ENEMY */
    u8 active;      /* ENTITY_ACTIVE / ENTITY_INACTIVE */
    u8 damage;      /* Damage value for combat */
    u8 oam_slot;    /* OAM slot index (12-35 range) */
    u8 sprite_size; /* OBJ_SMALL (16x16) or use 8x8 via small size config */
    u16 tile_offset;/* Tile number in OBJ VRAM */
    u8 palette;     /* OBJ palette slot */
} Bullet;

/* Player weapon state */
typedef struct {
    u8 weapon_type;      /* Current weapon: 0=single, 1=spread, 2=laser */
    u8 fire_cooldown;    /* Frames until next shot allowed */
    u8 fire_rate;        /* Frames between shots */
    u8 auto_fire;        /* 1 if auto-fire enabled (hold Y) */
} WeaponState;

extern WeaponState player_weapon;

/* Initialize bullet system */
void bulletInit(void);

/* Load bullet graphics into OBJ VRAM */
void bulletLoadGraphics(void);

/* Fire a player bullet based on current weapon */
void bulletPlayerFire(s16 playerX, s16 playerY);

/* Fire an enemy bullet from position toward target */
void bulletEnemyFire(s16 ex, s16 ey, s16 targetX, s16 targetY, u8 type);

/* Fire an enemy bullet straight down */
void bulletEnemyFireDown(s16 ex, s16 ey);

/* Update all bullets (movement, off-screen check) */
void bulletUpdateAll(void);

/* Render all active bullets to OAM */
void bulletRenderAll(void);

/* Deactivate all bullets (for scene transitions) */
void bulletClearAll(void);

/* Cycle player weapon type */
void bulletNextWeapon(void);
void bulletPrevWeapon(void);

/* Get pointer to bullet array for collision checks */
Bullet* bulletGetPool(void);

#endif
```

### J:/code/snes/snes-rpg-test/src/engine/bullets.c
```c
/*==============================================================================
 * Bullet/Projectile System
 *============================================================================*/

#include "engine/bullets.h"
#include "engine/sprites.h"

/* Extern asset labels */
extern char bullet_basic_tiles, bullet_basic_tiles_end;
extern char bullet_basic_pal, bullet_basic_pal_end;
extern char bullet_enemy_tiles, bullet_enemy_tiles_end;
extern char bullet_enemy_pal, bullet_enemy_pal_end;

static Bullet bullet_pool[MAX_BULLETS];
WeaponState player_weapon;

/* Weapon fire rates (frames between shots) */
#define FIRE_RATE_SINGLE   8
#define FIRE_RATE_SPREAD   12
#define FIRE_RATE_LASER    16

/* Bullet speeds (8.8 fixed point, pixels/frame) */
#define SPEED_SINGLE_Y     0xFC00  /* -4.0 (upward) */
#define SPEED_SPREAD_Y     0xFD00  /* -3.0 (upward, slower) */
#define SPEED_SPREAD_VX    0x0100  /* +/- 1.0 (horizontal spread) */
#define SPEED_LASER_Y      0xFE00  /* -2.0 (slow but strong) */
#define SPEED_ENEMY_Y      0x0200  /* +2.0 (downward) */
#define SPEED_ENEMY_AIMED  0x0180  /* 1.5 (aimed) */

/* OAM slots for bullets: slots 12-35 (oam_id = 48-140, step 4) */
#define BULLET_OAM_START 12

void bulletInit(void)
{
    u8 i;
    for (i = 0; i < MAX_BULLETS; i++) {
        bullet_pool[i].active = ENTITY_INACTIVE;
        bullet_pool[i].oam_slot = BULLET_OAM_START + i;
    }

    player_weapon.weapon_type = 0;
    player_weapon.fire_cooldown = 0;
    player_weapon.fire_rate = FIRE_RATE_SINGLE;
    player_weapon.auto_fire = 1;
}

void bulletLoadGraphics(void)
{
    /* Load player bullet tiles to OBJ VRAM at bullet offset */
    spriteLoadTiles(&bullet_basic_tiles,
                    &bullet_basic_tiles_end - &bullet_basic_tiles,
                    OBJ_BULLET_OFFSET);

    /* Load enemy bullet tiles after player bullets */
    spriteLoadTiles(&bullet_enemy_tiles,
                    &bullet_enemy_tiles_end - &bullet_enemy_tiles,
                    OBJ_BULLET_OFFSET + 0x0020);

    /* Load bullet palette into OBJ palette slot 3 */
    spriteLoadPalette(&bullet_basic_pal,
                      &bullet_basic_pal_end - &bullet_basic_pal,
                      3);
}

static Bullet* allocBullet(void)
{
    u8 i;
    for (i = 0; i < MAX_BULLETS; i++) {
        if (bullet_pool[i].active == ENTITY_INACTIVE) {
            bullet_pool[i].active = ENTITY_ACTIVE;
            return &bullet_pool[i];
        }
    }
    return (Bullet *)0;
}

static void spawnBullet(s16 x, s16 y, s16 vx, s16 vy, u8 type, u8 owner, u8 damage)
{
    Bullet *b = allocBullet();
    if (!b) return;

    b->x = x;
    b->y = y;
    b->fx = TO_FIXED8(x);
    b->fy = TO_FIXED8(y);
    b->vx = vx;
    b->vy = vy;
    b->type = type;
    b->owner = owner;
    b->damage = damage;
    b->palette = 3;

    /* Set tile offset and size based on type */
    switch (type) {
        case BULLET_TYPE_LASER:
            b->tile_offset = (OBJ_BULLET_OFFSET >> 4);  /* Tile number */
            b->sprite_size = OBJ_SMALL;  /* 16x16 in our config */
            break;
        case BULLET_TYPE_ENEMY_BASIC:
        case BULLET_TYPE_ENEMY_AIMED:
            b->tile_offset = (OBJ_BULLET_OFFSET >> 4) + 2;
            b->sprite_size = OBJ_SMALL;
            break;
        default:
            b->tile_offset = (OBJ_BULLET_OFFSET >> 4);
            b->sprite_size = OBJ_SMALL;
            break;
    }
}

void bulletPlayerFire(s16 playerX, s16 playerY)
{
    s16 cx, cy;

    /* Check cooldown */
    if (player_weapon.fire_cooldown > 0) {
        player_weapon.fire_cooldown--;
        return;
    }

    /* Calculate bullet spawn position (center-top of player ship) */
    cx = playerX + 12;  /* Center of 32px ship, offset by half bullet width */
    cy = playerY - 4;   /* Just above the ship */

    switch (player_weapon.weapon_type) {
        case 0: /* Single shot */
            spawnBullet(cx, cy, 0, SPEED_SINGLE_Y, BULLET_TYPE_SINGLE,
                       BULLET_OWNER_PLAYER, 10);
            player_weapon.fire_cooldown = FIRE_RATE_SINGLE;
            break;

        case 1: /* Spread shot: 3 bullets */
            spawnBullet(cx, cy, 0, SPEED_SPREAD_Y, BULLET_TYPE_SPREAD,
                       BULLET_OWNER_PLAYER, 6);
            spawnBullet(cx - 4, cy, -SPEED_SPREAD_VX, SPEED_SPREAD_Y,
                       BULLET_TYPE_SPREAD, BULLET_OWNER_PLAYER, 6);
            spawnBullet(cx + 4, cy, SPEED_SPREAD_VX, SPEED_SPREAD_Y,
                       BULLET_TYPE_SPREAD, BULLET_OWNER_PLAYER, 6);
            player_weapon.fire_cooldown = FIRE_RATE_SPREAD;
            break;

        case 2: /* Laser */
            spawnBullet(cx, cy, 0, SPEED_LASER_Y, BULLET_TYPE_LASER,
                       BULLET_OWNER_PLAYER, 25);
            player_weapon.fire_cooldown = FIRE_RATE_LASER;
            break;
    }
}

void bulletEnemyFireDown(s16 ex, s16 ey)
{
    spawnBullet(ex, ey + 8, 0, SPEED_ENEMY_Y, BULLET_TYPE_ENEMY_BASIC,
               BULLET_OWNER_ENEMY, 15);
}

void bulletEnemyFire(s16 ex, s16 ey, s16 targetX, s16 targetY, u8 type)
{
    s16 dx, dy;
    s16 vx, vy;
    s16 dist;

    /* Simple aimed shot: normalize direction vector */
    dx = targetX - ex;
    dy = targetY - ey;

    /* Approximate normalization using max(abs(dx), abs(dy)) */
    dist = dx;
    if (dist < 0) dist = -dist;
    if (dy < 0) {
        if (-dy > dist) dist = -dy;
    } else {
        if (dy > dist) dist = dy;
    }

    if (dist == 0) dist = 1;

    /* Scale to aimed speed */
    vx = (dx * (s16)SPEED_ENEMY_AIMED) / dist;
    vy = (dy * (s16)SPEED_ENEMY_AIMED) / dist;

    spawnBullet(ex, ey, vx, vy, type, BULLET_OWNER_ENEMY, 15);
}

void bulletUpdateAll(void)
{
    u8 i;
    Bullet *b;

    for (i = 0; i < MAX_BULLETS; i++) {
        b = &bullet_pool[i];
        if (b->active != ENTITY_ACTIVE) continue;

        /* Move bullet using fixed-point */
        b->fx += b->vx;
        b->fy += b->vy;
        b->x = (s16)FROM_FIXED8(b->fx);
        b->y = (s16)FROM_FIXED8(b->fy);

        /* Deactivate if off-screen */
        if (b->y < -16 || b->y > 240 || b->x < -16 || b->x > 272) {
            b->active = ENTITY_INACTIVE;
        }
    }
}

void bulletRenderAll(void)
{
    u8 i;
    Bullet *b;
    u16 oam_id;

    for (i = 0; i < MAX_BULLETS; i++) {
        b = &bullet_pool[i];
        oam_id = b->oam_slot * 4;

        if (b->active != ENTITY_ACTIVE) {
            oamSetVisible(oam_id, OBJ_HIDE);
            continue;
        }

        oamSet(oam_id,
               (u16)b->x, (u16)b->y,
               2,   /* priority */
               0, 0, /* no flip */
               b->tile_offset,
               b->palette);
        oamSetEx(oam_id, b->sprite_size, OBJ_SHOW);
    }
}

void bulletClearAll(void)
{
    u8 i;
    for (i = 0; i < MAX_BULLETS; i++) {
        bullet_pool[i].active = ENTITY_INACTIVE;
    }
}

void bulletNextWeapon(void)
{
    player_weapon.weapon_type++;
    if (player_weapon.weapon_type > 2) player_weapon.weapon_type = 0;

    switch (player_weapon.weapon_type) {
        case 0: player_weapon.fire_rate = FIRE_RATE_SINGLE; break;
        case 1: player_weapon.fire_rate = FIRE_RATE_SPREAD; break;
        case 2: player_weapon.fire_rate = FIRE_RATE_LASER; break;
    }
    player_weapon.fire_cooldown = 0;
}

void bulletPrevWeapon(void)
{
    if (player_weapon.weapon_type == 0)
        player_weapon.weapon_type = 2;
    else
        player_weapon.weapon_type--;

    switch (player_weapon.weapon_type) {
        case 0: player_weapon.fire_rate = FIRE_RATE_SINGLE; break;
        case 1: player_weapon.fire_rate = FIRE_RATE_SPREAD; break;
        case 2: player_weapon.fire_rate = FIRE_RATE_LASER; break;
    }
    player_weapon.fire_cooldown = 0;
}

Bullet* bulletGetPool(void)
{
    return bullet_pool;
}
```

## Technical Specifications

### Bullet Velocity Values (8.8 Fixed Point, Signed)
```
Negative = upward, Positive = downward (SNES Y increases downward)

Player single:   vy = 0xFC00 = -1024 = -4.0 px/frame = 240 px/sec
Player spread:   vy = 0xFD00 = -768  = -3.0 px/frame
                 vx = +/-0x0100 = +/-1.0 px/frame
Player laser:    vy = 0xFE00 = -512  = -2.0 px/frame
Enemy basic:     vy = 0x0200 = +512  = +2.0 px/frame
Enemy aimed:     speed = 0x0180 = 1.5 px/frame (direction varies)

Screen traversal time (224 pixels):
  Single: 224/4 = 56 frames (0.93 sec)
  Laser:  224/2 = 112 frames (1.87 sec)
  Enemy:  224/2 = 112 frames (1.87 sec)
```

### OAM Slot Usage
```
Bullets use OAM slots 12-35 (24 slots).
OAM id for slot N = N * 4.
Slot 12 = OAM id 48, Slot 35 = OAM id 140.

Each bullet is either:
  - 8x8 (small size in OBJ_SIZE16_L32 config) = actually NOT possible.

IMPORTANT: With OBJ_SIZE16_L32, the small size is 16x16, not 8x8.
For 8x8 bullets, we would need OBJ_SIZE8_L32 instead.

RESOLUTION: Use OBJ_SIZE8_L32 configuration:
  Small = 8x8  (bullets, particles)
  Large = 32x32 (ships)

This means player ship at 32x32 uses OBJ_LARGE flag.
Bullets at 8x8 use OBJ_SMALL flag.
16x16 sprites (some enemies) would need to be composed of 4x 8x8 or
use OBJ_SIZE16_L32 which would make 8x8 bullets impossible.

ALTERNATIVE: Use OBJ_SIZE8_L16 for some OAM entries.
But SNES only supports ONE size configuration for all sprites.

FINAL DECISION: Use OBJ_SIZE16_L32.
  - Bullets that appear 8x8 will be stored in 16x16 tiles (padded with transparency).
  - This wastes some VRAM but simplifies management.
  - Small sprites = 16x16 (bullets, icons)
  - Large sprites = 32x32 (ships, bosses)
```

### Memory Budget
```
Bullet pool: 24 * ~20 bytes = 480 bytes WRAM
Bullet tiles in VRAM: ~512 bytes (a few 16x16 tiles)
Total: minimal impact on overall budget
```

## Acceptance Criteria
1. Pressing Y fires a bullet upward from the player ship.
2. Holding Y auto-fires at the weapon's fire rate.
3. Bullets scroll upward and disappear off-screen.
4. L/R cycles through 3 weapon types with visible difference (speed, count, size).
5. Spread shot fires 3 bullets in a fan pattern.
6. Maximum 24 bullets on screen at once (oldest are recycled if pool is full).
7. No OAM corruption or flickering when many bullets are active.
8. Bullet sprites appear at correct positions and sizes.

## SNES-Specific Constraints
- With 24 bullets + player + enemies, total OAM entries can approach 40+. Must stay under 128.
- 32 sprites per scanline limit: bullets traveling in a horizontal line could exceed this. Spread bullet patterns vertically.
- All sprites share the same size configuration (OBJ_SIZE16_L32). Cannot mix 8x8 and 32x32 without using the small/large flag.
- DMA transfers for bullet tile data happen during force blank (loading phase), not every frame.

## Estimated Complexity
**Medium** - Pool management and fixed-point movement are straightforward. Aimed shots require simple vector math. The OBJ size configuration constraint requires careful planning.
