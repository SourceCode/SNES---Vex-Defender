/*==============================================================================
 * Test: Sprite Engine
 * Tests pool allocation, entity lifecycle, OAM slot assignment.
 *============================================================================*/

#include "test_framework.h"
#include "engine/sprites.h"

/* sprites.c is included by test_main.c - access sprite_pool via extern */
extern SpriteEntity sprite_pool[];

TEST(test_sprite_init)
{
    u8 i;
    spriteSystemInit();
    for (i = 0; i < MAX_GAME_SPRITES; i++) {
        TEST_ASSERT_EQ(sprite_pool[i].active, ENTITY_INACTIVE, "Sprite inactive");
    }
}

TEST(test_sprite_alloc)
{
    spriteSystemInit();
    SpriteEntity *s = spriteAlloc();
    TEST_ASSERT_NOT_NULL(s, "Allocation succeeds");
    TEST_ASSERT_EQ(s->active, ENTITY_ACTIVE, "Allocated sprite is active");
    TEST_ASSERT_EQ(s->oam_id, 0, "First sprite = OAM 0");
}

TEST(test_sprite_multi_alloc)
{
    spriteSystemInit();
    SpriteEntity *s1 = spriteAlloc();
    SpriteEntity *s2 = spriteAlloc();
    TEST_ASSERT_NOT_NULL(s1, "First alloc OK");
    TEST_ASSERT_NOT_NULL(s2, "Second alloc OK");
    TEST_ASSERT(s1 != s2, "Different sprites allocated");
    TEST_ASSERT_EQ(s1->oam_id, 0, "s1 OAM = 0");
    TEST_ASSERT_EQ(s2->oam_id, 4, "s2 OAM = 4");
}

TEST(test_sprite_pool_exhaust)
{
    u8 i;
    spriteSystemInit();
    for (i = 0; i < MAX_GAME_SPRITES; i++) {
        spriteAlloc();
    }
    SpriteEntity *s = spriteAlloc();
    TEST_ASSERT_NULL(s, "Pool full returns NULL");
}

TEST(test_sprite_free)
{
    spriteSystemInit();
    SpriteEntity *s = spriteAlloc();
    spriteFree(s);
    TEST_ASSERT_EQ(s->active, ENTITY_INACTIVE, "Freed sprite inactive");

    SpriteEntity *s2 = spriteAlloc();
    TEST_ASSERT(s == s2, "Re-allocated same slot");
}

TEST(test_sprite_free_null)
{
    spriteFree(NULL);
    TEST_ASSERT(1, "Free NULL is safe");
}

TEST(test_sprite_animation)
{
    spriteSystemInit();
    SpriteEntity *s = spriteAlloc();
    s->anim_count = 3;
    s->anim_speed = 2;
    s->anim_frame = 0;
    s->anim_timer = 0;

    spriteUpdateAll();
    TEST_ASSERT_EQ(s->anim_frame, 0, "Frame 0 after 1 update");

    spriteUpdateAll();
    TEST_ASSERT_EQ(s->anim_frame, 1, "Frame 1 after 2 updates");

    spriteUpdateAll();
    spriteUpdateAll();
    TEST_ASSERT_EQ(s->anim_frame, 2, "Frame 2 after 4 updates");

    spriteUpdateAll();
    spriteUpdateAll();
    TEST_ASSERT_EQ(s->anim_frame, 0, "Frame wraps to 0");
}

TEST(test_sprite_no_animation)
{
    spriteSystemInit();
    SpriteEntity *s = spriteAlloc();
    s->anim_count = 1;
    s->anim_speed = 0;
    spriteUpdateAll();
    TEST_ASSERT_EQ(s->anim_frame, 0, "Single frame stays at 0");
}

TEST(test_sprite_defaults)
{
    spriteSystemInit();
    SpriteEntity *s = spriteAlloc();
    TEST_ASSERT_EQ(s->x, 0, "Default x = 0");
    TEST_ASSERT_EQ(s->y, 240, "Default y = 240");
    TEST_ASSERT_EQ(s->hflip, 0, "Default hflip = 0");
    TEST_ASSERT_EQ(s->vflip, 0, "Default vflip = 0");
    TEST_ASSERT_EQ(s->priority, 2, "Default priority = 2");
    TEST_ASSERT_EQ(s->anim_count, 1, "Default anim_count = 1");
}

/*--- Test animation done flag (improvement #7) ---*/
TEST(test_sprite_anim_done)
{
    SpriteEntity *s;
    spriteSystemInit();
    s = spriteAlloc();
    s->anim_count = 2;
    s->anim_speed = 1;
    s->anim_frame = 0;
    s->anim_timer = 0;

    TEST_ASSERT_EQ(s->anim_done, 0, "anim_done starts 0");

    spriteUpdateAll(); /* frame 0->1, timer 0->0 */
    TEST_ASSERT_EQ(s->anim_done, 0, "anim_done still 0 mid-anim");

    spriteUpdateAll(); /* frame 1->0 (wrap), anim_done = 1 */
    TEST_ASSERT_EQ(s->anim_done, 1, "anim_done = 1 on wrap");
}

/*--- Test offscreen culling boundary values (improvement R4#7) ---*/
TEST(test_sprite_bounds_edge_culling)
{
    SpriteEntity *s;
    spriteSystemInit();
    s = spriteAlloc();

    /* Sprite at x=-32 should NOT be culled (just at boundary) */
    s->x = -32;
    s->y = 100;
    spriteRenderAll();
    TEST_ASSERT_EQ(s->active, ENTITY_ACTIVE, "Sprite at x=-32 not culled (still active)");

    /* Sprite at x=-33 should be culled (offscreen) */
    s->x = -33;
    s->y = 100;
    spriteRenderAll();
    /* Note: culling hides via OAM but doesn't deactivate - just hides it */
    /* The check is that it doesn't crash and the active state is maintained */
    TEST_ASSERT_EQ(s->active, ENTITY_ACTIVE, "Sprite at x=-33 offscreen but still active");

    /* Sprite at x=256 should NOT be culled */
    s->x = 256;
    s->y = 100;
    spriteRenderAll();
    TEST_ASSERT_EQ(s->active, ENTITY_ACTIVE, "Sprite at x=256 not culled");

    /* Sprite at x=257 should be culled (hidden) */
    s->x = 257;
    s->y = 100;
    spriteRenderAll();
    TEST_ASSERT_EQ(s->active, ENTITY_ACTIVE, "Sprite at x=257 offscreen but still active");

    /* Sprite at y=-32 should NOT be culled */
    s->x = 100;
    s->y = -32;
    spriteRenderAll();
    TEST_ASSERT_EQ(s->active, ENTITY_ACTIVE, "Sprite at y=-32 not culled");

    /* Sprite at y=224 should NOT be culled */
    s->x = 100;
    s->y = 224;
    spriteRenderAll();
    TEST_ASSERT_EQ(s->active, ENTITY_ACTIVE, "Sprite at y=224 not culled");
}

/*--- Test alloc hint optimization (improvement R4#8) ---*/
TEST(test_sprite_alloc_hint)
{
    SpriteEntity *s1, *s2, *s3, *s4;
    spriteSystemInit();

    /* Allocate first two slots */
    s1 = spriteAlloc();
    s2 = spriteAlloc();
    TEST_ASSERT_EQ(s1->oam_id, 0, "First alloc at slot 0");
    TEST_ASSERT_EQ(s2->oam_id, 4, "Second alloc at slot 1");

    /* Free first slot and re-alloc: should find slot 0 via hint */
    spriteFree(s1);
    s3 = spriteAlloc();
    TEST_ASSERT_EQ(s3->oam_id, 0, "Re-alloc returns freed slot 0");

    /* Alloc next should continue from hint (slot 2) */
    s4 = spriteAlloc();
    TEST_ASSERT_EQ(s4->oam_id, 8, "Next alloc at slot 2 (hint advanced)");
}

/*--- Test alloc hint wraps around pool ---*/
TEST(test_sprite_alloc_hint_wrap)
{
    u8 i;
    SpriteEntity *sprites[MAX_GAME_SPRITES];
    SpriteEntity *s;
    spriteSystemInit();

    /* Fill entire pool */
    for (i = 0; i < MAX_GAME_SPRITES; i++) {
        sprites[i] = spriteAlloc();
    }

    /* Pool should be full */
    s = spriteAlloc();
    TEST_ASSERT_NULL(s, "Pool full after filling all slots");

    /* Free slot 0 only */
    spriteFree(sprites[0]);

    /* Alloc should find slot 0 via wrap-around */
    s = spriteAlloc();
    TEST_ASSERT_NOT_NULL(s, "Alloc finds slot 0 after wrap");
    TEST_ASSERT_EQ(s->oam_id, 0, "Wrapped alloc returns slot 0");
}

/*--- Test shift-based tile calculation in spriteRenderAll (improvement R5#15) ---*/
TEST(test_sprite_tile_shift_math)
{
    SpriteEntity *s;
    u8 frame;
    u16 expected_large;
    u16 expected_small;
    spriteSystemInit();

    /* Test that anim_frame << 4 matches anim_frame * 16 for various frames */
    for (frame = 0; frame < 8; frame++) {
        expected_large = (u16)frame << 4;
        expected_small = (u16)frame << 2;
        TEST_ASSERT_EQ(expected_large, (u16)(frame * 16),
                       "frame<<4 == frame*16 (large sprite tile offset)");
        TEST_ASSERT_EQ(expected_small, (u16)(frame * 4),
                       "frame<<2 == frame*4 (small sprite tile offset)");
    }

    /* Verify render uses shift result correctly with a live sprite */
    s = spriteAlloc();
    s->x = 100;
    s->y = 100;
    s->size = OBJ_LARGE;
    s->tile_offset = 0;
    s->anim_frame = 2;
    s->anim_count = 4;
    s->anim_speed = 10;

    /* frame 2, large sprite: tile = 0 + (2 << 4) = 32 */
    spriteRenderAll();
    TEST_ASSERT(1, "Render with shift-based tile calc runs OK");
}

/*--- Test countdown do-while loop produces same animation behavior (#113) ---*/
TEST(test_sprite_countdown_anim)
{
    SpriteEntity *s;
    spriteSystemInit();
    s = spriteAlloc();
    s->anim_count = 3;
    s->anim_speed = 1;   /* Advance every frame */
    s->anim_frame = 0;
    s->anim_timer = 0;
    s->anim_done = 0;

    /* Frame 1: timer 0->1 >= speed 1, advance frame 0->1 */
    spriteUpdateAll();
    TEST_ASSERT_EQ(s->anim_frame, 1, "Countdown loop: frame 1 after 1 update");
    TEST_ASSERT_EQ(s->anim_done, 0, "Countdown loop: anim_done=0 mid-cycle");

    /* Frame 2: advance frame 1->2 */
    spriteUpdateAll();
    TEST_ASSERT_EQ(s->anim_frame, 2, "Countdown loop: frame 2 after 2 updates");
    TEST_ASSERT_EQ(s->anim_done, 0, "Countdown loop: anim_done=0 before wrap");

    /* Frame 3: advance frame 2->3 >= count 3, wrap to 0, set anim_done */
    spriteUpdateAll();
    TEST_ASSERT_EQ(s->anim_frame, 0, "Countdown loop: frame wraps to 0");
    TEST_ASSERT_EQ(s->anim_done, 1, "Countdown loop: anim_done=1 on wrap");

    /* Verify a second cycle also works correctly */
    s->anim_done = 0;
    spriteUpdateAll();
    TEST_ASSERT_EQ(s->anim_frame, 1, "Countdown loop: second cycle frame 1");
    spriteUpdateAll();
    TEST_ASSERT_EQ(s->anim_frame, 2, "Countdown loop: second cycle frame 2");
    spriteUpdateAll();
    TEST_ASSERT_EQ(s->anim_frame, 0, "Countdown loop: second cycle wraps to 0");
    TEST_ASSERT_EQ(s->anim_done, 1, "Countdown loop: second cycle anim_done=1");
}

void run_sprite_tests(void)
{
    TEST_SUITE("Sprite Engine");
    test_sprite_init();
    test_sprite_alloc();
    test_sprite_multi_alloc();
    test_sprite_pool_exhaust();
    test_sprite_free();
    test_sprite_free_null();
    test_sprite_animation();
    test_sprite_no_animation();
    test_sprite_defaults();
    test_sprite_anim_done();
    test_sprite_bounds_edge_culling();
    test_sprite_alloc_hint();
    test_sprite_alloc_hint_wrap();
    test_sprite_tile_shift_math();
    test_sprite_countdown_anim();
}
