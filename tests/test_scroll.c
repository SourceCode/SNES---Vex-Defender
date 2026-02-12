/*==============================================================================
 * Test: Scroll System
 * Tests trigger management, speed transitions, distance tracking.
 *============================================================================*/

#include "test_framework.h"
#include "engine/scroll.h"

/* scroll.c is included by test_main.c */

static u8 trigger_fired_count;
static void testCallback(void) { trigger_fired_count++; }

TEST(test_scroll_init)
{
    scrollInit();
    TEST_ASSERT_EQ(scrollGetY(), 0, "Init Y = 0");
    TEST_ASSERT_EQ(scrollGetDistance(), 0, "Init distance = 0");
    TEST_ASSERT_EQ(scrollGetSpeed(), SCROLL_SPEED_STOP, "Init speed = STOP");
}

TEST(test_scroll_set_speed)
{
    scrollInit();
    scrollSetSpeed(SCROLL_SPEED_NORMAL);
    TEST_ASSERT_EQ(scrollGetSpeed(), SCROLL_SPEED_NORMAL, "Speed = NORMAL");
    scrollSetSpeed(SCROLL_SPEED_FAST);
    TEST_ASSERT_EQ(scrollGetSpeed(), SCROLL_SPEED_FAST, "Speed = FAST");
}

TEST(test_scroll_accumulation)
{
    u16 i;
    scrollInit();
    scrollSetSpeed(SCROLL_SPEED_NORMAL);
    scrollUpdate();
    scrollUpdate();
    TEST_ASSERT_EQ(scrollGetDistance(), 1, "2 frames at 0.5 = 1 pixel");

    for (i = 0; i < 10; i++) scrollUpdate();
    TEST_ASSERT_EQ(scrollGetDistance(), 6, "12 frames at 0.5 = 6 pixels");
}

TEST(test_scroll_triggers)
{
    u16 i;
    scrollInit();
    trigger_fired_count = 0;
    scrollAddTrigger(5, testCallback);
    scrollSetSpeed(SCROLL_SPEED_FAST);

    for (i = 0; i < 4; i++) scrollUpdate();
    TEST_ASSERT_EQ(trigger_fired_count, 0, "Trigger not fired at 4px");

    scrollUpdate();
    TEST_ASSERT_EQ(trigger_fired_count, 1, "Trigger fired at 5px");

    scrollUpdate();
    TEST_ASSERT_EQ(trigger_fired_count, 1, "Trigger fires only once");
}

TEST(test_scroll_multiple_triggers)
{
    u16 i;
    scrollInit();
    trigger_fired_count = 0;
    scrollAddTrigger(3, testCallback);
    scrollAddTrigger(7, testCallback);
    scrollSetSpeed(SCROLL_SPEED_FAST);

    for (i = 0; i < 10; i++) scrollUpdate();
    TEST_ASSERT_EQ(trigger_fired_count, 2, "Both triggers fired");
}

TEST(test_scroll_clear_triggers)
{
    scrollInit();
    trigger_fired_count = 0;
    scrollAddTrigger(1, testCallback);
    scrollClearTriggers();
    scrollSetSpeed(SCROLL_SPEED_FAST);
    scrollUpdate();
    scrollUpdate();
    TEST_ASSERT_EQ(trigger_fired_count, 0, "Cleared triggers don't fire");
}

TEST(test_scroll_trigger_overflow)
{
    u8 i;
    u8 result;
    scrollInit();
    for (i = 0; i < MAX_SCROLL_TRIGGERS; i++) {
        result = scrollAddTrigger(i * 100, testCallback);
        TEST_ASSERT_EQ(result, 1, "Trigger add succeeds within capacity");
    }
    /* #132: Overflow now returns 0 instead of silently dropping */
    result = scrollAddTrigger(9999, testCallback);
    TEST_ASSERT_EQ(result, 0, "Trigger overflow returns 0");
}

TEST(test_scroll_speed_transition)
{
    u16 i;
    scrollInit();
    scrollSetSpeed(SCROLL_SPEED_NORMAL);
    scrollTransitionSpeed(SCROLL_SPEED_FAST, 10);
    for (i = 0; i < 20; i++) scrollUpdate();
    TEST_ASSERT_EQ(scrollGetSpeed(), SCROLL_SPEED_FAST, "Speed reached FAST");
}

TEST(test_scroll_instant_transition)
{
    scrollInit();
    scrollSetSpeed(SCROLL_SPEED_SLOW);
    scrollTransitionSpeed(SCROLL_SPEED_FAST, 0);
    TEST_ASSERT_EQ(scrollGetSpeed(), SCROLL_SPEED_FAST, "Instant transition");
}

TEST(test_scroll_stopped)
{
    scrollInit();
    scrollSetSpeed(SCROLL_SPEED_STOP);
    scrollUpdate();
    scrollUpdate();
    TEST_ASSERT_EQ(scrollGetDistance(), 0, "No movement when stopped");
}

TEST(test_scroll_parallax)
{
    u16 i;
    scrollInit();
    scrollSetSpeed(SCROLL_SPEED_FAST);
    for (i = 0; i < 10; i++) scrollUpdate();
    TEST_ASSERT_EQ(scrollGetDistance(), 10, "BG1 scrolled 10px");
}

TEST(test_scroll_reset_triggers)
{
    u16 i;
    scrollInit();
    trigger_fired_count = 0;
    scrollAddTrigger(3, testCallback);
    scrollSetSpeed(SCROLL_SPEED_FAST);

    for (i = 0; i < 5; i++) scrollUpdate();
    TEST_ASSERT_EQ(trigger_fired_count, 1, "Trigger fired once");

    scrollResetTriggers();
    scrollUpdate();
    TEST_ASSERT_EQ(trigger_fired_count, 2, "Trigger re-fires after reset");
}

/*--- Test trigger early-exit optimization (improvement #1) ---*/
TEST(test_scroll_trigger_early_exit)
{
    u16 i;
    scrollInit();
    trigger_fired_count = 0;
    scrollAddTrigger(2, testCallback);
    scrollSetSpeed(SCROLL_SPEED_FAST);

    /* Fire the trigger */
    for (i = 0; i < 5; i++) scrollUpdate();
    TEST_ASSERT_EQ(trigger_fired_count, 1, "Trigger fired");

    /* All triggers are now fired; further updates should still work fine */
    for (i = 0; i < 100; i++) scrollUpdate();
    TEST_ASSERT_EQ(trigger_fired_count, 1, "No re-fire after all triggers done");
}

/*--- Test trigger remaining count resets on clear ---*/
TEST(test_scroll_trigger_remaining_clear)
{
    scrollInit();
    trigger_fired_count = 0;
    scrollAddTrigger(100, testCallback);
    scrollAddTrigger(200, testCallback);
    scrollClearTriggers();

    /* Re-add trigger after clear */
    scrollAddTrigger(2, testCallback);
    scrollSetSpeed(SCROLL_SPEED_FAST);

    scrollUpdate();
    scrollUpdate();
    scrollUpdate();
    TEST_ASSERT_EQ(trigger_fired_count, 1, "New trigger fires after clear+re-add");
}

/*--- Test scroll easing transition (improvement #4) ---*/
TEST(test_scroll_easing_transition)
{
    u16 i;
    scrollInit();
    scrollSetSpeed(SCROLL_SPEED_STOP);
    scrollTransitionSpeed(SCROLL_SPEED_FAST, 20);

    for (i = 0; i < 30; i++) {
        scrollUpdate();
    }
    TEST_ASSERT_EQ(scrollGetSpeed(), SCROLL_SPEED_FAST, "Easing reaches target");
}

/*--- Test trigger loop early-exit with mixed fired/unfired (improvement R4#6) ---*/
TEST(test_scroll_trigger_loop_early_exit)
{
    u16 i;
    scrollInit();
    trigger_fired_count = 0;

    /* Add 3 triggers: at distances 2, 50, and 100 */
    scrollAddTrigger(2, testCallback);
    scrollAddTrigger(50, testCallback);
    scrollAddTrigger(100, testCallback);
    scrollSetSpeed(SCROLL_SPEED_FAST);

    /* Advance enough to fire only the first trigger (2 pixels) */
    for (i = 0; i < 3; i++) scrollUpdate();
    TEST_ASSERT_EQ(trigger_fired_count, 1, "Only first trigger fired after 3 frames");

    /* Continue to fire second trigger at 50 pixels */
    for (i = 0; i < 50; i++) scrollUpdate();
    TEST_ASSERT_EQ(trigger_fired_count, 2, "Second trigger fired around 50px");

    /* Continue to fire third */
    for (i = 0; i < 60; i++) scrollUpdate();
    TEST_ASSERT_EQ(trigger_fired_count, 3, "All three triggers fired");

    /* After all triggers fired, early-exit should skip loop iterations safely */
    for (i = 0; i < 100; i++) scrollUpdate();
    TEST_ASSERT_EQ(trigger_fired_count, 3, "No re-fire after all triggers done (early-exit)");
}

/*--- Test large distance tracking (Bug #1: was overflowing at 255px) ---*/
TEST(test_scroll_large_distance)
{
    u16 i;
    scrollInit();
    trigger_fired_count = 0;
    scrollAddTrigger(300, testCallback);
    scrollSetSpeed(SCROLL_SPEED_FAST);  /* 1 px/frame */

    /* Advance 310 frames = 310 pixels. Before the fix, total_dist_fp (u16
     * in 8.8 FP) would wrap at 255 pixels and the trigger at 300px would
     * never fire. With the fix, distance is tracked as a separate u16 for
     * pixels and u8 for sub-pixels, supporting up to 65535 pixels. */
    for (i = 0; i < 310; i++) scrollUpdate();
    TEST_ASSERT_GE(scrollGetDistance(), 300, "Distance >= 300 after 310 frames");
    TEST_ASSERT_EQ(trigger_fired_count, 1, "Trigger at 300px fires with large distance");
}

/*--- Test trigger overflow detection flag (#132) ---*/
TEST(test_scroll_trigger_overflow_flag)
{
    u8 i;
    u8 result;
    scrollInit();

    /* Fill all trigger slots */
    for (i = 0; i < MAX_SCROLL_TRIGGERS; i++) {
        result = scrollAddTrigger(i * 10, testCallback);
        TEST_ASSERT_EQ(result, 1, "Trigger slot available");
    }

    /* Attempt to add beyond capacity */
    result = scrollAddTrigger(9999, testCallback);
    TEST_ASSERT_EQ(result, 0, "Overflow detected: returns 0");

    /* After clear, adding should succeed again */
    scrollClearTriggers();
    result = scrollAddTrigger(5, testCallback);
    TEST_ASSERT_EQ(result, 1, "Trigger add succeeds after clear");
}

void run_scroll_tests(void)
{
    TEST_SUITE("Scroll System");
    test_scroll_init();
    test_scroll_set_speed();
    test_scroll_accumulation();
    test_scroll_triggers();
    test_scroll_multiple_triggers();
    test_scroll_clear_triggers();
    test_scroll_trigger_overflow();
    test_scroll_speed_transition();
    test_scroll_instant_transition();
    test_scroll_stopped();
    test_scroll_parallax();
    test_scroll_reset_triggers();
    test_scroll_trigger_early_exit();
    test_scroll_trigger_remaining_clear();
    test_scroll_easing_transition();
    test_scroll_trigger_loop_early_exit();
    test_scroll_large_distance();
    test_scroll_trigger_overflow_flag();
}
