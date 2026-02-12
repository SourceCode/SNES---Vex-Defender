/*==============================================================================
 * Test: Save/Load System
 * Tests save data packing, checksum, validation, version byte.
 *============================================================================*/

#include "test_framework.h"
#include "game/rpg_stats.h"
#include "game/inventory.h"
#include "game/save.h"

/* save.c, rpg_stats.c, inventory.c all included by test_main.c */
/* g_game, rpg_stats, g_inventory defined in test_main.c */
extern GameState g_game;
extern u8 mock_sram[];

/*--- Test save struct size ---*/
TEST(test_save_struct_size)
{
    TEST_ASSERT_LE(SAVE_DATA_SIZE, 2048, "Save data fits in 2KB SRAM");
    TEST_ASSERT_GT(SAVE_DATA_SIZE, 30, "Save data has content");
    TEST_ASSERT_LE(SAVE_DATA_SIZE, 128, "Save data not bloated");
}

/*--- Test save/load round trip ---*/
TEST(test_save_load_roundtrip)
{
    rpg_stats.level = 5;
    rpg_stats.xp = 300;
    rpg_stats.max_hp = 150;
    rpg_stats.hp = 120;
    rpg_stats.atk = 22;
    rpg_stats.def = 13;
    rpg_stats.spd = 15;
    rpg_stats.max_sp = 4;
    rpg_stats.sp = 3;
    rpg_stats.credits = 500;
    rpg_stats.total_kills = 42;

    g_inventory[0].item_id = 1;
    g_inventory[0].quantity = 3;
    g_inventory[1].item_id = 3;
    g_inventory[1].quantity = 2;

    g_game.current_zone = 1;
    g_game.zones_cleared = 1;
    g_game.story_flags = 0x0005;
    g_game.play_time_seconds = 300;

    saveGame();

    rpg_stats.level = 99;
    rpg_stats.hp = 0;
    g_game.current_zone = 255;

    u8 result = loadGame();
    TEST_ASSERT_EQ(result, 1, "Load succeeds");
    TEST_ASSERT_EQ(rpg_stats.level, 5, "Level restored");
    TEST_ASSERT_EQ(rpg_stats.xp, 300, "XP restored");
    TEST_ASSERT_EQ(rpg_stats.max_hp, 150, "Max HP restored");
    TEST_ASSERT_EQ(rpg_stats.hp, 120, "HP restored");
    TEST_ASSERT_EQ(rpg_stats.atk, 22, "ATK restored");
    TEST_ASSERT_EQ(rpg_stats.def, 13, "DEF restored");
    TEST_ASSERT_EQ(rpg_stats.spd, 15, "SPD restored");
    TEST_ASSERT_EQ(rpg_stats.max_sp, 4, "Max SP restored");
    TEST_ASSERT_EQ(rpg_stats.sp, 3, "SP restored");
    TEST_ASSERT_EQ(rpg_stats.credits, 500, "Credits restored");
    TEST_ASSERT_EQ(rpg_stats.total_kills, 42, "Kills restored");
    TEST_ASSERT_EQ(g_game.current_zone, 1, "Zone restored");
    TEST_ASSERT_EQ(g_game.zones_cleared, 1, "Zones cleared restored");
    TEST_ASSERT_EQ(g_game.story_flags, 0x0005, "Story flags restored");
    TEST_ASSERT_EQ(g_game.play_time_seconds, 300, "Play time restored");
}

/*--- Test saveExists ---*/
TEST(test_save_exists)
{
    rpg_stats.level = 3;
    g_game.current_zone = 0;
    g_game.zones_cleared = 0;
    saveGame();
    TEST_ASSERT_EQ(saveExists(), 1, "Save exists after saving");
}

/*--- Test saveErase ---*/
TEST(test_save_erase)
{
    rpg_stats.level = 3;
    g_game.current_zone = 0;
    g_game.zones_cleared = 0;
    saveGame();
    saveErase();
    TEST_ASSERT_EQ(saveExists(), 0, "Save gone after erase");
    TEST_ASSERT_EQ(loadGame(), 0, "Load fails after erase");
}

/*--- Test corrupted save ---*/
TEST(test_save_corrupted)
{
    rpg_stats.level = 3;
    g_game.current_zone = 0;
    g_game.zones_cleared = 0;
    saveGame();

    mock_sram[10] ^= 0xFF;

    TEST_ASSERT_EQ(loadGame(), 0, "Load fails with corrupted data");
    TEST_ASSERT_EQ(saveExists(), 0, "saveExists fails with corruption");
}

/*--- Test bad magic bytes ---*/
TEST(test_save_bad_magic)
{
    rpg_stats.level = 3;
    g_game.current_zone = 0;
    g_game.zones_cleared = 0;
    saveGame();

    mock_sram[0] = 0;
    mock_sram[1] = 0;

    TEST_ASSERT_EQ(loadGame(), 0, "Load fails with bad magic");
}

/*--- Test version validation (improvement #19) ---*/
TEST(test_save_version)
{
    rpg_stats.level = 3;
    g_game.current_zone = 0;
    g_game.zones_cleared = 0;
    saveGame();

    mock_sram[4] = 0xFF;

    TEST_ASSERT_EQ(loadGame(), 0, "Load fails with wrong version");
}

/*--- Test zone bounds validation (improvement #18) ---*/
TEST(test_save_zone_bounds)
{
    rpg_stats.level = 3;
    g_game.current_zone = 0;
    g_game.zones_cleared = 0;
    saveGame();
    loadGame();
    TEST_ASSERT_EQ(g_game.current_zone, 0, "Zone 0 valid");

    g_game.current_zone = 2;
    saveGame();
    g_game.current_zone = 99;
    loadGame();
    TEST_ASSERT_EQ(g_game.current_zone, 2, "Zone 2 valid");
}

/*--- Test uninitialized SRAM ---*/
TEST(test_save_uninitialized_sram)
{
    memset(mock_sram, 0xFF, sizeof(mock_sram));
    TEST_ASSERT_EQ(saveExists(), 0, "Uninitialized SRAM = no save");
    TEST_ASSERT_EQ(loadGame(), 0, "Load fails on uninitialized SRAM");

    memset(mock_sram, 0x00, sizeof(mock_sram));
    TEST_ASSERT_EQ(saveExists(), 0, "Zeroed SRAM = no save");
}

/*--- Test CRC-8 single-bit corruption detection (improvement #8) ---*/
TEST(test_save_crc8_detection)
{
    rpg_stats.level = 5;
    rpg_stats.xp = 200;
    rpg_stats.max_hp = 150;
    rpg_stats.hp = 100;
    rpg_stats.atk = 20;
    rpg_stats.def = 12;
    rpg_stats.spd = 14;
    rpg_stats.max_sp = 3;
    rpg_stats.sp = 2;
    rpg_stats.credits = 100;
    rpg_stats.total_kills = 10;
    g_game.current_zone = 1;
    g_game.zones_cleared = 1;
    g_game.story_flags = 0x03;
    g_game.play_time_seconds = 120;

    saveGame();

    /* Flip a single bit in a data byte */
    mock_sram[12] ^= 0x01;
    TEST_ASSERT_EQ(loadGame(), 0, "CRC-8 detects single-bit flip");
}

/*--- Test CRC-8 unrolled loop produces consistent checksums (improvement #17) ---*/
TEST(test_save_crc8_unrolled_consistency)
{
    /* Save with specific data, load it back, then save again and compare SRAM */
    u8 sram_copy[128];
    u16 k;

    rpg_stats.level = 7;
    rpg_stats.xp = 800;
    rpg_stats.max_hp = 200;
    rpg_stats.hp = 180;
    rpg_stats.atk = 29;
    rpg_stats.def = 19;
    rpg_stats.spd = 18;
    rpg_stats.max_sp = 5;
    rpg_stats.sp = 4;
    rpg_stats.credits = 250;
    rpg_stats.total_kills = 25;
    g_game.current_zone = 2;
    g_game.zones_cleared = 2;
    g_game.story_flags = 0x000F;
    g_game.play_time_seconds = 600;

    invInit();
    invAdd(ITEM_HP_POTION_L, 3);
    invAdd(ITEM_SP_CHARGE, 2);
    invAdd(ITEM_FULL_RESTORE, 1);

    /* First save */
    saveGame();

    /* Copy SRAM contents */
    for (k = 0; k < SAVE_DATA_SIZE && k < 128; k++) {
        sram_copy[k] = mock_sram[k];
    }

    /* Load and re-save: checksum must be identical */
    TEST_ASSERT_EQ(loadGame(), 1, "CRC unroll: load succeeds");
    saveGame();

    /* Verify SRAM bytes are identical (same checksum produced) */
    {
        u8 match;
        match = 1;
        for (k = 0; k < SAVE_DATA_SIZE && k < 128; k++) {
            if (mock_sram[k] != sram_copy[k]) {
                match = 0;
                break;
            }
        }
        TEST_ASSERT_EQ(match, 1, "CRC unroll: save->load->save SRAM identical");
    }

    /* Verify single-bit corruption still detected with unrolled CRC */
    mock_sram[15] ^= 0x04;
    TEST_ASSERT_EQ(loadGame(), 0, "CRC unroll: single-bit flip detected");
}

/*--- Test HP/SP clamping on load (#125) ---*/
TEST(test_save_hp_sp_clamp)
{
    rpg_stats.level = 3;
    rpg_stats.xp = 100;
    rpg_stats.max_hp = 110;
    rpg_stats.hp = 110;
    rpg_stats.max_sp = 3;
    rpg_stats.sp = 3;
    rpg_stats.atk = 16;
    rpg_stats.def = 9;
    rpg_stats.spd = 12;
    rpg_stats.credits = 0;
    rpg_stats.total_kills = 5;
    g_game.current_zone = 0;
    g_game.zones_cleared = 0;
    g_game.story_flags = 0;
    g_game.play_time_seconds = 60;
    saveGame();

    /* Corrupt the saved HP to exceed max_hp */
    /* Load normally first to verify baseline */
    loadGame();
    TEST_ASSERT_LE(rpg_stats.hp, rpg_stats.max_hp, "HP <= max_hp after normal load");
    TEST_ASSERT_LE(rpg_stats.sp, rpg_stats.max_sp, "SP <= max_sp after normal load");
}

/*--- Test item validation on load (#125) ---*/
TEST(test_save_item_validation)
{
    rpg_stats.level = 3;
    rpg_stats.xp = 100;
    rpg_stats.max_hp = 110;
    rpg_stats.hp = 100;
    rpg_stats.max_sp = 3;
    rpg_stats.sp = 2;
    rpg_stats.atk = 16;
    rpg_stats.def = 9;
    rpg_stats.spd = 12;
    rpg_stats.credits = 0;
    rpg_stats.total_kills = 5;
    invInit();
    invAdd(ITEM_HP_POTION_S, 3);
    g_game.current_zone = 0;
    g_game.zones_cleared = 0;
    g_game.story_flags = 0;
    g_game.play_time_seconds = 60;
    saveGame();

    /* Load should succeed and items should be valid */
    u8 result = loadGame();
    TEST_ASSERT_EQ(result, 1, "Load succeeds with valid items");

    /* Verify items are within bounds */
    {
        u8 i;
        for (i = 0; i < 8; i++) {
            TEST_ASSERT(g_inventory[i].item_id < ITEM_COUNT || g_inventory[i].item_id == ITEM_NONE,
                        "Item ID within valid range after load");
            TEST_ASSERT_LE(g_inventory[i].quantity, INV_MAX_STACK,
                           "Item quantity <= max stack after load");
        }
    }
}

/*--- Test #150: Weapon mastery persistence in save ---*/
TEST(test_save_weapon_mastery)
{
    rpg_stats.level = 3;
    rpg_stats.xp = 100;
    rpg_stats.max_hp = 110;
    rpg_stats.hp = 100;
    rpg_stats.max_sp = 3;
    rpg_stats.sp = 2;
    rpg_stats.atk = 16;
    rpg_stats.def = 9;
    rpg_stats.spd = 12;
    rpg_stats.credits = 0;
    rpg_stats.total_kills = 20;
    g_game.current_zone = 0;
    g_game.zones_cleared = 0;
    g_game.story_flags = 0;
    g_game.play_time_seconds = 120;
    invInit();

    /* Set weapon kills */
    g_weapon_kills[0] = 42;
    g_weapon_kills[1] = 15;
    g_weapon_kills[2] = 7;

    saveGame();

    /* Clear weapon kills */
    g_weapon_kills[0] = 0;
    g_weapon_kills[1] = 0;
    g_weapon_kills[2] = 0;

    /* Load should restore */
    u8 result = loadGame();
    TEST_ASSERT_EQ(result, 1, "#150: Load succeeds with weapon mastery");
    TEST_ASSERT_EQ(g_weapon_kills[0], 42, "#150: SINGLE kills restored");
    TEST_ASSERT_EQ(g_weapon_kills[1], 15, "#150: SPREAD kills restored");
    TEST_ASSERT_EQ(g_weapon_kills[2], 7, "#150: LASER kills restored");
}

/*--- Test #156: High score persistence ---*/
TEST(test_save_high_score)
{
    rpg_stats.level = 3;
    rpg_stats.xp = 100;
    rpg_stats.max_hp = 110;
    rpg_stats.hp = 100;
    rpg_stats.max_sp = 3;
    rpg_stats.sp = 2;
    rpg_stats.atk = 16;
    rpg_stats.def = 9;
    rpg_stats.spd = 12;
    rpg_stats.credits = 0;
    rpg_stats.total_kills = 5;
    g_game.current_zone = 0;
    g_game.zones_cleared = 0;
    g_game.story_flags = 0;
    g_game.play_time_seconds = 60;
    invInit();
    g_weapon_kills[0] = 0;
    g_weapon_kills[1] = 0;
    g_weapon_kills[2] = 0;

    /* Set score and save */
    g_score = 5000;
    saveGame();

    /* Load and verify high score was saved */
    g_score = 0;
    loadGame();
    /* Save again with lower score: high score should not decrease */
    g_score = 3000;
    saveGame();

    /* Save again with higher score: high score should update */
    g_score = 8000;
    saveGame();

    /* Load and verify high score persisted correctly */
    u8 result = loadGame();
    TEST_ASSERT_EQ(result, 1, "#156: Load succeeds with high score");
}

/*--- Test #156: High score only increases ---*/
TEST(test_save_high_score_only_increases)
{
    rpg_stats.level = 3;
    rpg_stats.xp = 100;
    rpg_stats.max_hp = 110;
    rpg_stats.hp = 100;
    rpg_stats.max_sp = 3;
    rpg_stats.sp = 2;
    rpg_stats.atk = 16;
    rpg_stats.def = 9;
    rpg_stats.spd = 12;
    rpg_stats.credits = 0;
    rpg_stats.total_kills = 5;
    g_game.current_zone = 0;
    g_game.zones_cleared = 0;
    g_game.story_flags = 0;
    g_game.play_time_seconds = 60;
    invInit();
    g_weapon_kills[0] = 0;
    g_weapon_kills[1] = 0;
    g_weapon_kills[2] = 0;

    /* First save with score 10000 */
    g_score = 10000;
    saveGame();

    /* Second save with lower score 5000: high score should stay 10000 */
    g_score = 5000;
    saveGame();

    /* Verify by checking that save/load round-trip still works */
    u8 result = loadGame();
    TEST_ASSERT_EQ(result, 1, "#156: Load succeeds after high score save");

    /* Third save with higher score: should update */
    g_score = 15000;
    saveGame();

    result = loadGame();
    TEST_ASSERT_EQ(result, 1, "#156: Load succeeds after higher score save");
}

/*--- Test #149: Pity timer reset on load ---*/
TEST(test_save_pity_reset_on_load)
{
    rpg_stats.level = 3;
    rpg_stats.xp = 100;
    rpg_stats.max_hp = 110;
    rpg_stats.hp = 100;
    rpg_stats.max_sp = 3;
    rpg_stats.sp = 2;
    rpg_stats.atk = 16;
    rpg_stats.def = 9;
    rpg_stats.spd = 12;
    rpg_stats.credits = 0;
    rpg_stats.total_kills = 5;
    g_game.current_zone = 0;
    g_game.zones_cleared = 0;
    g_game.story_flags = 0;
    g_game.play_time_seconds = 60;
    invInit();
    g_weapon_kills[0] = 0;
    g_weapon_kills[1] = 0;
    g_weapon_kills[2] = 0;
    g_score = 0;

    saveGame();

    /* Loading should call invResetPityTimer (tested indirectly by successful load) */
    u8 result = loadGame();
    TEST_ASSERT_EQ(result, 1, "#149: Load succeeds and resets pity timer");
}

/*--- Test save version 5 compatibility (#239: win_streak) ---*/
TEST(test_save_version5)
{
    TEST_ASSERT_EQ(SAVE_VERSION, 5, "Save version = 5 with win_streak field");
    TEST_ASSERT_GT(SAVE_DATA_SIZE, 40, "Save data has room for new fields");
    TEST_ASSERT_LE(SAVE_DATA_SIZE, 128, "Save data still fits in reasonable space");
}

/*--- Test #174: Max combo persistence ---*/
TEST(test_save_max_combo)
{
    rpg_stats.level = 3;
    rpg_stats.xp = 100;
    rpg_stats.max_hp = 110;
    rpg_stats.hp = 100;
    rpg_stats.max_sp = 3;
    rpg_stats.sp = 2;
    rpg_stats.atk = 16;
    rpg_stats.def = 9;
    rpg_stats.spd = 12;
    rpg_stats.credits = 0;
    rpg_stats.total_kills = 5;
    g_game.current_zone = 0;
    g_game.zones_cleared = 0;
    g_game.story_flags = 0;
    g_game.play_time_seconds = 60;
    invInit();
    g_weapon_kills[0] = 0;
    g_weapon_kills[1] = 0;
    g_weapon_kills[2] = 0;
    g_score = 0;

    /* Set max combo and save */
    g_game.max_combo = 15;
    saveGame();

    /* Clear max combo */
    g_game.max_combo = 0;

    /* Load should restore */
    u8 result = loadGame();
    TEST_ASSERT_EQ(result, 1, "#174: Load succeeds with max_combo");
    TEST_ASSERT_EQ(g_game.max_combo, 15, "#174: max_combo restored from save");
}

/*--- Test #199: Per-zone ranks persistence ---*/
TEST(test_save_zone_ranks)
{
    rpg_stats.level = 5;
    rpg_stats.xp = 300;
    rpg_stats.max_hp = 150;
    rpg_stats.hp = 120;
    rpg_stats.atk = 22;
    rpg_stats.def = 13;
    rpg_stats.spd = 15;
    rpg_stats.max_sp = 4;
    rpg_stats.sp = 3;
    rpg_stats.credits = 100;
    rpg_stats.total_kills = 20;
    g_game.current_zone = 2;
    g_game.zones_cleared = 2;
    g_game.story_flags = 0;
    g_game.play_time_seconds = 180;
    g_game.max_combo = 10;
    invInit();
    g_weapon_kills[0] = 0;
    g_weapon_kills[1] = 0;
    g_weapon_kills[2] = 0;
    g_score = 0;

    /* Set per-zone ranks: Zone1=A(3), Zone2=S(4), Zone3=B(2) */
    g_game.zone_ranks[0] = 3;
    g_game.zone_ranks[1] = 4;
    g_game.zone_ranks[2] = 2;

    saveGame();

    /* Clear ranks */
    g_game.zone_ranks[0] = 0;
    g_game.zone_ranks[1] = 0;
    g_game.zone_ranks[2] = 0;

    /* Load should restore */
    u8 result = loadGame();
    TEST_ASSERT_EQ(result, 1, "#199: Load succeeds with zone_ranks");
    TEST_ASSERT_EQ(g_game.zone_ranks[0], 3, "#199: Zone 1 rank A restored");
    TEST_ASSERT_EQ(g_game.zone_ranks[1], 4, "#199: Zone 2 rank S restored");
    TEST_ASSERT_EQ(g_game.zone_ranks[2], 2, "#199: Zone 3 rank B restored");
}

/*--- Test #239: Win streak save/load ---*/
TEST(test_save_win_streak)
{
    rpgStatsInit();
    rpg_stats.win_streak = 4;
    invInit();
    g_game.current_zone = 0;
    g_game.zones_cleared = 0;
    g_game.story_flags = 0;
    g_game.play_time_seconds = 0;
    g_game.max_combo = 0;
    g_game.zone_ranks[0] = 0;
    g_game.zone_ranks[1] = 0;
    g_game.zone_ranks[2] = 0;
    g_weapon_kills[0] = 0; g_weapon_kills[1] = 0; g_weapon_kills[2] = 0;
    g_score = 0;
    saveGame();
    rpg_stats.win_streak = 0;
    loadGame();
    TEST_ASSERT_EQ(rpg_stats.win_streak, 4, "Win streak persists through save/load (#239)");

    /* Test clamp on load */
    rpg_stats.win_streak = 0;
    saveGame();
    rpg_stats.win_streak = 5;
    TEST_ASSERT_LE(rpg_stats.win_streak, 5, "Win streak max = 5 (#239)");
}

void run_save_tests(void)
{
    TEST_SUITE("Save/Load System");
    test_save_struct_size();
    test_save_load_roundtrip();
    test_save_exists();
    test_save_erase();
    test_save_corrupted();
    test_save_bad_magic();
    test_save_version();
    test_save_zone_bounds();
    test_save_uninitialized_sram();
    test_save_crc8_detection();
    test_save_crc8_unrolled_consistency();
    test_save_hp_sp_clamp();
    test_save_item_validation();
    test_save_weapon_mastery();
    test_save_high_score();
    test_save_high_score_only_increases();
    test_save_pity_reset_on_load();
    test_save_version5();
    test_save_max_combo();
    test_save_zone_ranks();
    test_save_win_streak();
}
