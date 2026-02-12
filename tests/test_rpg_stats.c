/*==============================================================================
 * Test: RPG Stats & Leveling System
 * Tests XP progression, level-ups, stat growth, defeat penalty.
 *============================================================================*/

#include "test_framework.h"
#include "game/rpg_stats.h"

/* rpg_stats.c is included by test_main.c */

/*--- Test initialization ---*/
TEST(test_rpg_init)
{
    rpgStatsInit();
    TEST_ASSERT_EQ(rpg_stats.level, 1, "Init level = 1");
    TEST_ASSERT_EQ(rpg_stats.xp, 0, "Init XP = 0");
    TEST_ASSERT_EQ(rpg_stats.max_hp, RPG_BASE_HP, "Init max_hp = base");
    TEST_ASSERT_EQ(rpg_stats.hp, RPG_BASE_HP, "Init hp = base");
    TEST_ASSERT_EQ(rpg_stats.atk, RPG_BASE_ATK, "Init atk = base");
    TEST_ASSERT_EQ(rpg_stats.def, RPG_BASE_DEF, "Init def = base");
    TEST_ASSERT_EQ(rpg_stats.spd, RPG_BASE_SPD, "Init spd = base");
    TEST_ASSERT_EQ(rpg_stats.max_sp, RPG_BASE_SP, "Init max_sp = base");
    TEST_ASSERT_EQ(rpg_stats.sp, RPG_BASE_SP, "Init sp = base");
    TEST_ASSERT_EQ(rpg_stats.credits, 0, "Init credits = 0");
    TEST_ASSERT_EQ(rpg_stats.total_kills, 0, "Init kills = 0");
    TEST_ASSERT_EQ(rpg_stats.xp_to_next, 30, "Init xp_to_next = 30");
}

/*--- Test single level-up ---*/
TEST(test_rpg_single_levelup)
{
    rpgStatsInit();
    u8 result = rpgAddXP(30);
    TEST_ASSERT_EQ(result, 1, "Level up returned 1");
    TEST_ASSERT_EQ(rpg_stats.level, 2, "Level = 2 after 30 XP");
    TEST_ASSERT_EQ(rpg_stats.max_hp, 95, "L2 max_hp = 95");
    TEST_ASSERT_EQ(rpg_stats.hp, 95, "Full heal on level up");
    TEST_ASSERT_EQ(rpg_stats.atk, 14, "L2 atk = 14");
    TEST_ASSERT_EQ(rpg_stats.def, 7, "L2 def = 7");
    TEST_ASSERT_EQ(rpg_stats.spd, 11, "L2 spd = 11");
}

/*--- Test no level-up ---*/
TEST(test_rpg_no_levelup)
{
    rpgStatsInit();
    u8 result = rpgAddXP(10);
    TEST_ASSERT_EQ(result, 0, "No level up");
    TEST_ASSERT_EQ(rpg_stats.level, 1, "Still level 1");
    TEST_ASSERT_EQ(rpg_stats.xp, 10, "XP accumulated");
    TEST_ASSERT_EQ(rpg_stats.xp_to_next, 20, "XP to next = 20");
}

/*--- Test multi-level-up ---*/
TEST(test_rpg_multi_levelup)
{
    rpgStatsInit();
    rpgAddXP(160);
    TEST_ASSERT_EQ(rpg_stats.level, 4, "Multi level up to L4");
    TEST_ASSERT_EQ(rpg_stats.max_hp, 130, "L4 max_hp = 130");
    TEST_ASSERT_EQ(rpg_stats.atk, 19, "L4 atk = 19");
}

/*--- Test max level cap ---*/
TEST(test_rpg_max_level)
{
    rpgStatsInit();
    rpgAddXP(2000);
    TEST_ASSERT_EQ(rpg_stats.level, 10, "Max level = 10");
    TEST_ASSERT_EQ(rpg_stats.max_hp, 295, "L10 max_hp = 295");
    TEST_ASSERT_EQ(rpg_stats.atk, 43, "L10 atk = 43");
    TEST_ASSERT_EQ(rpg_stats.def, 30, "L10 def = 30");
    TEST_ASSERT_EQ(rpg_stats.spd, 23, "L10 spd = 23");
    TEST_ASSERT_EQ(rpg_stats.max_sp, 7, "L10 max_sp = 7");
    TEST_ASSERT_EQ(rpg_stats.xp_to_next, 0, "XP to next = 0 at max");

    rpgAddXP(5000);
    TEST_ASSERT_EQ(rpg_stats.level, 10, "Still L10 after excess XP");
}

/*--- Test defeat penalty ---*/
TEST(test_rpg_defeat_penalty)
{
    rpgStatsInit();
    rpg_stats.hp = 80;
    rpgApplyDefeatPenalty();
    TEST_ASSERT_EQ(rpg_stats.hp, 60, "80 HP - 25% = 60");

    rpg_stats.hp = 4;
    rpgApplyDefeatPenalty();
    TEST_ASSERT_EQ(rpg_stats.hp, 3, "4 HP - 25% = 3");

    rpg_stats.hp = 1;
    rpgApplyDefeatPenalty();
    TEST_ASSERT_EQ(rpg_stats.hp, 1, "Min HP = 1 after penalty");
}

/*--- Test XP table retrieval ---*/
TEST(test_rpg_xp_table)
{
    TEST_ASSERT_EQ(rpgGetXPForLevel(0), 0, "XP for L0 = 0");
    TEST_ASSERT_EQ(rpgGetXPForLevel(1), 30, "XP for L1 = 30");
    TEST_ASSERT_EQ(rpgGetXPForLevel(2), 80, "XP for L2 = 80");
    TEST_ASSERT_EQ(rpgGetXPForLevel(9), 2000, "XP for L9->L10 = 2000");
    TEST_ASSERT_EQ(rpgGetXPForLevel(10), 0xFFFF, "XP for L10 = sentinel");
    TEST_ASSERT_EQ(rpgGetXPForLevel(11), 0xFFFF, "XP beyond max = sentinel");
}

/*--- Test incremental XP accumulation ---*/
TEST(test_rpg_incremental_xp)
{
    rpgStatsInit();
    rpgAddXP(10);
    TEST_ASSERT_EQ(rpg_stats.xp, 10, "10 XP added");
    rpgAddXP(10);
    TEST_ASSERT_EQ(rpg_stats.xp, 20, "20 XP total");
    rpgAddXP(10);
    TEST_ASSERT_EQ(rpg_stats.level, 2, "Level 2 at 30 XP");
    rpgAddXP(50);
    TEST_ASSERT_EQ(rpg_stats.level, 3, "Level 3 at 80 XP");
}

/*--- Test defeat penalty minimum floor (improvement #10) ---*/
TEST(test_rpg_defeat_penalty_floor)
{
    rpgStatsInit();
    rpg_stats.hp = 2;
    rpgApplyDefeatPenalty();
    TEST_ASSERT_GE(rpg_stats.hp, 1, "HP stays >= 1 after penalty");
}

/*--- Test level-up bounds guard (improvement #20) ---*/
TEST(test_rpg_levelup_bounds)
{
    /* Test that leveling to max works correctly with the new bounds check */
    rpgStatsInit();

    /* Level up to exactly max level (10) */
    rpgAddXP(2000);
    TEST_ASSERT_EQ(rpg_stats.level, RPG_MAX_LEVEL, "Bounds: reached max level");
    TEST_ASSERT_EQ(rpg_stats.max_hp, 295, "Bounds: correct max HP at L10");

    /* Adding more XP at max level should not crash or change stats */
    {
        s16 hp_before;
        s16 atk_before;
        hp_before = rpg_stats.max_hp;
        atk_before = rpg_stats.atk;
        rpgAddXP(500);
        TEST_ASSERT_EQ(rpg_stats.level, RPG_MAX_LEVEL, "Bounds: still max after excess XP");
        TEST_ASSERT_EQ(rpg_stats.max_hp, hp_before, "Bounds: HP unchanged at max");
        TEST_ASSERT_EQ(rpg_stats.atk, atk_before, "Bounds: ATK unchanged at max");
    }

    /* Test that level 1 -> level 2 still works (level=2, idx=0 is valid) */
    rpgStatsInit();
    rpgAddXP(30);
    TEST_ASSERT_EQ(rpg_stats.level, 2, "Bounds: L1->L2 works");
    TEST_ASSERT_EQ(rpg_stats.max_hp, 95, "Bounds: L2 HP correct");
    TEST_ASSERT_EQ(rpg_stats.hp, 95, "Bounds: L2 full heal");
}

/*--- Test XP saturating addition (#131) ---*/
TEST(test_rpg_xp_saturating_add)
{
    rpgStatsInit();
    /* Set XP near u16 max */
    rpg_stats.xp = 0xFFF0;
    rpg_stats.level = RPG_MAX_LEVEL;  /* Already at max to avoid level-up logic */
    rpgAddXP(100);
    TEST_ASSERT_EQ(rpg_stats.xp, 0xFFFF, "XP saturates at 0xFFFF instead of wrapping");

    /* Normal add when there's room */
    rpgStatsInit();
    rpgAddXP(10);
    TEST_ASSERT_EQ(rpg_stats.xp, 10, "XP adds normally when no overflow");
}

/*--- Test zone-scaled defeat penalty (#138) ---*/
TEST(test_rpg_zone_scaled_penalty)
{
    /* Zone 0: ~25% penalty */
    rpgStatsInit();
    rpg_stats.hp = 80;
    g_game.current_zone = 0;
    rpgApplyDefeatPenalty();
    TEST_ASSERT_EQ(rpg_stats.hp, 60, "Zone 0: 80 HP - 25% = 60");

    /* Zone 1: ~37% penalty (hp>>2 + hp>>3 = 20+10 = 30) */
    rpgStatsInit();
    rpg_stats.hp = 80;
    g_game.current_zone = 1;
    rpgApplyDefeatPenalty();
    TEST_ASSERT_EQ(rpg_stats.hp, 50, "Zone 1: 80 HP - 37% = 50");

    /* Zone 2: ~50% penalty */
    rpgStatsInit();
    rpg_stats.hp = 80;
    g_game.current_zone = 2;
    rpgApplyDefeatPenalty();
    TEST_ASSERT_EQ(rpg_stats.hp, 40, "Zone 2: 80 HP - 50% = 40");

    /* Minimum penalty of 1 HP still applies */
    rpgStatsInit();
    rpg_stats.hp = 2;
    g_game.current_zone = 0;
    rpgApplyDefeatPenalty();
    TEST_ASSERT_GE(rpg_stats.hp, 1, "Zone 0: min HP >= 1 after penalty");
}

/*--- Test #144: SP passive regeneration ---*/
TEST(test_rpg_sp_regen)
{
    u16 i;
    rpgStatsInit();
    rpg_stats.sp = 0;  /* Deplete SP */

    /* Call rpgRegenSP 599 times: should not regen yet */
    for (i = 0; i < 599; i++) {
        rpgRegenSP();
    }
    TEST_ASSERT_EQ(rpg_stats.sp, 0, "#144: No SP regen before 600 frames");

    /* 600th call: should regen 1 SP */
    rpgRegenSP();
    TEST_ASSERT_EQ(rpg_stats.sp, 1, "#144: +1 SP after 600 frames");

    /* Another 600 calls: another SP */
    for (i = 0; i < 600; i++) {
        rpgRegenSP();
    }
    TEST_ASSERT_EQ(rpg_stats.sp, 2, "#144: +1 SP after 1200 total frames");

    /* SP at max: should not exceed max_sp */
    rpg_stats.sp = rpg_stats.max_sp;
    for (i = 0; i < 600; i++) {
        rpgRegenSP();
    }
    TEST_ASSERT_EQ(rpg_stats.sp, rpg_stats.max_sp, "#144: SP doesn't exceed max_sp");
}

/*--- Test #158: XP catch-up mechanic ---*/
TEST(test_rpg_xp_catchup)
{
    /* Zone 0: expected min level = 0*3+1 = 1 */
    rpgStatsInit();
    g_game.current_zone = 0;
    rpg_stats.level = 1;
    TEST_ASSERT_EQ(rpgGetCatchUpBonus(), 0, "#158: L1 in zone 0 = no catch-up");

    /* Zone 1: expected min level = 1*3+1 = 4 */
    g_game.current_zone = 1;
    rpg_stats.level = 2;
    TEST_ASSERT_EQ(rpgGetCatchUpBonus(), 1, "#158: L2 in zone 1 = catch-up active");

    rpg_stats.level = 4;
    TEST_ASSERT_EQ(rpgGetCatchUpBonus(), 0, "#158: L4 in zone 1 = no catch-up");

    /* Zone 2: expected min level = 2*3+1 = 7 */
    g_game.current_zone = 2;
    rpg_stats.level = 5;
    TEST_ASSERT_EQ(rpgGetCatchUpBonus(), 1, "#158: L5 in zone 2 = catch-up active");

    rpg_stats.level = 7;
    TEST_ASSERT_EQ(rpgGetCatchUpBonus(), 0, "#158: L7 in zone 2 = no catch-up");

    rpg_stats.level = 10;
    TEST_ASSERT_EQ(rpgGetCatchUpBonus(), 0, "#158: L10 in zone 2 = no catch-up");

    /* Restore for subsequent tests */
    g_game.current_zone = 0;
}

/*--- Test #160: Defeat streak tracking and dynamic difficulty ---*/
TEST(test_rpg_defeat_streak)
{
    rpgStatsInit();
    TEST_ASSERT_EQ(rpg_stats.defeat_streak, 0, "#160: defeat_streak init = 0");

    /* First defeat: streak = 1, no assist yet */
    rpg_stats.hp = 80;
    g_game.current_zone = 0;
    rpgApplyDefeatPenalty();
    TEST_ASSERT_EQ(rpg_stats.defeat_streak, 1, "#160: streak = 1 after 1 defeat");
    TEST_ASSERT_EQ(rpgGetDifficultyAssist(), 0, "#160: No assist at streak 1");

    /* Second defeat: streak = 2, assist activates */
    rpgApplyDefeatPenalty();
    TEST_ASSERT_EQ(rpg_stats.defeat_streak, 2, "#160: streak = 2 after 2 defeats");
    TEST_ASSERT_EQ(rpgGetDifficultyAssist(), 1, "#160: Assist active at streak 2");

    /* Third defeat: streak = 3, assist still active */
    rpgApplyDefeatPenalty();
    TEST_ASSERT_EQ(rpg_stats.defeat_streak, 3, "#160: streak = 3 after 3 defeats");
    TEST_ASSERT_EQ(rpgGetDifficultyAssist(), 1, "#160: Assist still active at streak 3");

    /* Victory resets streak */
    rpgAddXP(1);
    TEST_ASSERT_EQ(rpg_stats.defeat_streak, 0, "#160: streak reset on victory");
    TEST_ASSERT_EQ(rpgGetDifficultyAssist(), 0, "#160: No assist after victory");
}

/*--- Test #160: Defeat streak u8 overflow protection ---*/
TEST(test_rpg_defeat_streak_overflow)
{
    rpgStatsInit();
    rpg_stats.defeat_streak = 254;
    rpg_stats.hp = 80;
    g_game.current_zone = 0;
    rpgApplyDefeatPenalty();
    TEST_ASSERT_EQ(rpg_stats.defeat_streak, 255, "#160: streak caps at 255");
    rpgApplyDefeatPenalty();
    TEST_ASSERT_EQ(rpg_stats.defeat_streak, 255, "#160: streak stays at 255");
}

/*--- Test #171: Growth string formatting ---*/
TEST(test_rpg_growth_str)
{
    char buf[32];

    /* L1->L2: +15HP +2ATK +1DEF */
    rpgGetGrowthStr(2, buf);
    TEST_ASSERT_STR(buf, "+15HP +2ATK +1DEF", "#171: L2 growth string");

    /* L2->L3: +15HP +2ATK +2DEF */
    rpgGetGrowthStr(3, buf);
    TEST_ASSERT_STR(buf, "+15HP +2ATK +2DEF", "#171: L3 growth string");

    /* L3->L4: +20HP +3ATK +2DEF */
    rpgGetGrowthStr(4, buf);
    TEST_ASSERT_STR(buf, "+20HP +3ATK +2DEF", "#171: L4 growth string");

    /* L9->L10: +35HP +5ATK +4DEF */
    rpgGetGrowthStr(10, buf);
    TEST_ASSERT_STR(buf, "+35HP +5ATK +4DEF", "#171: L10 growth string");

    /* Invalid level returns empty string */
    rpgGetGrowthStr(1, buf);
    TEST_ASSERT_EQ(buf[0], 0, "#171: L1 growth string is empty");

    rpgGetGrowthStr(11, buf);
    TEST_ASSERT_EQ(buf[0], 0, "#171: L11 growth string is empty");
}

/*--- Test #239: Win streak initialization ---*/
TEST(test_rpg_win_streak_init)
{
    rpgStatsInit();
    TEST_ASSERT_EQ(rpg_stats.win_streak, 0, "Win streak starts at 0 (#239)");

    /* Simulate setting win streak and reinit */
    rpg_stats.win_streak = 3;
    rpgStatsInit();
    TEST_ASSERT_EQ(rpg_stats.win_streak, 0, "Win streak reset on init (#239)");
}

void run_rpg_stats_tests(void)
{
    TEST_SUITE("RPG Stats & Leveling");
    test_rpg_init();
    test_rpg_single_levelup();
    test_rpg_no_levelup();
    test_rpg_multi_levelup();
    test_rpg_max_level();
    test_rpg_defeat_penalty();
    test_rpg_xp_table();
    test_rpg_incremental_xp();
    test_rpg_defeat_penalty_floor();
    test_rpg_levelup_bounds();
    test_rpg_xp_saturating_add();
    test_rpg_zone_scaled_penalty();
    test_rpg_sp_regen();
    test_rpg_xp_catchup();
    test_rpg_defeat_streak();
    test_rpg_defeat_streak_overflow();
    test_rpg_growth_str();
    test_rpg_win_streak_init();
}
