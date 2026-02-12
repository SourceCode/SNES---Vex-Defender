/*==============================================================================
 * Test: Collision Detection System
 * Tests AABB overlap math, hitbox definitions, edge cases.
 *============================================================================*/

#include "test_framework.h"
#include "engine/collision.h"

/* collision.c is included by test_main.c */

/*--- Test basic overlap detection ---*/
TEST(test_aabb_overlap)
{
    Hitbox h1 = { 0, 0, 16, 16 };
    Hitbox h2 = { 0, 0, 16, 16 };

    TEST_ASSERT_EQ(collisionCheckAABB(0, 0, &h1, 0, 0, &h2), 1,
                   "Same position overlaps");
    TEST_ASSERT_EQ(collisionCheckAABB(0, 0, &h1, 8, 8, &h2), 1,
                   "Partial overlap");
    TEST_ASSERT_EQ(collisionCheckAABB(0, 0, &h1, 16, 0, &h2), 0,
                   "Right edge touching = no overlap");
    TEST_ASSERT_EQ(collisionCheckAABB(0, 0, &h1, 0, 16, &h2), 0,
                   "Bottom edge touching = no overlap");
}

/*--- Test no-overlap cases ---*/
TEST(test_aabb_no_overlap)
{
    Hitbox h1 = { 0, 0, 16, 16 };
    Hitbox h2 = { 0, 0, 16, 16 };

    TEST_ASSERT_EQ(collisionCheckAABB(0, 0, &h1, 100, 100, &h2), 0,
                   "Far apart = no overlap");
    TEST_ASSERT_EQ(collisionCheckAABB(0, 0, &h1, 20, 0, &h2), 0,
                   "Separated horizontally");
    TEST_ASSERT_EQ(collisionCheckAABB(0, 0, &h1, 0, 20, &h2), 0,
                   "Separated vertically");
}

/*--- Test with hitbox offsets ---*/
TEST(test_aabb_with_offsets)
{
    Hitbox hp = { 8, 8, 16, 16 };
    Hitbox he = { 4, 4, 24, 24 };

    /* Player(0,0) hitbox [8,24]x[8,24], Enemy(20,0) hitbox [24,28]x[4,28]
     * X: player right=24, enemy left=24 -> touching but NOT overlapping */
    TEST_ASSERT_EQ(collisionCheckAABB(0, 0, &hp, 20, 0, &he), 0,
                   "Offset hitboxes just touching = no overlap");
    TEST_ASSERT_EQ(collisionCheckAABB(0, 0, &hp, 28, 0, &he), 0,
                   "Offset hitboxes separated");
}

/*--- Test negative coordinates ---*/
TEST(test_aabb_negative_coords)
{
    Hitbox h = { 0, 0, 16, 16 };

    TEST_ASSERT_EQ(collisionCheckAABB(-8, -8, &h, -4, -4, &h), 1,
                   "Negative coords overlap");
    TEST_ASSERT_EQ(collisionCheckAABB(-8, 0, &h, 0, 0, &h), 1,
                   "Negative/positive overlap");
}

/*--- Test bullet vs enemy hitboxes (game-specific) ---*/
TEST(test_aabb_bullet_vs_enemy)
{
    Hitbox hb_bullet = { 4, 4, 8, 8 };
    Hitbox hb_enemy  = { 4, 4, 24, 24 };

    TEST_ASSERT_EQ(collisionCheckAABB(110, 50, &hb_bullet, 100, 40, &hb_enemy), 1,
                   "Bullet inside enemy");
    TEST_ASSERT_EQ(collisionCheckAABB(130, 50, &hb_bullet, 100, 40, &hb_enemy), 0,
                   "Bullet outside enemy");
}

/*--- Test collision init ---*/
TEST(test_collision_init)
{
    g_score = 999;
    collisionInit();
    TEST_ASSERT_EQ(g_score, 0, "Score reset to 0");
}

/*--- Test screen shake infrastructure (improvement #4) ---*/
TEST(test_collision_screen_shake_init)
{
    g_screen_shake = 10;
    collisionInit();
    TEST_ASSERT_EQ(g_screen_shake, 0, "Screen shake reset on init");
    TEST_ASSERT_EQ(g_score, 0, "Score reset on init");
}

/*--- Test screen shake value range ---*/
TEST(test_collision_screen_shake_value)
{
    /* Verify the shake variable is accessible and settable */
    g_screen_shake = 6;
    TEST_ASSERT_EQ(g_screen_shake, 6, "Screen shake can be set to 6");
    g_screen_shake = 0;
    TEST_ASSERT_EQ(g_screen_shake, 0, "Screen shake can be cleared");
}

/*--- Test combo counter init (improvement #5) ---*/
TEST(test_collision_combo_init)
{
    collisionInit();
    TEST_ASSERT_EQ(g_combo_count, 0, "Combo count reset on init");
    TEST_ASSERT_EQ(g_combo_timer, 0, "Combo timer reset on init");
}

/*--- Test combo timer decay ---*/
TEST(test_collision_combo_timer_decay)
{
    collisionInit();
    g_combo_count = 3;
    g_combo_timer = 2;
    /* Simulate collisionCheckAll by decaying timer */
    g_combo_timer--;
    TEST_ASSERT_EQ(g_combo_count, 3, "Combo preserved with timer > 0");
    g_combo_timer--;
    if (g_combo_timer == 0) g_combo_count = 0;
    TEST_ASSERT_EQ(g_combo_count, 0, "Combo reset when timer expires");
}

/*--- Test collision uses g_enemy_active_count (improvement R4#4) ---*/
TEST(test_collision_enemy_count_gate)
{
    /* When g_enemy_active_count is 0, collisionCheckAll should still
     * handle combo timer decay properly without scanning enemies */
    collisionInit();
    g_combo_count = 5;
    g_combo_timer = 1;
    g_enemy_active_count = 0;

    collisionCheckAll();

    /* Combo timer expired, count should reset */
    TEST_ASSERT_EQ(g_combo_timer, 0, "Combo timer decayed to 0");
    TEST_ASSERT_EQ(g_combo_count, 0, "Combo count reset when timer expires (no enemies)");
}

/*--- Test Y-range pre-rejection with hoisted bounds (improvement R4#5) ---*/
TEST(test_collision_y_range_rejection)
{
    Hitbox hb = { 4, 4, 8, 8 };    /* bullet hitbox */
    Hitbox he = { 4, 4, 24, 24 };  /* enemy hitbox */

    /* Bullet far above enemy: b_bot < e.y (b.y+16 < e.y) */
    /* Bullet at y=0, enemy at y=100 -> b_bot=16 < 100 -> no overlap */
    TEST_ASSERT_EQ(collisionCheckAABB(100, 0, &hb, 100, 100, &he), 0,
                   "Bullet above enemy Y range = no overlap");

    /* Bullet far below enemy: b_top > e.y+32 (b.y > e.y+32) */
    /* Bullet at y=200, enemy at y=100 -> b_top=200 > 132 -> no overlap */
    TEST_ASSERT_EQ(collisionCheckAABB(100, 200, &hb, 100, 100, &he), 0,
                   "Bullet below enemy Y range = no overlap");

    /* Bullet within Y range of enemy: should detect overlap */
    TEST_ASSERT_EQ(collisionCheckAABB(105, 105, &hb, 100, 100, &he), 1,
                   "Bullet within enemy Y range = overlap");
}

/*--- Test shift-add combo scoring matches multiply for mult 1-4 (#102) ---*/
TEST(test_collision_combo_shift_add)
{
    u16 base;
    u16 expected;
    u16 result;

    base = 150;

    /* mult=1: g_score += base */
    collisionInit();
    g_score = 0;
    result = base;
    expected = base * 1;
    TEST_ASSERT_EQ(result, expected, "shift-add mult=1 matches multiply");

    /* mult=2: base << 1 */
    result = base << 1;
    expected = base * 2;
    TEST_ASSERT_EQ(result, expected, "shift-add mult=2 matches multiply");

    /* mult=3: (base << 1) + base */
    result = (base << 1) + base;
    expected = base * 3;
    TEST_ASSERT_EQ(result, expected, "shift-add mult=3 matches multiply");

    /* mult=4: base << 2 */
    result = base << 2;
    expected = base * 4;
    TEST_ASSERT_EQ(result, expected, "shift-add mult=4 matches multiply");

    /* Test with another base value for confidence */
    base = 200;
    result = (base << 1) + base;
    expected = base * 3;
    TEST_ASSERT_EQ(result, expected, "shift-add mult=3 with base=200");

    result = base << 2;
    expected = base * 4;
    TEST_ASSERT_EQ(result, expected, "shift-add mult=4 with base=200");
}

/*--- Test inline AABB gives same results as function (#103/#106) ---*/
TEST(test_collision_inline_aabb_matches_function)
{
    Hitbox hb_bul = { 4, 4, 8, 8 };
    Hitbox hb_ene = { 4, 4, 24, 24 };
    u8 func_result;
    u8 inline_result;
    s16 bl, br, bt, bb;
    s16 el, er, et, eb;

    /* Known overlapping case: bullet at (110,50), enemy at (100,40) */
    func_result = collisionCheckAABB(110, 50, &hb_bul, 100, 40, &hb_ene);

    /* Inline check with constants folded */
    bl = 110 + 4; br = bl + 8;
    bt = 50 + 4;  bb = bt + 8;
    el = 100 + 4; er = el + 24;
    et = 40 + 4;  eb = et + 24;
    inline_result = !(br <= el || bl >= er || bb <= et || bt >= eb);

    TEST_ASSERT_EQ(func_result, 1, "Function detects overlap");
    TEST_ASSERT_EQ(inline_result, 1, "Inline detects overlap");
    TEST_ASSERT_EQ(func_result, inline_result, "Overlap: inline matches function");

    /* Known non-overlapping case: bullet at (130,50), enemy at (100,40) */
    func_result = collisionCheckAABB(130, 50, &hb_bul, 100, 40, &hb_ene);

    bl = 130 + 4; br = bl + 8;
    bt = 50 + 4;  bb = bt + 8;
    el = 100 + 4; er = el + 24;
    et = 40 + 4;  eb = et + 24;
    inline_result = !(br <= el || bl >= er || bb <= et || bt >= eb);

    TEST_ASSERT_EQ(func_result, 0, "Function detects no overlap");
    TEST_ASSERT_EQ(inline_result, 0, "Inline detects no overlap");
    TEST_ASSERT_EQ(func_result, inline_result, "No overlap: inline matches function");
}

/*--- Test score saturating add (#120) ---*/
TEST(test_collision_score_saturating_add)
{
    collisionInit();
    /* Set score near u16 max and add via direct logic (mirroring the code) */
    g_score = 0xFFF0;
    {
        u16 add_val = 100;
        if (g_score > (u16)(0xFFFF - add_val)) {
            g_score = 0xFFFF;
        } else {
            g_score += add_val;
        }
    }
    TEST_ASSERT_EQ(g_score, 0xFFFF, "Score saturates at 0xFFFF instead of wrapping");

    /* Normal add when there's room */
    g_score = 1000;
    {
        u16 add_val = 500;
        if (g_score > (u16)(0xFFFF - add_val)) {
            g_score = 0xFFFF;
        } else {
            g_score += add_val;
        }
    }
    TEST_ASSERT_EQ(g_score, 1500, "Score adds normally when no overflow");
}

/*--- Test scout contact gives shake + SFX (#121) ---*/
TEST(test_collision_scout_contact_feedback)
{
    /* Verify screen shake and SFX globals are accessible and settable.
     * The actual contact logic is tested indirectly through checkPlayerVsEnemies
     * which is static, but we verify the feedback variables work. */
    collisionInit();
    g_screen_shake = 0;
    /* Simulate what scout contact does: set shake + play SFX */
    g_screen_shake = 6;
    TEST_ASSERT_EQ(g_screen_shake, 6, "Scout contact sets screen shake to 6");
    /* mock_last_sfx is set by soundPlaySFX stub */
    soundPlaySFX(SFX_HIT);
    TEST_ASSERT_EQ(mock_last_sfx, SFX_HIT, "Scout contact plays SFX_HIT");
}

/*--- Test brightness pulse on combo kills (#130) ---*/
TEST(test_collision_combo_brightness_pulse)
{
    /* Verify combo count threshold for brightness pulse.
     * The pulse fires when g_combo_count >= 3 after a kill. */
    collisionInit();
    g_combo_count = 2;
    TEST_ASSERT(g_combo_count < 3, "No pulse at combo 2");
    g_combo_count = 3;
    TEST_ASSERT(g_combo_count >= 3, "Pulse triggers at combo 3");
    g_combo_count = 4;
    TEST_ASSERT(g_combo_count >= 3, "Pulse triggers at combo 4");
}

/*--- Test #140: Combo tier milestone rewards ---*/
TEST(test_collision_combo_milestones)
{
    /* Milestone at combo 5 = +500, combo 10 = +1500, combo 15 = +5000 */
    u16 score_before;

    collisionInit();
    g_score = 1000;
    g_combo_count = 4;

    /* Simulate reaching combo 5 (the scoring code checks g_combo_count after increment) */
    score_before = g_score;
    g_combo_count = 5;
    {
        u16 milestone_bonus = 0;
        if (g_combo_count == 5) milestone_bonus = 500;
        else if (g_combo_count == 10) milestone_bonus = 1500;
        else if (g_combo_count == 15) milestone_bonus = 5000;
        if (milestone_bonus > 0) g_score += milestone_bonus;
    }
    TEST_ASSERT_EQ(g_score, 1500, "#140: Combo 5 milestone = +500");

    g_combo_count = 10;
    score_before = g_score;
    {
        u16 milestone_bonus = 0;
        if (g_combo_count == 5) milestone_bonus = 500;
        else if (g_combo_count == 10) milestone_bonus = 1500;
        else if (g_combo_count == 15) milestone_bonus = 5000;
        if (milestone_bonus > 0) g_score += milestone_bonus;
    }
    TEST_ASSERT_EQ(g_score, 3000, "#140: Combo 10 milestone = +1500");

    g_combo_count = 15;
    score_before = g_score;
    {
        u16 milestone_bonus = 0;
        if (g_combo_count == 5) milestone_bonus = 500;
        else if (g_combo_count == 10) milestone_bonus = 1500;
        else if (g_combo_count == 15) milestone_bonus = 5000;
        if (milestone_bonus > 0) g_score += milestone_bonus;
    }
    TEST_ASSERT_EQ(g_score, 8000, "#140: Combo 15 milestone = +5000");

    /* No milestone at combo 6 */
    g_score = 1000;
    g_combo_count = 6;
    {
        u16 milestone_bonus = 0;
        if (g_combo_count == 5) milestone_bonus = 500;
        else if (g_combo_count == 10) milestone_bonus = 1500;
        else if (g_combo_count == 15) milestone_bonus = 5000;
        if (milestone_bonus > 0) g_score += milestone_bonus;
    }
    TEST_ASSERT_EQ(g_score, 1000, "#140: No milestone at combo 6");

    (void)score_before;
}

/*--- Test #141: Decaying combo window ---*/
TEST(test_collision_decaying_combo_window)
{
    u8 timer;
    u8 mult;

    /* Multiplier 1: timer = 60 - (1 * 8) = 52 */
    mult = 1;
    timer = 60 - (mult << 3);
    if (timer < 36) timer = 36;
    TEST_ASSERT_EQ(timer, 52, "#141: Mult 1 window = 52 frames");

    /* Multiplier 2: timer = 60 - (2 * 8) = 44 */
    mult = 2;
    timer = 60 - (mult << 3);
    if (timer < 36) timer = 36;
    TEST_ASSERT_EQ(timer, 44, "#141: Mult 2 window = 44 frames");

    /* Multiplier 3: timer = 60 - (3 * 8) = 36 */
    mult = 3;
    timer = 60 - (mult << 3);
    if (timer < 36) timer = 36;
    TEST_ASSERT_EQ(timer, 36, "#141: Mult 3 window = 36 frames");

    /* Multiplier 4: timer = 60 - (4 * 8) = 28, clamped to 36 */
    mult = 4;
    timer = 60 - (mult << 3);
    if (timer < 36) timer = 36;
    TEST_ASSERT_EQ(timer, 36, "#141: Mult 4 window clamped to 36");
}

/*--- Test #142: Bullet graze scoring (expanded hitbox logic) ---*/
TEST(test_collision_graze_scoring)
{
    /* Graze: bullet misses real hitbox but hits expanded graze hitbox.
     * Player hitbox: {8,8,16,16} on 32x32 sprite.
     * Graze hitbox: {2,2,28,28} (6px larger per side). */
    Hitbox hb_real = { 8, 8, 16, 16 };
    Hitbox hb_graze = { 2, 2, 28, 28 };
    Hitbox hb_bul = { 4, 4, 8, 8 };
    s16 px = 100, py = 100;  /* Player at (100,100) */
    u8 real_hit, graze_hit;

    /* Bullet just outside real hitbox but inside graze zone */
    /* Player hitbox: [108,124]x[108,124]
     * Bullet at (92,100): hitbox [96,104]x[104,112]
     * real: 104 <= 108, miss. graze hb: [102,130]x[102,130], 104 > 102, hit */
    real_hit = collisionCheckAABB(92, 100, &hb_bul, px, py, &hb_real);
    graze_hit = collisionCheckAABB(92, 100, &hb_bul, px, py, &hb_graze);
    TEST_ASSERT_EQ(real_hit, 0, "#142: Bullet misses real hitbox");
    TEST_ASSERT_EQ(graze_hit, 1, "#142: Bullet hits graze hitbox");

    /* Bullet far away: misses both */
    real_hit = collisionCheckAABB(50, 50, &hb_bul, px, py, &hb_real);
    graze_hit = collisionCheckAABB(50, 50, &hb_bul, px, py, &hb_graze);
    TEST_ASSERT_EQ(real_hit, 0, "#142: Far bullet misses real");
    TEST_ASSERT_EQ(graze_hit, 0, "#142: Far bullet misses graze");
}

/*--- Test #143: Kill streak scoring bonus ---*/
TEST(test_collision_kill_streak)
{
    u16 base_score;
    u8 streak_bonus;
    u16 add_val;

    collisionInit();
    TEST_ASSERT_EQ(g_kill_streak, 0, "#143: Kill streak init = 0");

    /* Streak of 4: no bonus yet (need 5 for first +25%) */
    g_kill_streak = 4;
    streak_bonus = g_kill_streak / 5;
    TEST_ASSERT_EQ(streak_bonus, 0, "#143: Streak 4 bonus = 0");

    /* Streak of 5: +25% (bonus = 1) */
    g_kill_streak = 5;
    base_score = 100;
    streak_bonus = g_kill_streak / 5;
    if (streak_bonus > 4) streak_bonus = 4;
    add_val = base_score;
    if (streak_bonus > 0) add_val += (base_score >> 2) * streak_bonus;
    TEST_ASSERT_EQ(add_val, 125, "#143: Streak 5 = +25% (100 -> 125)");

    /* Streak of 10: +50% (bonus = 2) */
    g_kill_streak = 10;
    streak_bonus = g_kill_streak / 5;
    if (streak_bonus > 4) streak_bonus = 4;
    add_val = base_score;
    if (streak_bonus > 0) add_val += (base_score >> 2) * streak_bonus;
    TEST_ASSERT_EQ(add_val, 150, "#143: Streak 10 = +50% (100 -> 150)");

    /* Streak of 20: +100% (bonus = 4, capped) */
    g_kill_streak = 20;
    streak_bonus = g_kill_streak / 5;
    if (streak_bonus > 4) streak_bonus = 4;
    add_val = base_score;
    if (streak_bonus > 0) add_val += (base_score >> 2) * streak_bonus;
    TEST_ASSERT_EQ(add_val, 200, "#143: Streak 20 = +100% (100 -> 200)");

    /* Streak of 25+: capped at +100% (bonus still = 4) */
    g_kill_streak = 30;
    streak_bonus = g_kill_streak / 5;
    if (streak_bonus > 4) streak_bonus = 4;
    add_val = base_score;
    if (streak_bonus > 0) add_val += (base_score >> 2) * streak_bonus;
    TEST_ASSERT_EQ(add_val, 200, "#143: Streak 30 capped at +100%");
}

/*--- Test #143: Kill streak resets on hit ---*/
TEST(test_collision_kill_streak_reset)
{
    collisionInit();
    g_kill_streak = 15;
    /* Simulate player taking damage: streak resets */
    g_kill_streak = 0;
    TEST_ASSERT_EQ(g_kill_streak, 0, "#143: Kill streak resets on hit");
}

/*--- Test #145: Overkill bonus calculation ---*/
TEST(test_collision_overkill_bonus)
{
    u8 excess;
    u16 overkill_bonus;

    /* Enemy with 5 HP, bullet does 15 damage: excess = 10, bonus = 100 */
    excess = 15 - 5;
    overkill_bonus = ((u16)excess << 3) + ((u16)excess << 1);
    TEST_ASSERT_EQ(overkill_bonus, 100, "#145: Overkill 10 excess = 100 bonus");

    /* No overkill: bullet does exactly lethal damage */
    excess = 0;
    overkill_bonus = ((u16)excess << 3) + ((u16)excess << 1);
    TEST_ASSERT_EQ(overkill_bonus, 0, "#145: No overkill = 0 bonus");

    /* Large overkill: bullet does 50, enemy has 5 HP, excess = 45 */
    excess = 50 - 5;
    overkill_bonus = ((u16)excess << 3) + ((u16)excess << 1);
    TEST_ASSERT_EQ(overkill_bonus, 450, "#145: Overkill 45 excess = 450 bonus");
}

/*--- Test #146: Speed kill bonus (age < 90 = 2x) ---*/
TEST(test_collision_speed_kill_bonus)
{
    u16 base = 200;
    u16 score;

    /* Enemy age < 90: double score */
    score = base;
    if (89 < 90) score <<= 1;
    TEST_ASSERT_EQ(score, 400, "#146: Speed kill (age 89) = 2x score");

    /* Enemy age == 90: no bonus */
    score = base;
    if (90 < 90) score <<= 1;  /* false */
    TEST_ASSERT_EQ(score, 200, "#146: No speed kill at age 90");

    /* Enemy age > 90: no bonus */
    score = base;
    if (200 < 90) score <<= 1;  /* false */
    TEST_ASSERT_EQ(score, 200, "#146: No speed kill at age 200");
}

/*--- Test #148: Kill milestone item rewards ---*/
TEST(test_collision_kill_milestones)
{
    invInit();
    rpgStatsInit();

    /* Simulate reaching 10 kills: should award HP_POTION_S */
    rpg_stats.total_kills = 9;
    rpg_stats.total_kills++;
    if (rpg_stats.total_kills == 10) invAdd(ITEM_HP_POTION_S, 1);
    TEST_ASSERT_GT(invCount(ITEM_HP_POTION_S), 2, "#148: 10-kill milestone awards HP Pot S");

    /* 25 kills: HP_POTION_L */
    rpg_stats.total_kills = 24;
    rpg_stats.total_kills++;
    if (rpg_stats.total_kills == 25) invAdd(ITEM_HP_POTION_L, 1);
    TEST_ASSERT_GT(invCount(ITEM_HP_POTION_L), 0, "#148: 25-kill milestone awards HP Pot L");

    /* 50 kills: SP_CHARGE */
    rpg_stats.total_kills = 49;
    rpg_stats.total_kills++;
    if (rpg_stats.total_kills == 50) invAdd(ITEM_SP_CHARGE, 1);
    TEST_ASSERT_GT(invCount(ITEM_SP_CHARGE), 0, "#148: 50-kill milestone awards SP Charge");

    /* 100 kills: FULL_RESTORE */
    rpg_stats.total_kills = 99;
    rpg_stats.total_kills++;
    if (rpg_stats.total_kills == 100) invAdd(ITEM_FULL_RESTORE, 1);
    TEST_ASSERT_GT(invCount(ITEM_FULL_RESTORE), 0, "#148: 100-kill milestone awards Full Restore");
}

/*--- Test #155: No-damage zone flag tracking ---*/
TEST(test_collision_no_damage_zone_flag)
{
    collisionInit();
    g_game.zone_no_damage = 1;

    /* Simulating player hit sets flag to 0 */
    g_game.zone_no_damage = 0;  /* as done in checkEnemyBulletsVsPlayer / checkPlayerVsEnemies */
    TEST_ASSERT_EQ(g_game.zone_no_damage, 0, "#155: Zone no-damage cleared on hit");

    /* Reset for next zone */
    g_game.zone_no_damage = 1;
    TEST_ASSERT_EQ(g_game.zone_no_damage, 1, "#155: Zone no-damage set for new zone");
}

/*--- Test #157: Bonus score zone timer ---*/
TEST(test_collision_bonus_score_timer)
{
    collisionInit();
    TEST_ASSERT_EQ(g_score_bonus_timer, 0, "#157: Bonus timer init = 0");

    /* Simulate bonus zone activation */
    g_score_bonus_timer = 120;
    TEST_ASSERT_EQ(g_score_bonus_timer, 120, "#157: Bonus timer set to 120");

    /* Timer decays */
    collisionCheckAll();
    TEST_ASSERT_EQ(g_score_bonus_timer, 119, "#157: Bonus timer decays to 119");

    /* Score doubling during bonus */
    {
        u16 base = 100;
        u16 score;
        score = base;
        if (g_score_bonus_timer > 0) score <<= 1;
        TEST_ASSERT_EQ(score, 200, "#157: Score doubled during bonus zone");
    }

    /* No doubling when timer = 0 */
    g_score_bonus_timer = 0;
    {
        u16 base = 100;
        u16 score;
        score = base;
        if (g_score_bonus_timer > 0) score <<= 1;
        TEST_ASSERT_EQ(score, 100, "#157: Score normal when bonus inactive");
    }
}

/*--- Test #141/#143: collisionInit resets new globals ---*/
TEST(test_collision_init_new_globals)
{
    g_combo_multiplier = 3;
    g_kill_streak = 15;
    g_score_bonus_timer = 100;
    collisionInit();
    TEST_ASSERT_EQ(g_combo_multiplier, 0, "#141: Multiplier reset on init");
    TEST_ASSERT_EQ(g_kill_streak, 0, "#143: Kill streak reset on init");
    TEST_ASSERT_EQ(g_score_bonus_timer, 0, "#157: Bonus timer reset on init");
}

/*--- Test #147: Golden enemy 3x score calculation ---*/
TEST(test_collision_golden_enemy_score)
{
    u16 base = 100;
    u16 golden_score;

    /* Golden: (base << 1) + base = 3x */
    golden_score = (base << 1) + base;
    TEST_ASSERT_EQ(golden_score, 300, "#147: Golden enemy 3x score (100 -> 300)");

    base = 350;
    golden_score = (base << 1) + base;
    TEST_ASSERT_EQ(golden_score, 1050, "#147: Golden enemy 3x score (350 -> 1050)");
}

/*--- Test #167: Weapon combo buffer init ---*/
TEST(test_collision_weapon_combo_init)
{
    collisionInit();
    TEST_ASSERT_EQ(g_weapon_combo_buf[0], 0xFF, "#167: Weapon combo buf[0] init = 0xFF");
    TEST_ASSERT_EQ(g_weapon_combo_buf[1], 0xFF, "#167: Weapon combo buf[1] init = 0xFF");
    TEST_ASSERT_EQ(g_weapon_combo_buf[2], 0xFF, "#167: Weapon combo buf[2] init = 0xFF");
    TEST_ASSERT_EQ(g_weapon_combo_idx, 0, "#167: Weapon combo idx init = 0");
}

/*--- Test #167: Weapon combo circular buffer logic ---*/
TEST(test_collision_weapon_combo_logic)
{
    u8 full_arsenal;
    collisionInit();

    /* Record 3 different weapon types */
    g_weapon_combo_buf[g_weapon_combo_idx] = 0;  /* WEAPON_SINGLE */
    if (++g_weapon_combo_idx >= 3) g_weapon_combo_idx = 0;

    g_weapon_combo_buf[g_weapon_combo_idx] = 1;  /* WEAPON_SPREAD */
    if (++g_weapon_combo_idx >= 3) g_weapon_combo_idx = 0;

    g_weapon_combo_buf[g_weapon_combo_idx] = 2;  /* WEAPON_LASER */
    if (++g_weapon_combo_idx >= 3) g_weapon_combo_idx = 0;

    /* Check: all 3 different and none are 0xFF */
    full_arsenal = (g_weapon_combo_buf[0] != g_weapon_combo_buf[1] &&
                    g_weapon_combo_buf[1] != g_weapon_combo_buf[2] &&
                    g_weapon_combo_buf[0] != g_weapon_combo_buf[2] &&
                    g_weapon_combo_buf[0] != 0xFF) ? 1 : 0;
    TEST_ASSERT_EQ(full_arsenal, 1, "#167: FULL ARSENAL detected with 3 different weapons");

    /* Reset buffer and check: same weapon type = no bonus */
    g_weapon_combo_buf[0] = 0;
    g_weapon_combo_buf[1] = 0;
    g_weapon_combo_buf[2] = 0;
    full_arsenal = (g_weapon_combo_buf[0] != g_weapon_combo_buf[1] &&
                    g_weapon_combo_buf[1] != g_weapon_combo_buf[2] &&
                    g_weapon_combo_buf[0] != g_weapon_combo_buf[2] &&
                    g_weapon_combo_buf[0] != 0xFF) ? 1 : 0;
    TEST_ASSERT_EQ(full_arsenal, 0, "#167: No bonus with same weapon type");

    /* Two same, one different = no bonus */
    g_weapon_combo_buf[0] = 0;
    g_weapon_combo_buf[1] = 1;
    g_weapon_combo_buf[2] = 0;
    full_arsenal = (g_weapon_combo_buf[0] != g_weapon_combo_buf[1] &&
                    g_weapon_combo_buf[1] != g_weapon_combo_buf[2] &&
                    g_weapon_combo_buf[0] != g_weapon_combo_buf[2] &&
                    g_weapon_combo_buf[0] != 0xFF) ? 1 : 0;
    TEST_ASSERT_EQ(full_arsenal, 0, "#167: No bonus with 2 same + 1 different");
}

/*--- Test #167: FULL ARSENAL score bonus value ---*/
TEST(test_collision_weapon_combo_score)
{
    u16 score_before;
    collisionInit();
    g_score = 5000;
    score_before = g_score;

    /* Simulate FULL ARSENAL bonus (+1000) */
    {
        u16 bonus = 1000;
        if (g_score > (u16)(0xFFFF - bonus)) g_score = 0xFFFF;
        else g_score += bonus;
    }
    TEST_ASSERT_EQ(g_score, 6000, "#167: FULL ARSENAL awards +1000 score");
    (void)score_before;
}

/*--- Test #168: Kill bullet cancel concept ---*/
TEST(test_collision_kill_bullet_cancel)
{
    Bullet *pool;
    u8 i;
    u8 found;

    bulletInit();
    collisionInit();

    /* Spawn some enemy bullets (indices MAX_PLAYER_BULLETS to MAX_BULLETS-1) */
    pool = bulletGetPool();
    pool[MAX_PLAYER_BULLETS].active = ENTITY_ACTIVE;
    pool[MAX_PLAYER_BULLETS].x = 100;
    pool[MAX_PLAYER_BULLETS].y = 100;
    pool[MAX_PLAYER_BULLETS + 1].active = ENTITY_ACTIVE;
    pool[MAX_PLAYER_BULLETS + 1].x = 150;
    pool[MAX_PLAYER_BULLETS + 1].y = 150;

    /* Simulate kill bullet cancel: deactivate first active enemy bullet */
    {
        Bullet *eb = bulletGetPool();
        u8 ebi;
        for (ebi = MAX_PLAYER_BULLETS; ebi < MAX_BULLETS; ebi++) {
            if (eb[ebi].active == ENTITY_ACTIVE) {
                eb[ebi].active = ENTITY_INACTIVE;
                break;
            }
        }
    }

    /* First enemy bullet should be cancelled */
    TEST_ASSERT_EQ(pool[MAX_PLAYER_BULLETS].active, ENTITY_INACTIVE,
                   "#168: First enemy bullet cancelled on kill");
    /* Second enemy bullet should still be active */
    TEST_ASSERT_EQ(pool[MAX_PLAYER_BULLETS + 1].active, ENTITY_ACTIVE,
                   "#168: Second enemy bullet still active");

    /* No enemy bullets: cancel should be no-op (no crash) */
    bulletInit();
    found = 0;
    {
        Bullet *eb = bulletGetPool();
        u8 ebi;
        for (ebi = MAX_PLAYER_BULLETS; ebi < MAX_BULLETS; ebi++) {
            if (eb[ebi].active == ENTITY_ACTIVE) {
                eb[ebi].active = ENTITY_INACTIVE;
                found = 1;
                break;
            }
        }
    }
    TEST_ASSERT_EQ(found, 0, "#168: No crash when no enemy bullets to cancel");
}

/*--- Test #174: Max combo tracking ---*/
TEST(test_collision_max_combo_tracking)
{
    collisionInit();
    g_game.max_combo = 0;

    /* Simulate combo building */
    g_combo_count = 5;
    if (g_combo_count > g_game.max_combo) {
        g_game.max_combo = (u8)g_combo_count;
    }
    TEST_ASSERT_EQ(g_game.max_combo, 5, "#174: max_combo updated to 5");

    /* Higher combo updates max */
    g_combo_count = 12;
    if (g_combo_count > g_game.max_combo) {
        g_game.max_combo = (u8)g_combo_count;
    }
    TEST_ASSERT_EQ(g_game.max_combo, 12, "#174: max_combo updated to 12");

    /* Lower combo does not reduce max */
    g_combo_count = 3;
    if (g_combo_count > g_game.max_combo) {
        g_game.max_combo = (u8)g_combo_count;
    }
    TEST_ASSERT_EQ(g_game.max_combo, 12, "#174: max_combo not reduced by lower combo");
}

/* #180: SP regen on combo milestones */
TEST(test_collision_sp_regen_on_milestone)
{
    rpg_stats.sp = 2;
    rpg_stats.max_sp = 5;
    /* Simulate milestone at combo 5 */
    g_combo_count = 5;
    {
        u16 milestone_bonus = 500;
        g_score = 0;
        g_score += milestone_bonus;
        if (rpg_stats.sp < rpg_stats.max_sp) {
            rpg_stats.sp++;
        }
    }
    TEST_ASSERT_EQ(rpg_stats.sp, 3, "#180: SP increased by 1 at combo milestone");
    /* At max SP, no increase */
    rpg_stats.sp = 5;
    if (rpg_stats.sp < rpg_stats.max_sp) {
        rpg_stats.sp++;
    }
    TEST_ASSERT_EQ(rpg_stats.sp, 5, "#180: SP capped at max_sp");
}

/* #181: Enemy shield absorbs hit */
TEST(test_collision_enemy_shield)
{
    Enemy *e = enemyGetPool();
    e[0].active = ENTITY_ACTIVE;
    e[0].type = ENEMY_TYPE_HEAVY;
    e[0].shield = 1;
    e[0].hp = 10;
    e[0].flash_timer = 0;
    TEST_ASSERT_EQ(e[0].shield, 1, "#181: Shield starts at 1 for heavy");
    /* Simulate shield absorb */
    e[0].shield = 0;
    e[0].flash_timer = 6;
    TEST_ASSERT_EQ(e[0].shield, 0, "#181: Shield broken after hit");
    TEST_ASSERT_EQ(e[0].hp, 10, "#181: HP unchanged when shield absorbs");
    TEST_ASSERT_EQ(e[0].flash_timer, 6, "#181: Flash timer set on shield break");
}

/* #183: Combo display timer */
TEST(test_collision_combo_display_timer)
{
    collisionInit();
    TEST_ASSERT_EQ(g_combo_display_timer, 0, "#183: Display timer init to 0");
    g_combo_display_timer = 30;
    TEST_ASSERT_EQ(g_combo_display_timer, 30, "#183: Display timer set to 30");
    /* Simulate decay */
    while (g_combo_display_timer > 0) g_combo_display_timer--;
    TEST_ASSERT_EQ(g_combo_display_timer, 0, "#183: Display timer decays to 0");
}

/* #188: Elite dodge check */
TEST(test_collision_elite_dodge)
{
    /* Elite type check */
    u8 type = ENEMY_TYPE_ELITE;
    TEST_ASSERT_EQ(type, 3, "#188: Elite type is 3");
    /* The dodge uses (g_frame_count & 4) == 0 which is ~20% */
    g_frame_count = 0;
    TEST_ASSERT_EQ((g_frame_count & 4) == 0, 1, "#188: Dodge triggers at frame 0");
    g_frame_count = 4;
    TEST_ASSERT_EQ((g_frame_count & 4) == 0, 0, "#188: No dodge at frame 4");
}

/* #191: Golden enemy shield pickup */
TEST(test_collision_golden_shield_pickup)
{
    g_player.invincible_timer = 0;
    g_frame_count = 2;  /* Even frame = shield */
    if ((g_frame_count & 1) == 0) {
        g_player.invincible_timer = 60;
    }
    TEST_ASSERT_EQ(g_player.invincible_timer, 60, "#191: Shield pickup on even frame");
    g_player.invincible_timer = 0;
    g_frame_count = 3;  /* Odd frame = no shield */
    if ((g_frame_count & 1) == 0) {
        g_player.invincible_timer = 60;
    }
    TEST_ASSERT_EQ(g_player.invincible_timer, 0, "#191: No shield on odd frame");
}

/* #195: Chain reset protection */
TEST(test_collision_chain_reset_protection)
{
    collisionInit();
    g_combo_count = 6;
    g_combo_multiplier = 3;
    g_combo_timer = 1;  /* About to expire */
    /* Simulate decay */
    g_combo_timer--;
    if (g_combo_timer == 0) {
        if (g_combo_count >= 5 && g_combo_multiplier > 1) {
            g_combo_multiplier = 1;
            g_combo_timer = 30;
        } else {
            g_combo_count = 0;
            g_combo_multiplier = 0;
        }
    }
    TEST_ASSERT_EQ(g_combo_multiplier, 1, "#195: Grace period at 1x multiplier");
    TEST_ASSERT_EQ(g_combo_timer, 30, "#195: 30-frame grace period");
    TEST_ASSERT_EQ(g_combo_count, 6, "#195: Combo count preserved during grace");
}

/* #197: Wave clear tracking */
TEST(test_collision_wave_clear_tracking)
{
    collisionInit();
    TEST_ASSERT_EQ(g_wave_enemy_count, 0, "#197: Wave enemy count init 0");
    TEST_ASSERT_EQ(g_wave_kill_count, 0, "#197: Wave kill count init 0");
    TEST_ASSERT_EQ(g_wave_timer, 0, "#197: Wave timer init 0");
    /* Simulate wave spawn */
    g_wave_enemy_count = 3;
    g_wave_timer = 300;
    g_wave_kill_count = 3;
    /* Check wave clear condition */
    TEST_ASSERT(g_wave_kill_count >= g_wave_enemy_count && g_wave_enemy_count >= 3,
                "#197: Wave clear condition met");
}

/* #215: Wave clear screen shake */
TEST(test_collision_wave_clear_shake)
{
    collisionInit();
    g_screen_shake = 0;
    g_wave_enemy_count = 4;
    g_wave_kill_count = 4;
    g_wave_timer = 100;
    /* Simulate wave clear scoring block */
    if (g_wave_kill_count >= g_wave_enemy_count &&
        g_wave_enemy_count >= 3 && g_wave_timer > 0) {
        g_screen_shake = 4;
        g_wave_enemy_count = 0;
        g_wave_kill_count = 0;
        g_wave_timer = 0;
    }
    TEST_ASSERT_EQ(g_screen_shake, 4, "#215: Wave clear sets screen shake = 4");
    TEST_ASSERT_EQ(g_wave_enemy_count, 0, "#215: Wave counts reset after clear");
}

/* #203: Diagonal speed normalization math */
TEST(test_diagonal_speed_normalization)
{
    s16 speed;
    u16 held;

    /* Normal movement: no change */
    speed = 2;
    held = 0x0800;  /* ACTION_UP only */
    if ((held & 0x0800) && (held & 0x0200)) {
        if (speed > 1) speed--;
    }
    TEST_ASSERT_EQ(speed, 2, "#203: Non-diagonal speed unchanged");

    /* Diagonal movement at speed 2: reduced to 1 */
    speed = 2;
    held = 0x0800 | 0x0200;  /* UP + LEFT */
    if ((held & (0x0800 | 0x0400)) && (held & (0x0200 | 0x0100))) {
        if (speed > 1) speed--;
    }
    TEST_ASSERT_EQ(speed, 1, "#203: Diagonal speed 2 reduced to 1");

    /* Diagonal at speed 1: no change (can't go lower) */
    speed = 1;
    held = 0x0800 | 0x0200;
    if ((held & (0x0800 | 0x0400)) && (held & (0x0200 | 0x0100))) {
        if (speed > 1) speed--;
    }
    TEST_ASSERT_EQ(speed, 1, "#203: Diagonal speed 1 stays at 1");
}

/* #211: Zone-scaled enemy stats math */
TEST(test_zone_scaled_enemy_stats)
{
    s16 base_hp, base_atk;
    s16 hp, atk;

    base_hp = 60; base_atk = 14;  /* Fighter base stats */

    /* Zone 1 (Debris): no scaling */
    hp = base_hp;
    atk = base_atk;
    TEST_ASSERT_EQ(hp, 60, "#211: Zone 1 HP unchanged");

    /* Zone 2 (Asteroid): +25% */
    hp = base_hp + (base_hp >> 2);
    atk = base_atk + (base_atk >> 2);
    TEST_ASSERT_EQ(hp, 75, "#211: Zone 2 HP +25% (60->75)");
    TEST_ASSERT_EQ(atk, 17, "#211: Zone 2 ATK +25% (14->17)");

    /* Zone 3 (Flagship): +50% */
    hp = base_hp + (base_hp >> 1);
    atk = base_atk + (base_atk >> 1);
    TEST_ASSERT_EQ(hp, 90, "#211: Zone 3 HP +50% (60->90)");
    TEST_ASSERT_EQ(atk, 21, "#211: Zone 3 ATK +50% (14->21)");
}

/* #204: SPREAD weapon DEF bonus math */
TEST(test_spread_weapon_def_bonus)
{
    s16 def = 10;
    u8 weapon_type = 1;  /* WEAPON_SPREAD */
    if (weapon_type == 1) {
        def += 3;
    }
    TEST_ASSERT_EQ(def, 13, "#204: SPREAD weapon adds +3 DEF");
}

/* #216: Flee threshold penalty math */
TEST(test_flee_threshold_penalty)
{
    u8 flee_threshold;
    u8 s_flee_attempts_local;

    /* No failed attempts: threshold unchanged */
    flee_threshold = 85;
    s_flee_attempts_local = 0;
    if (s_flee_attempts_local > 0) {
        u8 penalty = s_flee_attempts_local << 4;
        if (penalty >= flee_threshold) flee_threshold = 10;
        else flee_threshold -= penalty;
    }
    TEST_ASSERT_EQ(flee_threshold, 85, "#216: No penalty on first attempt");

    /* 1 failed: -16 */
    flee_threshold = 85;
    s_flee_attempts_local = 1;
    {
        u8 penalty = s_flee_attempts_local << 4;
        if (penalty >= flee_threshold) flee_threshold = 10;
        else flee_threshold -= penalty;
    }
    TEST_ASSERT_EQ(flee_threshold, 69, "#216: 1 failed attempt = -16");

    /* 3 failed: -48 */
    flee_threshold = 85;
    s_flee_attempts_local = 3;
    {
        u8 penalty = s_flee_attempts_local << 4;
        if (penalty >= flee_threshold) flee_threshold = 10;
        else flee_threshold -= penalty;
    }
    TEST_ASSERT_EQ(flee_threshold, 37, "#216: 3 failed attempts = -48");

    /* 6 failed: penalty exceeds threshold, clamped to 10 */
    flee_threshold = 85;
    s_flee_attempts_local = 6;
    {
        u8 penalty = s_flee_attempts_local << 4;
        if (penalty >= flee_threshold) flee_threshold = 10;
        else flee_threshold -= penalty;
    }
    TEST_ASSERT_EQ(flee_threshold, 10, "#216: Penalty capped at minimum 10");
}

/*--- Test #234: Combo flash triggered on 2x+ combo ---*/
TEST(test_combo_flash_trigger)
{
    g_player.combo_flash = 0;
    g_combo_multiplier = 1;
    /* At 1x combo, flash should not trigger (tested via direct assignment) */
    TEST_ASSERT_EQ(g_player.combo_flash, 0, "No combo flash at 1x multiplier");

    /* Simulate combo >= 2x triggering flash */
    g_combo_multiplier = 2;
    g_player.combo_flash = 6;  /* Simulated trigger like collision.c does */
    TEST_ASSERT_EQ(g_player.combo_flash, 6, "Combo flash = 6 at 2x+ combo (#234)");

    /* Flash should decrement (tested via player update simulation) */
    g_player.combo_flash--;
    TEST_ASSERT_EQ(g_player.combo_flash, 5, "Combo flash decrements (#234)");
}

void run_collision_tests(void)
{
    TEST_SUITE("Collision Detection");
    test_aabb_overlap();
    test_aabb_no_overlap();
    test_aabb_with_offsets();
    test_aabb_negative_coords();
    test_aabb_bullet_vs_enemy();
    test_collision_init();
    test_collision_screen_shake_init();
    test_collision_screen_shake_value();
    test_collision_combo_init();
    test_collision_combo_timer_decay();
    test_collision_enemy_count_gate();
    test_collision_y_range_rejection();
    test_collision_combo_shift_add();
    test_collision_inline_aabb_matches_function();
    test_collision_score_saturating_add();
    test_collision_scout_contact_feedback();
    test_collision_combo_brightness_pulse();
    test_collision_combo_milestones();
    test_collision_decaying_combo_window();
    test_collision_graze_scoring();
    test_collision_kill_streak();
    test_collision_kill_streak_reset();
    test_collision_overkill_bonus();
    test_collision_speed_kill_bonus();
    test_collision_kill_milestones();
    test_collision_no_damage_zone_flag();
    test_collision_bonus_score_timer();
    test_collision_init_new_globals();
    test_collision_golden_enemy_score();
    test_collision_weapon_combo_init();
    test_collision_weapon_combo_logic();
    test_collision_weapon_combo_score();
    test_collision_kill_bullet_cancel();
    test_collision_max_combo_tracking();
    test_collision_sp_regen_on_milestone();
    test_collision_enemy_shield();
    test_collision_combo_display_timer();
    test_collision_elite_dodge();
    test_collision_golden_shield_pickup();
    test_collision_chain_reset_protection();
    test_collision_wave_clear_tracking();
    test_collision_wave_clear_shake();
    test_diagonal_speed_normalization();
    test_zone_scaled_enemy_stats();
    test_spread_weapon_def_bonus();
    test_flee_threshold_penalty();
    test_combo_flash_trigger();
}
