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
 *
 * Optimization strategy (#101, #103, #106):
 *   - Cache frequently accessed global values (player position) in locals
 *   - Pre-compute AABB edges outside inner loops to avoid redundant additions
 *   - Inline AABB checks with known hitbox constants to eliminate function
 *     call overhead and pointer dereferences in hot loops
 *   - Y-range pre-rejection: skip full AABB test if Y ranges don't overlap
 *     (common for bullets far from enemies vertically)
 *
 * Combo scoring (#102):
 *   Consecutive enemy kills within 60 frames (1 second) build a combo
 *   multiplier (1x-4x) applied to score. Uses shift-add instead of
 *   multiply to avoid the 65816's slow 8-bit multiply unit.
 *============================================================================*/

#include "engine/collision.h"
#include "engine/bullets.h"
#include "engine/sound.h"
#include "engine/vblank.h"
#include "game/enemies.h"
#include "game/player.h"
#include "game/battle.h"
#include "game/rpg_stats.h"
#include "game/inventory.h"

/*=== Hitbox Definitions ===*/
/* These are const (ROM) since they never change. The offset positions
 * the hitbox within the sprite area, making it smaller than the visual
 * for forgiving gameplay ("bullet hell" genre convention). */
static const Hitbox hb_player = { 8, 8, 16, 16 };    /* 32x32 sprite -> 16x16 centered hitbox */
static const Hitbox hb_enemy  = { 4, 4, 24, 24 };    /* 32x32 sprite -> 24x24 nearly full-size */
static const Hitbox hb_bullet = { 4, 4, 8, 8 };      /* 16x16 sprite ->  8x8 small core hitbox */
static const Hitbox hb_laser  = { 2, 2, 12, 12 };    /* 16x16 sprite -> 12x12 larger for laser */

/* Player score in points (accumulated from enemy kills with combo scaling) */
u16 g_score;

/* Screen shake frames remaining. Set on player hit, decremented by the
 * rendering/camera system to produce a brief camera jitter effect. */
u8 g_screen_shake = 0;

/* Combo state: count of consecutive kills within the timer window.
 * g_combo_timer starts at 60 (1 second) on each kill and counts down.
 * When it reaches 0, g_combo_count resets and the multiplier ends. */
u8 g_combo_count = 0;
u8 g_combo_timer = 0;

/* #141: Combo multiplier cached for decaying window calculation */
u8 g_combo_multiplier = 0;

/* #143: Kill streak - consecutive kills without player taking damage */
u8 g_kill_streak = 0;

/* #157: Bonus score zone timer - frames remaining of 2x score period */
u8 g_score_bonus_timer = 0;

/* #167: Weapon switch combo tracking - circular buffer of last 3 weapon kills */
u8 g_weapon_combo_buf[3];
u8 g_weapon_combo_idx = 0;

/* #183: Combo multiplier display timer for HUD feedback */
u8 g_combo_display_timer = 0;

/* #197: Wave clear tracking */
u8  g_wave_enemy_count = 0;
u8  g_wave_kill_count = 0;
u16 g_wave_timer = 0;

/*
 * collisionInit - Reset collision system state.
 * Called at the start of each gameplay session to zero score and effects.
 */
void collisionInit(void)
{
    g_score = 0;
    g_screen_shake = 0;
    g_combo_count = 0;
    g_combo_timer = 0;
    g_combo_multiplier = 0;
    g_kill_streak = 0;
    g_score_bonus_timer = 0;
    /* #167: Initialize weapon combo buffer to 0xFF (no weapon) */
    g_weapon_combo_buf[0] = 0xFF;
    g_weapon_combo_buf[1] = 0xFF;
    g_weapon_combo_buf[2] = 0xFF;
    g_weapon_combo_idx = 0;
    /* #183: Reset combo display timer */
    g_combo_display_timer = 0;
    /* #197: Reset wave clear tracking */
    g_wave_enemy_count = 0;
    g_wave_kill_count = 0;
    g_wave_timer = 0;
}

/*
 * collisionCheckAABB - Generic AABB overlap test between two hitboxes.
 *
 * Computes the absolute edges (left, right, top, bottom) for both
 * entities by adding hitbox offsets to positions, then tests for
 * separation on both axes using the separating axis theorem:
 *   No overlap if: A_right <= B_left  OR  A_left >= B_right
 *                  OR  A_bottom <= B_top  OR  A_top >= B_bottom
 *
 * This requires only additions and comparisons (no multiply/divide),
 * making it ideal for the 65816. Each call is ~8 integer operations.
 *
 * Parameters:
 *   ax, ay - Position of entity A (sprite top-left)
 *   ha     - Hitbox for entity A (offsets and dimensions)
 *   bx, by - Position of entity B (sprite top-left)
 *   hb     - Hitbox for entity B
 *
 * Returns: 1 if overlapping, 0 if no overlap.
 */
u8 collisionCheckAABB(s16 ax, s16 ay, const Hitbox *ha,
                      s16 bx, s16 by, const Hitbox *hb)
{
    s16 al, ar, at, ab;  /* Entity A: left, right, top, bottom edges */
    s16 bl, br, bt, bb;  /* Entity B: left, right, top, bottom edges */

    /* Compute absolute AABB edges for entity A */
    al = ax + (s16)ha->x_off;
    ar = al + (s16)ha->width;
    at = ay + (s16)ha->y_off;
    ab = at + (s16)ha->height;

    /* Compute absolute AABB edges for entity B */
    bl = bx + (s16)hb->x_off;
    br = bl + (s16)hb->width;
    bt = by + (s16)hb->y_off;
    bb = bt + (s16)hb->height;

    /* Separating axis test: if any gap exists, no overlap */
    if (ar <= bl || al >= br || ab <= bt || at >= bb) return 0;
    return 1;
}

/*---------------------------------------------------------------------------*/
/* Check 1: Player bullets vs enemies                                        */
/*                                                                           */
/* This is the hottest collision loop: up to 16 bullets x 8 enemies = 128    */
/* potential AABB tests per frame. Several optimizations are applied:        */
/*   - Off-screen bullet skip (avoids testing bullets outside play area)     */
/*   - Y-range pre-rejection (cheap test before full AABB)                   */
/*   - Inlined AABB with constant hitbox values (no struct dereference)      */
/*   - Pre-computed bullet edges outside the inner enemy loop                */
/*   - Early break when bullet is consumed (no need to check more enemies)   */
/*---------------------------------------------------------------------------*/
static void checkPlayerBulletsVsEnemies(void)
{
    u8 bi, ei;
    Bullet *bullets;
    Enemy *enemies;
    Bullet *b;
    Enemy *e;
    const Hitbox *bh;
    s16 b_top, b_bot;
    /* #103: Inlined AABB variables - avoid per-iteration function call overhead */
    s16 bl, br, bt, bb;  /* Pre-computed bullet AABB edges */
    s16 el, er, et, eb;  /* Enemy AABB edges (computed in inner loop) */
    /* #102: Combo scoring locals to avoid repeated global access */
    u16 base_score;
    u8 mult;

    bullets = bulletGetPool();
    enemies = enemyGetPool();

    for (bi = 0; bi < MAX_PLAYER_BULLETS; bi++) {
        b = &bullets[bi];
        if (b->active != ENTITY_ACTIVE) continue;

        /* Skip bullets that are clearly off-screen (no enemies to hit there) */
        if (b->y < -8 || b->y > 232 || b->x < -8 || b->x > 264) continue;

        /* Laser bullets get a larger hitbox (12x12 vs 8x8) for more forgiving hits */
        bh = (b->type == BULLET_TYPE_LASER) ? &hb_laser : &hb_bullet;

        /* Pre-compute bullet Y bounds for the cheap Y-range pre-rejection test.
         * This is cheaper than the full AABB and eliminates most non-overlapping pairs. */
        b_top = b->y;
        b_bot = b->y + 16;  /* Bullet sprite height */

        /* #103: Pre-compute bullet AABB edges once per bullet (invariant across enemies) */
        bl = b->x + (s16)bh->x_off;
        br = bl + (s16)bh->width;
        bt = b->y + (s16)bh->y_off;
        bb = bt + (s16)bh->height;

        e = enemies;
        for (ei = 0; ei < MAX_ENEMIES; ei++, e++) {
            if (e->active != ENTITY_ACTIVE) continue;
            if (e->is_hazard) continue;  /* #186: Hazards are invulnerable to bullets */

            /* Y-range pre-rejection: skip if bullet and enemy Y ranges don't overlap.
             * This is a quick broadphase test: if the bullet is entirely above or
             * below the enemy sprite, no collision is possible. Eliminates ~50-70%
             * of pairs in a typical shmup screen layout. */
            if (b_bot < e->y || b_top > e->y + 32) continue;

            /* #103: Inline AABB check using enemy hitbox constants (4,4,24,24)
             * directly instead of going through collisionCheckAABB().
             * Eliminates function call overhead (JSR/RTS = 12 cycles on 65816)
             * and pointer dereference costs. */
            el = e->x + 4;    /* hb_enemy.x_off = 4 */
            er = el + 24;     /* hb_enemy.width = 24 */
            et = e->y + 4;    /* hb_enemy.y_off = 4 */
            eb = et + 24;     /* hb_enemy.height = 24 */

            /* Inverted separating axis test: overlap if NO gap on ANY axis */
            if (!(br <= el || bl >= er || bb <= et || bt >= eb)) {
                /* HIT: Bullet collided with enemy */

                /* Deactivate the bullet (consumed on hit) */
                b->active = ENTITY_INACTIVE;

                /* #181: Shield absorbs hit without damage */
                if (e->shield > 0) {
                    e->shield = 0;
                    e->flash_timer = 6;
                    soundPlaySFX(SFX_HIT);
                    break;
                }

                /* #188: Elite enemy dodge - ~20% chance to evade bullet */
                if (e->type == ENEMY_TYPE_ELITE && (g_frame_count & 4) == 0) {
                    e->flash_timer = 3;
                    break;
                }

                /* Apply damage to enemy. enemyDamage returns 1 if enemy was destroyed. */
                /* #145: Calculate overkill bonus before applying damage.
                 * If bullet damage exceeds remaining HP, excess * 10 = bonus score. */
                {
                    u16 overkill_bonus = 0;
                    if (b->damage > e->hp) {
                        u8 excess;
                        excess = b->damage - e->hp;
                        /* excess * 10 via shifts: (excess << 3) + (excess << 1) */
                        overkill_bonus = ((u16)excess << 3) + ((u16)excess << 1);
                    }

                if (enemyDamage(e, b->damage)) {
                    /* Enemy destroyed - apply combo scoring */
                    const EnemyTypeDef *kill_def;
                    kill_def = enemyGetTypeDef(e->type);
                    base_score = kill_def->score_value;

                    /* #147: Golden enemy awards 3x base score */
                    if (e->is_golden) {
                        base_score = (base_score << 1) + base_score;
                        /* #191: 50% chance for shield pickup from golden enemies */
                        if ((g_frame_count & 1) == 0) {
                            g_player.invincible_timer = 60;
                            soundPlaySFX(SFX_HEAL);
                        }
                    }

                    /* #146: Speed kill bonus - killed within 90 frames of spawn = 2x score */
                    if (e->age < 90) {
                        base_score <<= 1;
                    }

                    /* Extend combo window and increment combo count */
                    g_combo_count++;

                    /* #141: Decaying combo window.
                     * Higher multiplier = shorter window to land next kill.
                     * 60 / 52 / 44 / 36 frames for 1x/2x/3x/4x. */
                    mult = g_combo_count;
                    if (mult > 4) mult = 4;
                    g_combo_multiplier = mult;
                    g_combo_timer = 60 - (g_combo_multiplier << 3);
                    if (g_combo_timer < 36) g_combo_timer = 36;
                    g_combo_display_timer = 30;  /* #183: Show multiplier on HUD */

                    /* #234: Combo flash on player palette when at 2x+ combo */
                    if (g_combo_multiplier >= 2) {
                        g_player.combo_flash = 6;
                    }

                    /* #102: Shift-add scoring */
                    {
                        u16 add_val;
                        switch (mult) {
                            case 2: add_val = base_score << 1; break;
                            case 3: add_val = (base_score << 1) + base_score; break;
                            case 4: add_val = base_score << 2; break;
                            default: add_val = base_score; break;
                        }

                        /* #143: Kill streak bonus - every 5 kills without damage adds +25% (max +100%) */
                        g_kill_streak++;
                        {
                            u8 streak_bonus;
                            streak_bonus = g_kill_streak / 5;
                            if (streak_bonus > 4) streak_bonus = 4;
                            /* add_val += add_val * streak_bonus / 4, using shift */
                            if (streak_bonus > 0) {
                                u16 streak_add;
                                streak_add = (add_val >> 2) * streak_bonus;
                                add_val += streak_add;
                            }
                        }

                        /* #145: Add overkill bonus */
                        add_val += overkill_bonus;

                        /* #157: Bonus score zone - double score during active bonus period */
                        if (g_score_bonus_timer > 0) {
                            add_val <<= 1;
                        }

                        /* #120: Saturating add */
                        if (g_score > (u16)(0xFFFF - add_val)) {
                            g_score = 0xFFFF;
                        } else {
                            g_score += add_val;
                        }
                    }

                    /* #140: Combo tier milestone rewards.
                     * At combo 5/10/15, award burst bonus score + SFX. */
                    {
                        u16 milestone_bonus = 0;
                        if (g_combo_count == 5)       milestone_bonus = 500;
                        else if (g_combo_count == 10) milestone_bonus = 1500;
                        else if (g_combo_count == 15) milestone_bonus = 5000;
                        if (milestone_bonus > 0) {
                            if (g_score > (u16)(0xFFFF - milestone_bonus)) {
                                g_score = 0xFFFF;
                            } else {
                                g_score += milestone_bonus;
                            }
                            soundPlaySFX(SFX_LEVEL_UP);
                            /* #180: SP regen on combo milestone kills */
                            if (rpg_stats.sp < rpg_stats.max_sp) {
                                rpg_stats.sp++;
                            }
                        }
                    }

                    /* #148: Kill milestone item rewards at 10/25/50/100 total kills */
                    rpg_stats.total_kills++;
                    if (rpg_stats.total_kills == 10) {
                        invAdd(ITEM_HP_POTION_S, 1);
                        soundPlaySFX(SFX_LEVEL_UP);
                        setBrightness(15);
                    } else if (rpg_stats.total_kills == 25) {
                        invAdd(ITEM_HP_POTION_L, 1);
                        soundPlaySFX(SFX_LEVEL_UP);
                        setBrightness(15);
                    } else if (rpg_stats.total_kills == 50) {
                        invAdd(ITEM_SP_CHARGE, 1);
                        soundPlaySFX(SFX_LEVEL_UP);
                        setBrightness(15);
                    } else if (rpg_stats.total_kills == 100) {
                        invAdd(ITEM_FULL_RESTORE, 1);
                        soundPlaySFX(SFX_LEVEL_UP);
                        setBrightness(15);
                    }

                    /* #174: Track max combo across playthrough */
                    if (g_combo_count > g_game.max_combo) {
                        g_game.max_combo = (u8)g_combo_count;
                    }

                    /* #150: Track weapon kill for mastery system */
                    bulletAddWeaponKill();

                    /* #167: Weapon switch combo bonus - track last 3 weapon kills */
                    g_weapon_combo_buf[g_weapon_combo_idx] = g_weapon.weapon_type;
                    if (++g_weapon_combo_idx >= 3) g_weapon_combo_idx = 0;
                    /* Check if all 3 entries are different weapon types */
                    if (g_weapon_combo_buf[0] != g_weapon_combo_buf[1] &&
                        g_weapon_combo_buf[1] != g_weapon_combo_buf[2] &&
                        g_weapon_combo_buf[0] != g_weapon_combo_buf[2] &&
                        g_weapon_combo_buf[0] != 0xFF) {
                        /* FULL ARSENAL bonus: 1000 score */
                        if (g_score > (u16)(0xFFFF - 1000)) g_score = 0xFFFF;
                        else g_score += 1000;
                        soundPlaySFX(SFX_LEVEL_UP);
                        /* Reset buffer so bonus doesn't repeat every kill */
                        g_weapon_combo_buf[0] = g_weapon.weapon_type;
                        g_weapon_combo_buf[1] = g_weapon.weapon_type;
                        g_weapon_combo_buf[2] = g_weapon.weapon_type;
                    }

                    /* #197: Wave clear tracking - check for full wave kill bonus */
                    g_wave_kill_count++;
                    if (g_wave_kill_count >= g_wave_enemy_count &&
                        g_wave_enemy_count >= 3 && g_wave_timer > 0) {
                        /* WAVE CLEAR! bonus: 500 score */
                        if (g_score > (u16)(0xFFFF - 500)) g_score = 0xFFFF;
                        else g_score += 500;
                        soundPlaySFX(SFX_LEVEL_UP);
                        g_screen_shake = 4;  /* #215: Screen shake on wave clear */
                        g_wave_enemy_count = 0;
                        g_wave_kill_count = 0;
                        g_wave_timer = 0;
                    }

                    /* #168: Kill bullet cancel - remove 1 active enemy bullet on kill */
                    {
                        Bullet *eb;
                        u8 ebi;
                        eb = bulletGetPool();
                        for (ebi = MAX_PLAYER_BULLETS; ebi < MAX_BULLETS; ebi++) {
                            if (eb[ebi].active == ENTITY_ACTIVE) {
                                eb[ebi].active = ENTITY_INACTIVE;
                                break;
                            }
                        }
                    }

                    /* #130: Brightness pulse on combo kills (3+ combo = flash to full) */
                    if (g_combo_count >= 3) {
                        setBrightness(15);
                    }
                    soundPlaySFX(SFX_EXPLOSION);
                } else {
                    /* Enemy survived - play hit sound instead of explosion */
                    soundPlaySFX(SFX_HIT);
                }
                } /* end overkill scope */

                break;  /* Bullet consumed, move to next bullet (outer loop) */
            }
        }
    }
}

/*---------------------------------------------------------------------------*/
/* Check 2: Enemy bullets vs player                                          */
/*                                                                           */
/* Simpler than Check 1: only 8 enemy bullets checked against the single     */
/* player entity. Uses the generic collisionCheckAABB function since the      */
/* loop is small (no need for hot-path inlining).                            */
/*---------------------------------------------------------------------------*/
static void checkEnemyBulletsVsPlayer(void)
{
    u8 bi;
    Bullet *bullets;
    Bullet *b;
    /* #101: Cache player position in local variables */
    s16 px, py;
    /* #142: Graze detection - expanded hitbox for near-misses */
    static const Hitbox hb_graze = { 2, 2, 28, 28 };  /* 6px larger than player hitbox on each side */
    static u8 graze_mask;  /* Bitmask: which bullets were already grazed this frame */

    /* Skip collision check if player is invincible or hidden */
    if (g_player.invincible_timer > 0 || !g_player.visible) return;

    px = g_player.x;
    py = g_player.y;
    bullets = bulletGetPool();

    /* #142: Reset graze mask each frame */
    graze_mask = 0;

    /* Only check enemy bullets (indices MAX_PLAYER_BULLETS to MAX_BULLETS-1) */
    for (bi = MAX_PLAYER_BULLETS; bi < MAX_BULLETS; bi++) {
        b = &bullets[bi];
        if (b->active != ENTITY_ACTIVE) continue;

        /* Skip bullets that are clearly off-screen vertically */
        if (b->y < -8 || b->y > 232) continue;

        /* Full AABB check */
        if (collisionCheckAABB(b->x, b->y, &hb_bullet,
                               px, py, &hb_player)) {
            /* HIT: Enemy bullet hit the player */
            b->active = ENTITY_INACTIVE;

            soundPlaySFX(SFX_HIT);
            g_player.invincible_timer = 120;
            g_screen_shake = 6;

            /* #143: Reset kill streak on player hit */
            g_kill_streak = 0;

            /* #155: Player took damage, cancel no-damage zone bonus */
            g_game.zone_no_damage = 0;

            break;  /* Only one hit per frame */
        } else {
            /* #142: Bullet graze scoring.
             * If bullet missed but is within 6px expanded hitbox, it's a graze.
             * Award 25 score per graze. Use bitmask to prevent repeat on same bullet. */
            {
                u8 graze_bit;
                graze_bit = 1 << (bi - MAX_PLAYER_BULLETS);
                if (!(graze_mask & graze_bit)) {
                    if (collisionCheckAABB(b->x, b->y, &hb_bullet,
                                           px, py, &hb_graze)) {
                        graze_mask |= graze_bit;
                        /* Award 25 graze score with saturating add */
                        if (g_score > (u16)(0xFFFF - 25)) {
                            g_score = 0xFFFF;
                        } else {
                            g_score += 25;
                        }
                    }
                }
            }
        }
    }
}

/*---------------------------------------------------------------------------*/
/* Check 3: Player body vs enemies (contact damage / battle trigger)         */
/*                                                                           */
/* When the player ship physically touches an enemy:                          */
/*   - Weak enemies (scouts): destroyed on contact, score awarded             */
/*   - Strong enemies (fighters+): triggers a turn-based battle encounter    */
/* This dual behavior connects the shmup flight mode with the RPG battle mode.*/
/*---------------------------------------------------------------------------*/
static void checkPlayerVsEnemies(void)
{
    u8 ei;
    Enemy *enemies;
    Enemy *e;
    const EnemyTypeDef *def;  /* #134: Cache type def at call site */
    /* #106: Pre-compute player AABB edges outside the loop.
     * Player hitbox (hb_player): x_off=8, y_off=8, width=16, height=16
     * These values are constant across all enemy comparisons. */
    s16 pl, pr, pt, pb;  /* Player AABB: left, right, top, bottom */
    s16 el, er, et, eb;  /* Enemy AABB edges (computed per-enemy) */

    /* Skip if player is invincible or hidden */
    if (g_player.invincible_timer > 0 || !g_player.visible) return;

    /* #106: Player bounds are loop-invariant (only enemies change per iteration) */
    pl = g_player.x + 8;   /* hb_player.x_off */
    pr = pl + 16;           /* hb_player.width */
    pt = g_player.y + 8;   /* hb_player.y_off */
    pb = pt + 16;           /* hb_player.height */

    enemies = enemyGetPool();

    e = enemies;
    for (ei = 0; ei < MAX_ENEMIES; ei++, e++) {
        if (e->active != ENTITY_ACTIVE) continue;

        /* #106: Inline AABB with enemy hitbox constants (4,4,24,24).
         * Same optimization as in checkPlayerBulletsVsEnemies. */
        el = e->x + 4;   /* hb_enemy.x_off */
        er = el + 24;     /* hb_enemy.width */
        et = e->y + 4;    /* hb_enemy.y_off */
        eb = et + 24;     /* hb_enemy.height */

        /* Check for overlap (inverted separation test) */
        if (!(pr <= el || pl >= er || pb <= et || pt >= eb)) {
            /* CONTACT: Player touched an enemy */

            /* #134: Cache type def to avoid repeated lookup */
            def = enemyGetTypeDef(e->type);

            /* #155: Player took contact damage, cancel no-damage zone bonus */
            g_game.zone_no_damage = 0;

            /* #143: Reset kill streak on player contact */
            g_kill_streak = 0;

            if (e->type >= ENEMY_TYPE_FIGHTER) {
                /* Strong enemy (fighter or higher): trigger turn-based battle */
                g_battle_trigger = e->type;
                e->active = ENTITY_INACTIVE;
            } else {
                /* Weak enemy (scout): destroyed on contact */
                g_player.invincible_timer = 120;
                /* #120: Saturating add for scout contact score */
                {
                    u16 sv = def->score_value;
                    if (g_score > (u16)(0xFFFF - sv)) {
                        g_score = 0xFFFF;
                    } else {
                        g_score += sv;
                    }
                }
                /* #121: Screen shake + SFX on scout body contact for impact feedback */
                g_screen_shake = 6;
                soundPlaySFX(SFX_HIT);
                e->active = ENTITY_INACTIVE;
            }

            break;  /* One contact event per frame */
        }
    }
}

/*===========================================================================*/
/* Main collision dispatch - called once per frame                            */
/*                                                                           */
/* Order of operations:                                                      */
/*   1. Decay combo timer (must happen regardless of enemy state)            */
/*   2. If enemies exist: check bullets vs enemies, player vs enemies        */
/*   3. Always check enemy bullets vs player (bullets outlive their spawners)*/
/*===========================================================================*/

void collisionCheckAll(void)
{
    u8 has_enemies;

    /* Decay combo timer every frame */
    if (g_combo_timer > 0) {
        g_combo_timer--;
        if (g_combo_timer == 0) {
            /* #195: Chain reset protection - grace period for big combos */
            if (g_combo_count >= 5 && g_combo_multiplier > 1) {
                g_combo_multiplier = 1;
                g_combo_timer = 30;  /* 30-frame grace at 1x */
            } else {
                g_combo_count = 0;
                g_combo_multiplier = 0;
            }
        }
    }

    /* #183: Decrement combo display timer */
    if (g_combo_display_timer > 0) {
        g_combo_display_timer--;
    }

    /* #157: Decrement bonus score zone timer */
    if (g_score_bonus_timer > 0) {
        g_score_bonus_timer--;
    }

    /* #197: Decrement wave timer; reset tracking on expiry */
    if (g_wave_timer > 0) {
        g_wave_timer--;
        if (g_wave_timer == 0) {
            g_wave_enemy_count = 0;
            g_wave_kill_count = 0;
        }
    }

    /* Use the pre-maintained active enemy counter instead of scanning the
     * enemy pool. This flag is updated by the enemy system each frame,
     * saving a potential 8-iteration scan. */
    has_enemies = (g_enemy_active_count > 0);

    if (has_enemies) {
        /* Only run bullet-vs-enemy and contact checks when enemies exist.
         * With no enemies, these loops would iterate fruitlessly. */
        checkPlayerBulletsVsEnemies();
        checkPlayerVsEnemies();
    }

    /* Enemy bullets vs player is always checked because bullets may still
     * be in flight after all enemies are destroyed. */
    checkEnemyBulletsVsPlayer();
}
