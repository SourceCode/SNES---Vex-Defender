/*==============================================================================
 * Battle UI Module - Phase 12
 *
 * All battle screen drawing functions separated from battle.c logic.
 * Uses BG3 text (consoleDrawText) for HP bars, menus, and messages.
 * Uses OAM_UI slots 64-65 for battle sprites (enemy + player).
 * HP bar fill calculated using shifts + subtraction (no division on 65816).
 *
 * BG3 Text Layout:
 *   Row 1:  Enemy name (SCOUT/FIGHTER/CRUISER/ELITE)
 *   Row 2:  HP:[==========] 060   (enemy HP bar + number)
 *   Row 5:  Battle message ("VEX ATTACKS!", "ENEMY DEFENDS!", etc.)
 *   Row 6:  Damage/heal amount ("045 DAMAGE!" or "025 HEALED!")
 *   Row 9:  > ATTACK    (cursor menu, visible during PLAYER_TURN)
 *   Row 10:   DEFEND
 *   Row 11:   SPECIAL
 *   Row 12:   ITEM
 *   Row 16: VEX HP:[=========]100 SP:3
 *
 * Battle Sprites (OBJ layer, priority 3):
 *   Enemy:  OAM slot 64, tile 128, palette 1, at (28, 28)
 *   Player: OAM slot 65, tile 0,   palette 0, at (184, 96)
 *
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#include "game/battle_ui.h"
#include "game/battle.h"
#include "game/boss.h"
#include "game/inventory.h"

/*=== OBJ tile/palette constants (must match enemies.c and player.c) ===*/
#define BUI_TILE_PLAYER  0
#define BUI_PAL_PLAYER   0    /* PAL_OBJ_PLAYER(8) - 8 */
#define BUI_TILE_ENEMY   128  /* VRAM_OBJ_ENEMY_OFFSET(0x0800) >> 4 */
#define BUI_PAL_ENEMY    1    /* PAL_OBJ_ENEMY(9) - 8 */

/*=== Enemy Display Names (non-const for PVSnesLib API compat) ===*/
static char *enemy_names[4] = {
    "SCOUT", "FIGHTER", "CRUISER", "ELITE"
};

/*=== Action Menu Labels (non-const for PVSnesLib API compat) ===*/
static char *action_labels[4] = {
    "ATTACK", "DEFEND", "SPECIAL", "ITEM"
};

/*=== Shared String Buffer ===*/
static char num_buf[4];

/*=== HP Bar Buffer: [ + 10 chars + ] + null ===*/
static char bar_buf[14];

/*=== Shake State ===*/
static u8 shake_timer;
static u8 shake_target;  /* 0=enemy, 1=player */

/*===========================================================================*/
/* Number-to-string (subtraction loops, no division for 65816)               */
/*===========================================================================*/

static void numToStr3(s16 val)
{
    u16 v;
    if (val < 0) val = 0;
    if (val > 999) val = 999;
    v = (u16)val;

    num_buf[0] = '0';
    while (v >= 100) { v -= 100; num_buf[0]++; }
    num_buf[1] = '0';
    while (v >= 10) { v -= 10; num_buf[1]++; }
    num_buf[2] = '0' + (u8)v;
    num_buf[3] = 0;
}

/*===========================================================================*/
/* BG3 Text Helper                                                           */
/*===========================================================================*/

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
/*===========================================================================*/

static u8 calcBarFill(s16 current, s16 max_val)
{
    u16 prod;
    u8 fill;

    if (current <= 0) return 0;
    if (current >= max_val) return BUI_HP_BAR_WIDTH;

    /* Multiply current by 10 using shifts: 10 = 8 + 2 */
    prod = ((u16)current << 3) + ((u16)current << 1);

    /* Divide by max using subtraction loop */
    fill = 0;
    while (prod >= (u16)max_val) {
        prod -= (u16)max_val;
        fill++;
    }

    if (fill > BUI_HP_BAR_WIDTH) fill = BUI_HP_BAR_WIDTH;
    if (fill == 0 && current > 0) fill = 1; /* At least 1 bar when alive */

    return fill;
}

static void drawHPBar(u8 x, u8 y, s16 current, s16 max_val)
{
    u8 fill, i;

    fill = calcBarFill(current, max_val);

    bar_buf[0] = '[';
    for (i = 0; i < BUI_HP_BAR_WIDTH; i++) {
        bar_buf[i + 1] = (i < fill) ? '=' : '-';
    }
    bar_buf[BUI_HP_BAR_WIDTH + 1] = ']';
    bar_buf[BUI_HP_BAR_WIDTH + 2] = 0;

    consoleDrawText(x, y, bar_buf);
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

void battleUIInit(void)
{
    shake_timer = 0;
    shake_target = 0;
}

void battleUIShowSprites(u8 enemy_type)
{
    (void)enemy_type; /* All enemies use scout sprite for now */

    /* Enemy sprite (OAM slot 64) */
    oamSet(BUI_ENEMY_OAM_ID,
           BUI_ENEMY_SPR_X, BUI_ENEMY_SPR_Y,
           3, 0, 0,           /* priority 3 (above all BGs), no flip */
           BUI_TILE_ENEMY,    /* tile 128 */
           BUI_PAL_ENEMY);    /* palette 1 */
    oamSetEx(BUI_ENEMY_OAM_ID, OBJ_LARGE, OBJ_SHOW);

    /* Player sprite (OAM slot 65) */
    oamSet(BUI_PLAYER_OAM_ID,
           BUI_PLAYER_SPR_X, BUI_PLAYER_SPR_Y,
           3, 0, 0,
           BUI_TILE_PLAYER,   /* tile 0 */
           BUI_PAL_PLAYER);   /* palette 0 */
    oamSetEx(BUI_PLAYER_OAM_ID, OBJ_LARGE, OBJ_SHOW);
}

void battleUIHideSprites(void)
{
    oamSetVisible(BUI_ENEMY_OAM_ID, OBJ_HIDE);
    oamSetVisible(BUI_PLAYER_OAM_ID, OBJ_HIDE);
}

void battleUIDrawScreen(void)
{
    battleUIDrawEnemyStats();
    battleUIDrawPlayerStats();
    if (battle.is_boss) {
        battleUIDrawMessage("BOSS BATTLE!");
    } else {
        battleUIDrawMessage("ENCOUNTER!");
    }
}

void battleUIDrawEnemyStats(void)
{
    clearRow(1);
    clearRow(2);
    if (battle.is_boss) {
        consoleDrawText(2, 1, g_boss.name);
    } else {
        consoleDrawText(2, 1, enemy_names[battle.enemy_type]);
    }

    /* HP bar on row 2: HP:[==========] 060 */
    consoleDrawText(2, 2, "HP:");
    drawHPBar(5, 2, battle.enemy.hp, battle.enemy.max_hp);
    numToStr3(battle.enemy.hp);
    consoleDrawText(17, 2, num_buf);
}

void battleUIDrawPlayerStats(void)
{
    clearRow(16);
    /* VEX HP:[==========]100 SP:3 */
    consoleDrawText(2, 16, "VEX HP:");
    drawHPBar(9, 16, battle.player.hp, battle.player.max_hp);
    numToStr3(battle.player.hp);
    consoleDrawText(21, 16, num_buf);
    consoleDrawText(25, 16, "SP:");
    num_buf[0] = '0' + battle.player.sp;
    num_buf[1] = 0;
    consoleDrawText(28, 16, num_buf);
}

void battleUIDrawMenu(u8 cursor)
{
    u8 i;
    for (i = 0; i < 4; i++) {
        clearRow(9 + i);
        if (i == cursor) {
            consoleDrawText(2, 9 + i, ">");
        } else {
            consoleDrawText(2, 9 + i, " ");
        }
        consoleDrawText(4, 9 + i, action_labels[i]);
    }
}

void battleUIClearMenu(void)
{
    u8 i;
    for (i = 0; i < 4; i++) {
        clearRow(9 + i);
    }
}

void battleUIDrawMessage(char *msg)
{
    clearRow(5);
    clearRow(6);
    consoleDrawText(2, 5, msg);
}

void battleUIDrawDamage(s16 damage)
{
    clearRow(6);
    if (damage > 0) {
        numToStr3(damage);
        consoleDrawText(2, 6, num_buf);
        consoleDrawText(6, 6, "DAMAGE!");
    } else if (damage < 0) {
        numToStr3(-damage);
        consoleDrawText(2, 6, num_buf);
        consoleDrawText(6, 6, "HEALED!");
    }
}

void battleUIStartShake(u8 target)
{
    shake_timer = BUI_SHAKE_FRAMES;
    shake_target = target;
}

void battleUIUpdateShake(void)
{
    s16 offset;
    u16 oam_id;
    s16 base_x, base_y;
    u16 tile;
    u8 pal;

    if (shake_timer == 0) return;

    shake_timer--;

    if (shake_timer > 0) {
        offset = (shake_timer & 2) ? 2 : -2;
    } else {
        offset = 0;
    }

    if (shake_target == 0) {
        oam_id = BUI_ENEMY_OAM_ID;
        base_x = BUI_ENEMY_SPR_X;
        base_y = BUI_ENEMY_SPR_Y;
        tile = BUI_TILE_ENEMY;
        pal = BUI_PAL_ENEMY;
    } else {
        oam_id = BUI_PLAYER_OAM_ID;
        base_x = BUI_PLAYER_SPR_X;
        base_y = BUI_PLAYER_SPR_Y;
        tile = BUI_TILE_PLAYER;
        pal = BUI_PAL_PLAYER;
    }

    oamSet(oam_id,
           (u16)(base_x + offset), (u16)base_y,
           3, 0, 0,
           tile, pal);
}

void battleUIDrawVictory(u16 xp)
{
    battleUIDrawMessage("VICTORY!");
    clearRow(6);
    consoleDrawText(2, 6, "+");
    numToStr3((s16)xp);
    consoleDrawText(3, 6, num_buf);
    consoleDrawText(7, 6, "XP");
}

void battleUIDrawDefeat(void)
{
    battleUIDrawMessage("DEFEATED...");
}

void battleUIDrawLevelUp(u8 new_level)
{
    u8 lv;
    lv = new_level;
    clearRow(5);
    clearRow(6);
    consoleDrawText(2, 5, "LEVEL UP!");
    num_buf[0] = '0';
    if (lv >= 10) { num_buf[0] = '1'; lv -= 10; }
    num_buf[1] = '0' + lv;
    num_buf[2] = 0;
    consoleDrawText(2, 6, "NOW LV:");
    consoleDrawText(9, 6, num_buf);
    /* Refresh stats row to show new max HP/SP */
    battleUIDrawPlayerStats();
}

void battleUIDrawItemMenu(u8 *item_ids, u8 *qtys, u8 count, u8 cursor)
{
    u8 i;
    for (i = 0; i < 4; i++) {
        clearRow(9 + i);
        if (i < count) {
            if (i == cursor) {
                consoleDrawText(2, 9 + i, ">");
            } else {
                consoleDrawText(2, 9 + i, " ");
            }
            consoleDrawText(4, 9 + i, invGetName(item_ids[i]));
            /* Show quantity: xN */
            num_buf[0] = 'x';
            num_buf[1] = '0' + qtys[i];
            num_buf[2] = 0;
            consoleDrawText(15, 9 + i, num_buf);
        }
    }
}

void battleUIDrawItemDrop(char *item_name)
{
    clearRow(7);
    consoleDrawText(2, 7, "GOT:");
    consoleDrawText(7, 7, item_name);
}
