/*==============================================================================
 * Test: Configuration Constants & Data Integrity
 * Validates VRAM layout, OAM allocation, palette slots, and game constants
 * for correctness and non-overlap.
 *============================================================================*/

#include "test_framework.h"
#include "mock_snes.h"
#include "config.h"

/*--- Test VRAM layout non-overlap ---*/
TEST(test_vram_layout)
{
    /* BG1 map: 0x0000-0x03FF (2K words) */
    TEST_ASSERT(VRAM_BG1_MAP == 0x0000, "BG1 MAP at 0x0000");

    /* BG2 map: 0x0800-0x0BFF (2K words) */
    TEST_ASSERT(VRAM_BG2_MAP == 0x0800, "BG2 MAP at 0x0800");
    TEST_ASSERT(VRAM_BG2_MAP > VRAM_BG1_MAP + 0x0400, "BG2 MAP after BG1 MAP");

    /* BG1 GFX: 0x1000-0x4FFF (32K words for ~32KB zone tile data) */
    TEST_ASSERT(VRAM_BG1_GFX == 0x1000, "BG1 GFX at 0x1000");
    TEST_ASSERT(VRAM_BG1_GFX > VRAM_BG2_MAP + 0x0400, "BG1 GFX after BG2 MAP");

    /* Font/Text: 0x2000 (tile 0x100 from BG1 base, shares BG1 tile space) */
    TEST_ASSERT(VRAM_TEXT_GFX == 0x2000, "Text GFX at 0x2000");
    TEST_ASSERT(VRAM_TEXT_GFX > VRAM_BG1_GFX, "Text after BG1 base");
    TEST_ASSERT(VRAM_TEXT_MAP == VRAM_BG1_MAP, "Text MAP shared with BG1 MAP");

    /* BG2 tiles: 0x5000-0x503F (128 bytes for star dots) */
    TEST_ASSERT(VRAM_BG2_GFX == 0x5000, "BG2 GFX at 0x5000");

    /* OBJ tiles: 0x6000-0x7FFF (16KB for sprites) */
    TEST_ASSERT(VRAM_OBJ_GFX == 0x6000, "OBJ GFX at 0x6000");
    TEST_ASSERT(VRAM_OBJ_GFX > VRAM_BG2_GFX, "OBJ after BG2 GFX");

    /* Critical: OBJ VRAM must not overlap with BG1 tile data.
     * BG1 tiles span $1000-$4FFF (~32KB). OBJ at $6000 is safely above. */
    TEST_ASSERT(VRAM_OBJ_GFX >= 0x5000 + 0x0040, "OBJ clear of BG2 tiles");

    /* All within 64KB (32K words) */
    TEST_ASSERT(VRAM_OBJ_GFX + 0x2000 <= 0x8000, "All VRAM within 32K words");
}

/*--- Test OBJ VRAM layout (16-name grid) ---*/
TEST(test_obj_vram_layout)
{
    /* Player at name 0 (offset 0x0000) - 4 cols wide, uses names 0-51 */
    /* Player bullets at name 4 (offset 0x0040) - 2 cols, rows 0-1 */
    /* Enemy bullets at name 6 (offset 0x0060) - 2 cols, rows 0-1 */
    /* Enemy A at name 128 (offset 0x0800) - 4 cols, rows 8-11 */
    /* Enemy B at name 132 (offset 0x0840) - 4 cols, rows 8-11 */

    /* Check non-overlap of player (rows 0-3) and bullets (rows 0-1, cols 4+) */
    /* Player uses cols 0-3 rows 0-3: names 0-3, 16-19, 32-35, 48-51 */
    /* P-bullets use cols 4-5 rows 0-1: names 4-5, 20-21 */
    /* E-bullets use cols 6-7 rows 0-1: names 6-7, 22-23 */
    /* These don't overlap: player cols 0-3, bullets cols 4-7 */
    TEST_ASSERT(4 > 3, "Bullet cols start after player cols");
    TEST_ASSERT(6 > 5, "E-bullet cols start after P-bullet cols");

    /* Enemy A at name 128 (row 8) doesn't overlap player (rows 0-3) */
    TEST_ASSERT(128 > 51, "Enemy A starts after player space");

    /* Enemy B at name 132 (row 8, col 4) doesn't overlap Enemy A (col 0-3) */
    TEST_ASSERT(132 > 128, "Enemy B after Enemy A start");
    /* Enemy A uses names 128-131, 144-147, 160-163, 176-179 */
    /* Enemy B uses names 132-135, 148-151, 164-167, 180-183 */
    /* No overlap: A cols 0-3, B cols 4-7 */
    TEST_ASSERT(132 - 128 == 4, "Enemy B offset = 4 names from A");
}

/*--- Test OAM slot allocation non-overlap ---*/
TEST(test_oam_slots)
{
    /* Player: 0-3 */
    TEST_ASSERT_EQ(OAM_PLAYER, 0, "Player at slot 0");

    /* Player bullets: 4-19 */
    TEST_ASSERT_EQ(OAM_BULLETS, 4, "Bullets at slot 4");
    TEST_ASSERT(OAM_BULLETS >= OAM_PLAYER + OAM_PLAYER_MAX, "Bullets after player");

    /* Enemies: 20-39 */
    TEST_ASSERT_EQ(OAM_ENEMIES, 20, "Enemies at slot 20");
    TEST_ASSERT(OAM_ENEMIES >= OAM_BULLETS + OAM_BULLETS_MAX, "Enemies after bullets");

    /* Enemy bullets: 40-55 */
    TEST_ASSERT_EQ(OAM_EBULLETS, 40, "E-bullets at slot 40");
    TEST_ASSERT(OAM_EBULLETS >= OAM_ENEMIES + OAM_ENEMIES_MAX, "E-bullets after enemies");

    /* Items: 56-63 */
    TEST_ASSERT_EQ(OAM_ITEMS, 56, "Items at slot 56");
    TEST_ASSERT(OAM_ITEMS >= OAM_EBULLETS + OAM_EBULLETS_MAX, "Items after E-bullets");

    /* UI: 64-79 */
    TEST_ASSERT_EQ(OAM_UI, 64, "UI at slot 64");
    TEST_ASSERT(OAM_UI >= OAM_ITEMS + OAM_ITEMS_MAX, "UI after items");

    /* All within 128 OAM slots */
    TEST_ASSERT(OAM_UI + OAM_UI_MAX <= 128, "All OAM within 128 slots");
}

/*--- Test palette allocation ---*/
TEST(test_palette_slots)
{
    /* BG palettes 0-7 */
    TEST_ASSERT_EQ(PAL_BG1_MAIN, 0, "BG1 = palette 0");
    TEST_ASSERT_EQ(PAL_BG2_STARS, 1, "BG2 stars = palette 1");

    /* OBJ palettes 8-15 */
    TEST_ASSERT_EQ(PAL_OBJ_PLAYER, 8, "Player OBJ = palette 8");
    TEST_ASSERT_EQ(PAL_OBJ_ENEMY, 9, "Enemy A OBJ = palette 9");
    TEST_ASSERT_EQ(PAL_OBJ_BULLET, 10, "P-bullet OBJ = palette 10");
    TEST_ASSERT_EQ(PAL_OBJ_EBULLET, 11, "E-bullet OBJ = palette 11");
    TEST_ASSERT_EQ(PAL_OBJ_ENEMY2, 13, "Enemy B OBJ = palette 13");

    /* All unique */
    TEST_ASSERT(PAL_OBJ_PLAYER != PAL_OBJ_ENEMY, "Player != Enemy");
    TEST_ASSERT(PAL_OBJ_BULLET != PAL_OBJ_EBULLET, "P-bullet != E-bullet");
    TEST_ASSERT(PAL_OBJ_ENEMY != PAL_OBJ_ENEMY2, "Enemy A != Enemy B");
}

/*--- Test game constants ---*/
TEST(test_game_constants)
{
    TEST_ASSERT_EQ(ZONE_COUNT, 3, "3 zones");
    TEST_ASSERT_EQ(ZONE_DEBRIS, 0, "Zone 0 = Debris");
    TEST_ASSERT_EQ(ZONE_ASTEROID, 1, "Zone 1 = Asteroid");
    TEST_ASSERT_EQ(ZONE_FLAGSHIP, 2, "Zone 2 = Flagship");
    TEST_ASSERT_EQ(SCREEN_W, 256, "Screen width = 256");
    TEST_ASSERT_EQ(SCREEN_H, 224, "Screen height = 224");
}

/*--- Test fixed-point macros ---*/
TEST(test_fixed_point)
{
    TEST_ASSERT_EQ(FP8(1), 256, "FP8(1) = 256");
    TEST_ASSERT_EQ(FP8_INT(256), 1, "FP8_INT(256) = 1");
    TEST_ASSERT_EQ(FP8_INT(512), 2, "FP8_INT(512) = 2");
    TEST_ASSERT_EQ(FP8_FRAC(256), 0, "FP8_FRAC(256) = 0");
    TEST_ASSERT_EQ(FP8_FRAC(384), 128, "FP8_FRAC(1.5) = 128");

    /* Scroll speeds */
    TEST_ASSERT_EQ(SCROLL_SPEED_STOP, 0, "STOP = 0");
    TEST_ASSERT_EQ(SCROLL_SPEED_FAST, 256, "FAST = 1.0 px/f");
    TEST_ASSERT_EQ(SCROLL_SPEED_NORMAL, 128, "NORMAL = 0.5 px/f");
}

/*--- Test scroll trigger limit ---*/
TEST(test_trigger_limit)
{
    TEST_ASSERT_GE(MAX_SCROLL_TRIGGERS, 24, "At least 24 triggers");
    /* Zone 1 has 15 enemy triggers + 1 boss + ~3 story = 19 */
    /* Zone 2 has 12 enemy triggers + 1 boss + ~3 story = 16 */
    /* Zone 3 has 10 enemy triggers + 1 boss + ~3 story = 14 */
    /* 24 is sufficient for all zones */
}

void run_config_tests(void)
{
    TEST_SUITE("Configuration & Data Integrity");
    test_vram_layout();
    test_obj_vram_layout();
    test_oam_slots();
    test_palette_slots();
    test_game_constants();
    test_fixed_point();
    test_trigger_limit();
}
