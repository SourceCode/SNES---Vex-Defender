/*==============================================================================
 * Collision Detection System
 *
 * Three collision checks per frame:
 *   1. Player bullets (pool 0-15) vs enemies (pool 0-7)
 *   2. Enemy bullets (pool 16-23) vs player
 *   3. Player body vs enemies (contact damage)
 *
 * Hitboxes are smaller than sprites for fair gameplay:
 *   Player 32x32 -> 16x16 hitbox (cockpit area)
 *   Enemy  32x32 -> 24x24 hitbox
 *   Bullet 16x16 ->  8x8  hitbox (core area)
 *   Laser  16x16 -> 12x12 hitbox (larger impact)
 *
 * Worst case: 16 bullets x 8 enemies + 8 bullets + 8 enemies = 144 AABB checks
 * Each check is ~8 integer operations. Fits well within frame budget.
 *============================================================================*/

#include "engine/collision.h"
#include "engine/bullets.h"
#include "engine/sound.h"
#include "game/enemies.h"
#include "game/player.h"
#include "game/battle.h"

/*=== Hitbox Definitions ===*/
static const Hitbox hb_player = { 8, 8, 16, 16 };    /* 32x32 sprite */
static const Hitbox hb_enemy  = { 4, 4, 24, 24 };    /* 32x32 sprite */
static const Hitbox hb_bullet = { 4, 4, 8, 8 };      /* 16x16 sprite */
static const Hitbox hb_laser  = { 2, 2, 12, 12 };    /* 16x16 sprite (larger) */

u16 g_score;

void collisionInit(void)
{
    g_score = 0;
}

u8 collisionCheckAABB(s16 ax, s16 ay, const Hitbox *ha,
                      s16 bx, s16 by, const Hitbox *hb)
{
    s16 al, ar, at, ab;
    s16 bl, br, bt, bb;

    al = ax + (s16)ha->x_off;
    ar = al + (s16)ha->width;
    at = ay + (s16)ha->y_off;
    ab = at + (s16)ha->height;

    bl = bx + (s16)hb->x_off;
    br = bl + (s16)hb->width;
    bt = by + (s16)hb->y_off;
    bb = bt + (s16)hb->height;

    /* No overlap if any gap exists */
    if (ar <= bl || al >= br || ab <= bt || at >= bb) return 0;
    return 1;
}

/*---------------------------------------------------------------------------*/
/* Check 1: Player bullets vs enemies                                        */
/*---------------------------------------------------------------------------*/
static void checkPlayerBulletsVsEnemies(void)
{
    u8 bi, ei;
    Bullet *bullets;
    Enemy *enemies;
    Bullet *b;
    Enemy *e;
    const Hitbox *bh;

    bullets = bulletGetPool();
    enemies = enemyGetPool();

    for (bi = 0; bi < MAX_PLAYER_BULLETS; bi++) {
        b = &bullets[bi];
        if (b->active != ENTITY_ACTIVE) continue;

        /* Laser gets a larger hitbox */
        bh = (b->type == BULLET_TYPE_LASER) ? &hb_laser : &hb_bullet;

        for (ei = 0; ei < MAX_ENEMIES; ei++) {
            e = &enemies[ei];
            if (e->active != ENTITY_ACTIVE) continue;

            if (collisionCheckAABB(b->x, b->y, bh, e->x, e->y, &hb_enemy)) {
                /* Deactivate the bullet */
                b->active = ENTITY_INACTIVE;

                /* Apply damage to enemy */
                if (enemyDamage(e, b->damage)) {
                    /* Enemy destroyed - award score */
                    g_score += enemyGetTypeDef(e->type)->score_value;
                    soundPlaySFX(SFX_EXPLOSION);
                } else {
                    soundPlaySFX(SFX_HIT);
                }

                break;  /* Bullet consumed, check next bullet */
            }
        }
    }
}

/*---------------------------------------------------------------------------*/
/* Check 2: Enemy bullets vs player                                          */
/*---------------------------------------------------------------------------*/
static void checkEnemyBulletsVsPlayer(void)
{
    u8 bi;
    Bullet *bullets;
    Bullet *b;

    /* Skip if player is invincible or hidden */
    if (g_player.invincible_timer > 0 || !g_player.visible) return;

    bullets = bulletGetPool();

    for (bi = MAX_PLAYER_BULLETS; bi < MAX_BULLETS; bi++) {
        b = &bullets[bi];
        if (b->active != ENTITY_ACTIVE) continue;

        if (collisionCheckAABB(b->x, b->y, &hb_bullet,
                               g_player.x, g_player.y, &hb_player)) {
            /* Deactivate the bullet */
            b->active = ENTITY_INACTIVE;

            /* Player takes hit: invincibility frames (2 seconds) */
            soundPlaySFX(SFX_HIT);
            g_player.invincible_timer = 120;

            break;  /* One hit per frame */
        }
    }
}

/*---------------------------------------------------------------------------*/
/* Check 3: Player body vs enemies (contact damage)                          */
/*---------------------------------------------------------------------------*/
static void checkPlayerVsEnemies(void)
{
    u8 ei;
    Enemy *enemies;
    Enemy *e;

    /* Skip if player is invincible or hidden */
    if (g_player.invincible_timer > 0 || !g_player.visible) return;

    enemies = enemyGetPool();

    for (ei = 0; ei < MAX_ENEMIES; ei++) {
        e = &enemies[ei];
        if (e->active != ENTITY_ACTIVE) continue;

        if (collisionCheckAABB(g_player.x, g_player.y, &hb_player,
                               e->x, e->y, &hb_enemy)) {
            if (e->type >= ENEMY_TYPE_FIGHTER) {
                /* Non-scout: trigger turn-based battle */
                g_battle_trigger = e->type;
                e->active = ENTITY_INACTIVE;
            } else {
                /* Scout: destroyed on contact (too weak for battle) */
                g_player.invincible_timer = 120;
                g_score += enemyGetTypeDef(e->type)->score_value;
                e->active = ENTITY_INACTIVE;
            }

            break;  /* One contact per frame */
        }
    }
}

/*===========================================================================*/
/* Main collision dispatch                                                   */
/*===========================================================================*/

void collisionCheckAll(void)
{
    checkPlayerBulletsVsEnemies();
    checkEnemyBulletsVsPlayer();
    checkPlayerVsEnemies();
}
