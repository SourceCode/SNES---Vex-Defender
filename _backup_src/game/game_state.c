/*==============================================================================
 * Game State Machine - Phase 20
 *
 * Manages master game flow: Title -> Flight -> Battle -> GameOver/Victory.
 * Defines g_game (declared extern in game.h).
 *
 * Title Screen (Phase 20):
 *   BG3 text only (BG1/BG2 disabled). "VEX DEFENDER" title.
 *   Menu: NEW GAME / CONTINUE with cursor navigation.
 *   CONTINUE grayed out if no valid save data in SRAM.
 *   Auto-save on zone entry (gsFlightEnter).
 *
 * Game Over (Phase 20):
 *   Menu: RETRY ZONE / TITLE with cursor navigation.
 *   RETRY restarts current zone with full HP/SP restore.
 *
 * Victory (Phase 20):
 *   Shows mission stats (level, kills, play time) before PRESS START.
 *   Erases save on game completion (game is done).
 *
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#include "game/game_state.h"
#include "engine/system.h"
#include "engine/fade.h"
#include "engine/background.h"
#include "engine/sprites.h"
#include "engine/input.h"
#include "engine/scroll.h"
#include "engine/bullets.h"
#include "engine/collision.h"
#include "engine/vblank.h"
#include "game/player.h"
#include "game/enemies.h"
#include "game/battle.h"
#include "game/boss.h"
#include "game/rpg_stats.h"
#include "game/inventory.h"
#include "game/dialog.h"
#include "game/save.h"
#include "engine/sound.h"

/*=== Define the global game state (declared extern in game.h) ===*/
GameState g_game;

/*=== Zone advance flag (set by scroll trigger, checked by main.c) ===*/
u8 g_zone_advance;

/*=== Boss battle trigger distance (scroll pixels) ===*/
#define BOSS_TRIGGER_DISTANCE 4800

/*=== Title menu state (Phase 20) ===*/
static u8 title_cursor;        /* 0=NEW GAME, 1=CONTINUE */
static u8 title_has_save;      /* 1 if valid save exists */

/*=== Game Over menu state (Phase 20) ===*/
static u8 go_cursor;           /* 0=RETRY, 1=TITLE */

/*=== Victory screen state (Phase 20) ===*/
static u8 victory_stats_shown; /* 1 after stats drawn */

/*=== Number buffer for stat display ===*/
static char gs_num_buf[6];

/*--- Phase 19: Boss battle scroll trigger callback ---*/
static void gsOnBossTrigger(void)
{
    /* Set battle trigger to current zone's boss */
    g_battle_trigger = BOSS_TRIGGER_BASE + g_game.current_zone;
}

/*===========================================================================*/
/* Number to string helper (no division on 65816)                            */
/*===========================================================================*/

static void gsNumToStr(u16 val)
{
    u16 v;
    v = val;
    if (v > 9999) v = 9999;

    gs_num_buf[0] = '0';
    while (v >= 1000) { v -= 1000; gs_num_buf[0]++; }
    gs_num_buf[1] = '0';
    while (v >= 100) { v -= 100; gs_num_buf[1]++; }
    gs_num_buf[2] = '0';
    while (v >= 10) { v -= 10; gs_num_buf[2]++; }
    gs_num_buf[3] = '0' + (u8)v;
    gs_num_buf[4] = 0;
}

/*===========================================================================*/
/* Initialize                                                                */
/*===========================================================================*/

void gsInit(void)
{
    g_game.current_state = STATE_BOOT;
    g_game.previous_state = STATE_BOOT;
    g_game.current_zone = ZONE_DEBRIS;
    g_game.zones_cleared = 0;
    g_game.paused = 0;
    g_game.story_flags = 0;
    g_game.frame_counter = 0;
    g_game.play_time_seconds = 0;
    g_zone_advance = 0;

    /* Initialize dialog and story systems (Phase 16) */
    dlgInit();
    storyInit();

    /* Initialize save system (Phase 20) */
    saveInit();
}

/*===========================================================================*/
/* Title Screen                                                              */
/*===========================================================================*/

void gsTitleEnter(void)
{
    /* Force blank for VRAM operations */
    setScreenOff();

    /* Disable game BG layers */
    bgSetDisable(0);
    bgSetDisable(1);

    /* Initialize BG3 text system (loads font to VRAM 0x3000) */
    consoleInitText(0, BG_4COLORS, 0, 0);
    bgSetEnable(2);

    /* Draw title text */
    consoleDrawText(10, 9, "VEX DEFENDER");
    consoleDrawText(6, 15, "PRESS START");

    /* Phase 20 disabled for testing: no save check */
    title_has_save = 0;
    title_cursor = 0;

    /* Show screen and fade in */
    setScreenOn();
    fadeInBlocking(30);

    g_game.current_state = STATE_TITLE;
}

void gsTitleUpdate(u16 pad_pressed)
{
    /* Clear old cursor positions */
    consoleDrawText(8, 14, " ");
    consoleDrawText(8, 16, " ");

    /* Navigate menu */
    if (pad_pressed & ACTION_UP) {
        if (title_cursor > 0) {
            title_cursor--;
            soundPlaySFX(SFX_MENU_MOVE);
        }
    }
    if (pad_pressed & ACTION_DOWN) {
        if (title_cursor < 1 && title_has_save) {
            title_cursor++;
            soundPlaySFX(SFX_MENU_MOVE);
        }
    }

    /* Draw cursor at current position */
    consoleDrawText(8, 14 + (title_cursor << 1), ">");

    /* Confirm selection */
    if ((pad_pressed & ACTION_CONFIRM) || (pad_pressed & ACTION_PAUSE)) {
        soundPlaySFX(SFX_MENU_SELECT);
        fadeOutBlocking(30);
        bgSetDisable(2);

        if (title_cursor == 0) {
            /* NEW GAME: initialize fresh stats */
            rpgStatsInit();
            invInit();

            g_game.current_zone = ZONE_DEBRIS;
            g_game.zones_cleared = 0;
            g_game.story_flags = 0;
            g_game.play_time_seconds = 0;
            g_game.frame_counter = 0;
            g_zone_advance = 0;

            gsFlightEnter();
        } else if (title_cursor == 1 && title_has_save) {
            /* CONTINUE: load save data and start at saved zone */
            if (loadGame()) {
                g_zone_advance = 0;
                gsFlightEnter();
            } else {
                /* Load failed (shouldn't happen), fall back to new game */
                rpgStatsInit();
                invInit();
                g_game.current_zone = ZONE_DEBRIS;
                g_game.zones_cleared = 0;
                g_zone_advance = 0;
                gsFlightEnter();
            }
        }
    }
}

/*===========================================================================*/
/* Flight Mode                                                               */
/*===========================================================================*/

void gsFlightEnter(void)
{
    /* Initialize all flight subsystems */
    bgSystemInit();
    spriteSystemInit();
    scrollInit();
    bulletInit();
    enemyInit();
    collisionInit();
    battleInit();

    /* Load zone graphics (enters force blank internally) */
    bgLoadZone(g_game.current_zone);

    /* Load player ship sprite (still in force blank) */
    playerInit();

    /* Load bullet and enemy graphics (still in force blank) */
    bulletLoadGraphics();
    enemyLoadGraphics(g_game.current_zone);

    /* Register scroll triggers for enemy waves */
    enemySetupZoneTriggers(g_game.current_zone);

    /* Register scroll triggers for story dialog (AFTER enemy triggers) */
    storyRegisterTriggers(g_game.current_zone);

    /* Phase 19: Register boss battle trigger (AFTER story triggers) */
    scrollAddTrigger(BOSS_TRIGGER_DISTANCE, gsOnBossTrigger);
    g_zone_advance = 0;

    /* Set scroll speed (Zone 3 uses FAST for intensity) */
    if (g_game.current_zone == ZONE_FLAGSHIP) {
        scrollSetSpeed(SCROLL_SPEED_FAST);
    } else {
        scrollSetSpeed(SCROLL_SPEED_NORMAL);
    }

    /* Exit force blank and fade in */
    setScreenOn();
    fadeInBlocking(30);

    g_game.current_state = STATE_FLIGHT;
    g_game.paused = 0;

    /* Phase 20: Auto-save on zone entry */
    saveGame();
}

/*===========================================================================*/
/* Zone Advancement (Phase 18)                                               */
/*===========================================================================*/

void gsZoneAdvance(void)
{
    g_zone_advance = 0;

    /* Mark current zone as cleared */
    g_game.zones_cleared++;
    if (g_game.current_zone == ZONE_DEBRIS)
        g_game.story_flags |= STORY_ZONE1_CLEAR;
    else if (g_game.current_zone == ZONE_ASTEROID)
        g_game.story_flags |= STORY_ZONE2_CLEAR;

    /* Check if all zones completed */
    if (g_game.current_zone >= ZONE_COUNT - 1) {
        /* Final zone cleared: Victory! */
        fadeOutBlocking(20);
        gsVictoryEnter();
        return;
    }

    /* Advance to next zone */
    g_game.current_zone++;

    /* Fade out current zone */
    fadeOutBlocking(20);

    /* Full flight re-initialization for new zone.
     * gsFlightEnter handles: subsystem init, graphics loading,
     * trigger registration, scroll speed, fade in. */
    gsFlightEnter();
}

/*===========================================================================*/
/* Victory Screen (Phase 18)                                                 */
/*===========================================================================*/

void gsVictoryEnter(void)
{
    u16 mins;
    u16 secs;

    setScreenOff();

    /* Disable flight BG layers */
    bgSetDisable(0);

    /* Initialize BG3 text system */
    consoleInitText(0, BG_4COLORS, 0, 0);
    bgSetEnable(2);

    /* Draw victory text */
    consoleDrawText(11, 5, "VICTORY!");
    consoleDrawText(5, 7, "THE ARK IS SAVED!");

    /* Phase 20: Show mission stats */
    consoleDrawText(6, 10, "= MISSION STATS =");

    /* Level */
    consoleDrawText(6, 12, "LEVEL:");
    gs_num_buf[0] = '0';
    gs_num_buf[1] = '0' + rpg_stats.level;
    if (rpg_stats.level >= 10) {
        gs_num_buf[0] = '1';
        gs_num_buf[1] = '0' + rpg_stats.level - 10;
    }
    gs_num_buf[2] = 0;
    consoleDrawText(16, 12, gs_num_buf);

    /* Kills */
    consoleDrawText(6, 13, "KILLS:");
    gsNumToStr(rpg_stats.total_kills);
    consoleDrawText(16, 13, gs_num_buf);

    /* Play time (MM:SS) - use subtraction instead of division (65816) */
    consoleDrawText(6, 14, "TIME:");
    mins = 0;
    secs = g_game.play_time_seconds;
    while (secs >= 60) { secs -= 60; mins++; }
    gs_num_buf[0] = '0';
    while (mins >= 10) { mins -= 10; gs_num_buf[0]++; }
    gs_num_buf[1] = '0' + (u8)mins;
    gs_num_buf[2] = ':';
    gs_num_buf[3] = '0';
    while (secs >= 10) { secs -= 10; gs_num_buf[3]++; }
    gs_num_buf[4] = '0' + (u8)secs;
    gs_num_buf[5] = 0;
    consoleDrawText(16, 14, gs_num_buf);

    consoleDrawText(6, 18, "PRESS START");

    /* Erase save data (game is complete) */
    saveErase();

    /* Show screen and fade in */
    setScreenOn();
    fadeInBlocking(30);

    g_game.current_state = STATE_VICTORY;
}

void gsVictoryUpdate(u16 pad_pressed)
{
    /* Blink "PRESS START" */
    if ((g_frame_count & 0x1F) < 0x10) {
        consoleDrawText(6, 18, "PRESS START");
    } else {
        consoleDrawText(6, 18, "           ");
    }

    /* Start button returns to title screen */
    if (pad_pressed & ACTION_PAUSE) {
        soundPlaySFX(SFX_MENU_SELECT);
        fadeOutBlocking(30);
        bgSetDisable(2);

        /* Reset game state for fresh start */
        rpgStatsInit();
        invInit();

        gsTitleEnter();
    }
}

/*===========================================================================*/
/* Game Over Screen                                                          */
/*===========================================================================*/

void gsGameOverEnter(void)
{
    /* Screen is already dark from battle defeat exit.
     * BG1 is corrupted by font, BG2 stars may still be visible. */
    setScreenOff();

    /* Keep BG1 disabled (tiles corrupted by font at 0x3000) */
    bgSetDisable(0);
    /* BG2 stars stay as backdrop */

    /* Re-initialize BG3 text system */
    consoleInitText(0, BG_4COLORS, 0, 0);
    bgSetEnable(2);

    /* Draw game over text */
    consoleDrawText(11, 8, "GAME OVER");

    /* Phase 20: Menu options */
    consoleDrawText(8, 14, "> RETRY ZONE");
    consoleDrawText(8, 16, "  TITLE");
    go_cursor = 0;

    /* Show screen and fade in */
    setScreenOn();
    fadeInBlocking(30);

    g_game.current_state = STATE_GAMEOVER;
}

void gsGameOverUpdate(u16 pad_pressed)
{
    /* Clear old cursor positions */
    consoleDrawText(8, 14, " ");
    consoleDrawText(8, 16, " ");

    /* Navigate menu */
    if (pad_pressed & ACTION_UP) {
        if (go_cursor > 0) {
            go_cursor--;
            soundPlaySFX(SFX_MENU_MOVE);
        }
    }
    if (pad_pressed & ACTION_DOWN) {
        if (go_cursor < 1) {
            go_cursor++;
            soundPlaySFX(SFX_MENU_MOVE);
        }
    }

    /* Draw cursor */
    consoleDrawText(8, 14 + (go_cursor << 1), ">");

    /* Confirm selection */
    if ((pad_pressed & ACTION_CONFIRM) || (pad_pressed & ACTION_PAUSE)) {
        soundPlaySFX(SFX_MENU_SELECT);
        fadeOutBlocking(30);
        bgSetDisable(2);

        if (go_cursor == 0) {
            /* RETRY ZONE: restore HP/SP and restart current zone */
            rpg_stats.hp = rpg_stats.max_hp;
            rpg_stats.sp = rpg_stats.max_sp;
            g_zone_advance = 0;
            gsFlightEnter();
        } else {
            /* TITLE: full reset */
            rpgStatsInit();
            invInit();
            gsTitleEnter();
        }
    }
}

/*===========================================================================*/
/* Pause (Flight Only)                                                       */
/*===========================================================================*/

void gsPauseToggle(void)
{
    if (g_game.paused) {
        g_game.paused = 0;
        setBrightness(15);   /* Restore full brightness */
    } else {
        g_game.paused = 1;
        setBrightness(8);    /* Dim screen to indicate pause */
    }
}
