/*==============================================================================
 * Game State Machine - Phase 20
 *
 * Manages master game flow: Title -> Flight -> Battle -> GameOver/Victory.
 * Defines g_game (declared extern in game.h).
 *
 * Title Screen (Phase 20):
 *   Multi-phase SNES-style intro sequence:
 *     Phase A: "VEXCORP PRESENTS" splash on black (fade in/hold/fade out).
 *     Phase B: Stars + dividers fade in, letter-by-letter title reveal,
 *              then "PRESS START" blinks until player proceeds.
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
 * Each screen follows the same pattern:
 *   1. gs*Enter() - Blocking setup: force-blank, configure BG1 text,
 *      draw static elements, fade in. Sets g_game.current_state.
 *   2. gs*Update() - Per-frame update: handle input, animate cursor/text.
 *      Called from the main loop's state machine dispatch.
 *
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#include "game/game_state.h"
#include "assets.h"
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

/*=== Zone advance flag ===*/
/* Set to 1 by the boss victory handler to signal the main loop that
 * the player should advance to the next zone.  Checked each frame
 * during STATE_FLIGHT with priority over dialog and battle triggers. */
u8 g_zone_advance;

/*=== Boss battle trigger distance ===*/
/* The scroll distance (in pixels) at which the boss battle is triggered.
 * Placed after all enemy wave triggers and story dialog triggers. */
#define BOSS_TRIGGER_DISTANCE 4800

/*=== Title menu state (Phase 20) ===*/
static u8 title_cursor;        /* 0 = NEW GAME selected, 1 = CONTINUE selected */
static u8 title_has_save;      /* 1 if saveExists() found valid SRAM data */
static u8 title_phase;         /* 0 = PRESS START, 1 = menu */
static u16 title_scroll_fp;    /* 8.8 fixed-point BG2 Y position for star drift */

/*=== Game Over menu state (Phase 20) ===*/
static u8 go_cursor;           /* 0 = RETRY ZONE selected, 1 = TITLE selected */

/*=== Victory screen state (Phase 20) ===*/
static u8 victory_stats_shown; /* 1 after all stats have been drawn */

/* Count-up animation variables: display values increment each frame
 * toward their target values for a slot-machine visual effect. */
static u16 victory_display_kills;   /* Currently displayed kill count */
static u16 victory_target_kills;    /* Final kill count to reach */
static u16 victory_display_score;   /* Currently displayed score value */
static u16 victory_target_score;    /* Final score to reach */
static u8  victory_anim_done;       /* 1 when count-up animation is complete */

/*=== Shared number-to-string buffer ===*/
/* Used by gsNumToStr() and inline number formatting throughout this file.
 * Large enough for 4-digit numbers plus null terminator (+1 extra for MM:SS). */
static char gs_num_buf[8];

/*=== Cursor sine LUT for menu bounce effect ===*/
/* 8-entry sine approximation used to make the ">" cursor gently bob
 * horizontally.  Sampled at (frame_count >> 3) & 7 to produce a
 * smooth ~1-second period oscillation.  Values are pixel offsets. */
static const s8 cursor_sine[8] = { 0, 1, 2, 2, 2, 1, 0, -1 };

/*--- Forward declaration ---*/
static void gsShowZoneSplash(void);

/*
 * gsOnBossTrigger - Scroll trigger callback for the boss battle.
 *
 * Registered as a scroll trigger at BOSS_TRIGGER_DISTANCE pixels.
 * Sets g_battle_trigger to the boss ID for the current zone, which
 * is picked up by the main loop's collision/battle check.
 *
 * BOSS_TRIGGER_BASE is defined in boss.h; adding current_zone gives
 * the zone-specific boss (boss 0 for Zone 1, boss 1 for Zone 2, etc.).
 */
static void gsOnBossTrigger(void)
{
    g_battle_trigger = BOSS_TRIGGER_BASE + g_game.current_zone;
}

/*===========================================================================*/
/* Number to string helper                                                   */
/*===========================================================================*/

/*
 * gsNumToStr - Convert a u16 value to a 4-digit decimal string.
 *
 * Writes into the shared gs_num_buf[6] buffer.  Uses repeated subtraction
 * instead of division because the 65816 has no hardware divide instruction.
 * Values above 9999 are clamped to 9999 to fit 4 digits.
 *
 * val: The number to convert (0-9999).
 */
static void gsNumToStr(u16 val)
{
    u16 v;
    v = val;
    if (v > 9999) v = 9999;  /* Clamp to 4-digit display */

    /* Extract thousands digit via repeated subtraction */
    gs_num_buf[0] = '0';
    while (v >= 1000) { v -= 1000; gs_num_buf[0]++; }
    /* Extract hundreds digit */
    gs_num_buf[1] = '0';
    while (v >= 100) { v -= 100; gs_num_buf[1]++; }
    /* Extract tens digit */
    gs_num_buf[2] = '0';
    while (v >= 10) { v -= 10; gs_num_buf[2]++; }
    /* Ones digit is the remainder */
    gs_num_buf[3] = '0' + (u8)v;
    gs_num_buf[4] = 0;  /* Null terminator */
}

/*===========================================================================*/
/* Initialize                                                                */
/*===========================================================================*/

/*
 * gsInit - Initialize the master game state machine.
 *
 * Sets all GameState fields to their boot defaults, initializes the
 * dialog and story subsystems, and prepares the save system.
 * Called once from main() before entering the title screen.
 */
void gsInit(void)
{
    g_game.current_state = STATE_BOOT;
    g_game.previous_state = STATE_BOOT;
    g_game.current_zone = ZONE_DEBRIS;   /* First zone */
    g_game.zones_cleared = 0;
    g_game.paused = 0;
    g_game.story_flags = 0;              /* No story events seen yet */
    g_game.frame_counter = 0;
    g_game.play_time_seconds = 0;
    g_zone_advance = 0;

    /* Initialize dialog and story subsystems (Phase 16) */
    dlgInit();
    storyInit();

    /* Initialize save system (Phase 20): no-op since SRAM is persistent */
    saveInit();
}

/*===========================================================================*/
/* Title Screen                                                              */
/*===========================================================================*/

/*
 * titleWaitFrameWithStars - Wait one frame while keeping stars alive.
 *
 * Used during blocking intro sequences in gsTitleEnter() to maintain
 * the star scroll drift and twinkle animation.  Calls WaitForVBlank()
 * to sync, then applies palette updates, advances the BG2 scroll
 * position, and runs the twinkle palette cycle.
 */
static void titleWaitFrameWithStars(void)
{
    WaitForVBlank();
    bgVBlankUpdate();
    title_scroll_fp += SCROLL_SPEED_SLOW;
    bgSetScroll(1, 0, FP8_INT(title_scroll_fp));
    bgUpdate();
}

/*
 * gsTitleEnter - Multi-phase blocking intro sequence for the title screen.
 *
 * Plays a traditional SNES-style intro sequence:
 *   Phase A: Company splash ("VEXCORP PRESENTS") on black, fade in/out.
 *   Phase B: Stars + dividers fade in, title text reveals letter-by-letter,
 *            copyright drawn, then control returns to gsTitleUpdate().
 *
 * Total blocking time: ~5.7 seconds (standard for SNES intros).
 * Stars animate throughout Phase B via titleWaitFrameWithStars().
 *
 * On return, title_phase is set to 0 (PRESS START) and gsTitleUpdate()
 * takes over with per-frame input handling.
 */
void gsTitleEnter(void)
{
    u8 i, j, len;
    char reveal_buf[18];

    /*=== Phase A: Company splash (text on black, no stars) ===*/
    setScreenOff();
    bgSetDisable(1);  /* No BG2 stars during splash */

    /* Configure BG1 for text display using PVSnesLib 4bpp console font */
    consoleSetTextMapPtr(VRAM_TEXT_MAP);
    consoleSetTextGfxPtr(VRAM_TEXT_GFX);
    consoleSetTextOffset(0x0100);
    consoleInitText(0, 16 * 2, &snesfont, &snespal);
    bgSetGfxPtr(0, VRAM_BG1_GFX);
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);
    bgSetEnable(0);

    /* Draw "VEXCORP PRESENTS" centered (row 13) */
    consoleDrawText(6, 13, "VEXCORP PRESENTS");

    /* Fade in, hold, fade out */
    setScreenOn();
    fadeInBlocking(40);
    systemWaitFrames(90);
    fadeOutBlocking(40);

    /*=== Phase B: Title reveal with animated stars ===*/
    setScreenOff();
    bgLoadStarsOnly();
    scrollSetSpeed(SCROLL_SPEED_SLOW);
    title_scroll_fp = 0;

    /* Re-init BG1 text (clears company splash tilemap) */
    consoleSetTextMapPtr(VRAM_TEXT_MAP);
    consoleSetTextGfxPtr(VRAM_TEXT_GFX);
    consoleSetTextOffset(0x0100);
    consoleInitText(0, 16 * 2, &snesfont, &snespal);
    bgSetGfxPtr(0, VRAM_BG1_GFX);
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);
    bgSetEnable(0);

    /* Draw dividers */
    consoleDrawText(2, 3, "========================");
    consoleDrawText(2, 9, "========================");

    /* Fade in: stars + dividers visible */
    setScreenOn();
    fadeInBlocking(40);

    /* 20-frame pause with stars animating */
    for (i = 0; i < 20; i++) titleWaitFrameWithStars();

    /* Letter-by-letter reveal "VEX DEFENDER" at 3 frames/char (row 5) */
    len = 12;
    for (i = 0; i <= len; i++) {
        for (j = 0; j < i; j++) reveal_buf[j] = "VEX DEFENDER"[j];
        reveal_buf[i] = 0;
        consoleDrawText(7, 5, reveal_buf);
        titleWaitFrameWithStars();
        titleWaitFrameWithStars();
        titleWaitFrameWithStars();
    }

    /* 15-frame pause */
    for (i = 0; i < 15; i++) titleWaitFrameWithStars();

    /* Letter-by-letter reveal "A  SPACE  ODYSSEY" at 2 frames/char (row 7) */
    len = 17;
    for (i = 0; i <= len; i++) {
        for (j = 0; j < i; j++) reveal_buf[j] = "A  SPACE  ODYSSEY"[j];
        reveal_buf[i] = 0;
        consoleDrawText(5, 7, reveal_buf);
        titleWaitFrameWithStars();
        titleWaitFrameWithStars();
    }

    /* 20-frame pause */
    for (i = 0; i < 20; i++) titleWaitFrameWithStars();

    /* Draw copyright */
    consoleDrawText(1, 25, "(C)2026 Ryan Rentfro - rrentfro@gmail.com");

    /* Check save data and set initial phase */
    title_has_save = saveExists();
    title_cursor = 0;
    title_phase = 0;  /* Start with PRESS START phase */

    g_game.current_state = STATE_TITLE;
}

/*
 * gsTitleUpdate - Per-frame title screen update (two-phase dispatch).
 *
 * Star animation (scroll + twinkle) runs unconditionally at the top.
 *
 * Phase 0 (PRESS START): Blinks "PRESS START" on a 32-frame cycle.
 *   Start or A → play SFX, draw menu, transition to Phase 1.
 *
 * Phase 1 (Menu): Cursor navigation between NEW GAME / CONTINUE.
 *   Sine-wave bounce cursor with blink.  Confirm → fadeOut, disable
 *   BG layers, transition to flight mode.
 *
 * pad_pressed: Edge-triggered controller input.
 */
void gsTitleUpdate(u16 pad_pressed)
{
    u8 col_offset;

    /* Star animation runs unconditionally every frame */
    title_scroll_fp += SCROLL_SPEED_SLOW;
    bgSetScroll(1, 0, FP8_INT(title_scroll_fp));
    bgUpdate();

    if (title_phase == 0) {
        /*=== Phase 0: PRESS START ===*/

        /* Blink "PRESS START" on 32-frame cycle (16 on, 16 off) */
        if ((g_frame_count & 0x1F) < 0x10) {
            consoleDrawText(10, 18, "PRESS START");
        } else {
            consoleDrawText(10, 18, "           ");
        }

        /* Start or A button → reveal menu */
        if ((pad_pressed & ACTION_CONFIRM) || (pad_pressed & ACTION_PAUSE)) {
            soundPlaySFX(SFX_MENU_SELECT);

            /* Clear "PRESS START" text */
            consoleDrawText(10, 18, "           ");

            /* Draw menu options */
            consoleDrawText(7, 14, "> NEW GAME");
            if (title_has_save) {
                u8 saved_level;
                u8 saved_zone;
                consoleDrawText(7, 16, "  CONTINUE");
                saved_level = saveGetLevel();
                saved_zone = saveGetZone();
                consoleDrawText(18, 16, "L");
                gs_num_buf[0] = '0';
                gs_num_buf[1] = '0' + saved_level;
                if (saved_level >= 10) { gs_num_buf[0] = '1'; gs_num_buf[1] = '0' + saved_level - 10; }
                gs_num_buf[2] = 0;
                consoleDrawText(19, 16, gs_num_buf);
                gs_num_buf[0] = 'Z';
                gs_num_buf[1] = '1' + saved_zone;
                gs_num_buf[2] = 0;
                consoleDrawText(22, 16, gs_num_buf);
            }

            title_phase = 1;
        }
    } else {
        /*=== Phase 1: Menu (existing behavior preserved) ===*/

        /* Clear cursor from both possible positions to prevent ghosting */
        consoleDrawText(7, 14, " ");
        consoleDrawText(7, 16, " ");
        consoleDrawText(8, 14, " ");  /* Clear offset position too */
        consoleDrawText(8, 16, " ");

        /* D-pad up: move cursor to NEW GAME */
        if (pad_pressed & ACTION_UP) {
            if (title_cursor > 0) {
                title_cursor--;
                soundPlaySFX(SFX_MENU_MOVE);
            }
        }
        /* D-pad down: move cursor to CONTINUE (only if save exists) */
        if (pad_pressed & ACTION_DOWN) {
            if (title_cursor < 1 && title_has_save) {
                title_cursor++;
                soundPlaySFX(SFX_MENU_MOVE);
            }
        }

        /* Draw cursor with sine-wave horizontal bounce + blink */
        col_offset = (cursor_sine[(g_frame_count >> 3) & 7] > 1) ? 1 : 0;
        if ((g_frame_count & 0x1F) < 0x18) {
            consoleDrawText(7 + col_offset, 14 + (title_cursor << 1), ">");
        }

        /* Confirm selection (A button or Start button) */
        if ((pad_pressed & ACTION_CONFIRM) || (pad_pressed & ACTION_PAUSE)) {
            soundPlaySFX(SFX_MENU_SELECT);
            fadeOutBlocking(40);
            bgSetDisable(0);  /* Disable text BG1 before scene change */
            bgSetDisable(1);  /* Disable star BG2 before scene change */
            title_scroll_fp = 0;  /* Reset scroll counter for next title entry */

            if (title_cursor == 0) {
                /* NEW GAME: initialize fresh stats and inventory */
                rpgStatsInit();
                invInit();

                /* Reset game progress to initial state */
                g_game.current_zone = ZONE_DEBRIS;
                g_game.zones_cleared = 0;
                g_game.story_flags = 0;
                g_game.play_time_seconds = 0;
                g_game.frame_counter = 0;
                g_game.max_combo = 0;
                g_game.zone_ranks[0] = 0;
                g_game.zone_ranks[1] = 0;
                g_game.zone_ranks[2] = 0;
                g_game.zone_no_damage = 1;
                g_game.zone_start_score = 0;
                g_zone_advance = 0;

                /* Show zone name splash, then enter flight mode */
                gsShowZoneSplash();
                gsFlightEnter();
            } else if (title_cursor == 1 && title_has_save) {
                /* CONTINUE: load saved data and resume at the saved zone */
                if (loadGame()) {
                    g_zone_advance = 0;
                    gsFlightEnter();
                } else {
                    /* Load failed (shouldn't happen after saveExists() passed),
                     * fall back to a new game as a safety net */
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
}

/*===========================================================================*/
/* Flight Mode                                                               */
/*===========================================================================*/

/*
 * gsFlightEnter - Initialize and enter the flight (shoot-em-up) gameplay state.
 *
 * This is the most complex state entry because it initializes all gameplay
 * subsystems: backgrounds, sprites, scrolling, bullets, enemies, collision,
 * and battle.  It also registers scroll triggers for enemy waves, story
 * dialogs, and the boss battle.
 *
 * The initialization order matters:
 *   1. Background system (bgSystemInit + bgLoadZone) - must be first since
 *      it enters force-blank and loads tile/palette/map data into VRAM
 *   2. Player sprite (playerInit) - loads player ship tile data while
 *      still in force-blank
 *   3. Bullet and enemy graphics (bulletLoadGraphics + enemyLoadGraphics) -
 *      loads projectile and enemy tile data while still in force-blank
 *   4. Scroll triggers (enemySetupZoneTriggers, storyRegisterTriggers,
 *      boss trigger) - must be registered in this order because they
 *      share the same trigger array
 *
 * After setup, the screen fades in and the auto-save fires.
 */
void gsFlightEnter(void)
{
    /* Initialize all flight subsystems (clears internal state) */
    bgSystemInit();
    spriteSystemInit();
    scrollInit();
    bulletInit();
    enemyInit();
    collisionInit();
    battleInit();

    /* Load zone background graphics (BG1 tiles + palette + tilemap).
     * This enters force-blank internally for VRAM DMA transfers. */
    bgLoadZone(g_game.current_zone);

    /* Load player ship sprite (tile data + palette into OBJ VRAM) */
    playerInit();

    /* Load bullet and enemy sprite graphics (still in force-blank) */
    bulletLoadGraphics();
    enemyLoadGraphics(g_game.current_zone);

    /* Register scroll triggers for enemy wave spawns at predefined distances */
    enemySetupZoneTriggers(g_game.current_zone);

    /* Register scroll triggers for story dialog events (AFTER enemy triggers
     * to avoid overwriting them -- triggers share a fixed-size array) */
    storyRegisterTriggers(g_game.current_zone);

    /* Register the boss battle trigger at the end of the zone.
     * Must be last because it fires after all waves and story events. */
    scrollAddTrigger(BOSS_TRIGGER_DISTANCE, gsOnBossTrigger);
    g_zone_advance = 0;

    /* Reset SP regen counter so timing is consistent from zone start */
    rpgRegenResetCounter();

    /* Zone 3 (Flagship) scrolls faster for increased intensity/urgency */
    if (g_game.current_zone == ZONE_FLAGSHIP) {
        scrollSetSpeed(SCROLL_SPEED_FAST);
    } else {
        scrollSetSpeed(SCROLL_SPEED_NORMAL);
    }

    /* Exit force-blank and fade in over 40 frames */
    setScreenOn();
    fadeInBlocking(40);

    g_game.current_state = STATE_FLIGHT;
    g_game.paused = 0;

    /* #155: Reset no-damage tracking for this zone */
    g_game.zone_no_damage = 1;

    /* #162: Record score at zone start for rank calculation */
    g_game.zone_start_score = g_score;

    /* Auto-save to SRAM on zone entry.
     * This is the primary save trigger: if the player dies or quits,
     * they can CONTINUE from the start of this zone. */
    saveGame();
}

/*===========================================================================*/
/* Zone Name Splash Display                                                  */
/*===========================================================================*/

/*--- Zone name strings for the splash screen ---*/
static char *zone_names[ZONE_COUNT] = {
    "DEBRIS FIELD", "ASTEROID BELT", "FLAGSHIP"
};

/*
 * gsShowZoneSplash - Display a brief zone name splash screen.
 *
 * Shows "ZONE N" with a letter-by-letter reveal of the zone name below it.
 * Each letter is revealed at 2 frames per character for a cinematic effect.
 * The splash is held for 30 frames after the full name is shown, then
 * fades out. This builds anticipation before entering the new zone.
 *
 * Uses the same BG1 text setup pattern as the title/game-over screens.
 */
static void gsShowZoneSplash(void)
{
    u8 zone;
    u8 i, j, len;
    char reveal_buf[16];

    zone = g_game.current_zone;

    /* Set up BG1 text layer (same pattern as other text screens) */
    setScreenOff();

    /* Disable BG2 (star parallax) for a clean text-only splash screen */
    bgSetDisable(1);

    consoleSetTextMapPtr(VRAM_TEXT_MAP);
    consoleSetTextGfxPtr(VRAM_TEXT_GFX);
    consoleSetTextOffset(0x0100);
    consoleInitText(0, 16 * 2, &snesfont, &snespal);
    bgSetGfxPtr(0, VRAM_BG1_GFX);
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);
    bgSetEnable(0);

    /* Build and draw "ZONE N" header where N is 1-based zone number */
    gs_num_buf[0] = 'Z';
    gs_num_buf[1] = 'O';
    gs_num_buf[2] = 'N';
    gs_num_buf[3] = 'E';
    gs_num_buf[4] = ' ';
    gs_num_buf[5] = '1' + zone;  /* Convert 0-based zone to '1', '2', '3' */
    gs_num_buf[6] = 0;           /* Null terminator */
    consoleDrawText(12, 10, gs_num_buf);

    /* Fade in to show "ZONE N" */
    setScreenOn();
    fadeInBlocking(20);

    /* Letter-by-letter reveal of the full zone name.
     * Each iteration adds one more character to reveal_buf and redraws
     * the partial string.  Two WaitForVBlank() calls per letter = 2 frames
     * per character at 60fps (~33ms between each letter). */
    if (zone < ZONE_COUNT) {
        /* Calculate zone name length (manual strlen, capped at 15) */
        len = 0;
        while (zone_names[zone][len] != 0 && len < 15) len++;

        for (i = 0; i <= len; i++) {
            /* Copy characters 0..i-1 into the reveal buffer */
            for (j = 0; j < i; j++) {
                reveal_buf[j] = zone_names[zone][j];
            }
            reveal_buf[i] = 0;  /* Null terminate at current length */
            consoleDrawText(10, 12, reveal_buf);
            WaitForVBlank();
            WaitForVBlank();  /* 2 frames per letter for pacing */
        }
    }

    /* Hold the complete name on screen for 30 frames (~0.5 seconds) */
    systemWaitFrames(30);
    /* Fade out before transitioning to flight mode */
    fadeOutBlocking(20);
}

/*===========================================================================*/
/* Zone Advancement (Phase 18)                                               */
/*===========================================================================*/

/*
 * gsZoneAdvance - Advance the player to the next zone after boss defeat.
 *
 * Marks the current zone as cleared (sets story flags), checks if this
 * was the final zone (triggers victory), or advances to the next zone
 * with a splash screen and full flight re-initialization.
 *
 * Called from the main loop when g_zone_advance is set (by boss defeat)
 * or directly from the STATE_BATTLE handler after a boss victory.
 */
void gsZoneAdvance(void)
{
    /* Cache current_zone in a local to avoid repeated struct field loads
     * on the 65816 (struct access requires indirect addressing). */
    u8 zone = g_game.current_zone;

    g_zone_advance = 0;  /* Consume the advance flag */

    /* #155: No-damage zone bonus - award 50 XP + Full Restore if untouched */
    if (g_game.zone_no_damage) {
        rpgAddXP(50);
        invAdd(ITEM_FULL_RESTORE, 1);
        soundPlaySFX(SFX_LEVEL_UP);
    }

    /* #162: Calculate zone score rank (S/A/B/C/D) */
    {
        static const u16 rank_thresholds[3][4] = {
            { 5000, 3000, 1500, 500 },   /* Zone 0: S/A/B/C thresholds */
            { 8000, 5000, 2500, 1000 },   /* Zone 1 */
            { 12000, 8000, 4000, 1500 }   /* Zone 2 */
        };
        u16 zone_score;
        u8 zi = zone;
        if (zi >= 3) zi = 0;
        zone_score = g_score - g_game.zone_start_score;
        if (zone_score >= rank_thresholds[zi][0]) g_game.zone_ranks[zi] = 4;       /* S */
        else if (zone_score >= rank_thresholds[zi][1]) g_game.zone_ranks[zi] = 3;  /* A */
        else if (zone_score >= rank_thresholds[zi][2]) g_game.zone_ranks[zi] = 2;  /* B */
        else if (zone_score >= rank_thresholds[zi][3]) g_game.zone_ranks[zi] = 1;  /* C */
        else g_game.zone_ranks[zi] = 0;                                             /* D */
    }

    /* #165: Zone clear HP recovery - restore 50% of missing HP */
    {
        s16 missing;
        s16 heal;
        missing = rpg_stats.max_hp - rpg_stats.hp;
        heal = missing >> 1;  /* 50% of missing */
        rpg_stats.hp += heal;
    }

    /* #170: Credit-to-score conversion - 1 credit = 2 score points */
    if (rpg_stats.credits > 0) {
        u16 credit_bonus;
        credit_bonus = rpg_stats.credits << 1;
        if (g_score > (u16)(0xFFFF - credit_bonus)) g_score = 0xFFFF;
        else g_score += credit_bonus;
        rpg_stats.credits = 0;
        soundPlaySFX(SFX_MENU_SELECT);  /* #213: Audio feedback for credit conversion */
    }

    /* Mark current zone as cleared */
    g_game.zones_cleared++;
    if (zone == ZONE_DEBRIS)
        g_game.story_flags |= STORY_ZONE1_CLEAR;
    else if (zone == ZONE_ASTEROID)
        g_game.story_flags |= STORY_ZONE2_CLEAR;

    /* Check if all zones completed (final zone is ZONE_COUNT - 1) */
    if (zone >= ZONE_COUNT - 1) {
        /* Final zone cleared: transition to victory screen */
        fadeOutBlocking(30);
        gsVictoryEnter();
        return;
    }

    /* Advance to next zone */
    g_game.current_zone++;

    /* Fade out current zone */
    fadeOutBlocking(30);

    /* Show zone name splash screen for the new zone */
    gsShowZoneSplash();

    /* Full flight re-initialization for the new zone.
     * gsFlightEnter() handles everything: subsystem init, graphics loading,
     * trigger registration, scroll speed, fade in, and auto-save. */
    gsFlightEnter();
}

/*===========================================================================*/
/* Victory Screen (Phase 18)                                                 */
/*===========================================================================*/

/*
 * gsVictoryEnter - Set up and display the victory screen.
 *
 * Shows "VICTORY!" with mission stats: level, kills, play time, and score.
 * Kills and score use a count-up animation for dramatic effect.
 * Play time is displayed as MM:SS, computed via repeated subtraction
 * (no division on 65816).
 *
 * The save data is erased here because the game is "complete" -- the
 * CONTINUE option should not be available after finishing the game.
 */
void gsVictoryEnter(void)
{
    u16 mins;
    u16 secs;

    setScreenOff();

    /* Disable BG2 (star parallax) for a clean text-only screen */
    bgSetDisable(1);

    /* Set up BG1 text layer (standard pattern) */
    consoleSetTextMapPtr(VRAM_TEXT_MAP);
    consoleSetTextGfxPtr(VRAM_TEXT_GFX);
    consoleSetTextOffset(0x0100);
    consoleInitText(0, 16 * 2, &snesfont, &snespal);
    bgSetGfxPtr(0, VRAM_BG1_GFX);
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);
    bgSetEnable(0);

    /* Draw victory header */
    consoleDrawText(11, 5, "VICTORY!");
    consoleDrawText(5, 7, "THE ARK IS SAVED!");

    /* Draw mission stats header */
    consoleDrawText(6, 10, "= MISSION STATS =");

    /* Display player level (simple 2-digit formatting) */
    consoleDrawText(6, 12, "LEVEL:");
    gs_num_buf[0] = '0';
    gs_num_buf[1] = '0' + rpg_stats.level;
    if (rpg_stats.level >= 10) {
        gs_num_buf[0] = '1';
        gs_num_buf[1] = '0' + rpg_stats.level - 10;
    }
    gs_num_buf[2] = 0;
    consoleDrawText(16, 12, gs_num_buf);

    /* Kills and score start at "0000" for count-up animation */
    consoleDrawText(6, 13, "KILLS:");
    consoleDrawText(16, 13, "0000");
    consoleDrawText(6, 15, "SCORE:");
    consoleDrawText(16, 15, "0000");

    /* Display play time in MM:SS format.
     * Uses repeated subtraction to convert seconds to minutes+seconds
     * because the 65816 has no hardware divide instruction. */
    consoleDrawText(6, 14, "TIME:");
    mins = 0;
    secs = g_game.play_time_seconds;
    /* #139: Cap play time display at 99:59 (5999 seconds).
     * Prevents display overflow beyond 2-digit minutes. */
    if (secs > 5999) secs = 5999;
    /* #139: Fast-path subtraction: subtract 600 (10 minutes) first
     * to reduce iterations for long play times. */
    while (secs >= 600) { secs -= 600; mins += 10; }
    while (secs >= 60) { secs -= 60; mins++; }  /* Divide by 60 via subtraction */
    /* Format minutes (2 digits) */
    gs_num_buf[0] = '0';
    while (mins >= 10) { mins -= 10; gs_num_buf[0]++; }  /* Tens digit */
    gs_num_buf[1] = '0' + (u8)mins;                       /* Ones digit */
    gs_num_buf[2] = ':';                                   /* Separator */
    /* Format seconds (2 digits) */
    gs_num_buf[3] = '0';
    while (secs >= 10) { secs -= 10; gs_num_buf[3]++; }  /* Tens digit */
    gs_num_buf[4] = '0' + (u8)secs;                       /* Ones digit */
    gs_num_buf[5] = 0;                                     /* Null terminator */
    consoleDrawText(16, 14, gs_num_buf);

    /* Initialize count-up animation: display values start at 0 and
     * increment toward the target values in gsVictoryUpdate(). */
    victory_display_kills = 0;
    victory_target_kills = rpg_stats.total_kills;
    victory_display_score = 0;
    victory_target_score = g_score;
    victory_anim_done = 0;

    /* #199: Display per-zone ranks (S/A/B/C/D) */
    {
        static const char rank_chars[5] = { 'D', 'C', 'B', 'A', 'S' };
        u8 ri;
        for (ri = 0; ri < 3; ri++) {
            u8 r = g_game.zone_ranks[ri];
            if (r > 4) r = 0;
            gs_num_buf[0] = 'Z';
            gs_num_buf[1] = '1' + ri;
            gs_num_buf[2] = ':';
            gs_num_buf[3] = rank_chars[r];
            gs_num_buf[4] = 0;
            consoleDrawText(5 + (ri << 3), 17, gs_num_buf);
        }
    }

    /* #218: Display max combo achieved */
    consoleDrawText(6, 16, "COMBO:");
    gsNumToStr((u16)g_game.max_combo);
    consoleDrawText(16, 16, gs_num_buf);

    /* "PRESS START" prompt (will blink in gsVictoryUpdate) */
    consoleDrawText(6, 19, "PRESS START");

    /* Erase save data: the game is complete, so CONTINUE should no longer
     * appear on the title screen. */
    saveErase();

    /* Fade in and set state */
    setScreenOn();
    fadeInBlocking(40);

    g_game.current_state = STATE_VICTORY;
}

/*
 * gsVictoryUpdate - Per-frame victory screen update.
 *
 * Animates the kills and score counters counting up from 0 to their
 * final values (kills increment by 1 per frame, score by 5 per frame).
 * A subtle brightness pulse occurs during the count-up for visual flair.
 * The "PRESS START" text blinks on a 32-frame cycle.
 *
 * Pressing Start returns to the title screen with fresh stats.
 *
 * pad_pressed: Edge-triggered controller input.
 */
void gsVictoryUpdate(u16 pad_pressed)
{
    /* Animate the kill and score count-up */
    if (!victory_anim_done) {
        u8 changed = 0;
        u8 pulse;

        /* Increment kills display by 1 per frame toward target */
        if (victory_display_kills < victory_target_kills) {
            victory_display_kills++;
            if (victory_display_kills > victory_target_kills)
                victory_display_kills = victory_target_kills;
            gsNumToStr(victory_display_kills);
            consoleDrawText(16, 13, gs_num_buf);
            changed = 1;
        }

        /* Increment score display by 5 per frame (faster count for large values) */
        if (victory_display_score < victory_target_score) {
            victory_display_score += 5;
            if (victory_display_score > victory_target_score)
                victory_display_score = victory_target_score;
            gsNumToStr(victory_display_score);
            consoleDrawText(16, 15, gs_num_buf);
            changed = 1;
        }

        /* Brightness pulse during count-up: oscillate between 13-15.
         * The low 2 bits of the combined counters create a subtle flicker
         * that makes the counting feel more dynamic. */
        if (changed) {
            pulse = 13 + ((victory_display_kills + victory_display_score) & 3);
            if (pulse > 15) pulse = 15;
            setBrightness(pulse);
        }

        /* Both counters have reached their targets */
        if (!changed) {
            victory_anim_done = 1;
            setBrightness(15);  /* Restore full brightness */
        }
    }

    /* Blink "PRESS START" on a 32-frame cycle (visible for 16, hidden for 16).
     * Uses bitwise AND for efficient modulo on the 65816. */
    if ((g_frame_count & 0x1F) < 0x10) {
        consoleDrawText(6, 19, "PRESS START");
    } else {
        consoleDrawText(6, 19, "           ");
    }

    /* Start button: return to title screen */
    if (pad_pressed & ACTION_PAUSE) {
        soundPlaySFX(SFX_MENU_SELECT);
        fadeOutBlocking(40);
        bgSetDisable(0);

        /* Reset all game state for a fresh start */
        rpgStatsInit();
        invInit();

        gsTitleEnter();
    }
}

/*===========================================================================*/
/* Game Over Screen                                                          */
/*===========================================================================*/

/*
 * gsGameOverEnter - Set up and display the game-over screen.
 *
 * Shows "GAME OVER" text with the player's level and kill count,
 * plus a two-option menu: RETRY ZONE (restart current zone with
 * full HP/SP) or TITLE (return to title with fresh stats).
 *
 * The screen is expected to already be dark from the battle defeat
 * exit transition, so we enter force-blank and set up text without
 * needing to fade out first.
 */
void gsGameOverEnter(void)
{
    /* Screen is already dark from battle defeat exit. */
    setScreenOff();

    /* BG2 (star parallax) stays enabled as a backdrop behind the text */

    /* Set up BG1 text layer (standard pattern) */
    consoleSetTextMapPtr(VRAM_TEXT_MAP);
    consoleSetTextGfxPtr(VRAM_TEXT_GFX);
    consoleSetTextOffset(0x0100);
    consoleInitText(0, 16 * 2, &snesfont, &snespal);
    bgSetGfxPtr(0, VRAM_BG1_GFX);
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);
    bgSetEnable(0);

    /* Draw game over header */
    consoleDrawText(11, 8, "GAME OVER");

    /* Show brief stats: level and total kills */
    consoleDrawText(6, 10, "LV:");
    gs_num_buf[0] = '0';
    gs_num_buf[1] = '0' + rpg_stats.level;
    if (rpg_stats.level >= 10) { gs_num_buf[0] = '1'; gs_num_buf[1] = '0' + rpg_stats.level - 10; }
    gs_num_buf[2] = 0;
    consoleDrawText(9, 10, gs_num_buf);
    consoleDrawText(14, 10, "KILLS:");
    gsNumToStr(rpg_stats.total_kills);
    consoleDrawText(20, 10, gs_num_buf);

    /* Draw menu options with initial cursor on RETRY */
    consoleDrawText(8, 14, "> RETRY ZONE");
    consoleDrawText(8, 16, "  TITLE");
    go_cursor = 0;  /* Default cursor on RETRY ZONE */

    /* Fade in */
    setScreenOn();
    fadeInBlocking(40);

    g_game.current_state = STATE_GAMEOVER;
}

/*
 * gsGameOverUpdate - Per-frame game-over screen update.
 *
 * Blinks the "GAME OVER" header, handles D-pad menu navigation,
 * draws the bouncing cursor, and processes confirm button to either
 * retry the current zone or return to the title screen.
 *
 * pad_pressed: Edge-triggered controller input.
 */
void gsGameOverUpdate(u16 pad_pressed)
{
    u8 col_offset;

    /* Blink "GAME OVER" text: hidden for ~8 of every 64 frames.
     * The brief flicker draws attention to the text. */
    if ((g_frame_count & 0x3F) < 0x08) {
        consoleDrawText(11, 8, "         ");
    } else {
        consoleDrawText(11, 8, "GAME OVER");
    }

    /* Clear cursor from both positions to prevent ghosting */
    consoleDrawText(8, 14, " ");
    consoleDrawText(8, 16, " ");
    consoleDrawText(9, 14, " ");
    consoleDrawText(9, 16, " ");

    /* D-pad navigation between RETRY and TITLE */
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

    /* Draw cursor with sine-wave bounce (same pattern as title screen) */
    col_offset = (cursor_sine[(g_frame_count >> 3) & 7] > 1) ? 1 : 0;
    consoleDrawText(8 + col_offset, 14 + (go_cursor << 1), ">");

    /* Confirm selection (A or Start) */
    if ((pad_pressed & ACTION_CONFIRM) || (pad_pressed & ACTION_PAUSE)) {
        soundPlaySFX(SFX_MENU_SELECT);
        fadeOutBlocking(40);
        bgSetDisable(0);  /* Disable text BG1 before scene change */

        if (go_cursor == 0) {
            /* RETRY ZONE: restart the current zone with full HP/SP.
             * Stats and inventory are preserved; only HP/SP are restored. */
            rpg_stats.hp = rpg_stats.max_hp;
            rpg_stats.sp = rpg_stats.max_sp;
            g_zone_advance = 0;
            gsFlightEnter();
        } else {
            /* TITLE: full reset back to title screen.
             * All progress is lost (save data from the last zone entry
             * is still in SRAM though, so CONTINUE may still work). */
            rpgStatsInit();
            invInit();
            gsTitleEnter();
        }
    }
}

/*===========================================================================*/
/* Pause (Flight Only)                                                       */
/*===========================================================================*/

/*
 * gsPauseToggle - Toggle the pause state during flight mode.
 *
 * When paused, the screen dims to brightness 8 (out of 15) as a visual
 * indicator.  When unpaused, brightness returns to 15 (full).
 * The main loop skips all gameplay updates while g_game.paused is set,
 * with a subtle brightness pulse animation.
 */
void gsPauseToggle(void)
{
    if (g_game.paused) {
        g_game.paused = 0;
        setBrightness(15);   /* Restore full brightness */
        soundPlaySFX(SFX_MENU_SELECT);  /* #201: Audio feedback on unpause */
    } else {
        g_game.paused = 1;
        setBrightness(8);    /* Dim screen to indicate pause */
        soundPlaySFX(SFX_MENU_MOVE);    /* #201: Audio feedback on pause */
    }
}
