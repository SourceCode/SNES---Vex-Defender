/*==============================================================================
 * Battle UI Module - Phase 12
 *
 * All battle screen drawing functions separated from battle.c logic.
 * Uses BG3 text (consoleDrawText) for HP bars, menus, and messages.
 * Uses OAM_UI slots 64-65 for battle sprites (enemy + player).
 * HP bar fill calculated using shifts + subtraction (no division on 65816).
 *
 * BG3 Text Layout (row numbers = Y coordinates for consoleDrawText):
 *   Row 1:  Enemy name (SCOUT/FIGHTER/CRUISER/ELITE, or boss name)
 *   Row 2:  HP:[==========] 060   (enemy HP bar + number)
 *   Row 5:  Battle message ("VEX ATTACKS!", "ENEMY DEFENDS!", etc.)
 *   Row 6:  Damage/heal amount ("045 DAMAGE!" or "025 HEALED!")
 *   Row 7:  Item drop notification ("GOT: HP POTION")
 *   Row 9:  > ATTACK    (cursor menu, visible during PLAYER_TURN)
 *   Row 10:   DEFEND
 *   Row 11:   SPECIAL
 *   Row 12:   ITEM
 *   Row 16: VEX HP:[=========]100 SP:3
 *   Row 18: Turn counter (T01, T02, ...)
 *
 * Battle Sprites (OBJ layer, priority 3):
 *   Enemy:  OAM slot 64 (byte offset 256), tile 128, palette 1, at (28, 28)
 *   Player: OAM slot 65 (byte offset 260), tile 0,   palette 0, at (184, 96)
 *
 * The text UI uses PVSnesLib's consoleDrawText() which writes tile indices
 * into BG1's tilemap. Each character maps to a font tile loaded at offset
 * 0x100 in BG1's character space. This approach avoids needing BG3 (which
 * is only 2bpp in Mode 1 and can't display the 4bpp font).
 *
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#include "game/battle_ui.h"
#include "game/battle.h"
#include "game/boss.h"
#include "game/inventory.h"

/*=== OBJ tile/palette constants (must match enemies.c and player.c) ===*/
/* These match the VRAM layout: player tiles at name 0, enemy tiles at name 128.
 * Palette indices are relative to OBJ palette base (subtract 8 from CGRAM slot). */
#define BUI_TILE_PLAYER  0     /* Player ship: VRAM name 0 (offset 0x0000) */
#define BUI_PAL_PLAYER   0    /* PAL_OBJ_PLAYER(8) - 8 = 0 */
#define BUI_TILE_ENEMY   128  /* Enemy: VRAM name 128 (offset 0x0800) */
#define BUI_PAL_ENEMY    1    /* PAL_OBJ_ENEMY(9) - 8 = 1 */

/*=== Enemy Display Names ===*/
/* Non-const char* because PVSnesLib's consoleDrawText() expects char*
 * (not const char*). These are the names shown on row 1 during battle. */
static char *enemy_names[4] = {
    "SCOUT", "FIGHTER", "CRUISER", "ELITE"
};

/*=== Action Menu Labels ===*/
/* Labels for the 4 action choices in the battle menu.
 * Non-const for PVSnesLib API compatibility (same as enemy_names). */
static char *action_labels[4] = {
    "ATTACK", "DEFEND", "SPECIAL", "ITEM"
};

/*=== Shared String Buffer ===*/
/* Small buffer for number-to-string conversion. Holds up to 3 digits + null.
 * Shared across all UI functions to minimize WRAM usage. */
static char num_buf[4];

/*=== HP Bar Buffer ===*/
/* Holds the rendered HP bar string: "[" + 10 fill chars + "]" + null = 13 chars.
 * Fill characters: '=' for filled segments, '-' for empty segments.
 * Example: "[========--]" = 80% HP */
static char bar_buf[14];

/*=== Shake State ===*/
/* Controls the hit-reaction shake animation on battle sprites.
 * When an attack lands, the target sprite oscillates horizontally
 * for a few frames to provide visual impact feedback. */
static u8 shake_timer;    /* Frames remaining in shake animation. 0 = inactive. */
static u8 shake_target;   /* Which sprite to shake: 0=enemy, 1=player */

/*=== Menu Dirty Tracking ===*/
/* Optimizes menu drawing by avoiding full redraws when only the cursor moved.
 * After the first full draw (menu_drawn=1), subsequent cursor changes only
 * clear the old cursor and draw the new one (2 tile writes instead of ~20). */
static u8 menu_visible;   /* 1 if menu is currently shown on screen */
static u8 menu_drawn;     /* 1 after first full menu draw; enables cursor-only updates */
static u8 prev_cursor;    /* Previous cursor position for partial redraw optimization */

/*=== Animated HP Bar Display Values ===*/
/* The displayed HP values lerp toward the actual HP values at 3 HP/frame,
 * creating a smooth draining/filling animation on the HP bars.
 * battleUIAnimateHP() drives this each frame. */
static s16 displayed_player_hp;
static s16 displayed_enemy_hp;

/*===========================================================================*/
/* Number-to-string (subtraction loops, no division for 65816)               */
/*===========================================================================*/

/*
 * numToStr3 (static)
 * ------------------
 * Convert a number (0-999) to a 3-character zero-padded string in num_buf.
 * Uses subtraction loops instead of division/modulo because the 65816 CPU
 * has no hardware divide instruction -- division is implemented in software
 * and costs ~200+ cycles. Subtraction loops are faster for small quotients.
 *
 * Output: num_buf[0..2] = "000" to "999", num_buf[3] = null terminator.
 * Negative values are clamped to 0, values > 999 clamped to 999.
 *
 * Parameters:
 *   val - the number to convert (s16, clamped to 0-999 range)
 */
static void numToStr3(s16 val)
{
    u16 v;
    if (val < 0) val = 0;
    if (val > 999) val = 999;
    v = (u16)val;

    /* Extract hundreds digit via subtraction loop */
    num_buf[0] = '0';
    while (v >= 100) { v -= 100; num_buf[0]++; }

    /* Extract tens digit via subtraction loop */
    num_buf[1] = '0';
    while (v >= 10) { v -= 10; num_buf[1]++; }

    /* Ones digit: whatever remains */
    num_buf[2] = '0' + (u8)v;
    num_buf[3] = 0; /* Null terminator */
}

/*===========================================================================*/
/* BG3 Text Helper                                                           */
/*===========================================================================*/

/*
 * clearRow (static)
 * -----------------
 * Clear an entire text row by overwriting with spaces.
 * Uses a 30-character string of spaces to fill the visible portion of
 * the 32-tile-wide BG1 tilemap. This is faster than clearing individual
 * tiles and ensures no leftover characters from previous messages.
 *
 * Parameters:
 *   y - row number (0-based, maps to BG1 tilemap Y coordinate)
 */
static void clearRow(u8 y)
{
    consoleDrawText(0, y, "                              ");
}

/*===========================================================================*/
/* HP Bar (10-segment text bar, no division)                                 */
/*                                                                           */
/* Algorithm: fill = (current * 10) / max_hp using shift+subtract.           */
/*   prod = (current << 3) + (current << 1)  = current * 10                 */
/*   fill = 0; while (prod >= max) { prod -= max; fill++; }                 */
/* Returns 0..10 fill level. Guarantees fill >= 1 when current > 0.          */
/*                                                                           */
/* Why no division? The 65816 CPU has no hardware divider. Software division */
/* on the 65816 costs ~200-400 cycles per call. By multiplying current by 10 */
/* (using shifts: 10 = 8+2) and then doing a subtraction loop to divide by   */
/* max_hp, we achieve the same result much faster when max_hp is small       */
/* (typical values: 30-350), since the loop runs at most 10 iterations.      */
/*===========================================================================*/

/*
 * calcBarFill (static)
 * --------------------
 * Calculate how many segments of the 10-segment HP bar should be filled.
 *
 * Parameters:
 *   current  - current HP value
 *   max_val  - maximum HP value (denominator)
 *
 * Returns:
 *   Fill level 0-10 (number of '=' characters in the bar).
 *   Guarantees fill >= 1 when current > 0 (alive = at least 1 segment).
 */
static u8 calcBarFill(s16 current, s16 max_val)
{
    u16 prod;
    u8 fill;

    if (current <= 0) return 0;
    if (current >= max_val) return BUI_HP_BAR_WIDTH;

    /* Multiply current by 10 using shifts: 10 = 8 + 2.
     * (current << 3) = current * 8, (current << 1) = current * 2.
     * Sum = current * 10. Max value: 999 * 10 = 9990, fits in u16. */
    prod = ((u16)current << 3) + ((u16)current << 1);

    /* Divide by max using subtraction loop.
     * Each iteration subtracts max_val and increments fill.
     * Loop runs at most BUI_HP_BAR_WIDTH (10) times, with overflow guard. */
    fill = 0;
    while (prod >= (u16)max_val && fill <= BUI_HP_BAR_WIDTH) {
        prod -= (u16)max_val;
        fill++;
    }

    /* Clamp and ensure minimum 1 bar when alive */
    if (fill > BUI_HP_BAR_WIDTH) fill = BUI_HP_BAR_WIDTH;
    if (fill == 0 && current > 0) fill = 1; /* At least 1 bar when alive */

    return fill;
}

/*
 * drawHPBar (static)
 * ------------------
 * Render an HP bar to the screen at the given text position.
 * Format: "[=====-----]" where '=' = filled, '-' = empty.
 *
 * Parameters:
 *   x, y    - text grid position for the bar
 *   current - current HP (numerator for fill calculation)
 *   max_val - maximum HP (denominator for fill calculation)
 */
static void drawHPBar(u8 x, u8 y, s16 current, s16 max_val)
{
    u8 fill, i;

    fill = calcBarFill(current, max_val);

    /* Build bar string: "[" + fill chars + empty chars + "]" */
    bar_buf[0] = '[';
    for (i = 0; i < BUI_HP_BAR_WIDTH; i++) {
        bar_buf[i + 1] = (i < fill) ? '=' : '-';
    }
    bar_buf[BUI_HP_BAR_WIDTH + 1] = ']';
    bar_buf[BUI_HP_BAR_WIDTH + 2] = 0; /* Null terminator */

    consoleDrawText(x, y, bar_buf);
}

/*===========================================================================*/
/* Boss Phase Markers (| at 25% and 50% on 10-segment bar)                   */
/*===========================================================================*/

/*
 * drawBossPhaseMarkers (static)
 * -----------------------------
 * Overlay phase transition markers on the boss HP bar.
 * Shows '|' characters at the 25% and 50% positions to help the player
 * anticipate when the boss will change AI phases:
 *   50% mark (position 5): NORMAL -> ENRAGED transition
 *   25% mark (position 2): ENRAGED -> DESPERATE transition
 *
 * Parameters:
 *   x, y    - text grid position of the HP bar (same as drawHPBar)
 *   max_hp  - boss max HP (unused, markers are at fixed bar positions)
 */
static void drawBossPhaseMarkers(u8 x, u8 y, s16 max_hp)
{
    u8 mark_50;
    u8 mark_25;
    (void)max_hp; /* Markers are at fixed positions on the 10-segment bar */

    /* Calculate marker positions within the 10-character bar:
     * 50% of 10 = position 5, 25% of 10 = position 2.5 -> rounded to 2. */
    mark_50 = 5;  /* 50% position: NORMAL -> ENRAGED threshold */
    mark_25 = 2;  /* 25% position: ENRAGED -> DESPERATE threshold */

    /* Overlay markers on the bar. x+1 offsets past the opening '[' bracket.
     * These overwrite the '=' or '-' character at those positions. */
    consoleDrawText(x + 1 + mark_25, y, "|");
    consoleDrawText(x + 1 + mark_50, y, "|");
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

/*
 * battleUIInit
 * ------------
 * Reset all UI state. Called once at game startup by battleInit().
 * Clears shake animation, menu tracking, and cursor state.
 */
void battleUIInit(void)
{
    shake_timer = 0;
    shake_target = 0;
    menu_visible = 0;
    menu_drawn = 0;
    prev_cursor = 0;
}

/*
 * battleUIShowSprites
 * -------------------
 * Display the battle sprites (enemy and player) on screen.
 * Called during battleTransitionIn() after all flight sprites are hidden.
 *
 * Uses OAM_UI slots 64-65, which are reserved for UI elements and not
 * managed by the sprite engine pool. Direct OAM writes via oamSet().
 *
 * Both sprites use priority 3 (above all BG layers) and 32x32 size.
 * The enemy sprite appears top-left, player bottom-right, mimicking
 * classic JRPG battle layouts.
 *
 * Parameters:
 *   enemy_type - ENEMY_TYPE_* (currently unused; all enemies use slot A sprite)
 */
void battleUIShowSprites(u8 enemy_type)
{
    (void)enemy_type; /* All enemies use scout sprite for now (placeholder) */

    /* Enemy sprite: OAM slot 64 (byte offset 256), top-left corner */
    oamSet(BUI_ENEMY_OAM_ID,
           BUI_ENEMY_SPR_X, BUI_ENEMY_SPR_Y,  /* Screen position (28, 28) */
           3, 0, 0,           /* Priority 3 (above all BGs), no hflip/vflip */
           BUI_TILE_ENEMY,    /* Tile name 128 = VRAM offset 0x0800 */
           BUI_PAL_ENEMY);    /* OBJ palette 1 = CGRAM slot 9 (colors 144-159) */
    oamSetEx(BUI_ENEMY_OAM_ID, OBJ_LARGE, OBJ_SHOW);

    /* Player sprite: OAM slot 65 (byte offset 260), bottom-right area */
    oamSet(BUI_PLAYER_OAM_ID,
           BUI_PLAYER_SPR_X, BUI_PLAYER_SPR_Y,  /* Screen position (184, 96) */
           3, 0, 0,           /* Priority 3, no flip */
           BUI_TILE_PLAYER,   /* Tile name 0 = VRAM offset 0x0000 */
           BUI_PAL_PLAYER);   /* OBJ palette 0 = CGRAM slot 8 (colors 128-143) */
    oamSetEx(BUI_PLAYER_OAM_ID, OBJ_LARGE, OBJ_SHOW);
}

/*
 * battleUIHideSprites
 * -------------------
 * Hide both battle sprites. Called during battleTransitionOut().
 * Sets OBJ_HIDE which moves the sprite Y off-screen (Y >= 224).
 */
void battleUIHideSprites(void)
{
    oamSetVisible(BUI_ENEMY_OAM_ID, OBJ_HIDE);
    oamSetVisible(BUI_PLAYER_OAM_ID, OBJ_HIDE);
}

/*
 * battleUIDrawTurnCounter
 * -----------------------
 * Draw the turn counter in the bottom-right corner (row 18, column 26).
 * Format: "T01", "T02", ..., "T99".
 *
 * Uses subtraction loop for tens digit to avoid division (same pattern
 * as numToStr3 but for 2-digit numbers only).
 */
void battleUIDrawTurnCounter(void)
{
    u8 t;
    t = battle.turn_number;
    clearRow(18);
    num_buf[0] = 'T';
    /* Extract tens digit via subtraction loop */
    num_buf[1] = '0';
    while (t >= 10) { t -= 10; num_buf[1]++; }
    num_buf[2] = '0' + t;  /* Ones digit */
    num_buf[3] = 0;
    consoleDrawText(26, 18, num_buf);
}

/*
 * battleUIDrawScreen
 * ------------------
 * Draw the complete initial battle screen. Called once when entering battle.
 * Initializes displayed HP values and draws all UI elements:
 *   - Enemy stats (name + HP bar)
 *   - Player stats (name + HP bar + SP)
 *   - Turn counter
 *   - Intro message ("ENCOUNTER!" or "BOSS BATTLE!")
 */
void battleUIDrawScreen(void)
{
    /* Initialize animated HP display values to current HP */
    displayed_player_hp = battle.player.hp;
    displayed_enemy_hp = battle.enemy.hp;

    /* Draw all UI sections */
    battleUIDrawEnemyStats();
    battleUIDrawPlayerStats();
    battleUIDrawTurnCounter();

    /* Show encounter message based on battle type */
    if (battle.is_boss) {
        battleUIDrawMessage("BOSS BATTLE!");
    } else {
        battleUIDrawMessage("ENCOUNTER!");
    }
}

/*
 * battleUIDrawEnemyStats
 * ----------------------
 * Draw enemy name and HP bar on rows 1-2.
 * Format:
 *   Row 1: "  COMMANDER" (or "  SCOUT", etc.)
 *   Row 2: "  HP:[==========] 120"
 *
 * For boss battles, shows the boss name from g_boss.name instead of
 * the generic enemy_names[] table. Also draws phase markers on the
 * HP bar to indicate AI phase transition thresholds.
 */
void battleUIDrawEnemyStats(void)
{
    clearRow(1);
    clearRow(2);

    /* Draw enemy name: boss name or generic enemy name */
    if (battle.is_boss) {
        consoleDrawText(2, 1, g_boss.name);
    } else {
        consoleDrawText(2, 1, enemy_names[battle.enemy_type]);
    }

    /* HP bar on row 2: "HP:" label + bar + numeric value */
    consoleDrawText(2, 2, "HP:");
    drawHPBar(5, 2, displayed_enemy_hp, battle.enemy.max_hp);
    numToStr3(battle.enemy.hp);
    consoleDrawText(17, 2, num_buf);

    /* #214: Show enemy ATK value */
    consoleDrawText(21, 2, "ATK:");
    numToStr3(battle.enemy.atk);
    consoleDrawText(25, 2, num_buf);

    /* Boss battles: overlay phase transition markers on the HP bar */
    if (battle.is_boss) {
        drawBossPhaseMarkers(5, 2, battle.enemy.max_hp);
    }
}

/*
 * battleUIDrawPlayerStats
 * -----------------------
 * Draw player stats on row 16.
 * Format: "  VEX HP:[==========]100 SP:3"
 *
 * Shows both HP (bar + number) and SP (single digit) on one row.
 */
void battleUIDrawPlayerStats(void)
{
    clearRow(16);
    /* Label + HP bar */
    consoleDrawText(2, 16, "VEX HP:");
    drawHPBar(9, 16, displayed_player_hp, battle.player.max_hp);
    numToStr3(battle.player.hp);
    consoleDrawText(21, 16, num_buf);
    /* SP display (single digit since max SP is < 10) */
    consoleDrawText(25, 16, "SP:");
    num_buf[0] = '0' + battle.player.sp;
    num_buf[1] = 0;
    consoleDrawText(28, 16, num_buf);
}

/*
 * battleUIUpdateEnemyHP
 * ---------------------
 * Lightweight HP bar + number redraw for enemy (skip name/label clear).
 * Called after damage/heal when only the HP value changed, not the name.
 * Saves tilemap write bandwidth compared to full battleUIDrawEnemyStats().
 */
void battleUIUpdateEnemyHP(void)
{
    drawHPBar(5, 2, displayed_enemy_hp, battle.enemy.max_hp);
    numToStr3(battle.enemy.hp);
    consoleDrawText(17, 2, num_buf);

    /* Redraw phase markers for boss battles (bar overwrite removes them) */
    if (battle.is_boss) {
        drawBossPhaseMarkers(5, 2, battle.enemy.max_hp);
    }
}

/*
 * battleUIUpdatePlayerHP
 * ----------------------
 * Lightweight HP bar + number + SP redraw for player (skip label clear).
 * Same optimization rationale as battleUIUpdateEnemyHP().
 */
void battleUIUpdatePlayerHP(void)
{
    drawHPBar(9, 16, displayed_player_hp, battle.player.max_hp);
    numToStr3(battle.player.hp);
    consoleDrawText(21, 16, num_buf);
    /* Refresh SP display */
    consoleDrawText(25, 16, "SP:");
    num_buf[0] = '0' + battle.player.sp;
    num_buf[1] = 0;
    consoleDrawText(28, 16, num_buf);

    /* #233: Defend stance visual indicator */
    if (battle.player.defending) {
        consoleDrawText(2, 15, "[DEF]");
    } else {
        consoleDrawText(2, 15, "     ");
    }
}

/*
 * battleUIDrawMenu
 * ----------------
 * Draw the action menu on rows 9-12.
 * Supports two modes for performance optimization:
 *
 * Full draw (first call, menu_drawn=0): writes all 4 labels + cursor.
 *   This happens when the menu first appears for a new turn.
 *
 * Cursor-only update (subsequent calls, menu_drawn=1): only erases
 *   the old cursor position and draws the new one. This is much faster
 *   since it only writes 2 tiles instead of ~20+.
 *
 * Parameters:
 *   cursor - menu selection index (0-3, maps to rows 9-12)
 */
void battleUIDrawMenu(u8 cursor)
{
    u8 i;
    menu_visible = 1;

    if (menu_drawn) {
        /* Cursor-only update: clear old cursor, draw new cursor */
        consoleDrawText(2, 9 + prev_cursor, " ");  /* Erase old '>' */
        consoleDrawText(2, 9 + cursor, ">");        /* Draw new '>' */
    } else {
        /* Full draw: all 4 action labels with cursor indicator */
        for (i = 0; i < 4; i++) {
            clearRow(9 + i);
            /* Draw cursor '>' at selected row, space at others */
            if (i == cursor) {
                consoleDrawText(2, 9 + i, ">");
            } else {
                consoleDrawText(2, 9 + i, " ");
            }
            /* Draw action label */
            if (i == 2) {
                /* #221: Show SP cost on SPECIAL */
                consoleDrawText(4, 9 + i, "SPECIAL 2SP");
            } else if (i == 3 && !battle.is_boss) {
                /* #220: Show FLEE instead of ITEM in non-boss battles */
                consoleDrawText(4, 9 + i, "FLEE");
            } else {
                consoleDrawText(4, 9 + i, action_labels[i]);
            }
        }
        menu_drawn = 1; /* Mark as fully drawn for future cursor-only updates */
    }

    prev_cursor = cursor; /* Remember cursor position for next partial redraw */
}

/*
 * battleUIClearMenu
 * -----------------
 * Clear the menu area (rows 9-12) and reset menu tracking state.
 * Called when transitioning away from PLAYER_TURN state.
 * Early-exits if menu is already hidden to avoid unnecessary VRAM writes.
 */
void battleUIClearMenu(void)
{
    u8 i;
    if (!menu_visible) return; /* Already hidden, no work needed */
    for (i = 0; i < 4; i++) {
        clearRow(9 + i);
    }
    menu_visible = 0;
    menu_drawn = 0; /* Reset so next menu appearance gets a full draw */
}

/*
 * battleUIDrawMessage
 * -------------------
 * Draw a battle message on row 5 (also clears row 6 for damage display).
 * Used for action announcements: "VEX ATTACKS!", "ENEMY DEFENDS!", etc.
 *
 * Parameters:
 *   msg - null-terminated string to display (max ~28 chars for screen width)
 */
void battleUIDrawMessage(char *msg)
{
    clearRow(5);
    clearRow(6); /* Clear damage row too for clean display */
    consoleDrawText(2, 5, msg);
}

/*
 * battleUIDrawDamage
 * ------------------
 * Draw the damage/heal amount on row 6.
 * Positive damage shows "045 DAMAGE!", negative shows "025 HEALED!".
 * Zero damage shows nothing (used for defend and charge actions).
 *
 * Parameters:
 *   damage - positive = damage dealt, negative = HP healed, zero = no display
 */
void battleUIDrawDamage(s16 damage)
{
    clearRow(6);
    if (damage > 0) {
        numToStr3(damage);
        consoleDrawText(2, 6, num_buf);      /* "045" */
        consoleDrawText(6, 6, "DAMAGE!");     /* "DAMAGE!" */
    } else if (damage < 0) {
        numToStr3(-damage);                   /* Negate for display */
        consoleDrawText(2, 6, num_buf);      /* "025" */
        consoleDrawText(6, 6, "HEALED!");     /* "HEALED!" */
    }
    /* damage == 0: row left empty (defend, charge, etc.) */
}

/*
 * battleUIStartShake
 * ------------------
 * Start a shake animation with default duration (BUI_SHAKE_FRAMES = 8).
 * Convenience wrapper around battleUIStartShakeN().
 *
 * Parameters:
 *   target - 0 = shake enemy sprite, 1 = shake player sprite
 */
void battleUIStartShake(u8 target)
{
    battleUIStartShakeN(target, BUI_SHAKE_FRAMES);
}

/*
 * battleUIStartShakeN
 * -------------------
 * Start a shake animation with custom duration.
 * The shake makes the target battle sprite oscillate horizontally by +/-2
 * pixels for the specified number of frames. Used for damage feedback.
 *
 * Parameters:
 *   target - 0 = shake enemy sprite, 1 = shake player sprite
 *   frames - duration in frames (typically 4-10 based on damage dealt)
 */
void battleUIStartShakeN(u8 target, u8 frames)
{
    shake_timer = frames;
    shake_target = target;
}

/*
 * battleUIUpdateShake
 * -------------------
 * Per-frame update for the shake animation. No-op when shake_timer == 0.
 *
 * When active: oscillates the target sprite's X position by +/-2 pixels.
 * The direction alternates based on shake_timer bit 1:
 *   timer & 2 != 0 -> offset +2 (shift right)
 *   timer & 2 == 0 -> offset -2 (shift left)
 * This creates a 4-frame oscillation cycle.
 *
 * When timer reaches 0: resets the sprite to its base position (offset 0).
 *
 * This function writes directly to OAM via oamSet(), repositioning the
 * battle sprite at its base position plus the shake offset.
 */
void battleUIUpdateShake(void)
{
    s16 offset;
    u16 oam_id;
    s16 base_x, base_y;
    u16 tile;
    u8 pal;

    if (shake_timer == 0) return; /* No active shake */

    shake_timer--;

    /* Calculate X offset: alternating +/-2 while active, 0 when done */
    if (shake_timer > 0) {
        offset = (shake_timer & 2) ? 2 : -2;
    } else {
        offset = 0; /* Final frame: return to base position */
    }

    /* Select target sprite's properties based on shake_target */
    if (shake_target == 0) {
        /* Shake enemy sprite */
        oam_id = BUI_ENEMY_OAM_ID;
        base_x = BUI_ENEMY_SPR_X;
        base_y = BUI_ENEMY_SPR_Y;
        tile = BUI_TILE_ENEMY;
        pal = BUI_PAL_ENEMY;
    } else {
        /* Shake player sprite */
        oam_id = BUI_PLAYER_OAM_ID;
        base_x = BUI_PLAYER_SPR_X;
        base_y = BUI_PLAYER_SPR_Y;
        tile = BUI_TILE_PLAYER;
        pal = BUI_PAL_PLAYER;
    }

    /* Write updated position to OAM (base position + shake offset) */
    oamSet(oam_id,
           (u16)(base_x + offset), (u16)base_y,
           3, 0, 0,       /* Priority 3, no flip */
           tile, pal);
}

/*
 * battleUIDrawVictory
 * -------------------
 * Draw the victory screen: "VICTORY!" message and XP gained.
 * Format:
 *   Row 5: "VICTORY!"
 *   Row 6: "+100 XP"
 *
 * Parameters:
 *   xp - XP amount awarded (displayed as "+NNN XP")
 */
void battleUIDrawVictory(u16 xp)
{
    battleUIDrawMessage("VICTORY!");
    clearRow(6);
    consoleDrawText(2, 6, "+");
    numToStr3((s16)xp);
    consoleDrawText(3, 6, num_buf);  /* XP amount */
    consoleDrawText(7, 6, "XP");

    /* #224: Show turns taken on victory screen */
    {
        u8 t = battle.turn_number;
        consoleDrawText(11, 6, "TURNS:");
        num_buf[0] = '0';
        while (t >= 10) { t -= 10; num_buf[0]++; }
        num_buf[1] = '0' + t;
        num_buf[2] = 0;
        consoleDrawText(17, 6, num_buf);
    }
}

/*
 * battleUIDrawDefeat
 * ------------------
 * Draw the defeat screen: simple "DEFEATED..." message.
 */
void battleUIDrawDefeat(void)
{
    battleUIDrawMessage("DEFEATED...");
}

/*
 * battleUIDrawLevelUp
 * -------------------
 * Draw the level-up notification.
 * Format:
 *   Row 5: "LEVEL UP!"
 *   Row 6: "NOW LV:05"
 * Also refreshes the player stat row to show updated max HP/SP.
 *
 * Parameters:
 *   new_level - the player's new level (1-10)
 */
void battleUIDrawLevelUp(u8 new_level)
{
    u8 lv;
    lv = new_level;
    clearRow(5);
    clearRow(6);
    consoleDrawText(2, 5, "LEVEL UP!");

    /* Convert 2-digit level number using subtraction (no division).
     * Levels cap at 10, so this only handles tens digit 0 or 1. */
    num_buf[0] = '0';
    if (lv >= 10) { num_buf[0] = '1'; lv -= 10; }
    num_buf[1] = '0' + lv;
    num_buf[2] = 0;
    consoleDrawText(2, 6, "NOW LV:");
    consoleDrawText(9, 6, num_buf);

    /* Refresh player stats row to show new max HP/SP after level-up */
    battleUIDrawPlayerStats();
}

/*
 * battleUIDrawItemMenu
 * --------------------
 * Draw the item sub-menu on rows 9-12 (same area as action menu).
 * Shows up to 4 items with cursor indicator and quantity.
 * Format per row: "> HP POTION S  x3" (cursor + name + quantity)
 *
 * Parameters:
 *   item_ids - array of ITEM_* IDs to display
 *   qtys     - array of quantities matching each item
 *   count    - number of items to show (1-4)
 *   cursor   - currently selected item index
 */
void battleUIDrawItemMenu(u8 *item_ids, u8 *qtys, u8 count, u8 cursor)
{
    u8 i;
    for (i = 0; i < 4; i++) {
        clearRow(9 + i);
        if (i < count) {
            /* Draw cursor or space */
            if (i == cursor) {
                consoleDrawText(2, 9 + i, ">");
            } else {
                consoleDrawText(2, 9 + i, " ");
            }
            /* Draw item name (from inventory system lookup) */
            consoleDrawText(4, 9 + i, invGetName(item_ids[i]));
            /* Draw quantity: "xN" format */
            num_buf[0] = 'x';
            num_buf[1] = '0' + qtys[i];  /* Single digit (max quantity < 10) */
            num_buf[2] = 0;
            consoleDrawText(15, 9 + i, num_buf);
        }
        /* Rows beyond item count are left blank (already cleared) */
    }
}

/*
 * battleUIAnimateHP
 * -----------------
 * Smoothly animate HP bars toward their actual values.
 * Called every frame during battle to create a draining/filling effect
 * when HP changes (damage or healing).
 *
 * Animation rate: 3 HP per frame. At 60fps, a 100 HP change takes
 * ~33 frames (~0.55 seconds) to fully animate.
 *
 * Early exit when both displayed values match actual values to avoid
 * unnecessary HP bar redraws (VRAM writes are expensive during vblank).
 */
void battleUIAnimateHP(void)
{
    u8 p_diff;
    u8 e_diff;

    /* Check if either bar needs updating */
    p_diff = (displayed_player_hp != battle.player.hp) ? 1 : 0;
    e_diff = (displayed_enemy_hp != battle.enemy.hp) ? 1 : 0;

    /* Early exit when both bars already match their target values.
     * Saves VRAM write bandwidth during idle frames. */
    if (!p_diff && !e_diff) {
        return;
    }

    /* Animate player HP bar toward actual value (3 HP/frame lerp) */
    if (p_diff) {
        if (displayed_player_hp > battle.player.hp) {
            /* HP decreased (took damage): drain bar downward */
            displayed_player_hp -= 3;
            if (displayed_player_hp < battle.player.hp)
                displayed_player_hp = battle.player.hp; /* Clamp to target */
        } else {
            /* HP increased (healed): fill bar upward */
            displayed_player_hp += 3;
            if (displayed_player_hp > battle.player.hp)
                displayed_player_hp = battle.player.hp; /* Clamp to target */
        }
        battleUIUpdatePlayerHP(); /* Redraw bar with new displayed value */
    }

    /* Animate enemy HP bar toward actual value (same algorithm) */
    if (e_diff) {
        if (displayed_enemy_hp > battle.enemy.hp) {
            displayed_enemy_hp -= 3;
            if (displayed_enemy_hp < battle.enemy.hp)
                displayed_enemy_hp = battle.enemy.hp;
        } else {
            displayed_enemy_hp += 3;
            if (displayed_enemy_hp > battle.enemy.hp)
                displayed_enemy_hp = battle.enemy.hp;
        }
        battleUIUpdateEnemyHP(); /* Redraw bar with new displayed value */
    }
}

/*
 * battleUIDrawItemDrop
 * --------------------
 * Draw an item drop notification on the victory screen (row 7).
 * Shown when an enemy drops an item (guaranteed for bosses, RNG for normal).
 * Format: "  GOT: HP POTION L"
 *
 * Parameters:
 *   item_name - display name of the dropped item (from invGetName())
 */
void battleUIDrawItemDrop(char *item_name)
{
    clearRow(7);
    consoleDrawText(2, 7, "GOT:");
    consoleDrawText(7, 7, item_name);
}

/*
 * battleUIDrawXPPreview (#185)
 * ----------------------------
 * Show XP reward preview during player turn.
 * Format: "XP:NNN" at row 17.
 */
void battleUIDrawXPPreview(u16 xp)
{
    clearRow(17);
    consoleDrawText(2, 17, "XP:");
    numToStr3((s16)xp);
    consoleDrawText(5, 17, num_buf);
}

/*
 * battleUIDrawTurnCount (#189)
 * ----------------------------
 * Show turn number during player turn at row 18.
 * Format: "T:N" at column 25.
 */
void battleUIDrawTurnCount(u8 turn)
{
    num_buf[0] = 'T';
    num_buf[1] = ':';
    num_buf[2] = '0' + (turn > 9 ? 9 : turn);
    num_buf[3] = 0;
    consoleDrawText(25, 18, num_buf);
}

/* #222, #223, #230 removed to reduce bank pressure (banks 1,3 at 0 bytes) */
