/*==============================================================================
 * Turn-Based Battle Engine
 *
 * State machine for JRPG-style combat:
 *   INIT -> turn order by SPD -> alternating PLAYER/ENEMY turns ->
 *   RESOLVE after each action -> VICTORY/DEFEAT -> EXIT
 *
 * During battle:
 *   BG1: ENABLED  (text UI via consoleDrawText, 4bpp font)
 *   BG2: ENABLED  (star parallax as backdrop)
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
 *
 * PPU reconfiguration during battle:
 *   battleTransitionIn():
 *     - BG1 is switched from game background tiles to text font (4bpp).
 *       The tilemap at VRAM_TEXT_MAP is shared with BG1, so writing text
 *       via consoleDrawText() appears on BG1.
 *     - BG2 continues showing the star parallax (unmodified).
 *     - OBJ sprites 64-65 show enemy and player battle sprites.
 *   battleTransitionOut():
 *     - BG1 is reloaded with zone background tiles via bgLoadZone().
 *     - OBJ battle sprites are hidden, flight sprites restored.
 *
 * Memory note: the SNES has 128KB of WRAM. This file's static data is
 * minimal (battle context + item arrays), but the code itself is large
 * enough to overflow Bank 0's 32KB code space. WLA-DX automatically
 * places overflow into Bank 1 via the linker configuration.
 *============================================================================*/

#include "game/battle.h"
#include "assets.h"
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

/* Global battle context singleton - holds all state for the current battle */
BattleContext battle;

/* Battle trigger: set by collision system or debug key. Read by main loop
 * each frame. BATTLE_TRIGGER_NONE = no battle pending. */
u8 g_battle_trigger;

/*=== Enemy Battle Stats Table ===*/
/* Stat blocks for each enemy type when fought in turn-based battle.
 * These are separate from the flight-mode EnemyTypeDef stats (which control
 * HP/speed/fire rate during scrolling gameplay). Battle stats are tuned for
 * multi-turn RPG combat with the ATK^2/(ATK+DEF) damage formula.
 *
 * Column order: HP, ATK, DEF, SPD, SP, MaxSP
 *
 * Design notes:
 *   SCOUT:   Low stats across the board. Quick fights to teach mechanics.
 *   FIGHTER: Balanced stats. First real challenge; has 2 SP for specials.
 *   HEAVY:   High HP/DEF, low SPD. Tanks hits, rewards patient play.
 *   ELITE:   High ATK/SPD, moderate DEF. Goes first, hits hard. */
static const s16 enemy_battle_stats[4][6] = {
    {  30,   8,  3,  5, 0, 0 },   /* SCOUT:   30 HP, weak in all stats, no SP */
    {  60,  14,  8, 10, 2, 2 },   /* FIGHTER: 60 HP, balanced, 2 SP for specials */
    { 100,  20, 15,  6, 3, 3 },   /* HEAVY:   100 HP, high DEF, slow */
    {  80,  18, 10, 14, 4, 4 },   /* ELITE:   80 HP, high ATK/SPD, 4 SP */
};

/* XP awards per enemy type. Scales with difficulty to reward fighting
 * harder enemies. Player needs ~100 XP per level early on. */
static const u16 enemy_xp[4] = { 15, 30, 50, 75 };

/*=== Phase 14: Item Selection State ===*/
/* State for the item sub-menu during BSTATE_ITEM_SELECT.
 * These are populated by buildItemList() when the player selects ITEM. */
static u8 s_item_cursor;    /* Current selection index in the item sub-menu */
static u8 s_item_count;     /* Number of items available (0-4) */
static u8 s_item_ids[4];    /* Item IDs for sub-menu display (max 4 visible at once) */
static u8 s_item_qtys[4];   /* Quantities for display next to each item */
static u8 s_drop_item;      /* Item dropped on victory (ITEM_NONE if no drop) */
static u8 s_defend_carry;   /* #187: 1 = carry defend stance to next turn */
static s16 s_enemy_base_atk; /* #196: Original enemy ATK for scaling calc */
static u8 s_flee_attempts;  /* #216: Failed flee attempts this battle */
static u8 s_enemy_enraged;  /* #217: 1 when enemy has low-HP ATK boost */
static u8 s_attack_streak; /* #231: Consecutive ATTACK turns (bonus at 3) */

/* #206: Build enemy-type attack message into buffer */
static char bt_msg_buf[20];
static void buildEnemyMsg(const char *action)
{
    static const char *bt_names[4] = { "SCOUT", "FIGHTER", "CRUISER", "ELITE" };
    u8 i, j;
    const char *name;
    name = bt_names[battle.enemy_type];
    for (i = 0; i < 10 && name[i]; i++) {
        bt_msg_buf[i] = name[i];
    }
    bt_msg_buf[i++] = ' ';
    for (j = 0; j < 10 && action[j]; j++) {
        bt_msg_buf[i + j] = action[j];
    }
    bt_msg_buf[i + j] = 0;
}

/*===========================================================================*/
/* Damage Calculation (integer math only, no floating point)                 */
/*===========================================================================*/

/*
 * battleCalcDamageRaw
 * -------------------
 * Core damage formula shared by battle.c (normal enemies) and boss.c
 * (boss attacks). Extracted to avoid code duplication.
 *
 * Formula: ATK^2 / (ATK + DEF) + random(0-3), minimum 1.
 *
 * This is a diminishing-returns formula where high ATK vs low DEF gives
 * large damage, but stacking ATK has diminishing effect. At equal ATK and
 * DEF, damage = ATK/2. The formula naturally scales without needing lookup
 * tables or level-based multipliers.
 *
 * Overflow analysis for s16 (max 32767):
 *   Max ATK in this game is ~43 (level 10 player with ATK boost item).
 *   43^2 = 1849, well within s16 range.
 *   Denominator minimum is clamped to 1 to prevent divide-by-zero.
 *
 * Variance uses g_frame_count (VBlank counter) as a cheap pseudo-random
 * source: (frame_count & 3) gives 0-3 extra damage. Not cryptographically
 * random, but sufficient for gameplay variety.
 *
 * Parameters:
 *   atk_val - attacker's ATK stat
 *   def_val - defender's DEF stat (may be doubled if defending)
 *
 * Returns:
 *   Damage value, minimum 1 (guaranteed scratch damage).
 */
s16 battleCalcDamageRaw(s16 atk_val, s16 def_val)
{
    s16 numerator;
    s16 denominator;
    s16 damage;

    /* ATK^2 / (ATK + DEF): diminishing returns damage formula.
     * Max ATK=43 -> 43*43=1849, safely within s16 max of 32767. */
    numerator = atk_val * atk_val;
    denominator = atk_val + def_val;
    if (denominator < 1) denominator = 1; /* Prevent division by zero */
    damage = numerator / denominator;

    /* Variance: add 0-3 random damage using frame counter as RNG source.
     * Bitwise AND with 3 gives values 0,1,2,3 cycling every 4 frames. */
    damage += (s16)(g_frame_count & 3);

    /* Minimum 1 damage: even heavily armored targets take scratch damage */
    if (damage < 1) damage = 1;
    return damage;
}

/*
 * battleCalcDamage (static)
 * -------------------------
 * Calculate damage from attacker to defender, factoring in the defend stance.
 * Wraps battleCalcDamageRaw with defender's DEF doubling when guarding.
 *
 * Parameters:
 *   attacker - the combatant dealing damage
 *   defender - the combatant receiving damage
 *
 * Returns:
 *   Damage value (always >= 1).
 */
static s16 battleCalcDamage(BattleCombatant *attacker, BattleCombatant *defender)
{
    s16 defense;

    defense = defender->def;
    if (defender->defending) {
        defense = defense << 1;  /* Double DEF when guarding (shift = fast multiply by 2) */
    }

    return battleCalcDamageRaw(attacker->atk, defense);
}

/*===========================================================================*/
/* Shake Duration Helper (improvement #14)                                   */
/*===========================================================================*/

/*
 * calcShakeDur (static)
 * ---------------------
 * Calculate shake animation duration based on damage dealt.
 * Higher damage = longer shake for more impactful visual feedback.
 *
 * Mapping (using >>5 = divide by 32 to bucket damage):
 *   0-31 damage   -> 4 frames (light tap)
 *   32-63 damage  -> 6 frames
 *   64-95 damage  -> 8 frames
 *   96+ damage    -> 10 frames (heavy hit)
 *
 * Uses shift instead of divide for 65816 performance.
 *
 * Parameters:
 *   damage - the damage amount (positive)
 *
 * Returns:
 *   Shake duration in frames (4, 6, 8, or 10).
 */
static u8 calcShakeDur(s16 damage)
{
    u8 idx;
    /* >>5 = divide by 32: maps damage into 4 buckets */
    idx = (u8)((u16)damage >> 5);
    if (idx > 3) idx = 3;         /* Clamp to max bucket */
    return 4 + (idx << 1);        /* 4 + 0/2/4/6 = 4/6/8/10 frames */
}

/*===========================================================================*/
/* Action Resolution                                                         */
/*===========================================================================*/

/*
 * resolveAction (static)
 * ----------------------
 * Resolve one combatant's chosen action against the target.
 * This is the core combat logic: applies damage, handles healing,
 * updates UI, and triggers visual feedback (shake, sound effects).
 *
 * Parameters:
 *   actor  - the combatant performing the action
 *   target - the combatant receiving the action (for attacks)
 *   action - BACT_* action ID, or BOSS_ACT_* (>= 10) for boss attacks
 *
 * Action details:
 *   BACT_ATTACK:  Standard damage using ATK^2/(ATK+DEF) formula.
 *   BACT_SPECIAL: 1.5x damage (base + base/2), costs 1 SP.
 *                 Falls through to ATTACK if SP is 0 (no wasted turn).
 *   BACT_DEFEND:  Sets defending flag, no damage dealt.
 *   BACT_ITEM:    Heals 25% max HP using >>2 (shift approximation).
 *   >= 10:        Boss-specific attacks, delegated to bossResolveAction().
 *
 * UI updates are performed inline: message, damage number, HP bars.
 */
static void resolveAction(BattleCombatant *actor, BattleCombatant *target, u8 action)
{
    s16 damage;
    s16 heal;
    u8 shake_tgt;
    u8 shake_dur;

    /* Determine shake target: player attacks shake the enemy sprite (0),
     * enemy attacks shake the player sprite (1). */
    shake_tgt = actor->is_player ? 0 : 1;

    switch (action) {
        case BACT_SPECIAL:
            /* Special attack: 1.5x damage, costs 1 SP */
            if (actor->sp > 0) {
                actor->sp--;
                damage = battleCalcDamage(actor, target);

                /* #173: Desperation power - 2x damage when HP < 25% max */
                if (actor->is_player && actor->hp < (actor->max_hp >> 2)) {
                    damage = damage << 1;  /* 2x instead of 1.5x */
                    battleUIDrawMessage("DESPERATION!");
                    soundPlaySFX(SFX_EXPLOSION);
                    target->hp -= damage;
                    if (target->hp < 0) target->hp = 0;
                    battle.last_damage = damage;
                    shake_dur = calcShakeDur(damage);
                    battleUIStartShakeN(shake_tgt, shake_dur);
                    break;
                }

                /* 1.5x = base + base/2 */
                damage = damage + (damage >> 1);

                /* #152: Critical hit on special attacks too */
                if (actor->is_player) {
                    u8 crit_roll;
                    crit_roll = (u8)((g_frame_count * 31) & 0xFF);
                    if (crit_roll < (u8)(actor->spd << 2)) {
                        damage = damage + (damage >> 1);  /* Additional 1.5x on crit */
                        battleUIDrawMessage("CRIT SPECIAL!");
                    } else {
                        battleUIDrawMessage("VEX SPECIAL!");
                    }
                } else {
                    /* #206: Show enemy type name in message */
                buildEnemyMsg("SPECIAL!");
                battleUIDrawMessage(bt_msg_buf);
                }

                target->hp -= damage;
                if (target->hp < 0) target->hp = 0;
                battle.last_damage = damage;
                shake_dur = calcShakeDur(damage);
                battleUIStartShakeN(shake_tgt, shake_dur);
                break;
            }
            /* No SP remaining: fall through to normal ATTACK.
             * This prevents the enemy AI from wasting a turn when out of SP. */

        case BACT_ATTACK:
            /* Standard attack: apply base damage formula */
            damage = battleCalcDamage(actor, target);

            /* #231: Attack streak bonus - +25% on 3rd consecutive ATTACK */
            if (actor->is_player && s_attack_streak >= 3) {
                damage += (damage >> 2);
                s_attack_streak = 0;
            }

            /* #152: Critical hit system - player attacks have SPD-based crit chance. */
            if (actor->is_player) {
                u8 crit_roll;
                crit_roll = (u8)((g_frame_count * 31) & 0xFF);
                if (crit_roll < (u8)(actor->spd << 2)) {
                    damage = damage + (damage >> 1);
                    battleUIDrawMessage("CRITICAL!");
                    soundPlaySFX(SFX_EXPLOSION);
                } else {
                    battleUIDrawMessage("VEX ATTACKS!");
                    soundPlaySFX(SFX_HIT);
                }
            } else {
                buildEnemyMsg("ATTACKS!");
                battleUIDrawMessage(bt_msg_buf);
                soundPlaySFX(SFX_HIT);
            }

            target->hp -= damage;
            if (target->hp < 0) target->hp = 0;
            battle.last_damage = damage;
            shake_dur = calcShakeDur(damage);
            battleUIStartShakeN(shake_tgt, shake_dur);

            /* #124/#228: Defend counter-attack (~37.5% proc, 75% damage) */
            if (target->defending && !actor->is_player) {
                if ((g_frame_count & 7) < 3) {
                    s16 counter_dmg;
                    counter_dmg = (damage >> 1) + (damage >> 2);
                    if (counter_dmg < 1) counter_dmg = 1;
                    actor->hp -= counter_dmg;
                    if (actor->hp < 0) actor->hp = 0;
                    battleUIDrawMessage("COUNTER!");
                    soundPlaySFX(SFX_HIT);
                }
                if ((g_frame_count & 7) == 0) {
                    invAdd(ITEM_HP_POTION_S, 1);
                    battleUIDrawMessage("INTERCEPTED!");
                    soundPlaySFX(SFX_HEAL);
                }
            }

            /* #166: Defend SP recovery */
            if (target->defending && target->is_player) {
                s16 raw_dmg;
                raw_dmg = battleCalcDamageRaw(actor->atk, target->def);
                if (raw_dmg > 25 && battle.player.sp < battle.player.max_sp) {
                    battle.player.sp++;
                    battleUIDrawMessage("SP RECOVERED!");
                }
            }

            /* #182: Elite poison (25% chance) */
            if (!actor->is_player && battle.enemy_type == ENEMY_TYPE_ELITE &&
                (g_frame_count & 3) == 0 && target->poison_turns == 0) {
                target->poison_turns = 3;
                battleUIDrawMessage("POISONED!");
            }

            /* #238: Elite SP drain in Zone 3 (25% chance) */
            if (!actor->is_player && battle.enemy_type == ENEMY_TYPE_ELITE &&
                bgGetCurrentZone() >= 2 && (g_frame_count & 3) == 0) {
                if (rpg_stats.sp > 0) {
                    rpg_stats.sp--;
                    battle.player.sp = rpg_stats.sp;
                    battleUIDrawMessage("SP DRAINED!");
                }
            }
            break;

        case BACT_DEFEND:
            /* Defend: set flag, no damage. DEF doubling happens in battleCalcDamage()
             * when the defender has 'defending' set. */
            actor->defending = 1;
            battle.last_damage = 0;
            /* #232: Poison cure via defend - extra poison turn removed */
            if (actor->is_player && actor->poison_turns > 0) {
                actor->poison_turns--;
                battleUIDrawMessage("DETOX -1T!");
            } else if (actor->is_player) {
                battleUIDrawMessage("VEX DEFENDS!");
            } else {
                /* #206: Show enemy type name in message */
                buildEnemyMsg("DEFENDS!");
                battleUIDrawMessage(bt_msg_buf);
            }
            break;

        case BACT_ITEM:
            /* Heal 25% of max HP using bitshift (no multiply/divide needed).
             * >>2 = divide by 4 = 25%. Minimum 1 HP healed. */
            heal = actor->max_hp >> 2;
            if (heal < 1) heal = 1;
            actor->hp += heal;
            if (actor->hp > actor->max_hp) actor->hp = actor->max_hp;
            /* Negative last_damage signals healing to the UI display */
            battle.last_damage = -heal;
            soundPlaySFX(SFX_HEAL);
            if (actor->is_player) {
                battleUIDrawMessage("VEX USES ITEM!");
            }
            break;

        default:
            /* Phase 19: Boss-specific attacks have action IDs >= 10.
             * These are resolved by the boss module which has its own
             * damage formulas and special effects. */
            if (action >= 10 && battle.is_boss) {
                bossResolveAction(action);
                return;  /* bossResolveAction handles its own UI updates */
            }
            break;
    }

    /* Update UI: show damage number and refresh both HP bars */
    battleUIDrawDamage(battle.last_damage);
    battleUIUpdateEnemyHP();
    battleUIUpdatePlayerHP();
}

/*===========================================================================*/
/* Enemy AI Decision                                                         */
/*===========================================================================*/

/*
 * enemyChooseAction (static)
 * --------------------------
 * AI decision logic for normal (non-boss) enemies.
 * Uses pseudo-random selection weighted by HP percentage.
 *
 * Randomness source: (frame_count + turn_number*8) & 0x0F gives a 0-15
 * value that changes each frame and each turn. Not perfectly random, but
 * sufficient for enemy AI variety. The turn_number shift prevents enemies
 * from always choosing the same action at the same frame offset.
 *
 * Strategy:
 *   HP > 25%:  Mostly attack (62%), sometimes special (19%) or defend (19%)
 *   HP <= 25%: More specials (25% if SP available), more defending (25%),
 *              remaining attacks. Low HP enemies become more cautious.
 *
 * Returns: BACT_ATTACK, BACT_SPECIAL, or BACT_DEFEND.
 */
static u8 enemyChooseAction(void)
{
    u8 r;

    /* Pseudo-random using frame counter + turn number.
     * <<3 = multiply by 8 to add turn-based variation. */
    r = (u8)(g_frame_count + ((u16)battle.turn_number << 3)) & 0x0F;

    /* #126: Per-enemy-type AI behavior variation.
     * Each enemy archetype has a distinct battle personality:
     *   SCOUT:   Aggressive - mostly attacks, rarely defends
     *   FIGHTER: Balanced   - standard mixed behavior (default)
     *   HEAVY:   Defensive  - defends more, uses specials when low
     *   ELITE:   Special-heavy - favors special attacks when SP available */
    switch (battle.enemy_type) {
        case ENEMY_TYPE_SCOUT:
            /* Scouts are aggressive: 75% attack, 25% special if SP */
            if (r < 12) return BACT_ATTACK;
            if (battle.enemy.sp > 0) return BACT_SPECIAL;
            return BACT_ATTACK;

        case ENEMY_TYPE_HEAVY:
            /* Heavies are defensive: defend more, special when low HP */
            if (battle.enemy.hp < (battle.enemy.max_hp >> 2)) {
                if (r < 6 && battle.enemy.sp > 0) return BACT_SPECIAL;
                if (r < 10) return BACT_DEFEND;
                return BACT_ATTACK;
            }
            if (r < 6) return BACT_ATTACK;
            if (r < 10) return BACT_DEFEND;
            if (battle.enemy.sp > 0) return BACT_SPECIAL;
            return BACT_ATTACK;

        case ENEMY_TYPE_ELITE:
            /* Elites favor specials: 37.5% special if SP, 37.5% attack, 25% defend */
            if (r < 6 && battle.enemy.sp > 0) return BACT_SPECIAL;
            if (r < 12) return BACT_ATTACK;
            return BACT_DEFEND;

        default:
            /* FIGHTER and fallback: balanced behavior (original logic) */
            break;
    }

    /* Default (FIGHTER type): balanced behavior */
    if (battle.enemy.hp < (battle.enemy.max_hp >> 2)) {
        if (r < 4 && battle.enemy.sp > 0) return BACT_SPECIAL;
        if (r < 8) return BACT_DEFEND;
        return BACT_ATTACK;
    }
    if (r < 10) return BACT_ATTACK;
    if (r < 13 && battle.enemy.sp > 0) return BACT_SPECIAL;
    return BACT_DEFEND;
}

/*===========================================================================*/
/* Phase 14: Item Helpers                                                    */
/*===========================================================================*/

/*
 * buildItemList (static)
 * ----------------------
 * Scan the inventory and build a list of up to 4 usable items for the
 * battle item sub-menu. Populates s_item_ids[] and s_item_qtys[].
 *
 * The sub-menu displays at most 4 items because of limited screen space
 * (rows 9-12 are used for the menu). Items are listed in inventory order.
 *
 * Sets s_item_count to the number of available items (0 = "NO ITEMS!" message).
 */
static void buildItemList(void)
{
    u8 i;
    s_item_count = 0;
    for (i = 0; i < INV_SIZE; i++) {
        if (s_item_count >= 4) break; /* Sub-menu can only show 4 items */
        if (g_inventory[i].item_id != ITEM_NONE && g_inventory[i].quantity > 0) {
            s_item_ids[s_item_count] = g_inventory[i].item_id;
            s_item_qtys[s_item_count] = g_inventory[i].quantity;
            s_item_count++;
        }
    }
}

/*
 * applyBattleItem (static)
 * ------------------------
 * Apply an item's effect to the player combatant during battle.
 * Handles all item types: HP potions, SP charge, ATK/DEF boosts,
 * and full restore.
 *
 * Parameters:
 *   item_id - ITEM_* constant identifying which item to use.
 *
 * The item's numeric effect is retrieved from the inventory system
 * via invGetEffect(). Each item type has specific application logic:
 *   HP potions:    Add effect to HP (clamped to max_hp)
 *   SP charge:     Add effect to SP (clamped to max_sp)
 *   ATK/DEF boost: Add effect to stat (permanent for this battle)
 *   Full restore:  Set HP and SP to maximum
 *
 * Updates battle.last_damage (negative = heal for UI display) and
 * draws appropriate UI messages. inventory removal is handled by caller.
 */
static void applyBattleItem(u8 item_id)
{
    s16 effect;

    effect = invGetEffect(item_id);
    soundPlaySFX(SFX_HEAL);

    switch (item_id) {
        case ITEM_HP_POTION_S:
            /* #227: Scale HP Potion S with level: 30 + level*3 */
            effect = 30 + (s16)rpg_stats.level + ((s16)rpg_stats.level << 1);
            /* Fall through to HP potion logic */
        case ITEM_HP_POTION_L:
            /* #194: Critical heal - +25% when HP below 25% max */
            if (battle.player.hp < (battle.player.max_hp >> 2)) {
                effect += (effect >> 2);
                battle.player.hp += effect;
                if (battle.player.hp > battle.player.max_hp)
                    battle.player.hp = battle.player.max_hp;
                battle.last_damage = -effect;
                battleUIDrawMessage("CRITICAL HEAL!");
            } else {
                /* HP restoration: add effect amount, clamp to max */
                battle.player.hp += effect;
                if (battle.player.hp > battle.player.max_hp)
                    battle.player.hp = battle.player.max_hp;
                battle.last_damage = -effect;
                battleUIDrawMessage("VEX HEALS!");
            }
            battleUIDrawDamage(battle.last_damage);
            break;

        case ITEM_SP_CHARGE:
            /* SP restoration: add effect, clamp to max */
            battle.player.sp += (u8)effect;
            if (battle.player.sp > battle.player.max_sp)
                battle.player.sp = battle.player.max_sp;
            battle.last_damage = 0; /* No damage display for SP items */
            battleUIDrawMessage("SP RESTORED!");
            break;

        case ITEM_ATK_BOOST:
            /* Temporary ATK increase (lasts until battle ends) */
            battle.player.atk += effect;
            battle.last_damage = 0;
            battleUIDrawMessage("ATK UP!");
            break;

        case ITEM_DEF_BOOST:
            /* Temporary DEF increase (lasts until battle ends) */
            battle.player.def += effect;
            battle.last_damage = 0;
            battleUIDrawMessage("DEF UP!");
            break;

        case ITEM_FULL_RESTORE:
            /* Full heal: set HP and SP to their maximums */
            battle.player.hp = battle.player.max_hp;
            battle.player.sp = battle.player.max_sp;
            battle.last_damage = -(battle.player.max_hp); /* Show max HP as heal amount */
            battleUIDrawMessage("FULLY HEALED!");
            battleUIDrawDamage(battle.last_damage);
            break;
    }

    /* Refresh player stat display to show updated HP/SP/ATK/DEF */
    battleUIDrawPlayerStats();
}

/*===========================================================================*/
/* Battle Transitions                                                        */
/*===========================================================================*/

/*
 * battleTransitionIn (static)
 * ---------------------------
 * Blocking transition from flight mode into battle screen.
 * Reconfigures the SNES PPU for the battle UI:
 *
 * Sequence:
 *   1. Fade screen to black (blocking - waits for completion)
 *   2. Stop all flight systems (scrolling, bullets, enemies, player)
 *   3. Enter force blank (screen off) for safe VRAM writes
 *   4. Initialize BG1 text system: load 4bpp font tiles into VRAM,
 *      configure BG1 char base and tilemap pointers
 *   5. Enable BG1 for text display
 *   6. Show battle sprites (enemy + player) at OAM slots 64-65
 *   7. Draw initial battle UI (stat bars, "ENCOUNTER!" message)
 *   8. Exit force blank and fade screen back in
 *
 * SNES PPU notes:
 *   - Force blank ($2100 bit 7) must be set before VRAM/CGRAM writes.
 *   - The font is 4bpp and shares BG1's tilemap. consoleSetTextOffset(0x0100)
 *     places font tiles starting at tile index 256 (0x100) within BG1's
 *     character space, keeping them separate from game BG tiles at index 0.
 *   - bgSetGfxPtr and bgSetMapPtr configure PPU registers $210B/$2107 to
 *     point BG1 at the correct VRAM addresses for font rendering.
 */
static void battleTransitionIn(void)
{
    /* Fade to black before reconfiguring PPU */
    fadeOutBlocking(15);

    /* Stop all flight-mode systems to prevent updates during battle */
    scrollSetSpeed(SCROLL_SPEED_STOP);  /* Freeze background scrolling */
    bulletClearAll();                    /* Remove all projectiles */
    enemyKillAll();                     /* Remove all flight-mode enemies */
    playerHide();                       /* Hide player ship sprite */
    spriteHideAll();                    /* Hide all sprite-pool sprites */

    /* Enter force blank: screen off, allows VRAM writes.
     * SNES register $2100 = 0x80 (forced blanking). */
    setScreenOff();

    /* Initialize BG1 text system for battle UI.
     * PVSnesLib's console text system uses BG1 in 4bpp mode:
     *   - Font tiles at VRAM_TEXT_GFX (tile index 0x100 from BG1 char base)
     *   - Text tilemap at VRAM_TEXT_MAP (shared with BG1 map)
     *   - consoleDrawText() writes tile indices into this tilemap */
    consoleSetTextMapPtr(VRAM_TEXT_MAP);
    consoleSetTextGfxPtr(VRAM_TEXT_GFX);
    consoleSetTextOffset(0x0100);  /* Font tiles start at index 256 in BG1 space */
    consoleInitText(0, 16 * 2, &snesfont, &snespal);

    /* Point BG1 hardware registers at the correct VRAM addresses */
    bgSetGfxPtr(0, VRAM_BG1_GFX);         /* BG1 char base */
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32); /* BG1 map, 32x32 tiles */

    /* Enable BG1 layer on screen for text display */
    bgSetEnable(0);

    /* Show 32x32 battle sprites for enemy and player at fixed screen positions */
    battleUIShowSprites(battle.enemy_type);

    /* Draw the complete initial battle UI: stat bars, names, "ENCOUNTER!" */
    battleUIDrawScreen();

    /* Exit force blank and fade in to reveal the battle screen */
    setScreenOn();
    fadeInBlocking(15);
}

/*
 * battleTransitionOut (static)
 * ----------------------------
 * Blocking transition from battle screen back to flight mode.
 * Restores the SNES PPU configuration for normal gameplay.
 *
 * Sequence:
 *   1. Fade screen to black
 *   2. Hide battle sprites
 *   3. Disable BG1 text layer
 *   4. Reload zone background (enters force blank internally,
 *      restores BG1 game tiles, re-enables BG1 + BG2)
 *   5. Show player ship again
 *   6. Exit force blank and fade in
 *   7. Resume scrolling at normal speed
 *   8. Grant 2 seconds of post-battle invincibility (120 frames)
 */
static void battleTransitionOut(void)
{
    /* Fade to black before restoring PPU */
    fadeOutBlocking(15);

    /* Clean up battle-mode display elements */
    battleUIHideSprites();  /* Hide enemy/player battle sprites at OAM 64-65 */
    bgSetDisable(0);        /* Disable BG1 text layer */

    /* Reload zone background to restore BG1 game tiles.
     * bgLoadZone() enters force blank internally, loads tileset + tilemap
     * from ROM into VRAM via DMA, and re-enables BG1 + BG2. */
    bgLoadZone(g_game.current_zone);

    /* Restore player ship visibility */
    playerShow();

    /* Exit force blank and fade in to reveal the flight screen */
    setScreenOn();
    fadeInBlocking(15);

    /* Resume gameplay systems */
    /* #135: Zone-appropriate scroll speed restoration.
     * Zone 3 (Flagship) uses FAST speed, others use NORMAL.
     * Previously hardcoded to NORMAL, which slowed Zone 3 after battle. */
    if (g_game.current_zone == ZONE_FLAGSHIP) {
        scrollSetSpeed(SCROLL_SPEED_FAST);
    } else {
        scrollSetSpeed(SCROLL_SPEED_NORMAL);
    }

    /* #128: Reset fire cooldown after battle exit.
     * Prevents leftover cooldown from causing a brief inability to fire
     * when returning to flight mode. */
    g_weapon.fire_cooldown = 0;
    /* Grant post-battle invincibility: 120 frames = 2 seconds at 60fps.
     * Prevents the player from immediately getting hit after returning
     * to flight mode (enemies may have respawned via scroll triggers). */
    g_player.invincible_timer = 120;
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

/*
 * battleInit
 * ----------
 * One-time initialization at game startup. Clears battle state,
 * initializes the battle UI subsystem and boss subsystem.
 * Must be called before any battle can start.
 */
void battleInit(void)
{
    battle.state = BSTATE_NONE;
    battle.is_boss = 0;
    battle.boss_zone = 0;
    g_battle_trigger = BATTLE_TRIGGER_NONE;
    battleUIInit();  /* Reset UI state (shake timers, menu tracking) */
    bossInit();      /* Reset boss state (charging, phase, etc.) */
}

/*
 * battleStart
 * -----------
 * Start a new battle against the specified enemy.
 *
 * Parameters:
 *   enemyType - ENEMY_TYPE_* (0-3) for normal enemies, or
 *               BOSS_TRIGGER_* (0x80+) for boss battles.
 *
 * This function:
 *   1. Initializes the BattleContext with both combatants' stats
 *   2. Determines turn order by comparing SPD values
 *   3. Performs the blocking transition into the battle screen
 *   4. Starts the intro timer (60 frames = 1 second "ENCOUNTER!" display)
 *
 * Player stats are copied from the persistent rpg_stats structure
 * (Phase 13). They will be synced back on victory/defeat.
 *
 * Boss battles (Phase 19): when enemyType is in the 0x80+ range,
 * bossSetup() is called to load boss-specific stats. The is_boss flag
 * changes AI selection, exit behavior, and enables boss-specific attacks.
 */
void battleStart(u8 enemyType)
{
    u8 et;

    /* Reset battle context to clean state */
    battle.state = BSTATE_INIT;
    battle.turn_number = 1;
    battle.menu_cursor = 0;
    battle.last_damage = 0;
    battle.last_actor = 0;
    battle.is_boss = 0;
    battle.boss_zone = 0;

    /* Initialize player combatant from persistent RPG stats (Phase 13).
     * These are the real player stats that persist between battles. */
    battle.player.hp      = rpg_stats.hp;
    battle.player.max_hp  = rpg_stats.max_hp;
    battle.player.atk     = rpg_stats.atk;
    battle.player.def     = rpg_stats.def;
    battle.player.spd     = rpg_stats.spd;
    battle.player.sp      = rpg_stats.sp;
    battle.player.max_sp  = rpg_stats.max_sp;
    battle.player.defending = 0;
    battle.player.is_player = 1;
    battle.player.poison_turns = 0;  /* #182: Clear poison */

    /* #123: Weapon-type stat bonus in battle.
     * Different weapon types grant ATK bonuses during battle:
     *   SPREAD: +0 (default, no bonus)
     *   SINGLE: +2 (focused fire = slightly stronger)
     *   LASER:  +4 (powerful beam = significant bonus)
     * Uses the global g_weapon state from bullets.h. */
    if (g_weapon.weapon_type == WEAPON_SINGLE) {
        battle.player.atk += 2;
    } else if (g_weapon.weapon_type == WEAPON_LASER) {
        battle.player.atk += 4;
    } else if (g_weapon.weapon_type == WEAPON_SPREAD) {
        battle.player.def += 3;  /* #204: SPREAD grants +3 DEF (defensive playstyle) */
    }

    /* Phase 19: Boss battles use 0x80+ trigger range.
     * IS_BOSS_TRIGGER checks if enemyType >= BOSS_TRIGGER_BASE. */
    if (IS_BOSS_TRIGGER(enemyType)) {
        const BossTypeDef *bdef;
        u8 btype;

        /* Extract boss type index (0-2) from trigger value */
        btype = BOSS_TYPE_FROM_TRIGGER(enemyType);
        bdef = bossSetup(btype);  /* Initialize boss runtime state */

        battle.is_boss = 1;
        battle.boss_zone = btype;
        battle.enemy_type = 0;  /* Use VRAM slot A sprite for battle display */

        /* Load boss stats into enemy combatant (much higher than normal enemies) */
        battle.enemy.hp      = bdef->hp;
        battle.enemy.max_hp  = bdef->hp;   /* max_hp = initial HP for bosses */
        battle.enemy.atk     = bdef->atk;
        battle.enemy.def     = bdef->def;
        battle.enemy.spd     = bdef->spd;
        battle.enemy.sp      = bdef->sp;
        battle.enemy.max_sp  = bdef->max_sp;
        battle.enemy.defending = 0;
        battle.enemy.is_player = 0;
        battle.enemy.poison_turns = 0;  /* #182 */

        battle.xp_gained = bdef->xp_reward;

        /* #161: Boss weapon weakness - reduce DEF if player has the right weapon.
         * COMMANDER weak to LASER, CRUISER weak to SPREAD, FLAGSHIP weak to SINGLE. */
        if (g_weapon.weapon_type == bdef->weakness) {
            battle.enemy.def -= 4;
            if (battle.enemy.def < 0) battle.enemy.def = 0;
        }
    } else {
        /* Standard enemy battle: load stats from enemy_battle_stats table */
        et = enemyType;
        if (et >= ENEMY_TYPE_COUNT) et = 0; /* Clamp invalid types to SCOUT */
        battle.enemy_type = et;

        /* Initialize enemy combatant from the stat table.
         * Array layout: [HP, ATK, DEF, SPD, SP, MaxSP] */
        battle.enemy.hp      = enemy_battle_stats[et][0];
        battle.enemy.max_hp  = enemy_battle_stats[et][0];
        battle.enemy.atk     = enemy_battle_stats[et][1];
        battle.enemy.def     = enemy_battle_stats[et][2];
        battle.enemy.spd     = enemy_battle_stats[et][3];
        battle.enemy.sp      = (u8)enemy_battle_stats[et][4];
        battle.enemy.max_sp  = (u8)enemy_battle_stats[et][5];
        battle.enemy.defending = 0;
        battle.enemy.is_player = 0;
        battle.enemy.poison_turns = 0;  /* #182 */

        battle.xp_gained = enemy_xp[et];

        /* #229: Zone-scaled XP for normal enemies */
        {
            u8 zone = g_game.current_zone;
            if (zone == 1) {
                battle.xp_gained += (battle.xp_gained >> 1); /* +50% Zone 2 */
            } else if (zone >= 2) {
                battle.xp_gained += (battle.xp_gained >> 1) + (battle.xp_gained >> 2); /* +75% Zone 3 */
            }
        }

        /* #211: Zone-scaled enemy stats - stronger enemies in later zones */
        if (g_game.current_zone == ZONE_ASTEROID) {
            /* Zone 2: +25% HP and ATK */
            battle.enemy.hp += (battle.enemy.hp >> 2);
            battle.enemy.max_hp = battle.enemy.hp;
            battle.enemy.atk += (battle.enemy.atk >> 2);
        } else if (g_game.current_zone == ZONE_FLAGSHIP) {
            /* Zone 3: +50% HP and ATK */
            battle.enemy.hp += (battle.enemy.hp >> 1);
            battle.enemy.max_hp = battle.enemy.hp;
            battle.enemy.atk += (battle.enemy.atk >> 1);
        }
    }

    /* #160: Dynamic difficulty assist - reduce enemy ATK after 2+ consecutive defeats */
    if (rpgGetDifficultyAssist()) {
        battle.enemy.atk -= (battle.enemy.atk >> 3);  /* ~12.5% ATK reduction */
    }

    /* #196: Store base ATK for turn-scaling calculation */
    s_enemy_base_atk = battle.enemy.atk;

    /* Determine turn order: compare SPD stats. Ties favor the player. */
    battle.player_goes_first = (battle.player.spd >= battle.enemy.spd) ? 1 : 0;

    /* Perform blocking PPU transition into battle screen */
    battleTransitionIn();

    /* #175: Zone-themed battle messages */
    {
        static const char *zone_encounter_msgs[3] = {
            "DEBRIS ENCOUNTER!",
            "ASTEROID AMBUSH!",
            "FLAGSHIP ASSAULT!"
        };
        u8 zi = g_game.current_zone;
        if (zi >= 3) zi = 0;
        battleUIDrawMessage((char *)zone_encounter_msgs[zi]);
    }

    s_defend_carry = 0;  /* #187: Reset defend carry */
    s_flee_attempts = 0; /* #216: Reset flee attempt counter */
    s_enemy_enraged = 0; /* #217: Reset low-HP enrage flag */
    s_attack_streak = 0; /* #231: Reset attack streak */

    /* Start intro timer: show encounter message for 60 frames (1 second) */
    battle.anim_timer = 60;
}

/*
 * battleUpdate
 * ------------
 * Main battle state machine: called once per frame while a battle is active.
 * Drives the entire battle flow through state transitions.
 *
 * Parameters:
 *   pad_pressed - edge-triggered button presses (rising edges only, not held
 *                 state). Used for menu navigation: UP/DOWN move cursor,
 *                 CONFIRM (A) selects action, CANCEL (Select) backs out.
 *
 * Returns:
 *   1 while battle is active (caller should keep calling battleUpdate)
 *   0 when battle has ended (state returned to BSTATE_NONE)
 *
 * State machine overview:
 *   INIT          -> Wait 60 frames, then first turn (by SPD order)
 *   PLAYER_TURN   -> Show menu, process D-pad + A button input
 *   PLAYER_ACT    -> 15-frame delay, then resolve player's action
 *   ENEMY_TURN    -> AI selects action instantly, 15-frame delay
 *   ENEMY_ACT     -> Resolve enemy's action
 *   RESOLVE       -> 30-frame display, check HP for victory/defeat
 *   ITEM_SELECT   -> Item sub-menu navigation (Phase 14)
 *   VICTORY       -> 90-frame display, sync stats, check level-up
 *   LEVELUP       -> 90-frame display with brightness flash
 *   DEFEAT        -> 90-frame display, apply penalty
 *   EXIT          -> Transition out (fade, restore PPU)
 */
u8 battleUpdate(u16 pad_pressed)
{
    BattleCombatant *plr;

    /* No battle active: return immediately */
    if (battle.state == BSTATE_NONE) return 0;

    plr = &battle.player;

    /* Per-frame UI animations: update sprite shake offset */
    battleUIUpdateShake();

    /* Boss desperate phase visual: flicker enemy sprite every 4 frames.
     * This creates a visual warning that the boss is in its most dangerous phase.
     * (improvement #19) */
    if (battle.is_boss && g_boss.ai_phase == BOSS_AI_DESPERATE) {
        if ((g_frame_count & 3) == 0) {
            oamSetVisible(BUI_ENEMY_OAM_ID, OBJ_HIDE);
        }
    }

    switch (battle.state) {
        case BSTATE_INIT:
            /* Intro display: wait for "ENCOUNTER!" message timer */
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            /* Clear defending flags from any previous state */
            plr->defending = 0;
            battle.enemy.defending = 0;
            /* Start first turn based on SPD comparison */
            if (battle.player_goes_first) {
                battle.state = BSTATE_PLAYER_TURN;
                setBrightness(15);
                /* #182: Poison tick on player turn */
                if (plr->poison_turns > 0) {
                    plr->hp -= 3;
                    if (plr->hp < 1) plr->hp = 1;
                    plr->poison_turns--;
                    /* #207: Show remaining poison turns */
                    if (plr->poison_turns > 0) {
                        bt_msg_buf[0]='P'; bt_msg_buf[1]='O'; bt_msg_buf[2]='I';
                        bt_msg_buf[3]='S'; bt_msg_buf[4]='O'; bt_msg_buf[5]='N';
                        bt_msg_buf[6]=' '; bt_msg_buf[7]='-'; bt_msg_buf[8]='3';
                        bt_msg_buf[9]='('; bt_msg_buf[10]='0'+plr->poison_turns;
                        bt_msg_buf[11]=')'; bt_msg_buf[12]=0;
                        battleUIDrawMessage(bt_msg_buf);
                    } else {
                        battleUIDrawMessage("POISON -3HP!");
                    }
                    battleUIUpdatePlayerHP();
                } else {
                    battleUIDrawMessage("YOUR TURN");
                }
                battleUIDrawMenu(battle.menu_cursor);
                battleUIDrawXPPreview(battle.xp_gained);  /* #185 */
                battleUIDrawTurnCount(battle.turn_number); /* #189 */
            } else {
                /* Enemy goes first: skip straight to enemy turn */
                battle.state = BSTATE_ENEMY_TURN;
            }
            return 1;

        case BSTATE_PLAYER_TURN:
            /* Menu navigation: D-pad up/down moves cursor */
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
            /* A button to confirm selected action */
            if (pad_pressed & ACTION_CONFIRM) {
                /* Validate: SPECIAL requires SP > 0 */
                if (battle.menu_cursor == BACT_SPECIAL && plr->sp == 0) {
                    /* #205: Feedback when trying to use SPECIAL with no SP */
                    battleUIDrawMessage("NO SP!");
                    soundPlaySFX(SFX_HIT);
                    return 1;  /* Can't use special without SP, stay in menu */
                }
                /* #163: FLEE option for non-boss battles (replaces ITEM at slot 3) */
                if (battle.menu_cursor == BACT_ITEM && !battle.is_boss) {
                    /* Flee attempt: 33% base + 6% per SPD above 10 */
                    u8 flee_roll;
                    u8 flee_threshold;
                    flee_threshold = 85;  /* ~33% of 256 */
                    if (battle.player.spd > 10) {
                        u8 spd_bonus;
                        spd_bonus = (u8)(battle.player.spd - 10);
                        flee_threshold += spd_bonus * 15;  /* ~6% per point */
                    }
                    /* #216: Each failed flee reduces success chance */
                    if (s_flee_attempts > 0) {
                        u8 penalty;
                        penalty = s_flee_attempts << 4;  /* 16 per attempt */
                        if (penalty >= flee_threshold) flee_threshold = 10;
                        else flee_threshold -= penalty;
                    }
                    flee_roll = (u8)((g_frame_count * 31) & 0xFF);
                    if (flee_roll < flee_threshold) {
                        /* Flee success */
                        battleUIDrawMessage("ESCAPED!");
                        soundPlaySFX(SFX_MENU_SELECT);
                        battle.anim_timer = 30;
                        battle.state = BSTATE_EXIT;
                    } else {
                        /* Flee failed - enemy gets free turn */
                        s_flee_attempts++;  /* #216: Track failed attempt */
                        battleUIDrawMessage("FAILED TO FLEE!");
                        soundPlaySFX(SFX_HIT);
                        battleUIClearMenu();
                        battle.state = BSTATE_ENEMY_TURN;
                    }
                    return 1;
                }
                /* ITEM: transition to item sub-menu (Phase 14) - boss battles only, or slot 3 */
                if (battle.menu_cursor == BACT_ITEM) {
                    buildItemList();
                    if (s_item_count == 0) {
                        battleUIDrawMessage("NO ITEMS!");
                        return 1; /* No items available, stay in menu */
                    }
                    /* Open item sub-menu */
                    s_item_cursor = 0;
                    battleUIClearMenu();
                    battleUIDrawMessage("USE ITEM:");
                    battleUIDrawItemMenu(s_item_ids, s_item_qtys,
                                         s_item_count, s_item_cursor);
                    battle.state = BSTATE_ITEM_SELECT;
                    return 1;
                }
                /* Standard action selected */
                soundPlaySFX(SFX_MENU_SELECT);
                battle.player_action = battle.menu_cursor;
                battleUIClearMenu();
                battle.anim_timer = 15;  /* Brief 0.25-sec pause before action */
                battle.state = BSTATE_PLAYER_ACT;
            }
            /* #209: Y/Cancel button opens item sub-menu in non-boss battles */
            if ((pad_pressed & ACTION_CANCEL) && !battle.is_boss) {
                buildItemList();
                if (s_item_count == 0) {
                    battleUIDrawMessage("NO ITEMS!");
                    return 1;
                }
                s_item_cursor = 0;
                battleUIClearMenu();
                battleUIDrawMessage("USE ITEM:");
                battleUIDrawItemMenu(s_item_ids, s_item_qtys, s_item_count, s_item_cursor);
                battle.state = BSTATE_ITEM_SELECT;
                return 1;
            }
            return 1;

        case BSTATE_PLAYER_ACT:
            /* Wait for pre-action delay */
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            /* #187: Only clear defend if not carried from mutual defend */
            if (!s_defend_carry) {
                plr->defending = 0;
            }
            s_defend_carry = 0;
            /* #231: Track attack streak */
            if (battle.player_action == BACT_ATTACK) {
                s_attack_streak++;
            } else {
                s_attack_streak = 0;
            }
            /* Resolve the player's chosen action against the enemy */
            resolveAction(plr, &battle.enemy, battle.player_action);
            battle.anim_timer = 30;  /* Show result for 0.5 seconds */
            battle.last_actor = 0;   /* Record: player acted last */
            battle.state = BSTATE_RESOLVE;
            return 1;

        case BSTATE_ENEMY_TURN:
            /* #217: Enemy low HP enrage - +4 ATK below 25% HP (non-boss, once) */
            if (!battle.is_boss && !s_enemy_enraged &&
                battle.enemy.hp < (battle.enemy.max_hp >> 2)) {
                battle.enemy.atk += 4;
                s_enemy_enraged = 1;
                buildEnemyMsg("ENRAGED!");
                battleUIDrawMessage(bt_msg_buf);
            }
            /* #219: Update turn counter display during enemy turn */
            battleUIDrawTurnCount(battle.turn_number);
            /* Enemy AI: choose action instantly (no input delay needed) */
            if (battle.is_boss) {
                /* Boss battles: check for AI phase transition, then use boss AI */
                bossUpdatePhase();
                battle.enemy_action = bossChooseAction();
            } else {
                /* Normal enemies: use standard AI decision */
                battle.enemy_action = enemyChooseAction();
            }
            /* #196: Enemy ATK scales after turn 8 (pressure to end quickly) */
            if (battle.turn_number > 8) {
                u8 bonus;
                bonus = battle.turn_number - 8;
                if (bonus > 5) bonus = 5;
                battle.enemy.atk = s_enemy_base_atk + (s16)bonus;
                if (battle.turn_number == 9) {
                    battleUIDrawMessage("ENEMY STRONGER!");
                }
            }
            /* Clear enemy defending flag for fresh turn */
            battle.enemy.defending = 0;
            battle.anim_timer = 15;  /* Brief pause before enemy acts */
            battle.state = BSTATE_ENEMY_ACT;
            return 1;

        case BSTATE_ENEMY_ACT:
            /* Wait for pre-action delay */
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            /* Resolve the enemy's chosen action against the player */
            resolveAction(&battle.enemy, plr, battle.enemy_action);
            battle.anim_timer = 30;  /* Show result for 0.5 seconds */
            battle.last_actor = 1;   /* Record: enemy acted last */
            battle.state = BSTATE_RESOLVE;
            return 1;

        case BSTATE_RESOLVE:
            /* Wait for result display delay */
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            /* Check for battle end: enemy defeated */
            if (battle.enemy.hp <= 0) {
                /* Roll item drop: boss = guaranteed drop, normal = RNG-based */
                if (battle.is_boss) {
                    s_drop_item = g_boss.drop_item;
                } else {
                    s_drop_item = invRollDrop(battle.enemy_type);
                }
                /* Add dropped item to inventory */
                if (s_drop_item != ITEM_NONE) {
                    invAdd(s_drop_item, 1);
                }
                /* Transition to victory state */
                battle.state = BSTATE_VICTORY;
                battleUIDrawVictory(battle.xp_gained);
                if (s_drop_item != ITEM_NONE) {
                    battleUIDrawItemDrop(invGetName(s_drop_item));
                }
                battle.anim_timer = 90; /* Victory display: 1.5 seconds */
                return 1;
            }
            /* Check for battle end: player defeated */
            if (plr->hp <= 0) {
                battle.state = BSTATE_DEFEAT;
                battleUIDrawDefeat();
                battle.anim_timer = 90; /* Defeat display: 1.5 seconds */
                return 1;
            }
            /* Battle continues: alternate turns.
             * After player acts -> enemy turn.
             * After enemy acts -> new round, player turn. */
            if (battle.last_actor == 0) {
                /* Player just acted -> enemy's turn next */
                battle.state = BSTATE_ENEMY_TURN;
            } else {
                /* Enemy just acted -> new round, player's turn.
                 * Increment turn counter (used in AI randomness). */
                battle.turn_number++;
                /* #187: Defend stance carry - both defended, carry player's */
                if (plr->defending && battle.enemy.defending) {
                    s_defend_carry = 1;
                } else {
                    s_defend_carry = 0;
                }
                battle.state = BSTATE_PLAYER_TURN;
                setBrightness(15);
                /* #182: Poison tick on player turn */
                if (plr->poison_turns > 0) {
                    plr->hp -= 3;
                    if (plr->hp < 1) plr->hp = 1;
                    plr->poison_turns--;
                    /* #207: Show remaining poison turns */
                    if (plr->poison_turns > 0) {
                        bt_msg_buf[0]='P'; bt_msg_buf[1]='O'; bt_msg_buf[2]='I';
                        bt_msg_buf[3]='S'; bt_msg_buf[4]='O'; bt_msg_buf[5]='N';
                        bt_msg_buf[6]=' '; bt_msg_buf[7]='-'; bt_msg_buf[8]='3';
                        bt_msg_buf[9]='('; bt_msg_buf[10]='0'+plr->poison_turns;
                        bt_msg_buf[11]=')'; bt_msg_buf[12]=0;
                        battleUIDrawMessage(bt_msg_buf);
                    } else {
                        battleUIDrawMessage("POISON -3HP!");
                    }
                    battleUIUpdatePlayerHP();
                } else if (s_defend_carry) {
                    battleUIDrawMessage("STANCE HELD!");
                } else {
                    battleUIDrawMessage("YOUR TURN");
                }
                battleUIDrawMenu(battle.menu_cursor);
                battleUIDrawXPPreview(battle.xp_gained);  /* #185 */
                battleUIDrawTurnCount(battle.turn_number); /* #189 */
            }
            return 1;

        case BSTATE_ITEM_SELECT:
            /* Item sub-menu: D-pad up/down navigates item list */
            if (pad_pressed & ACTION_UP) {
                if (s_item_cursor > 0) {
                    s_item_cursor--;
                    battleUIDrawItemMenu(s_item_ids, s_item_qtys,
                                         s_item_count, s_item_cursor);
                    soundPlaySFX(SFX_MENU_MOVE);
                }
            }
            if (pad_pressed & ACTION_DOWN) {
                /* #122: Guard against underflow when s_item_count is 0.
                 * Without the s_item_count > 0 check, (s_item_count - 1) wraps
                 * to 255 on u8, allowing cursor to go out of bounds. */
                if (s_item_count > 0 && s_item_cursor < s_item_count - 1) {
                    s_item_cursor++;
                    battleUIDrawItemMenu(s_item_ids, s_item_qtys,
                                         s_item_count, s_item_cursor);
                    soundPlaySFX(SFX_MENU_MOVE);
                }
            }
            /* A button: use the selected item */
            if (pad_pressed & ACTION_CONFIRM) {
                applyBattleItem(s_item_ids[s_item_cursor]);
                invRemove(s_item_ids[s_item_cursor], 1); /* Consume one from inventory */
                battleUIClearMenu();
                plr->defending = 0;
                battle.anim_timer = 30;    /* Show item effect for 0.5 seconds */
                battle.last_actor = 0;     /* Player acted (used item counts as a turn) */
                battle.state = BSTATE_RESOLVE;
            }
            /* Select/Cancel button: back out to main action menu */
            if (pad_pressed & ACTION_CANCEL) {
                battleUIClearMenu();
                battle.state = BSTATE_PLAYER_TURN;
                battleUIDrawMessage("YOUR TURN");
                battleUIDrawMenu(battle.menu_cursor);
            }
            return 1;

        case BSTATE_VICTORY:
            /* Wait for victory display to complete */
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            /* Sync surviving HP/SP back to persistent RPG stats.
             * ATK/DEF boosts from items are NOT persisted (battle-only). */
            rpg_stats.hp = plr->hp;
            rpg_stats.sp = plr->sp;
            rpg_stats.total_kills++;

            /* #236: Post-battle HP recovery - ~6.25% of max HP (min 1) */
            {
                u16 recovery;
                recovery = rpg_stats.max_hp >> 4;
                if (recovery < 1) recovery = 1;
                rpg_stats.hp += (s16)recovery;
                if (rpg_stats.hp > rpg_stats.max_hp) rpg_stats.hp = rpg_stats.max_hp;
            }

            /* #239: Win streak increment (max 5) */
            if (rpg_stats.win_streak < 5) rpg_stats.win_streak++;
            rpg_stats.defeat_streak = 0;  /* #160: Reset defeat streak on win */

            /* #136: Award credits from battle victories.
             * Credits scale with XP gained (same amount). Saturating add
             * prevents u16 overflow on the credits counter. */
            {
                u16 cr = battle.xp_gained;
                if (rpg_stats.credits > (u16)(0xFFFF - cr)) {
                    rpg_stats.credits = 0xFFFF;
                } else {
                    rpg_stats.credits += cr;
                }
            }

            /* #189: Granular turn bonus (replaces #153 flat 5-turn check)
             * 3 turns = +100%, 4 turns = +75%, 5 turns = +50% */
            if (battle.turn_number <= 3) {
                battle.xp_gained <<= 1;  /* +100% */
            } else if (battle.turn_number <= 4) {
                battle.xp_gained += (battle.xp_gained >> 1) + (battle.xp_gained >> 2); /* +75% */
            } else if (battle.turn_number <= 5) {
                battle.xp_gained += (battle.xp_gained >> 1);  /* +50% */
            }

            /* #158: XP catch-up bonus - +50% XP when under-leveled for zone */
            if (rpgGetCatchUpBonus()) {
                battle.xp_gained += (battle.xp_gained >> 1);
            }

            /* #176: Boss death score bonus */
            if (battle.is_boss) {
                static const u16 boss_score_bonus[3] = { 2000, 5000, 10000 };
                u16 bonus;
                u8 bz = battle.boss_zone;
                if (bz >= 3) bz = 0;
                bonus = boss_score_bonus[bz];
                if (g_score > (u16)(0xFFFF - bonus)) g_score = 0xFFFF;
                else g_score += bonus;
            }

            /* #179: Post-battle score bonus = XP gained * 3 (replaces simple g_score += xp) */
            {
                u16 battle_score;
                battle_score = (battle.xp_gained << 1) + battle.xp_gained;
                if (g_score > (u16)(0xFFFF - battle_score)) g_score = 0xFFFF;
                else g_score += battle_score;
            }

            /* #239: Win streak XP bonus - ~12.5% per streak point */
            if (rpg_stats.win_streak > 0) {
                u16 per_point;
                per_point = battle.xp_gained >> 3;
                battle.xp_gained += per_point * rpg_stats.win_streak;
            }

            /* #198: Adjust remaining XP for boss multi-phase awards */
            if (battle.is_boss) {
                if (g_boss.xp_phases_awarded >= 2) {
                    battle.xp_gained >>= 1;  /* 50% remaining (already gave 50%) */
                } else if (g_boss.xp_phases_awarded >= 1) {
                    battle.xp_gained -= (battle.xp_gained >> 2);  /* 75% remaining (already gave 25%) */
                }
            }

            if (rpgAddXP(battle.xp_gained)) {
                /* Level up! Refresh battle combatant stats for UI display.
                 * rpgAddXP auto-heals to full HP/SP on level-up. */
                plr->hp     = rpg_stats.hp;
                plr->max_hp = rpg_stats.max_hp;
                plr->sp     = rpg_stats.sp;
                plr->max_sp = rpg_stats.max_sp;
                battle.state = BSTATE_LEVELUP;
                soundPlaySFX(SFX_LEVEL_UP);
                setBrightness(15);
                /* #171: Display stat growth values on level-up */
                {
                    char stat_buf[24];
                    rpgGetGrowthStr(rpg_stats.level, stat_buf);
                    if (stat_buf[0]) {
                        battleUIDrawMessage(stat_buf);
                    }
                }
                battleUIDrawLevelUp(rpg_stats.level);
                battle.anim_timer = 90; /* Level-up display: 1.5 seconds */
            } else {
                /* No level-up: proceed directly to exit */
                battle.state = BSTATE_EXIT;
            }
            return 1;

        case BSTATE_LEVELUP:
            /* Level-up flash: brief brightness dip for dramatic visual impact.
             * Frame 88 (2 frames in): dim to 8/15 brightness.
             * Frame 86 (4 frames in): restore full brightness.
             * Creates a quick "pulse" effect. */
            if (battle.anim_timer == 88) {
                setBrightness(8);  /* Dim briefly */
            } else if (battle.anim_timer == 86) {
                setBrightness(15); /* Restore full brightness */
            }
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            battle.state = BSTATE_EXIT;
            return 1;

        case BSTATE_DEFEAT:
            /* Wait for defeat display to complete */
            if (battle.anim_timer > 0) {
                /* #212: Pulsing brightness during defeat display */
                if ((battle.anim_timer & 7) == 0) {
                    setBrightness(8);
                } else if ((battle.anim_timer & 7) == 4) {
                    setBrightness(15);
                }
                battle.anim_timer--;
                return 1;
            }
            /* Sync remaining stats back (HP may be 0) */
            rpg_stats.hp = plr->hp;
            rpg_stats.sp = plr->sp;
            /* Apply defeat penalty (e.g., lose 25% of max HP from future recovery).
             * rpgApplyDefeatPenalty() is defined in rpg_stats.c */
            rpgApplyDefeatPenalty();
            rpg_stats.win_streak = 0;  /* #239: Reset win streak on defeat */
            battle.state = BSTATE_EXIT;
            return 1;

        case BSTATE_EXIT:
            /* Handle exit differently based on battle outcome */
            if (plr->hp <= 0) {
                /* Defeat: minimal cleanup. Leave screen dark so main.c can
                 * detect the defeat condition and transition to game over.
                 * The game over screen will be drawn fresh by gsGameOverEnter(). */
                fadeOutBlocking(15);
                battleUIHideSprites();
                bgSetDisable(0);
            } else if (battle.is_boss) {
                /* Phase 19: Boss victory - fade out but don't restore flight.
                 * main.c detects is_boss and calls gsZoneAdvance() to handle
                 * zone transition (reload backgrounds, advance zone counter,
                 * or trigger final victory screen after Zone 3). */
                fadeOutBlocking(15);
                battleUIHideSprites();
                bgSetDisable(0);
                g_boss.active = 0; /* Mark boss as defeated */
            } else {
                /* Normal enemy victory: full transition back to flight mode.
                 * Restores BG1 game tiles, shows player, resumes scrolling. */
                battleTransitionOut();
            }
            /* Return to idle state */
            battle.state = BSTATE_NONE;
            return 0; /* Battle ended */
    }

    return 0; /* Unreachable, but satisfies compiler */
}
