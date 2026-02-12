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
 * This format provides sub-pixel precision: the high byte is the integer
 * pixel displacement per frame, and the low byte accumulates fractional
 * movement across frames. Only the integer part (>>8) is applied to position.
 *
 * SNES OBJ VRAM layout:
 *   The PPU treats OBJ tile memory as a 16-name-wide grid where each
 *   "name" is one 8x8 tile. Multi-tile sprites (16x16, 32x32) span
 *   multiple names with rows separated by 16 names (256 VRAM words).
 *   Bullet tiles are placed at specific column offsets within this grid
 *   to avoid overlapping with player ship tiles (cols 0-3).
 *============================================================================*/

#include "engine/bullets.h"
#include "engine/sprites.h"
#include "engine/sound.h"
#include "config.h"
#include "assets.h"

/*=== VRAM Layout for Bullet Tiles ===*/
/* SNES OBJ VRAM is a 16-name-wide grid. Names are spaced 16 words apart.
 * Player ship (32x32) uses cols 0-3, rows 0-3 (names 0-3,16-19,32-35,48-51).
 * Bullets (16x16) placed at cols 4-5 and 6-7 of rows 0-1.
 * Word offset = name * 16. */
#define VRAM_OBJ_PBULLET_OFFSET  0x0040  /* Name 4: col 4, row 0 -> 4*16=64 words */
#define VRAM_OBJ_EBULLET_OFFSET  0x0060  /* Name 6: col 6, row 0 -> 6*16=96 words */

/* Tile numbers = OBJ name number = VRAM word offset / 16.
 * The PPU indexes OBJ tiles by name number, not byte address.
 * Shift right by 4 converts word offset to name number. */
#define TILE_PBULLET  (VRAM_OBJ_PBULLET_OFFSET >> 4)  /* 4 */
#define TILE_EBULLET  (VRAM_OBJ_EBULLET_OFFSET >> 4)  /* 6 */

/*=== OBJ Palette Indices for oamSet (0-7) ===*/
/* oamSet expects palette index 0-7 (relative to OBJ palettes).
 * PAL_OBJ_BULLET is the absolute CGRAM palette number (8-15),
 * so subtract 8 to get the OBJ-relative index. */
#define PAL_PBULLET  (PAL_OBJ_BULLET - 8)      /* 10-8 = 2 */
#define PAL_EBULLET  (PAL_OBJ_EBULLET - 8)     /* 11-8 = 3 */

/*=== Fire Rates (frames between consecutive shots) ===*/
/* At 60fps: rate 8 = 7.5 shots/sec, rate 12 = 5 shots/sec, etc.
 * Lower values = faster firing. Tuned for gameplay feel. */
#define FIRE_RATE_SINGLE   8   /* ~7.5 shots/sec - fast pew-pew */
#define FIRE_RATE_SPREAD   12  /* ~5 shots/sec - moderate (3 bullets per shot) */
#define FIRE_RATE_LASER    13  /* ~4.6 shots/sec - buffed from 16 (#226) */

/*=== Bullet Velocities (8.8 signed fixed-point) ===*/
/* Negative Y = upward (SNES screen Y increases downward from top).
 * Positive Y = downward. X follows standard left-negative, right-positive. */
#define SPEED_SINGLE_VY    ((s16)0xFC00)  /* -4.0 px/frame (fast upward) */
#define SPEED_SPREAD_VY    ((s16)0xFD00)  /* -3.0 px/frame (medium upward) */
#define SPEED_SPREAD_VX    ((s16)0x0100)  /* +1.0 px/frame (sideways splay) */
#define SPEED_LASER_VY     ((s16)0xFE00)  /* -2.0 px/frame (slow, powerful) */
#define SPEED_ENEMY_VY     ((s16)0x0200)  /* +2.0 px/frame (downward toward player) */
#define SPEED_ENEMY_AIMED  ((s16)0x0180)  /* 1.5 px/frame total aimed speed */
#define HALF_SPEED_AIMED   (SPEED_ENEMY_AIMED >> 1)  /* 0.75 - compile-time constant for LUT math */

/*=== Damage Values ===*/
/* Tuned relative to enemy HP pools. Single is baseline, spread trades
 * damage per hit for coverage, laser is a heavy single-target option. */
#define DMG_SINGLE     10
#define DMG_SPREAD      6   /* Lower per-bullet, but 3 bullets per shot */
#define DMG_LASER      25   /* High damage compensates for slow fire rate */
#define DMG_ENEMY      15   /* All enemy bullets deal the same damage */

/*--- Module State ---*/
/* The bullet pool: indices 0..15 are player bullets, 16..23 are enemy bullets.
 * Each bullet has a pre-assigned OAM slot that never changes. */
static Bullet bullet_pool[MAX_BULLETS];

/* Global weapon state (current weapon type + fire cooldown).
 * Exposed globally so the HUD can display the current weapon. */
WeaponState g_weapon;

/* Count of currently active (moving) bullets. Updated by bulletUpdateAll().
 * Used by bulletRenderAll() to fast-path the zero-bullet case. */
u8 g_bullet_active_count = 0;

/* #150: Per-weapon-type kill counts for mastery damage bonus */
u16 g_weapon_kills[3] = { 0, 0, 0 };

/* #151: Consecutive frames the fire button is held */
u8 g_fire_hold_frames = 0;

/*
 * bulletInit - Initialize the bullet pool and weapon state.
 *
 * Sets all 24 bullets to inactive and assigns their permanent OAM slot IDs.
 * Player bullets (indices 0-15) map to OAM slots 4-19 (byte offsets 16-76).
 * Enemy bullets (indices 16-23) map to OAM slots 40-47 (byte offsets 160-188).
 *
 * The OAM byte offset formula is: (base_slot + local_index) * 4
 * where each OAM entry occupies 4 bytes in the shadow buffer.
 */
void bulletInit(void)
{
    u8 i;

    for (i = 0; i < MAX_BULLETS; i++) {
        bullet_pool[i].active = ENTITY_INACTIVE;

        /* Assign OAM slots based on owner region */
        if (i < MAX_PLAYER_BULLETS) {
            /* Player bullet: OAM slot = OAM_BULLETS + i, byte offset = slot * 4 */
            bullet_pool[i].oam_id = (OAM_BULLETS + i) * 4;
        } else {
            /* Enemy bullet: OAM slot = OAM_EBULLETS + (i - 16), byte offset = slot * 4 */
            bullet_pool[i].oam_id = (OAM_EBULLETS + (i - MAX_PLAYER_BULLETS)) * 4;
        }
    }

    g_weapon.weapon_type = WEAPON_SINGLE;
    g_weapon.fire_cooldown = 0;
    g_bullet_active_count = 0;
    g_fire_hold_frames = 0;
}

/*
 * bulletLoadGraphics - Load bullet tile and palette data into VRAM and CGRAM.
 *
 * Uses spriteLoadTiles16() which handles the SNES OBJ VRAM row-strided
 * layout for 16x16 sprites (2 rows of 2 tiles, rows 256 words apart).
 *
 * Must be called during force blank since VRAM/CGRAM writes require
 * the PPU to be in blanking mode.
 */
void bulletLoadGraphics(void)
{
    /* Load player bullet tiles (16x16 = 4 tiles, 128 bytes) */
    spriteLoadTiles16((u8 *)&bullet_player_til,
                      VRAM_OBJ_PBULLET_OFFSET);

    /* Load enemy bullet tiles (16x16 = 4 tiles, 128 bytes) */
    spriteLoadTiles16((u8 *)&bullet_enemy_til,
                      VRAM_OBJ_EBULLET_OFFSET);

    /* Player bullet palette -> OBJ palette slot 2 (CGRAM 160-175) */
    spriteLoadPalette((u8 *)&bullet_player_pal,
                      ASSET_SIZE(bullet_player_pal),
                      PAL_PBULLET);

    /* Enemy bullet palette -> OBJ palette slot 3 (CGRAM 176-191) */
    spriteLoadPalette((u8 *)&bullet_enemy_pal,
                      ASSET_SIZE(bullet_enemy_pal),
                      PAL_EBULLET);
}

/*
 * bulletAlloc - Find and activate a free bullet slot in the correct pool region.
 *
 * Player bullets search indices 0-15, enemy bullets search indices 16-23.
 * This partitioning prevents enemy fire from consuming player bullet slots
 * and vice versa, guaranteeing each side always has its full allocation.
 *
 * Parameters:
 *   owner - BULLET_OWNER_PLAYER or BULLET_OWNER_ENEMY
 *
 * Returns:
 *   Pointer to the activated bullet, or NULL (0) if the region is full.
 *   The returned bullet has active = ENTITY_ACTIVE but other fields are
 *   uninitialized (caller must set them).
 */
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

    /* Linear scan for first inactive slot in the owner's region */
    for (i = start; i < end; i++) {
        if (bullet_pool[i].active == ENTITY_INACTIVE) {
            bullet_pool[i].active = ENTITY_ACTIVE;
            return &bullet_pool[i];
        }
    }
    return (Bullet *)0;  /* Pool region exhausted */
}

/*
 * spawnBullet - Initialize a bullet with the given parameters.
 *
 * Allocates from the correct pool region based on owner, then fills
 * in all fields. Sets tile and palette based on whether it's a player
 * or enemy bullet.
 *
 * Parameters:
 *   x, y     - Initial screen position (pixels)
 *   vx, vy   - Velocity in 8.8 signed fixed-point
 *   type     - BULLET_TYPE_* constant
 *   owner    - BULLET_OWNER_PLAYER or BULLET_OWNER_ENEMY
 *   damage   - Damage value for collision system
 *
 * Silently returns if the pool region is full (bullet is simply not spawned).
 */
static void spawnBullet(s16 x, s16 y, s16 vx, s16 vy,
                        u8 type, u8 owner, u8 damage)
{
    Bullet *b = bulletAlloc(owner);
    if (!b) return;  /* Pool full - drop the bullet silently */

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

/*
 * bulletPlayerFire - Fire player projectile(s) based on current weapon type.
 *
 * Spawn position is computed as the center-top of the 32x32 player ship,
 * offset inward for the 16x16 bullet sprite:
 *   cx = playerX + 8  (center of 32px ship, minus 8px for 16px bullet width / 2)
 *   cy = playerY - 4  (just above the ship's top edge)
 *
 * Weapon behaviors:
 *   SINGLE: One bullet, straight up, fast velocity, moderate damage
 *   SPREAD: Three bullets in a fan pattern (left-angled, center, right-angled)
 *   LASER:  One bullet, straight up, slow velocity, high damage
 *
 * Parameters:
 *   playerX, playerY - Top-left pixel position of the 32x32 player sprite
 */
void bulletPlayerFire(s16 playerX, s16 playerY)
{
    s16 cx, cy;
    u8 dmg;
    u8 cooldown;

    /* Respect fire cooldown - can't fire until cooldown expires */
    if (g_weapon.fire_cooldown > 0) return;

    /* #151: Track fire hold frames for momentum bonus */
    if (g_fire_hold_frames < 255) g_fire_hold_frames++;

    /* Spawn position: center-top of 32px player ship, offset for 16px bullet */
    cx = playerX + 8;
    cy = playerY - 4;

    /* Play shoot SFX regardless of weapon type */
    soundPlaySFX(SFX_PLAYER_SHOOT);

    switch (g_weapon.weapon_type) {
        case WEAPON_SINGLE:
            /* #150: Add mastery bonus to damage */
            dmg = DMG_SINGLE + bulletGetMasteryBonus(WEAPON_SINGLE);
            spawnBullet(cx, cy, 0, SPEED_SINGLE_VY,
                       BULLET_TYPE_SINGLE, BULLET_OWNER_PLAYER, dmg);
            cooldown = FIRE_RATE_SINGLE;
            break;

        case WEAPON_SPREAD:
            dmg = DMG_SPREAD + bulletGetMasteryBonus(WEAPON_SPREAD);
            spawnBullet(cx, cy, 0, SPEED_SPREAD_VY,
                       BULLET_TYPE_SPREAD, BULLET_OWNER_PLAYER, dmg);
            spawnBullet(cx - 4, cy, -SPEED_SPREAD_VX, SPEED_SPREAD_VY,
                       BULLET_TYPE_SPREAD, BULLET_OWNER_PLAYER, dmg);
            spawnBullet(cx + 4, cy, SPEED_SPREAD_VX, SPEED_SPREAD_VY,
                       BULLET_TYPE_SPREAD, BULLET_OWNER_PLAYER, dmg);
            cooldown = FIRE_RATE_SPREAD;
            break;

        case WEAPON_LASER:
        default:
            dmg = DMG_LASER + bulletGetMasteryBonus(WEAPON_LASER);
            spawnBullet(cx, cy, 0, SPEED_LASER_VY,
                       BULLET_TYPE_LASER, BULLET_OWNER_PLAYER, dmg);
            cooldown = FIRE_RATE_LASER;
            break;
    }

    /* #151: Rapid fire momentum - reduce cooldown by 25% after 30 frames held */
    if (g_fire_hold_frames > 30) {
        cooldown -= (cooldown >> 2);
    }
    g_weapon.fire_cooldown = cooldown;
}

/*
 * bulletEnemyFireDown - Fire a simple downward enemy bullet.
 *
 * Spawns at (ex, ey+8) to offset from the enemy sprite's center,
 * moving straight down at SPEED_ENEMY_VY (2 px/frame).
 *
 * Parameters:
 *   ex, ey - Enemy sprite top-left position
 */
void bulletEnemyFireDown(s16 ex, s16 ey)
{
    spawnBullet(ex, ey + 8, 0, SPEED_ENEMY_VY,
               BULLET_TYPE_ENEMY_BASIC, BULLET_OWNER_ENEMY, DMG_ENEMY);
}

/*
 * Reciprocal lookup table for division-free aimed bullet normalization (#107).
 *
 * recip_lut[d] = floor(256 / d), saturated to 255. Index 0 is unused
 * (guarded by max_d == 0 check). This allows computing (value / d) as
 * (value * recip_lut[d]) >> 8, turning a slow software division loop
 * into a single multiply + shift.
 *
 * The 65816 has no hardware divide instruction. A software divide for
 * arbitrary values would take 30-50 cycles. The LUT costs 128 bytes of
 * ROM (negligible on a 2-4MB cartridge) but reduces the operation to
 * a table lookup + multiply (~8-12 cycles).
 *
 * Domain: indices 1-127. The while-loop in bulletEnemyFire ensures
 * abs_dx and abs_dy are <= 127 before the LUT is accessed.
 */
static const u8 recip_lut[128] = {
    255,255,128, 85, 64, 51, 42, 36,  /* 0-7   */
     32, 28, 25, 23, 21, 19, 18, 17,  /* 8-15  */
     16, 15, 14, 13, 12, 12, 11, 11,  /* 16-23 */
     10, 10,  9,  9,  9,  8,  8,  8,  /* 24-31 */
      8,  7,  7,  7,  7,  6,  6,  6,  /* 32-39 */
      6,  6,  6,  5,  5,  5,  5,  5,  /* 40-47 */
      5,  5,  5,  5,  4,  4,  4,  4,  /* 48-55 */
      4,  4,  4,  4,  4,  4,  4,  3,  /* 56-63 */
      4,  3,  3,  3,  3,  3,  3,  3,  /* 64-71 */
      3,  3,  3,  3,  3,  3,  3,  3,  /* 72-79 */
      3,  3,  3,  3,  3,  3,  2,  2,  /* 80-87 */
      2,  2,  2,  2,  2,  2,  2,  2,  /* 88-95 */
      2,  2,  2,  2,  2,  2,  2,  2,  /* 96-103 */
      2,  2,  2,  2,  2,  2,  2,  2,  /* 104-111 */
      2,  2,  2,  2,  2,  2,  2,  2,  /* 112-119 */
      2,  2,  2,  2,  2,  2,  2,  2   /* 120-127 */
};

/*
 * bulletEnemyFire - Fire an aimed enemy bullet toward a target position.
 *
 * Algorithm: Division-free vector normalization using reciprocal LUT.
 *
 *   1. Compute direction vector (dx, dy) from enemy to target.
 *   2. Find the dominant axis magnitude: max_d = max(|dx|, |dy|).
 *   3. Reduce components by right-shifting until both |dx| and |dy| < 128.
 *      This prevents s16 overflow in the subsequent multiplication step,
 *      since component * HALF_SPEED_AIMED (192) must fit in s16 range.
 *   4. Look up inv = recip_lut[max_d] which approximates 256/max_d.
 *   5. Compute velocity: vx = (dx * HALF_SPEED * inv) >> 8, then * 2.
 *      The >>8 and <<1 are separated to maintain precision.
 *   6. Uses s32 intermediate to avoid s16 overflow during the triple multiply.
 *
 * The result is an approximate unit vector scaled to SPEED_ENEMY_AIMED
 * magnitude, with ~2% error from LUT quantization (imperceptible in gameplay).
 *
 * Parameters:
 *   ex, ey         - Enemy position (bullet spawn point)
 *   targetX, targetY - Target position (typically player center)
 *   type           - BULLET_TYPE_ENEMY_AIMED or similar
 */
void bulletEnemyFire(s16 ex, s16 ey, s16 targetX, s16 targetY, u8 type)
{
    s16 dx, dy, abs_dx, abs_dy, max_d, vx, vy;
    u8 inv;  /* Reciprocal value from LUT */

    /* Step 1: Direction vector from enemy to target */
    dx = targetX - ex;
    dy = targetY - ey;

    /* Compute absolute values for magnitude comparison */
    abs_dx = dx < 0 ? -dx : dx;
    abs_dy = dy < 0 ? -dy : dy;

    /* Dominant axis magnitude (Chebyshev distance) */
    max_d = abs_dx > abs_dy ? abs_dx : abs_dy;

    /* Step 3: Reduce components to prevent s16 overflow.
     * Max screen distance is ~256px. We need both components < 128
     * so that component * HALF_SPEED_AIMED (192 max) fits in s16 (max 32767).
     * 127 * 192 = 24384, safely within s16 range.
     * Each right-shift halves all values, preserving direction ratios. */
    while (abs_dx > 127 || abs_dy > 127) {
        dx >>= 1;
        dy >>= 1;
        abs_dx >>= 1;
        abs_dy >>= 1;
        max_d >>= 1;
    }

    /* Guard against zero-length direction (target on top of enemy) */
    if (max_d == 0) max_d = 1;

    /* Step 4-5: Reciprocal LUT normalization.
     *
     * Mathematical derivation:
     *   We want: vx = dx * SPEED / max_d
     *   Rewrite: vx = dx * (SPEED/2) * (1/max_d) * 2
     *   Approx:  vx = dx * HALF_SPEED * (256/max_d) / 256 * 2
     *          = (dx * HALF_SPEED * inv) >> 8 << 1
     *
     * The s32 cast prevents overflow: dx (up to 127) * HALF_SPEED (192) * inv (up to 255)
     * = up to 6,193,920, which exceeds s16 but fits s32. */
    inv = recip_lut[max_d];
    vx = (s16)(((s32)dx * (s16)HALF_SPEED_AIMED * (s16)inv) >> 8) << 1;
    vy = (s16)(((s32)dy * (s16)HALF_SPEED_AIMED * (s16)inv) >> 8) << 1;

    spawnBullet(ex, ey, vx, vy, type, BULLET_OWNER_ENEMY, DMG_ENEMY);
}

/*
 * bulletUpdateAll - Per-frame update: move bullets, cull off-screen, tick cooldown.
 *
 * For each active bullet:
 *   - Applies the integer part of the 8.8 velocity to position (>>8 extracts
 *     the whole pixel displacement, discarding the fractional accumulator).
 *   - Tests if the bullet has left the visible area with margin for sprite size.
 *     The unsigned cast trick: (u16)(value + margin) > threshold catches both
 *     negative values (which wrap to large positive) and large positive values
 *     in a single comparison, eliminating the need for two separate checks.
 *
 * Uses pointer arithmetic (b = bullet_pool; b++) instead of array indexing
 * (bullet_pool[i]) to reduce 65816 address calculation overhead.
 */
void bulletUpdateAll(void)
{
    u8 i;
    u8 active_count;
    Bullet *b;

    /* Tick down player fire cooldown independently of bullet movement */
    if (g_weapon.fire_cooldown > 0) {
        g_weapon.fire_cooldown--;
    }

    active_count = 0;
    b = bullet_pool;
    for (i = 0; i < MAX_BULLETS; i++, b++) {
        if (b->active != ENTITY_ACTIVE) continue;

        /* Apply velocity: extract integer pixel part from 8.8 fixed-point.
         * The fractional part stays in vx/vy for sub-pixel accumulation,
         * but only whole pixels are added to screen position. */
        b->y += (b->vy >> 8);
        b->x += (b->vx >> 8);

        /* Off-screen culling with unsigned wrapping trick:
         * By adding a margin and casting to unsigned, negative values wrap
         * to very large numbers. If the result exceeds the screen + margin,
         * the bullet is off-screen in that axis.
         *
         * Y check: (u16)(y + 16) > 256 catches y < -16 (wraps to ~65520) and y > 240
         * X check: (u16)(x + 16) > 288 catches x < -16 and x > 272
         * The 16px margin accounts for the bullet sprite size. */
        if ((u16)(b->y + 16) > 256u || (u16)(b->x + 16) > 288u) {
            b->active = ENTITY_INACTIVE;
        } else {
            active_count++;
        }
    }

    g_bullet_active_count = active_count;
}

/*
 * bulletRenderAll - Write all bullet OAM data for this frame.
 *
 * Fast path (#112): When no bullets are active, hides all bullet OAM slots
 * using an additive-stride do-while loop instead of per-slot multiply.
 * On the 65816, adding 4 to an OAM offset each iteration (ADD #4) is
 * cheaper than computing (slot_base + i) * 4 each time.
 *
 * Normal path: Iterates all 24 pool entries, hiding inactive bullets and
 * rendering active ones with oamSet() + oamSetEx().
 *
 * oamSet parameters:
 *   - oam_id: byte offset into OAM shadow buffer
 *   - x, y: screen pixel position (cast to u16 for PVSnesLib API)
 *   - priority 2: renders above BG1 and BG2 (the background layers)
 *   - 0,0 flip: bullets don't flip (symmetric sprites)
 *   - tile_num: OBJ character name for the bullet graphic
 *   - palette: OBJ palette index (0-7)
 *
 * oamSetEx parameters:
 *   - OBJ_SMALL: use the small sprite size (16x16) from OBSEL register
 *   - OBJ_SHOW: make the sprite visible
 */
void bulletRenderAll(void)
{
    u8 i;
    Bullet *b;

    /* Fast path: if no bullets are active, hide all bullet OAM slots
     * using an optimized additive stride loop (#112). */
    if (g_bullet_active_count == 0) {
        u16 oam;
        /* Hide all player bullet OAM slots (16 slots starting at OAM_BULLETS) */
        oam = OAM_BULLETS * 4;
        i = MAX_PLAYER_BULLETS;
        do { oamSetVisible(oam, OBJ_HIDE); oam += 4; } while (--i);
        /* Hide all enemy bullet OAM slots (8 slots starting at OAM_EBULLETS) */
        oam = OAM_EBULLETS * 4;
        i = MAX_ENEMY_BULLETS;
        do { oamSetVisible(oam, OBJ_HIDE); oam += 4; } while (--i);
        return;
    }

    /* Normal rendering path: process each bullet in the pool */
    b = bullet_pool;
    for (i = 0; i < MAX_BULLETS; i++, b++) {
        if (b->active != ENTITY_ACTIVE) {
            /* Inactive bullet: hide its OAM slot so no stale sprite appears */
            oamSetVisible(b->oam_id, OBJ_HIDE);
            continue;
        }

        /* Active bullet: set OAM entry with position, tile, and attributes */
        oamSet(b->oam_id,
               (u16)b->x, (u16)b->y,
               2,           /* priority 2: above BG1/BG2 background layers */
               0, 0,        /* no horizontal or vertical flip */
               b->tile_num, /* OBJ character name (TILE_PBULLET or TILE_EBULLET) */
               b->palette); /* OBJ palette index (PAL_PBULLET or PAL_EBULLET) */
        oamSetEx(b->oam_id, OBJ_SMALL, OBJ_SHOW);  /* 16x16 size, visible */
    }
}

/*
 * bulletClearAll - Deactivate all bullets in the pool.
 *
 * Used during scene transitions (e.g., entering turn-based battle or
 * loading a new zone) to instantly remove all projectiles. The OAM
 * entries will be hidden on the next bulletRenderAll() call.
 */
void bulletClearAll(void)
{
    u8 i;
    for (i = 0; i < MAX_BULLETS; i++) {
        bullet_pool[i].active = ENTITY_INACTIVE;
    }
}

/*
 * bulletNextWeapon - Cycle to the next weapon type (L/R shoulder).
 *
 * Wraps from WEAPON_LASER (2) back to WEAPON_SINGLE (0).
 * Resets fire cooldown so the player can immediately fire with
 * the new weapon (no delay penalty for switching).
 */
void bulletNextWeapon(void)
{
    g_weapon.weapon_type++;
    if (g_weapon.weapon_type >= WEAPON_COUNT) {
        g_weapon.weapon_type = 0;  /* Wrap to first weapon */
    }
    g_weapon.fire_cooldown = 0;  /* Allow immediate fire after switch */
}

/*
 * bulletPrevWeapon - Cycle to the previous weapon type.
 *
 * Wraps from WEAPON_SINGLE (0) to WEAPON_LASER (2).
 * Same cooldown reset as bulletNextWeapon.
 */
void bulletPrevWeapon(void)
{
    if (g_weapon.weapon_type == 0) {
        g_weapon.weapon_type = WEAPON_COUNT - 1;  /* Wrap to last weapon */
    } else {
        g_weapon.weapon_type--;
    }
    g_weapon.fire_cooldown = 0;  /* Allow immediate fire after switch */
}

/*
 * bulletGetPool - Return a pointer to the bullet pool array.
 *
 * Used by the collision system (collision.c) to iterate over bullets
 * for hit detection against enemies and the player. The collision system
 * accesses bullet positions, active states, and damage values directly.
 *
 * Returns: Pointer to bullet_pool[0].
 */
Bullet* bulletGetPool(void)
{
    return bullet_pool;
}

/*
 * bulletGetMasteryBonus - Get damage bonus for a weapon type (#150).
 * Returns 0/1/2/3 for <10/<25/<50/>=50 kills respectively.
 */
u8 bulletGetMasteryBonus(u8 weapon_type)
{
    u16 kills;
    if (weapon_type >= WEAPON_COUNT) return 0;
    kills = g_weapon_kills[weapon_type];
    if (kills >= 50) return 3;
    if (kills >= 25) return 2;
    if (kills >= 10) return 1;
    return 0;
}

/*
 * bulletAddWeaponKill - Increment kill count for current weapon type (#150).
 * Called from collision.c on enemy destruction.
 */
void bulletAddWeaponKill(void)
{
    u8 wt;
    wt = g_weapon.weapon_type;
    if (wt < WEAPON_COUNT && g_weapon_kills[wt] < 0xFFFF) {
        g_weapon_kills[wt]++;
    }
}

/*
 * bulletResetMomentum - Reset rapid fire hold counter (#151).
 * Called when fire button is released.
 */
void bulletResetMomentum(void)
{
    g_fire_hold_frames = 0;
}
