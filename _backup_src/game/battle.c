/*==============================================================================
 * Turn-Based Battle Engine
 *
 * State machine for JRPG-style combat:
 *   INIT -> turn order by SPD -> alternating PLAYER/ENEMY turns ->
 *   RESOLVE after each action -> VICTORY/DEFEAT -> EXIT
 *
 * During battle:
 *   BG1: DISABLED (font tiles corrupt 0x3000 region)
 *   BG2: ENABLED  (star parallax as backdrop)
 *   BG3: ENABLED  (text UI via consoleDrawText)
 *   OBJ: Battle sprites at OAM_UI slots 64-65 (Phase 12)
 *
 * Damage formula (Phase 13): ATK^2 / (ATK+DEF) + variance, min 1
 * Special: 1.5x damage, costs 1 SP
 * Defend: doubles DEF for one incoming attack
 * Item: consumable items from inventory (Phase 14)
 *
 * Phase 13: RPG stats integration, ATK^2/(ATK+DEF) damage, XP/leveling.
 * Phase 14: Inventory items in battle, item sub-menu, loot drops on victory.
 * All drawing functions moved to battle_ui.c (Phase 12 refactor).
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#include "game/battle.h"
#include "game/battle_ui.h"
#include "game/boss.h"
#include "game/enemies.h"
#include "game/player.h"
#include "game/rpg_stats.h"
#include "game/inventory.h"
#include "engine/input.h"
#include "engine/sound.h"
#include "engine/scroll.h"
#include "engine/bullets.h"
#include "engine/sprites.h"
#include "engine/background.h"
#include "engine/fade.h"
#include "engine/vblank.h"
#include "engine/collision.h"

BattleContext battle;
u8 g_battle_trigger;

/*=== Enemy Battle Stats: HP, ATK, DEF, SPD, SP, MaxSP ===*/
static const s16 enemy_battle_stats[4][6] = {
    {  30,   8,  3,  5, 0, 0 },   /* SCOUT */
    {  60,  14,  8, 10, 2, 2 },   /* FIGHTER */
    { 100,  20, 15,  6, 3, 3 },   /* HEAVY */
    {  80,  18, 10, 14, 4, 4 },   /* ELITE */
};

/* XP awards per enemy type */
static const u16 enemy_xp[4] = { 15, 30, 50, 75 };

/*=== Phase 14: Item Selection State ===*/
static u8 s_item_cursor;
static u8 s_item_count;
static u8 s_item_ids[4];    /* Item IDs for sub-menu (max 4 visible) */
static u8 s_item_qtys[4];   /* Quantities for display */
static u8 s_drop_item;      /* Last drop rolled on victory */

/*===========================================================================*/
/* Damage Calculation (integer math only, no floating point)                 */
/*===========================================================================*/

static s16 battleCalcDamage(BattleCombatant *attacker, BattleCombatant *defender)
{
    s16 atk_val;
    s16 defense;
    s16 numerator;
    s16 denominator;
    s16 damage;

    atk_val = attacker->atk;
    defense = defender->def;

    if (defender->defending) {
        defense = defense << 1;  /* Double DEF when guarding */
    }

    /* Phase 13 formula: ATK^2 / (ATK + DEF)
     * Max ATK=43, ATK^2=1849, fits s16 (max 32767) */
    numerator = atk_val * atk_val;
    denominator = atk_val + defense;
    if (denominator < 1) denominator = 1;
    damage = numerator / denominator;

    /* Variance using frame counter as pseudo-random: -1 to +2 */
    damage += (s16)(g_frame_count & 3) - 1;

    if (damage < 1) damage = 1;
    return damage;
}

/*===========================================================================*/
/* Action Resolution                                                         */
/*===========================================================================*/

static void resolveAction(BattleCombatant *actor, BattleCombatant *target, u8 action)
{
    s16 damage;
    s16 heal;
    u8 shake_tgt;

    /* Determine shake target: player attacks -> shake enemy (0),
     * enemy attacks -> shake player (1) */
    shake_tgt = actor->is_player ? 0 : 1;

    switch (action) {
        case BACT_ATTACK:
            damage = battleCalcDamage(actor, target);
            target->hp -= damage;
            if (target->hp < 0) target->hp = 0;
            battle.last_damage = damage;
            soundPlaySFX(SFX_HIT);
            if (actor->is_player) {
                battleUIDrawMessage("VEX ATTACKS!");
            } else {
                battleUIDrawMessage("ENEMY ATTACKS!");
            }
            battleUIStartShake(shake_tgt);
            break;

        case BACT_DEFEND:
            actor->defending = 1;
            battle.last_damage = 0;
            if (actor->is_player) {
                battleUIDrawMessage("VEX DEFENDS!");
            } else {
                battleUIDrawMessage("ENEMY DEFENDS!");
            }
            break;

        case BACT_SPECIAL:
            if (actor->sp > 0) {
                actor->sp--;
                damage = battleCalcDamage(actor, target);
                damage = damage + (damage >> 1);  /* 1.5x damage */
                target->hp -= damage;
                if (target->hp < 0) target->hp = 0;
                battle.last_damage = damage;
                if (actor->is_player) {
                    battleUIDrawMessage("VEX SPECIAL!");
                } else {
                    battleUIDrawMessage("ENEMY SPECIAL!");
                }
                battleUIStartShake(shake_tgt);
            } else {
                /* No SP: fallback to normal attack */
                damage = battleCalcDamage(actor, target);
                target->hp -= damage;
                if (target->hp < 0) target->hp = 0;
                battle.last_damage = damage;
                if (actor->is_player) {
                    battleUIDrawMessage("VEX ATTACKS!");
                } else {
                    battleUIDrawMessage("ENEMY ATTACKS!");
                }
                battleUIStartShake(shake_tgt);
            }
            break;

        case BACT_ITEM:
            /* Heal 25% of max HP (bitshift, no multiply/divide) */
            heal = actor->max_hp >> 2;
            if (heal < 1) heal = 1;
            actor->hp += heal;
            if (actor->hp > actor->max_hp) actor->hp = actor->max_hp;
            battle.last_damage = -heal;
            soundPlaySFX(SFX_HEAL);
            if (actor->is_player) {
                battleUIDrawMessage("VEX USES ITEM!");
            }
            break;

        default:
            /* Phase 19: Boss-specific attacks (action >= 10) */
            if (action >= 10 && battle.is_boss) {
                bossResolveAction(action);
                return;  /* bossResolveAction handles UI updates */
            }
            break;
    }

    battleUIDrawDamage(battle.last_damage);
    battleUIDrawEnemyStats();
    battleUIDrawPlayerStats();
}

/*===========================================================================*/
/* Enemy AI Decision                                                         */
/*===========================================================================*/

static u8 enemyChooseAction(void)
{
    u8 r;

    /* Pseudo-random using frame counter + turn number */
    r = (u8)(g_frame_count + ((u16)battle.turn_number << 3)) & 0x0F;

    /* HP below 25%: higher chance to defend or use special */
    if (battle.enemy.hp < (battle.enemy.max_hp >> 2)) {
        if (r < 4 && battle.enemy.sp > 0) return BACT_SPECIAL;
        if (r < 8) return BACT_DEFEND;
        return BACT_ATTACK;
    }

    /* Normal: mostly attack, sometimes special or defend */
    if (r < 10) return BACT_ATTACK;
    if (r < 13 && battle.enemy.sp > 0) return BACT_SPECIAL;
    return BACT_DEFEND;
}

/*===========================================================================*/
/* Phase 14: Item Helpers                                                    */
/*===========================================================================*/

/* Build list of available items for sub-menu (max 4) */
static void buildItemList(void)
{
    u8 i;
    s_item_count = 0;
    for (i = 0; i < INV_SIZE; i++) {
        if (s_item_count >= 4) break;
        if (g_inventory[i].item_id != ITEM_NONE && g_inventory[i].quantity > 0) {
            s_item_ids[s_item_count] = g_inventory[i].item_id;
            s_item_qtys[s_item_count] = g_inventory[i].quantity;
            s_item_count++;
        }
    }
}

/* Apply item effect to battle player combatant, draw message and stats */
static void applyBattleItem(u8 item_id)
{
    s16 effect;

    effect = invGetEffect(item_id);
    soundPlaySFX(SFX_HEAL);

    switch (item_id) {
        case ITEM_HP_POTION_S:
        case ITEM_HP_POTION_L:
            battle.player.hp += effect;
            if (battle.player.hp > battle.player.max_hp)
                battle.player.hp = battle.player.max_hp;
            battle.last_damage = -effect;
            battleUIDrawMessage("VEX HEALS!");
            battleUIDrawDamage(battle.last_damage);
            break;

        case ITEM_SP_CHARGE:
            battle.player.sp += (u8)effect;
            if (battle.player.sp > battle.player.max_sp)
                battle.player.sp = battle.player.max_sp;
            battle.last_damage = 0;
            battleUIDrawMessage("SP RESTORED!");
            break;

        case ITEM_ATK_BOOST:
            battle.player.atk += effect;
            battle.last_damage = 0;
            battleUIDrawMessage("ATK UP!");
            break;

        case ITEM_DEF_BOOST:
            battle.player.def += effect;
            battle.last_damage = 0;
            battleUIDrawMessage("DEF UP!");
            break;

        case ITEM_FULL_RESTORE:
            battle.player.hp = battle.player.max_hp;
            battle.player.sp = battle.player.max_sp;
            battle.last_damage = -(battle.player.max_hp);
            battleUIDrawMessage("FULLY HEALED!");
            battleUIDrawDamage(battle.last_damage);
            break;
    }

    battleUIDrawPlayerStats();
}

/*===========================================================================*/
/* Battle Transitions                                                        */
/*===========================================================================*/

static void battleTransitionIn(void)
{
    /* Fade to black */
    fadeOutBlocking(15);

    /* Stop flight systems */
    scrollSetSpeed(SCROLL_SPEED_STOP);
    bulletClearAll();
    enemyKillAll();
    playerHide();
    spriteHideAll();

    /* Enter force blank for VRAM operations */
    setScreenOff();

    /* Disable BG1 (tiles will be corrupted by font at 0x3000) */
    bgSetDisable(0);

    /* Initialize BG3 text system (loads built-in font to VRAM 0x3000) */
    consoleInitText(0, BG_4COLORS, 0, 0);

    /* Enable BG3 for text display */
    bgSetEnable(2);

    /* Show battle sprites (enemy and player) at OAM_UI slots */
    battleUIShowSprites(battle.enemy_type);

    /* Draw initial battle UI (stats, HP bars, "ENCOUNTER!") */
    battleUIDrawScreen();

    /* Exit force blank and fade in */
    setScreenOn();
    fadeInBlocking(15);
}

static void battleTransitionOut(void)
{
    /* Fade to black */
    fadeOutBlocking(15);

    /* Hide battle sprites */
    battleUIHideSprites();

    /* Disable BG3 text */
    bgSetDisable(2);

    /* Reload zone background to fix BG1 tiles corrupted by font.
     * bgLoadZone enters force blank internally and re-enables BG1+BG2. */
    bgLoadZone(g_game.current_zone);

    /* Show player again */
    playerShow();

    /* Exit force blank and fade in */
    setScreenOn();
    fadeInBlocking(15);

    /* Resume flight */
    scrollSetSpeed(SCROLL_SPEED_NORMAL);
    g_player.invincible_timer = 120;
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

void battleInit(void)
{
    battle.state = BSTATE_NONE;
    battle.is_boss = 0;
    battle.boss_zone = 0;
    g_battle_trigger = BATTLE_TRIGGER_NONE;
    battleUIInit();
    bossInit();
}

void battleStart(u8 enemyType)
{
    u8 et;

    /* Initialize battle context */
    battle.state = BSTATE_INIT;
    battle.turn_number = 1;
    battle.menu_cursor = 0;
    battle.last_damage = 0;
    battle.last_actor = 0;
    battle.is_boss = 0;
    battle.boss_zone = 0;

    /* Initialize player combatant from persistent RPG stats (Phase 13) */
    battle.player.hp      = rpg_stats.hp;
    battle.player.max_hp  = rpg_stats.max_hp;
    battle.player.atk     = rpg_stats.atk;
    battle.player.def     = rpg_stats.def;
    battle.player.spd     = rpg_stats.spd;
    battle.player.sp      = rpg_stats.sp;
    battle.player.max_sp  = rpg_stats.max_sp;
    battle.player.defending = 0;
    battle.player.is_player = 1;

    /* Phase 19: Boss battles use 0x80+ trigger range */
    if (IS_BOSS_TRIGGER(enemyType)) {
        const BossTypeDef *bdef;
        u8 btype;

        btype = BOSS_TYPE_FROM_TRIGGER(enemyType);
        bdef = bossSetup(btype);

        battle.is_boss = 1;
        battle.boss_zone = btype;
        battle.enemy_type = 0;  /* Use slot A sprite for battle display */

        /* Load boss stats into enemy combatant */
        battle.enemy.hp      = bdef->hp;
        battle.enemy.max_hp  = bdef->hp;
        battle.enemy.atk     = bdef->atk;
        battle.enemy.def     = bdef->def;
        battle.enemy.spd     = bdef->spd;
        battle.enemy.sp      = bdef->sp;
        battle.enemy.max_sp  = bdef->max_sp;
        battle.enemy.defending = 0;
        battle.enemy.is_player = 0;

        battle.xp_gained = bdef->xp_reward;
    } else {
        /* Standard enemy battle */
        et = enemyType;
        if (et >= ENEMY_TYPE_COUNT) et = 0;
        battle.enemy_type = et;

        /* Initialize enemy combatant from type table */
        battle.enemy.hp      = enemy_battle_stats[et][0];
        battle.enemy.max_hp  = enemy_battle_stats[et][0];
        battle.enemy.atk     = enemy_battle_stats[et][1];
        battle.enemy.def     = enemy_battle_stats[et][2];
        battle.enemy.spd     = enemy_battle_stats[et][3];
        battle.enemy.sp      = (u8)enemy_battle_stats[et][4];
        battle.enemy.max_sp  = (u8)enemy_battle_stats[et][5];
        battle.enemy.defending = 0;
        battle.enemy.is_player = 0;

        battle.xp_gained = enemy_xp[et];
    }

    /* Determine turn order by speed */
    battle.player_goes_first = (battle.player.spd >= battle.enemy.spd) ? 1 : 0;

    /* Do blocking transition into battle screen */
    battleTransitionIn();

    /* Start intro timer (60 frames = 1 second) */
    battle.anim_timer = 60;
}

u8 battleUpdate(u16 pad_pressed)
{
    if (battle.state == BSTATE_NONE) return 0;

    /* Per-frame UI animations (shake effect) */
    battleUIUpdateShake();

    switch (battle.state) {
        case BSTATE_INIT:
            /* Wait for intro message timer */
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            /* Clear defending flags and start first turn */
            battle.player.defending = 0;
            battle.enemy.defending = 0;
            if (battle.player_goes_first) {
                battle.state = BSTATE_PLAYER_TURN;
                battleUIDrawMessage("YOUR TURN");
                battleUIDrawMenu(battle.menu_cursor);
            } else {
                battle.state = BSTATE_ENEMY_TURN;
            }
            return 1;

        case BSTATE_PLAYER_TURN:
            /* D-pad up/down to navigate menu */
            if (pad_pressed & ACTION_UP) {
                if (battle.menu_cursor > 0) {
                    battle.menu_cursor--;
                    battleUIDrawMenu(battle.menu_cursor);
                    soundPlaySFX(SFX_MENU_MOVE);
                }
            }
            if (pad_pressed & ACTION_DOWN) {
                if (battle.menu_cursor < BACT_COUNT - 1) {
                    battle.menu_cursor++;
                    battleUIDrawMenu(battle.menu_cursor);
                    soundPlaySFX(SFX_MENU_MOVE);
                }
            }
            /* A button to confirm action */
            if (pad_pressed & ACTION_CONFIRM) {
                /* Validate: special needs SP */
                if (battle.menu_cursor == BACT_SPECIAL && battle.player.sp == 0) {
                    return 1;  /* Can't use, stay in menu */
                }
                /* Item: open item sub-menu (Phase 14) */
                if (battle.menu_cursor == BACT_ITEM) {
                    buildItemList();
                    if (s_item_count == 0) {
                        battleUIDrawMessage("NO ITEMS!");
                        return 1;
                    }
                    s_item_cursor = 0;
                    battleUIClearMenu();
                    battleUIDrawMessage("USE ITEM:");
                    battleUIDrawItemMenu(s_item_ids, s_item_qtys,
                                         s_item_count, s_item_cursor);
                    battle.state = BSTATE_ITEM_SELECT;
                    return 1;
                }
                soundPlaySFX(SFX_MENU_SELECT);
                battle.player_action = battle.menu_cursor;
                battleUIClearMenu();
                battle.anim_timer = 15;  /* Brief pause before action */
                battle.state = BSTATE_PLAYER_ACT;
            }
            return 1;

        case BSTATE_PLAYER_ACT:
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            /* Resolve player action */
            battle.player.defending = 0;
            resolveAction(&battle.player, &battle.enemy, battle.player_action);
            battle.anim_timer = 30;  /* Show result for 0.5 sec */
            battle.last_actor = 0;   /* Player acted */
            battle.state = BSTATE_RESOLVE;
            return 1;

        case BSTATE_ENEMY_TURN:
            /* AI chooses action instantly */
            if (battle.is_boss) {
                bossUpdatePhase();
                battle.enemy_action = bossChooseAction();
            } else {
                battle.enemy_action = enemyChooseAction();
            }
            battle.enemy.defending = 0;
            battle.anim_timer = 15;  /* Brief pause before enemy acts */
            battle.state = BSTATE_ENEMY_ACT;
            return 1;

        case BSTATE_ENEMY_ACT:
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            /* Resolve enemy action */
            resolveAction(&battle.enemy, &battle.player, battle.enemy_action);
            battle.anim_timer = 30;
            battle.last_actor = 1;   /* Enemy acted */
            battle.state = BSTATE_RESOLVE;
            return 1;

        case BSTATE_RESOLVE:
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            /* Check for battle end */
            if (battle.enemy.hp <= 0) {
                /* Phase 19: Boss drops guaranteed item; normal enemies use RNG */
                if (battle.is_boss) {
                    s_drop_item = g_boss.drop_item;
                } else {
                    s_drop_item = invRollDrop(battle.enemy_type);
                }
                if (s_drop_item != ITEM_NONE) {
                    invAdd(s_drop_item, 1);
                }
                battle.state = BSTATE_VICTORY;
                battleUIDrawVictory(battle.xp_gained);
                if (s_drop_item != ITEM_NONE) {
                    battleUIDrawItemDrop(invGetName(s_drop_item));
                }
                battle.anim_timer = 90;
                return 1;
            }
            if (battle.player.hp <= 0) {
                battle.state = BSTATE_DEFEAT;
                battleUIDrawDefeat();
                battle.anim_timer = 90;
                return 1;
            }
            /* Battle continues - go to other combatant's turn */
            if (battle.last_actor == 0) {
                /* Player just acted -> enemy's turn */
                battle.state = BSTATE_ENEMY_TURN;
            } else {
                /* Enemy just acted -> player's turn, new round */
                battle.turn_number++;
                battle.state = BSTATE_PLAYER_TURN;
                battleUIDrawMessage("YOUR TURN");
                battleUIDrawMenu(battle.menu_cursor);
            }
            return 1;

        case BSTATE_ITEM_SELECT:
            /* D-pad up/down to navigate item list */
            if (pad_pressed & ACTION_UP) {
                if (s_item_cursor > 0) {
                    s_item_cursor--;
                    battleUIDrawItemMenu(s_item_ids, s_item_qtys,
                                         s_item_count, s_item_cursor);
                    soundPlaySFX(SFX_MENU_MOVE);
                }
            }
            if (pad_pressed & ACTION_DOWN) {
                if (s_item_cursor < s_item_count - 1) {
                    s_item_cursor++;
                    battleUIDrawItemMenu(s_item_ids, s_item_qtys,
                                         s_item_count, s_item_cursor);
                    soundPlaySFX(SFX_MENU_MOVE);
                }
            }
            /* A button: use selected item */
            if (pad_pressed & ACTION_CONFIRM) {
                applyBattleItem(s_item_ids[s_item_cursor]);
                invRemove(s_item_ids[s_item_cursor], 1);
                battleUIClearMenu();
                battle.player.defending = 0;
                battle.anim_timer = 30;
                battle.last_actor = 0;  /* Player acted */
                battle.state = BSTATE_RESOLVE;
            }
            /* Select button: cancel back to main menu */
            if (pad_pressed & ACTION_CANCEL) {
                battleUIClearMenu();
                battle.state = BSTATE_PLAYER_TURN;
                battleUIDrawMessage("YOUR TURN");
                battleUIDrawMenu(battle.menu_cursor);
            }
            return 1;

        case BSTATE_VICTORY:
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            /* Sync surviving HP/SP back to persistent stats */
            rpg_stats.hp = battle.player.hp;
            rpg_stats.sp = battle.player.sp;
            rpg_stats.total_kills++;

            /* Add XP to score for display, then process RPG leveling */
            g_score += battle.xp_gained;
            if (rpgAddXP(battle.xp_gained)) {
                /* Level up! Update battle combatant for UI display */
                battle.player.hp     = rpg_stats.hp;
                battle.player.max_hp = rpg_stats.max_hp;
                battle.player.sp     = rpg_stats.sp;
                battle.player.max_sp = rpg_stats.max_sp;
                battle.state = BSTATE_LEVELUP;
                soundPlaySFX(SFX_LEVEL_UP);
                battleUIDrawLevelUp(rpg_stats.level);
                battle.anim_timer = 90;
            } else {
                battle.state = BSTATE_EXIT;
            }
            return 1;

        case BSTATE_LEVELUP:
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            battle.state = BSTATE_EXIT;
            return 1;

        case BSTATE_DEFEAT:
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            /* Battle defeat = game over (Phase 15).
             * Don't sync stats or apply penalty; player is dead.
             * BSTATE_EXIT will skip flight restore when player.hp <= 0. */
            battle.state = BSTATE_EXIT;
            return 1;

        case BSTATE_EXIT:
            if (battle.player.hp <= 0) {
                /* Defeat: minimal cleanup, leave screen dark for game over.
                 * main.c will detect defeat and call gsGameOverEnter(). */
                fadeOutBlocking(15);
                battleUIHideSprites();
                bgSetDisable(2);
            } else if (battle.is_boss) {
                /* Phase 19: Boss victory - fade out, don't restore flight.
                 * main.c will call gsZoneAdvance() which handles reloading. */
                fadeOutBlocking(15);
                battleUIHideSprites();
                bgSetDisable(2);
                g_boss.active = 0;
            } else {
                /* Normal victory: full transition back to flight */
                battleTransitionOut();
            }
            battle.state = BSTATE_NONE;
            return 0;
    }

    return 0;
}
