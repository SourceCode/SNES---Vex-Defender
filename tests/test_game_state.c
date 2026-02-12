/*==============================================================================
 * Test: Game State Machine
 * Tests state definitions, zone count, story flags, game constants.
 *============================================================================*/

#include "test_framework.h"
#include "mock_snes.h"
#include "game.h"

/*--- Test state constants are unique ---*/
TEST(test_states_unique)
{
    u8 states[] = {
        STATE_BOOT, STATE_TITLE, STATE_FLIGHT, STATE_BATTLE,
        STATE_DIALOG, STATE_MENU, STATE_ZONE_TRANS, STATE_GAMEOVER,
        STATE_VICTORY
    };
    u8 i, j;
    u8 count = sizeof(states) / sizeof(states[0]);

    for (i = 0; i < count; i++) {
        for (j = i + 1; j < count; j++) {
            TEST_ASSERT(states[i] != states[j], "States are unique");
        }
    }
}

/*--- Test story flags are unique bits ---*/
TEST(test_story_flags)
{
    /* Each flag should be a single bit */
    TEST_ASSERT_EQ(STORY_ZONE1_CLEAR & (STORY_ZONE1_CLEAR - 1), 0, "Z1 is power of 2");
    TEST_ASSERT_EQ(STORY_ZONE2_CLEAR & (STORY_ZONE2_CLEAR - 1), 0, "Z2 is power of 2");
    TEST_ASSERT_EQ(STORY_TWIST_SEEN & (STORY_TWIST_SEEN - 1), 0, "TWIST is power of 2");
    TEST_ASSERT_EQ(STORY_CHOSE_TRUTH & (STORY_CHOSE_TRUTH - 1), 0, "TRUTH is power of 2");
    TEST_ASSERT_EQ(STORY_CHOSE_LOYALTY & (STORY_CHOSE_LOYALTY - 1), 0, "LOYALTY is power of 2");
    TEST_ASSERT_EQ(STORY_BOSS_DEFEATED & (STORY_BOSS_DEFEATED - 1), 0, "BOSS is power of 2");

    /* All flags are distinct */
    u16 all_flags = STORY_ZONE1_CLEAR | STORY_ZONE2_CLEAR | STORY_TWIST_SEEN |
                    STORY_CHOSE_TRUTH | STORY_CHOSE_LOYALTY | STORY_BOSS_DEFEATED;
    u8 bit_count = 0;
    u16 tmp = all_flags;
    while (tmp) { bit_count += tmp & 1; tmp >>= 1; }
    TEST_ASSERT_EQ(bit_count, 6, "6 unique story flags");
}

/*--- Test GameState struct ---*/
TEST(test_gamestate_struct)
{
    GameState gs;
    memset(&gs, 0, sizeof(gs));

    gs.current_state = STATE_FLIGHT;
    gs.current_zone = ZONE_ASTEROID;
    gs.zones_cleared = 1;
    gs.paused = 1;
    gs.story_flags = STORY_ZONE1_CLEAR;
    gs.frame_counter = 30;
    gs.play_time_seconds = 600;

    TEST_ASSERT_EQ(gs.current_state, STATE_FLIGHT, "State stored");
    TEST_ASSERT_EQ(gs.current_zone, ZONE_ASTEROID, "Zone stored");
    TEST_ASSERT_EQ(gs.zones_cleared, 1, "Zones cleared stored");
    TEST_ASSERT_EQ(gs.paused, 1, "Paused stored");
    TEST_ASSERT_EQ(gs.story_flags, STORY_ZONE1_CLEAR, "Flags stored");
    TEST_ASSERT_EQ(gs.frame_counter, 30, "Frame counter stored");
    TEST_ASSERT_EQ(gs.play_time_seconds, 600, "Play time stored");
}

/*--- Test zone constants ---*/
TEST(test_zone_constants)
{
    TEST_ASSERT(ZONE_DEBRIS < ZONE_COUNT, "Debris is valid zone");
    TEST_ASSERT(ZONE_ASTEROID < ZONE_COUNT, "Asteroid is valid zone");
    TEST_ASSERT(ZONE_FLAGSHIP < ZONE_COUNT, "Flagship is valid zone");
    TEST_ASSERT_EQ(ZONE_FLAGSHIP, ZONE_COUNT - 1, "Flagship is last zone");
}

/*--- Test play time tracking ---*/
TEST(test_play_time)
{
    GameState gs;
    gs.frame_counter = 0;
    gs.play_time_seconds = 0;

    /* Simulate 60 frames = 1 second */
    u16 i;
    for (i = 0; i < 60; i++) {
        gs.frame_counter++;
        if (gs.frame_counter >= 60) {
            gs.frame_counter = 0;
            if (gs.play_time_seconds < 0xFFFF) {
                gs.play_time_seconds++;
            }
        }
    }
    TEST_ASSERT_EQ(gs.play_time_seconds, 1, "60 frames = 1 second");
    TEST_ASSERT_EQ(gs.frame_counter, 0, "Frame counter reset");

    /* Simulate 5 minutes (300 seconds = 18000 frames) */
    for (i = 0; i < 17940; i++) {
        gs.frame_counter++;
        if (gs.frame_counter >= 60) {
            gs.frame_counter = 0;
            if (gs.play_time_seconds < 0xFFFF) {
                gs.play_time_seconds++;
            }
        }
    }
    TEST_ASSERT_EQ(gs.play_time_seconds, 300, "5 minutes tracked");
}

/*--- Test u16 play time max ---*/
TEST(test_play_time_max)
{
    GameState gs;
    gs.play_time_seconds = 0xFFFE;
    gs.frame_counter = 59;

    gs.frame_counter++;
    if (gs.frame_counter >= 60) {
        gs.frame_counter = 0;
        if (gs.play_time_seconds < 0xFFFF) {
            gs.play_time_seconds++;
        }
    }
    TEST_ASSERT_EQ(gs.play_time_seconds, 0xFFFF, "Play time caps at 0xFFFF");

    /* Should not overflow */
    gs.frame_counter = 59;
    gs.frame_counter++;
    if (gs.frame_counter >= 60) {
        gs.frame_counter = 0;
        if (gs.play_time_seconds < 0xFFFF) {
            gs.play_time_seconds++;
        }
    }
    TEST_ASSERT_EQ(gs.play_time_seconds, 0xFFFF, "Play time stays at max");
}

void run_game_state_tests(void)
{
    TEST_SUITE("Game State Machine");
    test_states_unique();
    test_story_flags();
    test_gamestate_struct();
    test_zone_constants();
    test_play_time();
    test_play_time_max();
}
