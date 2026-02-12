/*==============================================================================
 * Test: Bullet System
 * Tests pool allocation, weapon cycling, fire rates, velocity math.
 *============================================================================*/

#include "test_framework.h"
#include "engine/bullets.h"

/* bullets.c is included by test_main.c */

TEST(test_bullet_init)
{
    u8 i;
    bulletInit();
    Bullet *pool = bulletGetPool();
    for (i = 0; i < MAX_BULLETS; i++) {
        TEST_ASSERT_EQ(pool[i].active, ENTITY_INACTIVE, "Bullet inactive");
    }
    TEST_ASSERT_EQ(g_weapon.weapon_type, WEAPON_SINGLE, "Default weapon = SINGLE");
    TEST_ASSERT_EQ(g_weapon.fire_cooldown, 0, "Cooldown = 0");
}

TEST(test_bullet_weapon_cycle)
{
    bulletInit();
    TEST_ASSERT_EQ(g_weapon.weapon_type, WEAPON_SINGLE, "Start: SINGLE");

    bulletNextWeapon();
    TEST_ASSERT_EQ(g_weapon.weapon_type, WEAPON_SPREAD, "Next: SPREAD");

    bulletNextWeapon();
    TEST_ASSERT_EQ(g_weapon.weapon_type, WEAPON_LASER, "Next: LASER");

    bulletNextWeapon();
    TEST_ASSERT_EQ(g_weapon.weapon_type, WEAPON_SINGLE, "Wrap: SINGLE");
}

TEST(test_bullet_prev_weapon)
{
    bulletInit();
    bulletPrevWeapon();
    TEST_ASSERT_EQ(g_weapon.weapon_type, WEAPON_LASER, "Prev from SINGLE = LASER");

    bulletPrevWeapon();
    TEST_ASSERT_EQ(g_weapon.weapon_type, WEAPON_SPREAD, "Prev: SPREAD");
}

TEST(test_bullet_player_fire)
{
    u8 i, active_count;
    bulletInit();
    bulletPlayerFire(100, 100);

    Bullet *pool = bulletGetPool();
    active_count = 0;
    for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
        if (pool[i].active == ENTITY_ACTIVE) active_count++;
    }
    TEST_ASSERT_EQ(active_count, 1, "Single shot = 1 bullet");
    TEST_ASSERT_GT(g_weapon.fire_cooldown, 0, "Cooldown set after fire");
}

TEST(test_bullet_fire_cooldown)
{
    u8 i, count_before = 0, count_after = 0;
    bulletInit();
    bulletPlayerFire(100, 100);

    Bullet *pool = bulletGetPool();
    for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
        if (pool[i].active == ENTITY_ACTIVE) count_before++;
    }

    bulletPlayerFire(100, 100);  /* Should be blocked by cooldown */
    for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
        if (pool[i].active == ENTITY_ACTIVE) count_after++;
    }
    TEST_ASSERT_EQ(count_before, count_after, "Cooldown blocks fire");
}

TEST(test_bullet_spread)
{
    u8 i, active_count;
    bulletInit();
    g_weapon.weapon_type = WEAPON_SPREAD;
    bulletPlayerFire(100, 100);

    Bullet *pool = bulletGetPool();
    active_count = 0;
    for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
        if (pool[i].active == ENTITY_ACTIVE) active_count++;
    }
    TEST_ASSERT_EQ(active_count, 3, "Spread = 3 bullets");
}

TEST(test_bullet_update)
{
    bulletInit();
    bulletPlayerFire(100, 100);

    Bullet *pool = bulletGetPool();
    s16 y_before = pool[0].y;
    bulletUpdateAll();
    TEST_ASSERT(pool[0].y < y_before, "Bullet moves upward");
}

TEST(test_bullet_despawn)
{
    u16 i;
    bulletInit();
    bulletPlayerFire(100, 100);

    for (i = 0; i < 100; i++) {
        bulletUpdateAll();
        g_weapon.fire_cooldown = 0;
    }

    Bullet *pool = bulletGetPool();
    TEST_ASSERT_EQ(pool[0].active, ENTITY_INACTIVE, "Bullet despawned off-screen");
}

TEST(test_bullet_clear_all)
{
    u8 i;
    bulletInit();
    bulletPlayerFire(100, 100);
    g_weapon.fire_cooldown = 0;
    bulletPlayerFire(120, 100);

    bulletClearAll();
    Bullet *pool = bulletGetPool();
    for (i = 0; i < MAX_BULLETS; i++) {
        TEST_ASSERT_EQ(pool[i].active, ENTITY_INACTIVE, "All bullets cleared");
    }
}

TEST(test_bullet_enemy_fire_down)
{
    u8 i, found;
    bulletInit();
    bulletEnemyFireDown(100, 50);

    Bullet *pool = bulletGetPool();
    found = 0;
    for (i = MAX_PLAYER_BULLETS; i < MAX_BULLETS; i++) {
        if (pool[i].active == ENTITY_ACTIVE) {
            found = 1;
            TEST_ASSERT(pool[i].vy > 0, "Enemy bullet moves downward");
            TEST_ASSERT_EQ(pool[i].vx, 0, "No horizontal movement");
        }
    }
    TEST_ASSERT_EQ(found, 1, "Enemy bullet spawned in enemy region");
}

TEST(test_bullet_enemy_aimed)
{
    u8 i, found;
    bulletInit();
    bulletEnemyFire(100, 50, 100, 200, 0);

    Bullet *pool = bulletGetPool();
    found = 0;
    for (i = MAX_PLAYER_BULLETS; i < MAX_BULLETS; i++) {
        if (pool[i].active == ENTITY_ACTIVE) {
            found = 1;
            TEST_ASSERT(pool[i].vy > 0, "Aimed bullet moves toward target Y");
        }
    }
    TEST_ASSERT_EQ(found, 1, "Aimed bullet spawned");
}

TEST(test_bullet_oam_slots)
{
    bulletInit();
    Bullet *pool = bulletGetPool();

    TEST_ASSERT_EQ(pool[0].oam_id, 4 * 4, "Player bullet 0 OAM = 16");
    TEST_ASSERT_EQ(pool[1].oam_id, 5 * 4, "Player bullet 1 OAM = 20");
    TEST_ASSERT_EQ(pool[MAX_PLAYER_BULLETS].oam_id, 40 * 4,
                   "Enemy bullet 0 OAM = 160");
}

/*--- Test bullet vx==0 fast-path (improvement #5) ---*/
TEST(test_bullet_vx_zero_fastpath)
{
    bulletInit();
    bulletPlayerFire(100, 100);  /* Single shot: vx=0, vy<0 */

    Bullet *pool = bulletGetPool();
    s16 x_before = pool[0].x;
    s16 y_before = pool[0].y;

    bulletUpdateAll();

    TEST_ASSERT_EQ(pool[0].x, x_before, "Single bullet X unchanged (vx=0 fastpath)");
    TEST_ASSERT(pool[0].y < y_before, "Single bullet Y decreased (moves up)");
}

/*--- Test spread bullets have non-zero vx ---*/
TEST(test_bullet_spread_vx_moves)
{
    u8 i;
    u8 has_left = 0;
    u8 has_right = 0;
    bulletInit();
    g_weapon.weapon_type = WEAPON_SPREAD;
    bulletPlayerFire(100, 100);

    Bullet *pool = bulletGetPool();
    bulletUpdateAll();

    for (i = 0; i < MAX_PLAYER_BULLETS; i++) {
        if (pool[i].active == ENTITY_ACTIVE) {
            if (pool[i].vx < 0) has_left = 1;
            if (pool[i].vx > 0) has_right = 1;
        }
    }
    TEST_ASSERT_EQ(has_left, 1, "Spread has left-moving bullet");
    TEST_ASSERT_EQ(has_right, 1, "Spread has right-moving bullet");
}

/*--- Test enemy bullet with vx==0 ---*/
TEST(test_bullet_enemy_straight_down)
{
    u8 i;
    bulletInit();
    bulletEnemyFireDown(100, 50);

    Bullet *pool = bulletGetPool();
    s16 x_before = 0;
    for (i = MAX_PLAYER_BULLETS; i < MAX_BULLETS; i++) {
        if (pool[i].active == ENTITY_ACTIVE) {
            x_before = pool[i].x;
            TEST_ASSERT_EQ(pool[i].vx, 0, "Enemy down bullet vx=0");
            break;
        }
    }
    bulletUpdateAll();
    for (i = MAX_PLAYER_BULLETS; i < MAX_BULLETS; i++) {
        if (pool[i].active == ENTITY_ACTIVE) {
            TEST_ASSERT_EQ(pool[i].x, x_before, "Enemy bullet X unchanged");
            break;
        }
    }
}

/*--- Test bullet active count tracking (improvement #2) ---*/
TEST(test_bullet_active_count)
{
    bulletInit();
    TEST_ASSERT_EQ(g_bullet_active_count, 0, "Init active count = 0");

    bulletPlayerFire(100, 100);
    bulletUpdateAll();
    TEST_ASSERT_EQ(g_bullet_active_count, 1, "1 bullet active after fire+update");

    g_weapon.fire_cooldown = 0;
    bulletPlayerFire(120, 100);
    bulletUpdateAll();
    TEST_ASSERT_EQ(g_bullet_active_count, 2, "2 bullets active");

    bulletClearAll();
    bulletUpdateAll();
    TEST_ASSERT_EQ(g_bullet_active_count, 0, "0 after clear+update");
}

/*--- Test bounds check optimization: boundary values (improvement R4#2) ---*/
TEST(test_bullet_bounds_edge_values)
{
    Bullet *pool;
    bulletInit();
    bulletPlayerFire(100, 100);
    pool = bulletGetPool();

    /* Place bullet just inside bounds: x=-16, y=-16 should NOT despawn */
    pool[0].active = ENTITY_ACTIVE;
    pool[0].x = -16;
    pool[0].y = -16;
    pool[0].vy = 0;
    pool[0].vx = 0;
    bulletUpdateAll();
    TEST_ASSERT_EQ(pool[0].active, ENTITY_ACTIVE, "Bullet at (-16,-16) stays active");

    /* Place bullet just outside bounds: x=-17 should despawn */
    pool[0].active = ENTITY_ACTIVE;
    pool[0].x = -17;
    pool[0].y = 100;
    pool[0].vy = 0;
    pool[0].vx = 0;
    bulletUpdateAll();
    TEST_ASSERT_EQ(pool[0].active, ENTITY_INACTIVE, "Bullet at x=-17 despawns");

    /* Place bullet at far right edge: x=272 should NOT despawn */
    pool[0].active = ENTITY_ACTIVE;
    pool[0].x = 272;
    pool[0].y = 100;
    pool[0].vy = 0;
    pool[0].vx = 0;
    bulletUpdateAll();
    TEST_ASSERT_EQ(pool[0].active, ENTITY_ACTIVE, "Bullet at x=272 stays active");

    /* Place bullet past far right: x=273 should despawn */
    pool[0].active = ENTITY_ACTIVE;
    pool[0].x = 273;
    pool[0].y = 100;
    pool[0].vy = 0;
    pool[0].vx = 0;
    bulletUpdateAll();
    TEST_ASSERT_EQ(pool[0].active, ENTITY_INACTIVE, "Bullet at x=273 despawns");

    /* Place bullet at bottom edge: y=240 should NOT despawn */
    pool[0].active = ENTITY_ACTIVE;
    pool[0].x = 100;
    pool[0].y = 240;
    pool[0].vy = 0;
    pool[0].vx = 0;
    bulletUpdateAll();
    TEST_ASSERT_EQ(pool[0].active, ENTITY_ACTIVE, "Bullet at y=240 stays active");

    /* Place bullet past bottom: y=241 should despawn */
    pool[0].active = ENTITY_ACTIVE;
    pool[0].x = 100;
    pool[0].y = 241;
    pool[0].vy = 0;
    pool[0].vx = 0;
    bulletUpdateAll();
    TEST_ASSERT_EQ(pool[0].active, ENTITY_INACTIVE, "Bullet at y=241 despawns");
}

/*--- Test render skip when no bullets active (improvement R4#3) ---*/
TEST(test_bullet_render_idle_skip)
{
    bulletInit();
    bulletUpdateAll();  /* Ensure active count = 0 */
    TEST_ASSERT_EQ(g_bullet_active_count, 0, "No bullets active before render");

    /* Should execute the early-exit path without crash */
    bulletRenderAll();
    TEST_ASSERT(1, "bulletRenderAll() idle path runs without crash");

    /* Fire a bullet so active count > 0, then render normal path */
    bulletPlayerFire(100, 100);
    bulletUpdateAll();
    TEST_ASSERT_GT(g_bullet_active_count, 0, "Bullets active after fire");
    bulletRenderAll();
    TEST_ASSERT(1, "bulletRenderAll() normal path runs without crash");
}

/*--- Test branchless vx=0 path (improvement R5#1) ---*/
TEST(test_bullet_vx_zero_branchless)
{
    Bullet *pool;
    s16 x_before;
    bulletInit();
    bulletPlayerFire(100, 100);  /* Single shot: vx=0 */
    pool = bulletGetPool();

    /* Verify vx is indeed 0 */
    TEST_ASSERT_EQ(pool[0].vx, 0, "Single bullet vx=0");

    x_before = pool[0].x;
    bulletUpdateAll();

    /* After branchless add of (0>>8)=0, x should be unchanged */
    TEST_ASSERT_EQ(pool[0].x, x_before, "vx=0 branchless: x unchanged after update");
}

/*--- Test off-screen bullet NOT counted in active count (improvement R5#2) ---*/
TEST(test_bullet_offscreen_not_counted)
{
    Bullet *pool;
    bulletInit();
    bulletPlayerFire(100, 100);
    pool = bulletGetPool();

    /* Place bullet just outside top of screen */
    pool[0].active = ENTITY_ACTIVE;
    pool[0].x = 100;
    pool[0].y = -17;
    pool[0].vx = 0;
    pool[0].vy = 0;

    bulletUpdateAll();

    /* Bullet should be deactivated and NOT counted */
    TEST_ASSERT_EQ(pool[0].active, ENTITY_INACTIVE, "Off-screen bullet deactivated");
    TEST_ASSERT_EQ(g_bullet_active_count, 0, "Off-screen bullet not in active count");
}

/*--- Test bulletRenderAll with zero active count exercises computed OAM path (R5#3) ---*/
TEST(test_bullet_render_zero_computed_oam)
{
    bulletInit();
    bulletUpdateAll();
    TEST_ASSERT_EQ(g_bullet_active_count, 0, "No bullets active");

    /* This exercises the computed OAM ID path (no struct stride).
     * Should not crash and should hide all bullet OAM slots. */
    bulletRenderAll();
    TEST_ASSERT(1, "Zero-count render with computed OAM IDs runs OK");
}

/*--- Test compile-time constant HALF_SPEED_AIMED (improvement R5#20) ---*/
TEST(test_half_speed_aimed_constant)
{
    s16 expected;
    expected = SPEED_ENEMY_AIMED >> 1;  /* Runtime computation */
    TEST_ASSERT_EQ(HALF_SPEED_AIMED, expected,
                   "HALF_SPEED_AIMED equals SPEED_ENEMY_AIMED >> 1");
    TEST_ASSERT_EQ(HALF_SPEED_AIMED, (s16)0x00C0,
                   "HALF_SPEED_AIMED == 0x00C0 (192)");
}

/*--- Test bulletEnemyFire produces reasonable velocity for known dx/dy (#107) ---*/
TEST(test_bullet_enemy_fire_shift_approx)
{
    u8 i;
    u8 found;
    Bullet *pool;
    s16 vx_val, vy_val;

    /* Fire aimed bullet from (50,50) toward (150,200):
     * dx=100, dy=150, max_d=150. Bullet should move right and down. */
    bulletInit();
    bulletEnemyFire(50, 50, 150, 200, BULLET_TYPE_ENEMY_AIMED);

    pool = bulletGetPool();
    found = 0;
    vx_val = 0;
    vy_val = 0;
    for (i = MAX_PLAYER_BULLETS; i < MAX_BULLETS; i++) {
        if (pool[i].active == ENTITY_ACTIVE) {
            found = 1;
            vx_val = pool[i].vx;
            vy_val = pool[i].vy;
            break;
        }
    }
    TEST_ASSERT_EQ(found, 1, "Aimed bullet spawned");
    TEST_ASSERT(vx_val > 0, "Aimed bullet vx positive (target is right)");
    TEST_ASSERT(vy_val > 0, "Aimed bullet vy positive (target is below)");

    /* Fire aimed bullet from (200,200) toward (50,50):
     * dx=-150, dy=-150. Bullet should move left and up. */
    bulletInit();
    bulletEnemyFire(200, 200, 50, 50, BULLET_TYPE_ENEMY_AIMED);

    pool = bulletGetPool();
    found = 0;
    for (i = MAX_PLAYER_BULLETS; i < MAX_BULLETS; i++) {
        if (pool[i].active == ENTITY_ACTIVE) {
            found = 1;
            vx_val = pool[i].vx;
            vy_val = pool[i].vy;
            break;
        }
    }
    TEST_ASSERT_EQ(found, 1, "Reverse aimed bullet spawned");
    TEST_ASSERT(vx_val < 0, "Reverse aimed bullet vx negative (target is left)");
    TEST_ASSERT(vy_val < 0, "Reverse aimed bullet vy negative (target is above)");
}

/*--- Test additive OAM stride path with 0 active bullets (#112) ---*/
TEST(test_bullet_render_additive_stride)
{
    bulletInit();
    bulletUpdateAll();
    TEST_ASSERT_EQ(g_bullet_active_count, 0, "No bullets active for stride test");

    /* Exercise the additive stride do-while path; should not crash */
    bulletRenderAll();
    TEST_ASSERT(1, "Additive stride OAM zero-path runs without crash");

    /* Fire one bullet, render again to verify normal path still works */
    bulletPlayerFire(100, 100);
    bulletUpdateAll();
    bulletRenderAll();
    TEST_ASSERT(1, "Normal render path after stride test runs OK");
}

/*--- Test weapon type field access for battle ATK bonus (#123 regression) ---*/
TEST(test_bullet_weapon_type_field)
{
    /* Regression: battle.c #123 used g_weapon.type instead of g_weapon.weapon_type.
     * This test ensures the weapon_type field is accessible and weapon constants
     * are distinct values that battle.c can branch on. */
    bulletInit();
    TEST_ASSERT_EQ(g_weapon.weapon_type, WEAPON_SINGLE, "Init weapon_type = SINGLE");

    g_weapon.weapon_type = WEAPON_SINGLE;
    TEST_ASSERT_EQ(g_weapon.weapon_type, 0, "WEAPON_SINGLE = 0");

    g_weapon.weapon_type = WEAPON_SPREAD;
    TEST_ASSERT_EQ(g_weapon.weapon_type, 1, "WEAPON_SPREAD = 1");

    g_weapon.weapon_type = WEAPON_LASER;
    TEST_ASSERT_EQ(g_weapon.weapon_type, 2, "WEAPON_LASER = 2");

    /* Verify constants are distinct (battle.c branches on these) */
    TEST_ASSERT(WEAPON_SINGLE != WEAPON_SPREAD, "SINGLE != SPREAD");
    TEST_ASSERT(WEAPON_SINGLE != WEAPON_LASER, "SINGLE != LASER");
    TEST_ASSERT(WEAPON_SPREAD != WEAPON_LASER, "SPREAD != LASER");

    /* Reset for subsequent tests */
    bulletInit();
}

/*--- Test #150: Weapon mastery bonus thresholds ---*/
TEST(test_bullet_mastery_bonus)
{
    /* Reset weapon kills */
    g_weapon_kills[0] = 0;
    g_weapon_kills[1] = 0;
    g_weapon_kills[2] = 0;

    /* < 10 kills: bonus = 0 */
    TEST_ASSERT_EQ(bulletGetMasteryBonus(WEAPON_SINGLE), 0, "#150: 0 kills = 0 bonus");

    g_weapon_kills[0] = 9;
    TEST_ASSERT_EQ(bulletGetMasteryBonus(WEAPON_SINGLE), 0, "#150: 9 kills = 0 bonus");

    /* 10 kills: bonus = 1 */
    g_weapon_kills[0] = 10;
    TEST_ASSERT_EQ(bulletGetMasteryBonus(WEAPON_SINGLE), 1, "#150: 10 kills = 1 bonus");

    /* 24 kills: still bonus = 1 */
    g_weapon_kills[0] = 24;
    TEST_ASSERT_EQ(bulletGetMasteryBonus(WEAPON_SINGLE), 1, "#150: 24 kills = 1 bonus");

    /* 25 kills: bonus = 2 */
    g_weapon_kills[0] = 25;
    TEST_ASSERT_EQ(bulletGetMasteryBonus(WEAPON_SINGLE), 2, "#150: 25 kills = 2 bonus");

    /* 49 kills: still bonus = 2 */
    g_weapon_kills[0] = 49;
    TEST_ASSERT_EQ(bulletGetMasteryBonus(WEAPON_SINGLE), 2, "#150: 49 kills = 2 bonus");

    /* 50 kills: bonus = 3 */
    g_weapon_kills[0] = 50;
    TEST_ASSERT_EQ(bulletGetMasteryBonus(WEAPON_SINGLE), 3, "#150: 50 kills = 3 bonus");

    /* 100 kills: still bonus = 3 (max) */
    g_weapon_kills[0] = 100;
    TEST_ASSERT_EQ(bulletGetMasteryBonus(WEAPON_SINGLE), 3, "#150: 100 kills = 3 bonus (max)");

    /* Invalid weapon type */
    TEST_ASSERT_EQ(bulletGetMasteryBonus(WEAPON_COUNT), 0, "#150: Invalid type = 0 bonus");

    /* Each weapon type tracks independently */
    g_weapon_kills[0] = 50;  /* SINGLE: 50 kills */
    g_weapon_kills[1] = 10;  /* SPREAD: 10 kills */
    g_weapon_kills[2] = 0;   /* LASER: 0 kills */
    TEST_ASSERT_EQ(bulletGetMasteryBonus(WEAPON_SINGLE), 3, "#150: Single mastery independent");
    TEST_ASSERT_EQ(bulletGetMasteryBonus(WEAPON_SPREAD), 1, "#150: Spread mastery independent");
    TEST_ASSERT_EQ(bulletGetMasteryBonus(WEAPON_LASER), 0, "#150: Laser mastery independent");

    /* Clean up */
    g_weapon_kills[0] = 0;
    g_weapon_kills[1] = 0;
    g_weapon_kills[2] = 0;
}

/*--- Test #150: bulletAddWeaponKill ---*/
TEST(test_bullet_add_weapon_kill)
{
    bulletInit();
    g_weapon_kills[0] = 0;
    g_weapon_kills[1] = 0;
    g_weapon_kills[2] = 0;

    /* Single weapon active, add kill */
    g_weapon.weapon_type = WEAPON_SINGLE;
    bulletAddWeaponKill();
    TEST_ASSERT_EQ(g_weapon_kills[0], 1, "#150: SINGLE kill incremented");
    TEST_ASSERT_EQ(g_weapon_kills[1], 0, "#150: SPREAD unchanged");
    TEST_ASSERT_EQ(g_weapon_kills[2], 0, "#150: LASER unchanged");

    /* Switch to spread and add kills */
    g_weapon.weapon_type = WEAPON_SPREAD;
    bulletAddWeaponKill();
    bulletAddWeaponKill();
    TEST_ASSERT_EQ(g_weapon_kills[1], 2, "#150: SPREAD kills = 2");

    /* Kills saturate at 0xFFFF */
    g_weapon_kills[2] = 0xFFFF;
    g_weapon.weapon_type = WEAPON_LASER;
    bulletAddWeaponKill();
    TEST_ASSERT_EQ(g_weapon_kills[2], 0xFFFF, "#150: Kill count saturates at 0xFFFF");

    /* Clean up */
    g_weapon_kills[0] = 0;
    g_weapon_kills[1] = 0;
    g_weapon_kills[2] = 0;
}

/*--- Test #150: Mastery bonus applied to bullet damage ---*/
TEST(test_bullet_mastery_damage)
{
    Bullet *pool;
    bulletInit();
    g_weapon_kills[0] = 50;  /* Max mastery for SINGLE */
    g_weapon.weapon_type = WEAPON_SINGLE;

    bulletPlayerFire(100, 100);
    pool = bulletGetPool();

    /* SINGLE base = 10, mastery bonus = 3, expected = 13 */
    TEST_ASSERT_EQ(pool[0].damage, 13, "#150: SINGLE damage includes mastery bonus");

    /* Clean up */
    g_weapon_kills[0] = 0;
    bulletInit();
}

/*--- Test #151: Rapid fire momentum ---*/
TEST(test_bullet_rapid_fire_momentum)
{
    bulletInit();
    TEST_ASSERT_EQ(g_fire_hold_frames, 0, "#151: Hold frames init = 0");

    /* Fire increments hold frames */
    bulletPlayerFire(100, 100);
    TEST_ASSERT_EQ(g_fire_hold_frames, 1, "#151: Hold frames = 1 after first fire");

    /* Wait for cooldown, fire again */
    g_weapon.fire_cooldown = 0;
    bulletPlayerFire(100, 100);
    TEST_ASSERT_EQ(g_fire_hold_frames, 2, "#151: Hold frames = 2 after second fire");

    /* Reset momentum */
    bulletResetMomentum();
    TEST_ASSERT_EQ(g_fire_hold_frames, 0, "#151: Hold frames = 0 after reset");
}

/*--- Test #151: Momentum cooldown reduction ---*/
TEST(test_bullet_momentum_cooldown)
{
    u8 normal_cooldown;
    u8 momentum_cooldown;

    bulletInit();
    g_weapon_kills[0] = 0;  /* No mastery bonus */

    /* Fire without momentum (hold < 30) to get normal cooldown */
    g_fire_hold_frames = 0;
    bulletPlayerFire(100, 100);
    normal_cooldown = g_weapon.fire_cooldown;
    TEST_ASSERT_GT(normal_cooldown, 0, "#151: Normal cooldown > 0");

    /* Fire with momentum (hold > 30) to get reduced cooldown */
    bulletInit();
    g_fire_hold_frames = 31;
    g_weapon_kills[0] = 0;
    bulletPlayerFire(100, 100);
    momentum_cooldown = g_weapon.fire_cooldown;

    /* Momentum cooldown should be ~75% of normal (cooldown -= cooldown>>2) */
    TEST_ASSERT(momentum_cooldown < normal_cooldown, "#151: Momentum reduces cooldown");
    TEST_ASSERT_EQ(momentum_cooldown, normal_cooldown - (normal_cooldown >> 2),
                   "#151: Cooldown reduced by 25%");
}

/*--- Test #151: Hold frames cap at 255 ---*/
TEST(test_bullet_hold_frames_cap)
{
    bulletInit();
    g_fire_hold_frames = 254;
    g_weapon.fire_cooldown = 0;
    bulletPlayerFire(100, 100);
    TEST_ASSERT_EQ(g_fire_hold_frames, 255, "#151: Hold frames = 255 from 254");

    /* At 255, should not overflow */
    g_weapon.fire_cooldown = 0;
    bulletPlayerFire(100, 100);
    TEST_ASSERT_EQ(g_fire_hold_frames, 255, "#151: Hold frames capped at 255");
}

/*--- Test #226: Laser fire rate buffed to 13 ---*/
TEST(test_bullet_laser_fire_rate)
{
    TEST_ASSERT_EQ(FIRE_RATE_LASER, 13, "Laser fire rate = 13 frames (#226)");
    TEST_ASSERT_GT(FIRE_RATE_SINGLE + FIRE_RATE_SPREAD, FIRE_RATE_LASER, "Laser rate sane");
}

void run_bullet_tests(void)
{
    TEST_SUITE("Bullet System");
    test_bullet_init();
    test_bullet_weapon_cycle();
    test_bullet_prev_weapon();
    test_bullet_player_fire();
    test_bullet_fire_cooldown();
    test_bullet_spread();
    test_bullet_update();
    test_bullet_despawn();
    test_bullet_clear_all();
    test_bullet_enemy_fire_down();
    test_bullet_enemy_aimed();
    test_bullet_oam_slots();
    test_bullet_vx_zero_fastpath();
    test_bullet_spread_vx_moves();
    test_bullet_enemy_straight_down();
    test_bullet_active_count();
    test_bullet_bounds_edge_values();
    test_bullet_render_idle_skip();
    test_bullet_vx_zero_branchless();
    test_bullet_offscreen_not_counted();
    test_bullet_render_zero_computed_oam();
    test_half_speed_aimed_constant();
    test_bullet_enemy_fire_shift_approx();
    test_bullet_render_additive_stride();
    test_bullet_weapon_type_field();
    test_bullet_mastery_bonus();
    test_bullet_add_weapon_kill();
    test_bullet_mastery_damage();
    test_bullet_rapid_fire_momentum();
    test_bullet_momentum_cooldown();
    test_bullet_hold_frames_cap();
    test_bullet_laser_fire_rate();
}
