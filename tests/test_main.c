/*==============================================================================
 * VEX DEFENDER - Test Runner (Single Compilation Unit)
 *
 * Compile: clang -I. -I../include -Wall -Wno-unused-function
 *                -Wno-unused-variable test_main.c -o run_tests
 *
 * This file uses a "single compilation unit" (SCU) pattern: all source
 * files and test files are #included directly into one .c file.  This
 * avoids linker complications from symbol conflicts and ensures that
 * static functions in source files are accessible to their corresponding
 * test files.
 *
 * The include order matters:
 *   Phase 1: Mock headers that replace SNES hardware functions with stubs
 *   Phase 2: Global variables needed by multiple source files
 *   Phase 3: Game source files (order respects dependencies)
 *   Phase 4: Stub functions for cross-module calls not being tested
 *   Phase 5: Test files that exercise the included source code
 *
 * The test runner returns exit code 0 if all tests pass, 1 if any fail.
 * This makes it compatible with CI pipelines and automated test scripts.
 *============================================================================*/

#include <stdio.h>
#include <string.h>

/*=== Global test counters (used by TEST_ASSERT_* macros in test_framework.h) ===*/
int tf_pass = 0;   /* Assertions that passed */
int tf_fail = 0;   /* Assertions that failed */
int tf_total = 0;  /* Total assertions evaluated */

/*===========================================================================*/
/* Phase 1: Mock headers (override <snes.h> and assets.h)                    */
/*===========================================================================*/

/* mock_snes.h provides type definitions (u8, u16, s16, etc.) and stub
 * functions for all PVSnesLib/hardware calls so the game code compiles
 * on a standard C compiler without SNES-specific headers. */

/* Note: mock_snes.h is also pulled in via the test directory's snes.h
 * redirect, and assets.h in the test directory takes priority over
 * the include/ directory version (providing dummy asset labels). */

/*===========================================================================*/
/* Phase 2: Shared globals needed by multiple source files                   */
/*===========================================================================*/

#include "mock_snes.h"
#include "game.h"

/* Global game state - normally defined in game_state.c.
 * Needed here because save.c, enemies.c, and story.c reference g_game. */
GameState g_game;

/* VBlank frame counter - normally maintained by the VBlank system.
 * Used by inventory.c's invRollDrop() as a PRNG seed. */
u16 g_frame_count = 0;

/*===========================================================================*/
/* Phase 3: Include source files (order matters for dependencies)            */
/*===========================================================================*/

/* collision.c - Standalone module; only needs basic types.
 * Provides AABB collision detection used by bullet/enemy interactions. */
#include "../src/engine/collision.c"

/* rpg_stats.c - Standalone module.
 * Provides the leveling system, XP tables, and stat growth. */
#include "../src/game/rpg_stats.c"

/* inventory.c - Depends on g_frame_count (defined above).
 * Provides item management and loot drop rolls. */
#include "../src/game/inventory.c"

/* scroll.c - Standalone module.
 * Provides scroll position tracking and distance-based trigger callbacks. */
#include "../src/engine/scroll.c"

/* sprites.c - Standalone module (hardware calls are stubbed by mock_snes.h).
 * Provides sprite slot management and OAM rendering. */
#include "../src/engine/sprites.c"

/* bullets.c - Depends on sprites.h (included by sprites.c) and sound stubs.
 * Provides player/enemy projectile management. */
#include "../src/engine/bullets.c"

/* Player stub - enemies.c references g_player for targeting.
 * We provide a minimal PlayerShip struct initialized to zeros. */
#include "game/player.h"
PlayerShip g_player = { 0 };

/* enemies.c - Depends on bullets (for enemy firing), scroll (for triggers),
 * and g_player (for targeting/collision). */
#include "../src/game/enemies.c"

/* save.c - Depends on rpg_stats (stat serialization), inventory (item
 * serialization), and g_game (progress serialization). */
#include "../src/game/save.c"

/*===========================================================================*/
/* Phase 4: Stub functions for cross-module dependencies                     */
/*===========================================================================*/

/* These stub out functions called by the included source files but which
 * belong to modules we're NOT including in the test build.  Without these
 * stubs, we'd get linker (or SCU compile) errors for unresolved symbols. */

void bgSystemInit(void) {}                        /* background.c */
void bgLoadZone(u8 z) { (void)z; }               /* background.c */
void bgUpdate(void) {}                            /* background.c */
void bgVBlankUpdate(void) {}                      /* background.c */
void bgSetParallaxVisible(u8 v) { (void)v; }     /* background.c */
u8 bgGetCurrentZone(void) { return 0; }           /* background.c */
void playerInit(void) {}                           /* player.c */
void battleInit(void) {}                           /* battle.c */
void storyInit(void) {}                            /* story.c */
void storyRegisterTriggers(u8 z) { (void)z; }    /* story.c */
void dlgInit(void) {}                              /* dialog.c */
void soundInit(void) {}                            /* sound.c */
void soundUpdate(void) {}                          /* sound.c */
u8 g_battle_trigger = 0;                           /* battle.c */

/*===========================================================================*/
/* Phase 5: Include test files                                               */
/*===========================================================================*/

/* Each test file defines a run_*_tests() function that calls TEST_SUITE()
 * followed by individual TEST() functions.  Test files are included in
 * the same SCU so they can access static functions from the source files. */

#include "test_config.c"       /* Tests for config.h constants and macros */
#include "test_game_state.c"   /* Tests for GameState initialization */
#include "test_collision.c"    /* Tests for AABB collision detection */
#include "test_rpg_stats.c"    /* Tests for XP, leveling, growth tables */
#include "test_inventory.c"    /* Tests for item add/remove/stack/loot */
#include "test_scroll.c"       /* Tests for scroll triggers and distance */
#include "test_sprites.c"      /* Tests for sprite slot allocation */
#include "test_bullets.c"      /* Tests for bullet firing and management */
#include "test_enemies.c"      /* Tests for enemy spawning and AI */
#include "test_save.c"         /* Tests for SRAM save/load/checksum */

/*===========================================================================*/
/* Main - Test Runner Entry Point                                            */
/*===========================================================================*/

/*
 * main - Run all test suites and print summary results.
 *
 * Executes each module's test suite in order, accumulating pass/fail
 * counts via the global tf_pass/tf_fail/tf_total counters.
 *
 * Returns 0 if all tests passed (success), 1 if any failed (failure).
 * This exit code convention is compatible with CI systems like GitHub Actions,
 * Jenkins, etc. that interpret non-zero exit as a failed build step.
 */
int main(void)
{
    printf("========================================\n");
    printf("VEX DEFENDER - Test Suite\n");
    printf("10 modules, 1019 assertions\n");
    printf("========================================\n");

    /* Execute all test suites */
    run_config_tests();
    run_game_state_tests();
    run_collision_tests();
    run_rpg_stats_tests();
    run_inventory_tests();
    run_scroll_tests();
    run_sprite_tests();
    run_bullet_tests();
    run_enemy_tests();
    run_save_tests();

    /* Print summary */
    printf("\n========================================\n");
    printf("RESULTS: %d/%d passed", tf_pass, tf_total);
    if (tf_fail > 0) {
        printf(" (%d FAILED)", tf_fail);
    }
    printf("\n");

    /* Calculate and display pass rate percentage */
    if (tf_total > 0) {
        int pct = (tf_pass * 100) / tf_total;
        printf("Pass rate: %d%%\n", pct);
    }
    printf("========================================\n");

    /* Return 0 for success, 1 for any failures (CI-compatible exit code) */
    return tf_fail > 0 ? 1 : 0;
}
