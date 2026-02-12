/*==============================================================================
 * Bullet/Projectile System
 *
 * Pool of 24 bullets (16 player + 8 enemy) with dedicated OAM slots.
 * Player bullets use OAM slots OAM_BULLETS..OAM_BULLETS+15 (slots 4-19).
 * Enemy bullets use OAM slots OAM_EBULLETS..OAM_EBULLETS+7 (slots 40-47).
 *
 * Bullets manage their own OAM rendering separate from the sprite engine.
 * bulletRenderAll() must be called AFTER spriteRenderAll() to overwrite
 * the sprite engine's default hiding of these OAM slots.
 *
 * All velocities use 8.8 signed fixed-point. Negative Y = upward.
 *============================================================================*/

#include "engine/bullets.h"
#include "engine/sprites.h"
#include "engine/sound.h"
#include "config.h"
#include "assets.h"

/*=== VRAM Layout for Bullet Tiles ===*/
/* Word offsets from VRAM_OBJ_GFX base.
 * Player ship occupies offset 0x0000 (up to ~0x03FF for 32x32 4bpp).
 * Bullet tiles placed after player data. */
#define VRAM_OBJ_PBULLET_OFFSET  0x0400  /* Player bullet tiles */
#define VRAM_OBJ_EBULLET_OFFSET  0x0600  /* Enemy bullet tiles */

/* Tile numbers = VRAM word offset / 16 (each 8x8 4bpp tile = 16 words) */
#define TILE_PBULLET  (VRAM_OBJ_PBULLET_OFFSET >> 4)  /* 64 */
#define TILE_EBULLET  (VRAM_OBJ_EBULLET_OFFSET >> 4)  /* 96 */

/*=== OBJ Palette Indices for oamSet (0-7) ===*/
#define PAL_PBULLET  (PAL_OBJ_BULLET - 8)      /* 2 */
#define PAL_EBULLET  (PAL_OBJ_EBULLET - 8)     /* 3 */

/*=== Fire Rates (frames between shots) ===*/
#define FIRE_RATE_SINGLE   8   /* ~7.5 shots/sec */
#define FIRE_RATE_SPREAD   12  /* ~5 shots/sec */
#define FIRE_RATE_LASER    16  /* ~3.75 shots/sec */

/*=== Bullet Velocities (8.8 signed fixed-point) ===*/
/* Negative = upward, Positive = downward (SNES Y increases downward) */
#define SPEED_SINGLE_VY    ((s16)0xFC00)  /* -4.0 px/frame */
#define SPEED_SPREAD_VY    ((s16)0xFD00)  /* -3.0 px/frame */
#define SPEED_SPREAD_VX    ((s16)0x0100)  /* +1.0 px/frame sideways */
#define SPEED_LASER_VY     ((s16)0xFE00)  /* -2.0 px/frame */
#define SPEED_ENEMY_VY     ((s16)0x0200)  /* +2.0 px/frame downward */
#define SPEED_ENEMY_AIMED  ((s16)0x0180)  /* 1.5 px/frame aimed */

/*=== Damage Values ===*/
#define DMG_SINGLE     10
#define DMG_SPREAD      6
#define DMG_LASER      25
#define DMG_ENEMY      15

/*--- Module State ---*/
static Bullet bullet_pool[MAX_BULLETS];
WeaponState g_weapon;

void bulletInit(void)
{
    u8 i;

    for (i = 0; i < MAX_BULLETS; i++) {
        bullet_pool[i].active = ENTITY_INACTIVE;

        /* Assign OAM slots based on owner region */
        if (i < MAX_PLAYER_BULLETS) {
            bullet_pool[i].oam_id = (OAM_BULLETS + i) * 4;
        } else {
            bullet_pool[i].oam_id = (OAM_EBULLETS + (i - MAX_PLAYER_BULLETS)) * 4;
        }
    }

    g_weapon.weapon_type = WEAPON_SINGLE;
    g_weapon.fire_cooldown = 0;
}

void bulletLoadGraphics(void)
{
    /* Load player bullet tiles into OBJ VRAM */
    spriteLoadTiles((u8 *)&bullet_player_til,
                    ASSET_SIZE(bullet_player_til),
                    VRAM_OBJ_PBULLET_OFFSET);

    /* Load enemy bullet tiles into OBJ VRAM */
    spriteLoadTiles((u8 *)&bullet_enemy_til,
                    ASSET_SIZE(bullet_enemy_til),
                    VRAM_OBJ_EBULLET_OFFSET);

    /* Player bullet palette -> OBJ palette slot 2 */
    spriteLoadPalette((u8 *)&bullet_player_pal,
                      ASSET_SIZE(bullet_player_pal),
                      PAL_PBULLET);

    /* Enemy bullet palette -> OBJ palette slot 3 */
    spriteLoadPalette((u8 *)&bullet_enemy_pal,
                      ASSET_SIZE(bullet_enemy_pal),
                      PAL_EBULLET);
}

/*--- Internal: allocate a bullet from the correct pool region ---*/
static Bullet* bulletAlloc(u8 owner)
{
    u8 i, start, end;

    if (owner == BULLET_OWNER_PLAYER) {
        start = 0;
        end = MAX_PLAYER_BULLETS;
    } else {
        start = MAX_PLAYER_BULLETS;
        end = MAX_BULLETS;
    }

    for (i = start; i < end; i++) {
        if (bullet_pool[i].active == ENTITY_INACTIVE) {
            bullet_pool[i].active = ENTITY_ACTIVE;
            return &bullet_pool[i];
        }
    }
    return (Bullet *)0;
}

/*--- Internal: spawn a bullet with given parameters ---*/
static void spawnBullet(s16 x, s16 y, s16 vx, s16 vy,
                        u8 type, u8 owner, u8 damage)
{
    Bullet *b = bulletAlloc(owner);
    if (!b) return;

    b->x = x;
    b->y = y;
    b->vx = vx;
    b->vy = vy;
    b->type = type;
    b->owner = owner;
    b->damage = damage;

    /* Set tile and palette based on owner */
    if (owner == BULLET_OWNER_PLAYER) {
        b->tile_num = TILE_PBULLET;
        b->palette = PAL_PBULLET;
    } else {
        b->tile_num = TILE_EBULLET;
        b->palette = PAL_EBULLET;
    }
}

void bulletPlayerFire(s16 playerX, s16 playerY)
{
    s16 cx, cy;

    /* Check cooldown */
    if (g_weapon.fire_cooldown > 0) return;

    /* Spawn position: center-top of 32px player ship, offset for 16px bullet */
    cx = playerX + 8;
    cy = playerY - 4;

    soundPlaySFX(SFX_PLAYER_SHOOT);

    switch (g_weapon.weapon_type) {
        case WEAPON_SINGLE:
            spawnBullet(cx, cy, 0, SPEED_SINGLE_VY,
                       BULLET_TYPE_SINGLE, BULLET_OWNER_PLAYER, DMG_SINGLE);
            g_weapon.fire_cooldown = FIRE_RATE_SINGLE;
            break;

        case WEAPON_SPREAD:
            /* Center bullet (straight up) */
            spawnBullet(cx, cy, 0, SPEED_SPREAD_VY,
                       BULLET_TYPE_SPREAD, BULLET_OWNER_PLAYER, DMG_SPREAD);
            /* Left bullet (angled left-up) */
            spawnBullet(cx - 4, cy, -SPEED_SPREAD_VX, SPEED_SPREAD_VY,
                       BULLET_TYPE_SPREAD, BULLET_OWNER_PLAYER, DMG_SPREAD);
            /* Right bullet (angled right-up) */
            spawnBullet(cx + 4, cy, SPEED_SPREAD_VX, SPEED_SPREAD_VY,
                       BULLET_TYPE_SPREAD, BULLET_OWNER_PLAYER, DMG_SPREAD);
            g_weapon.fire_cooldown = FIRE_RATE_SPREAD;
            break;

        case WEAPON_LASER:
            spawnBullet(cx, cy, 0, SPEED_LASER_VY,
                       BULLET_TYPE_LASER, BULLET_OWNER_PLAYER, DMG_LASER);
            g_weapon.fire_cooldown = FIRE_RATE_LASER;
            break;
    }
}

void bulletEnemyFireDown(s16 ex, s16 ey)
{
    spawnBullet(ex, ey + 8, 0, SPEED_ENEMY_VY,
               BULLET_TYPE_ENEMY_BASIC, BULLET_OWNER_ENEMY, DMG_ENEMY);
}

void bulletEnemyFire(s16 ex, s16 ey, s16 targetX, s16 targetY, u8 type)
{
    s16 dx, dy, abs_dx, abs_dy, max_d, vx, vy;
    s16 half_speed;

    dx = targetX - ex;
    dy = targetY - ey;

    abs_dx = dx < 0 ? -dx : dx;
    abs_dy = dy < 0 ? -dy : dy;
    max_d = abs_dx > abs_dy ? abs_dx : abs_dy;

    /* Reduce components to prevent s16 overflow in multiplication.
     * Max screen distance ~256px; shift until max component < 128
     * so that component * half_speed (192) fits in s16. */
    while (abs_dx > 127 || abs_dy > 127) {
        dx >>= 1;
        dy >>= 1;
        abs_dx >>= 1;
        abs_dy >>= 1;
        max_d >>= 1;
    }

    if (max_d == 0) max_d = 1;

    /* Normalize direction and scale to aimed speed (split multiply to avoid overflow) */
    half_speed = SPEED_ENEMY_AIMED >> 1;
    vx = ((dx * half_speed) / max_d) << 1;
    vy = ((dy * half_speed) / max_d) << 1;

    spawnBullet(ex, ey, vx, vy, type, BULLET_OWNER_ENEMY, DMG_ENEMY);
}

void bulletUpdateAll(void)
{
    u8 i;
    Bullet *b;

    /* Tick down player fire cooldown */
    if (g_weapon.fire_cooldown > 0) {
        g_weapon.fire_cooldown--;
    }

    for (i = 0; i < MAX_BULLETS; i++) {
        b = &bullet_pool[i];
        if (b->active != ENTITY_ACTIVE) continue;

        /* Move bullet (integer part of 8.8 velocity) */
        b->x += (b->vx >> 8);
        b->y += (b->vy >> 8);

        /* Deactivate if off-screen (with 16px margin for sprite size) */
        if (b->y < -16 || b->y > 240 || b->x < -16 || b->x > 272) {
            b->active = ENTITY_INACTIVE;
        }
    }
}

void bulletRenderAll(void)
{
    u8 i;
    Bullet *b;

    for (i = 0; i < MAX_BULLETS; i++) {
        b = &bullet_pool[i];

        if (b->active != ENTITY_ACTIVE) {
            oamSetVisible(b->oam_id, OBJ_HIDE);
            continue;
        }

        oamSet(b->oam_id,
               (u16)b->x, (u16)b->y,
               2,           /* priority (above BG1/BG2) */
               0, 0,        /* no flip */
               b->tile_num,
               b->palette);
        oamSetEx(b->oam_id, OBJ_SMALL, OBJ_SHOW);
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
    g_weapon.weapon_type++;
    if (g_weapon.weapon_type >= WEAPON_COUNT) {
        g_weapon.weapon_type = 0;
    }
    g_weapon.fire_cooldown = 0;
}

void bulletPrevWeapon(void)
{
    if (g_weapon.weapon_type == 0) {
        g_weapon.weapon_type = WEAPON_COUNT - 1;
    } else {
        g_weapon.weapon_type--;
    }
    g_weapon.fire_cooldown = 0;
}

Bullet* bulletGetPool(void)
{
    return bullet_pool;
}
