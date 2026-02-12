/*==============================================================================
 * Boss Battle System - Phase 19
 *
 * Multi-phase boss AI for turn-based combat. 3 bosses (one per zone) with
 * escalating stats and increasingly complex attack patterns.
 *
 * Boss AI phases change at HP thresholds:
 *   NORMAL  (>50% HP):  Balanced - attacks, occasional special/heavy
 *   ENRAGED (25-50% HP): Aggressive - multi-hits, no defending, heals
 *   DESPERATE (<25% HP): All-out - charge, drain, heavy specials
 *
 * Boss-specific attacks (resolved here, called from battle.c):
 *   HEAVY:  2x base damage
 *   MULTI:  2-3 hits at 75% base damage each
 *   DRAIN:  Base damage to player + heal self for half
 *   CHARGE: No damage this turn, next attack gets bonus
 *   REPAIR: Heal 20% max HP (limited to once per 3 turns)
 *
 * Bank 0 is full; this file auto-overflows via WLA-DX linker.
 *============================================================================*/

#include "game/boss.h"
#include "game/battle.h"
#include "game/battle_ui.h"
#include "game/inventory.h"
#include "engine/sound.h"
#include "engine/vblank.h"

BossState g_boss;

/*=== Boss Type Definitions (ROM data) ===*/
/*                     HP   ATK  DEF  SPD  SP MSP  XP   DROP             NAME        */
static const BossTypeDef boss_types[BOSS_TYPE_COUNT] = {
    { 120,  18,  10,   8,  3,  3,  100, ITEM_HP_POTION_L,  "COMMANDER"  },
    { 200,  22,  18,   6,  4,  4,  200, ITEM_SP_CHARGE,    "CRUISER"    },
    { 350,  30,  22,  12,  6,  6,  400, ITEM_FULL_RESTORE, "FLAGSHIP"   },
};

/*===========================================================================*/
/* Initialization                                                            */
/*===========================================================================*/

void bossInit(void)
{
    g_boss.active = 0;
    g_boss.type = 0;
    g_boss.ai_phase = BOSS_AI_NORMAL;
    g_boss.is_charging = 0;
    g_boss.charge_bonus = 0;
    g_boss.turns_since_heal = 0;
    g_boss.drop_item = ITEM_NONE;
    g_boss.name[0] = 0;
}

const BossTypeDef* bossSetup(u8 bossType)
{
    const BossTypeDef *def;
    u8 i;

    if (bossType >= BOSS_TYPE_COUNT) bossType = 0;
    def = &boss_types[bossType];

    g_boss.active = 1;
    g_boss.type = bossType;
    g_boss.ai_phase = BOSS_AI_NORMAL;
    g_boss.is_charging = 0;
    g_boss.charge_bonus = 0;
    g_boss.turns_since_heal = 0;
    g_boss.drop_item = def->drop_item;

    /* Copy boss name */
    for (i = 0; i < 11 && def->name[i]; i++) {
        g_boss.name[i] = def->name[i];
    }
    g_boss.name[i] = 0;

    return def;
}

/*===========================================================================*/
/* Damage Calculation (duplicated from battle.c to avoid exposing static)    */
/*===========================================================================*/

static s16 bossCalcDamage(s16 atk_val, s16 def_val)
{
    s16 numerator;
    s16 denominator;
    s16 damage;

    /* ATK^2 / (ATK + DEF) - same formula as battle.c */
    numerator = atk_val * atk_val;
    denominator = atk_val + def_val;
    if (denominator < 1) denominator = 1;
    damage = numerator / denominator;

    /* Variance: -1 to +2 */
    damage += (s16)(g_frame_count & 3) - 1;
    if (damage < 1) damage = 1;

    return damage;
}

/*===========================================================================*/
/* AI Phase Tracking                                                         */
/*===========================================================================*/

void bossUpdatePhase(void)
{
    u8 old_phase;
    s16 hp;
    s16 quarter;

    old_phase = g_boss.ai_phase;
    hp = battle.enemy.hp;
    quarter = battle.enemy.max_hp >> 2;

    if (hp <= quarter) {
        g_boss.ai_phase = BOSS_AI_DESPERATE;
    } else if (hp <= (quarter << 1)) {
        g_boss.ai_phase = BOSS_AI_ENRAGED;
    } else {
        g_boss.ai_phase = BOSS_AI_NORMAL;
    }

    /* Show message on phase change */
    if (g_boss.ai_phase > old_phase) {
        if (g_boss.ai_phase == BOSS_AI_ENRAGED) {
            battleUIDrawMessage("ENEMY POWERS UP!");
        } else {
            battleUIDrawMessage("GOING ALL OUT!");
        }
        soundPlaySFX(SFX_EXPLOSION);
    }
}

/*===========================================================================*/
/* Boss AI Action Selection                                                  */
/*===========================================================================*/

u8 bossChooseAction(void)
{
    u8 r;

    /* If charging from last turn, release charged heavy attack */
    if (g_boss.is_charging) {
        g_boss.is_charging = 0;
        return BOSS_ACT_HEAVY;
    }

    /* Track turns since last heal */
    g_boss.turns_since_heal++;

    /* Pseudo-random using frame counter + turn number */
    r = (u8)(g_frame_count * 7 + (u16)battle.turn_number * 13) & 0x0F;

    switch (g_boss.ai_phase) {
        case BOSS_AI_NORMAL:
            /* Balanced: attacks, occasional special/heavy, rare defend */
            if (r < 6) return BACT_ATTACK;
            if (r < 9 && battle.enemy.sp > 0) return BACT_SPECIAL;
            if (r < 11) return BOSS_ACT_HEAVY;
            if (r < 13) return BACT_DEFEND;
            return BACT_ATTACK;

        case BOSS_AI_ENRAGED:
            /* Aggressive: multi-hits, specials, heals when low */
            if (r < 4) return BACT_ATTACK;
            if (r < 7 && battle.enemy.sp > 0) return BOSS_ACT_MULTI;
            if (r < 10) return BOSS_ACT_HEAVY;
            if (r < 12 && battle.enemy.sp > 0) return BACT_SPECIAL;
            if (r < 14 && g_boss.turns_since_heal >= 3) {
                return BOSS_ACT_REPAIR;
            }
            return BACT_ATTACK;

        case BOSS_AI_DESPERATE:
            /* All-out: drain, charge, multi-hits, desperation heals */
            if (r < 3 && battle.enemy.sp > 0) return BOSS_ACT_DRAIN;
            if (r < 5) return BOSS_ACT_CHARGE;
            if (r < 8 && battle.enemy.sp > 0) return BOSS_ACT_MULTI;
            if (r < 11) return BOSS_ACT_HEAVY;
            if (r < 13 && g_boss.turns_since_heal >= 3) {
                return BOSS_ACT_REPAIR;
            }
            return BACT_ATTACK;
    }

    return BACT_ATTACK;
}

/*===========================================================================*/
/* Boss Attack Resolution                                                    */
/*===========================================================================*/

void bossResolveAction(u8 action)
{
    s16 base_dmg;
    s16 def_val;
    s16 damage;
    s16 heal;
    u8 hits;
    u8 h;
    s16 per_hit;
    s16 total;

    /* Calculate base damage: boss ATK vs player DEF */
    def_val = battle.player.def;
    if (battle.player.defending) {
        def_val = def_val << 1;  /* Defending doubles DEF */
    }
    base_dmg = bossCalcDamage(battle.enemy.atk, def_val);

    switch (action) {
        case BOSS_ACT_HEAVY:
            /* 2x damage, plus charge bonus if any */
            damage = base_dmg << 1;
            if (g_boss.charge_bonus > 0) {
                damage += g_boss.charge_bonus;
                g_boss.charge_bonus = 0;
            }
            battle.player.hp -= damage;
            if (battle.player.hp < 0) battle.player.hp = 0;
            battle.last_damage = damage;
            battleUIDrawMessage("HEAVY STRIKE!");
            soundPlaySFX(SFX_HIT);
            battleUIStartShake(1);
            break;

        case BOSS_ACT_MULTI:
            /* 2-3 hits at 75% damage each */
            hits = 2 + (u8)(g_frame_count & 1);
            per_hit = (base_dmg * 3) >> 2;
            if (per_hit < 1) per_hit = 1;
            total = 0;
            for (h = 0; h < hits; h++) {
                battle.player.hp -= per_hit;
                if (battle.player.hp < 0) battle.player.hp = 0;
                total += per_hit;
            }
            battle.last_damage = total;
            if (hits == 3) {
                battleUIDrawMessage("RAPID FIRE x3!");
            } else {
                battleUIDrawMessage("RAPID FIRE x2!");
            }
            soundPlaySFX(SFX_HIT);
            battleUIStartShake(1);
            /* Costs 1 SP */
            if (battle.enemy.sp > 0) battle.enemy.sp--;
            break;

        case BOSS_ACT_DRAIN:
            /* Damage player + heal self for half */
            damage = base_dmg;
            heal = damage >> 1;
            if (heal < 1) heal = 1;
            battle.player.hp -= damage;
            if (battle.player.hp < 0) battle.player.hp = 0;
            battle.enemy.hp += heal;
            if (battle.enemy.hp > battle.enemy.max_hp) {
                battle.enemy.hp = battle.enemy.max_hp;
            }
            battle.last_damage = damage;
            battleUIDrawMessage("ENERGY DRAIN!");
            soundPlaySFX(SFX_HIT);
            battleUIStartShake(1);
            /* Costs 1 SP */
            if (battle.enemy.sp > 0) battle.enemy.sp--;
            break;

        case BOSS_ACT_CHARGE:
            /* Skip damage this turn, store bonus for next attack */
            g_boss.is_charging = 1;
            g_boss.charge_bonus = base_dmg;
            battle.last_damage = 0;
            battleUIDrawMessage("CHARGING...");
            break;

        case BOSS_ACT_REPAIR:
            /* Heal 20% max HP */
            heal = battle.enemy.max_hp / 5;
            if (heal < 1) heal = 1;
            battle.enemy.hp += heal;
            if (battle.enemy.hp > battle.enemy.max_hp) {
                battle.enemy.hp = battle.enemy.max_hp;
            }
            battle.last_damage = 0;
            g_boss.turns_since_heal = 0;
            battleUIDrawMessage("SELF-REPAIR!");
            soundPlaySFX(SFX_HEAL);
            break;
    }

    /* Update UI after attack resolution */
    battleUIDrawDamage(battle.last_damage);
    battleUIDrawEnemyStats();
    battleUIDrawPlayerStats();
}
