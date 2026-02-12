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
 *   HEAVY:  2x base damage (plus stored charge bonus)
 *   MULTI:  2-3 hits at 75% base damage each (costs 1 SP)
 *   DRAIN:  Base damage to player + heal self for half (costs 1 SP)
 *   CHARGE: No damage this turn, next attack gets bonus damage
 *   REPAIR: Heal ~25% max HP (limited to once per 3 turns)
 *
 * Boss design philosophy:
 *   Zone 1 COMMANDER (HP 120, ATK 18): Intro boss. Teaches the player
 *     that bosses hit harder and require item usage. NORMAL phase is
 *     straightforward; ENRAGED introduces multi-hits.
 *   Zone 2 CRUISER (HP 200, ATK 22, DEF 18): Tanky mid-boss. High DEF
 *     means the player needs SPECIAL attacks or ATK boosts. ENRAGED phase
 *     introduces self-repair, testing the player's damage output.
 *   Zone 3 FLAGSHIP (HP 350, ATK 30, SPD 12): Final boss. Extremely
 *     aggressive with high SPD (often goes first). DESPERATE phase uses
 *     CHARGE -> HEAVY combos for devastating damage.
 *
 * Bank 0 is full; this file auto-overflows via WLA-DX linker.
 *============================================================================*/

#include "game/boss.h"
#include "game/battle.h"
#include "game/battle_ui.h"
#include "game/inventory.h"
#include "game/rpg_stats.h"
#include "engine/bullets.h"
#include "engine/sound.h"
#include "engine/vblank.h"

/* Forward declarations for boss message buffer and builder (defined later in file) */
static char boss_msg_buf[24];
static void buildBossMsg(const char *action);

/* Global boss runtime state singleton */
BossState g_boss;

/*=== Boss Type Definitions (ROM data) ===*/
/* Static stat blocks for all 3 bosses. Stored in ROM (const) to save WRAM.
 * Stats are significantly higher than normal enemies to create multi-turn
 * encounters that test all of the player's battle options.
 *
 * Column alignment: HP   ATK  DEF  SPD  SP MSP  XP   DROP             NAME
 *
 * Drop items are guaranteed (unlike normal enemies which use RNG):
 *   COMMANDER drops HP Potion L  -> teaches item value early
 *   CRUISER   drops SP Charge    -> enables more SPECIAL usage
 *   FLAGSHIP  drops Full Restore -> ultimate reward for final boss */
static const BossTypeDef boss_types[BOSS_TYPE_COUNT] = {
    { 120,  18,  10,   8,  3,  3,  100, ITEM_HP_POTION_L,  "COMMANDER", WEAPON_LASER  },
    { 200,  22,  18,   6,  4,  4,  200, ITEM_SP_CHARGE,    "CRUISER",   WEAPON_SPREAD },
    { 350,  30,  22,  12,  6,  6,  400, ITEM_FULL_RESTORE, "FLAGSHIP",  WEAPON_SINGLE },
};

/*===========================================================================*/
/* Initialization                                                            */
/*===========================================================================*/

/*
 * bossInit
 * --------
 * Reset all boss runtime state to defaults. Called once at game startup
 * by battleInit(). Clears the active flag, charging state, heal cooldown,
 * and name buffer.
 */
void bossInit(void)
{
    g_boss.active = 0;
    g_boss.type = 0;
    g_boss.ai_phase = BOSS_AI_NORMAL;
    g_boss.is_charging = 0;
    g_boss.charge_bonus = 0;
    g_boss.turns_since_heal = 0;
    g_boss.drop_item = ITEM_NONE;
    g_boss.name[0] = 0;  /* Empty string */
    g_boss.xp_phases_awarded = 0;  /* #198 */
}

/*
 * bossSetup
 * ---------
 * Initialize boss runtime state for a new boss battle.
 * Copies name and drop item from the ROM definition into mutable g_boss
 * state. Resets all per-battle runtime fields (phase, charging, heal timer).
 *
 * Parameters:
 *   bossType - BOSS_ZONE* index (0-2). Clamped to 0 if out of range.
 *
 * Returns:
 *   Pointer to the BossTypeDef in ROM. The caller (battleStart) reads
 *   HP/ATK/DEF/SPD/SP/XP from this to initialize the enemy combatant.
 *
 * Note: boss stats are NOT stored in g_boss -- they go into
 * battle.enemy (BattleCombatant) for use by the generic battle system.
 * g_boss only holds boss-specific AI state (phase, charging, etc.).
 */
const BossTypeDef* bossSetup(u8 bossType)
{
    const BossTypeDef *def;
    u8 i;

    /* Clamp invalid boss types to Zone 1 boss */
    if (bossType >= BOSS_TYPE_COUNT) bossType = 0;
    def = &boss_types[bossType];

    /* Initialize runtime state for this boss battle */
    g_boss.active = 1;
    g_boss.type = bossType;
    g_boss.ai_phase = BOSS_AI_NORMAL;  /* Always start in normal phase */
    g_boss.is_charging = 0;            /* No stored charge */
    g_boss.charge_bonus = 0;
    g_boss.turns_since_heal = 0;       /* Allow immediate heal if needed */
    g_boss.xp_phases_awarded = 0;     /* #198: Reset phase XP tracking */
    g_boss.drop_item = def->drop_item; /* Guaranteed drop on defeat */

    /* Copy boss name from ROM into mutable state (max 11 chars + null).
     * Manual copy loop avoids pulling in strcpy from the C library,
     * which would add unnecessary code size to the ROM. */
    for (i = 0; i < 11 && def->name[i]; i++) {
        g_boss.name[i] = def->name[i];
    }
    g_boss.name[i] = 0; /* Null terminate */

    return def;
}

/* Damage calculation now uses shared battleCalcDamageRaw() from battle.c.
 * This avoids duplicating the ATK^2/(ATK+DEF) formula in two places. */

/*===========================================================================*/
/* AI Phase Tracking                                                         */
/*===========================================================================*/

/*
 * bossUpdatePhase
 * ---------------
 * Check boss HP against phase thresholds and update AI phase.
 * Called at the start of each enemy turn during boss battles.
 *
 * Phase thresholds are based on max HP quarters (using >>2 shift):
 *   NORMAL:    HP > 50% (HP > max_hp/2, i.e., HP > quarter*2)
 *   ENRAGED:   25% < HP <= 50% (HP > quarter but <= quarter*2)
 *   DESPERATE: HP <= 25% (HP <= quarter)
 *
 * Phase transitions only go forward: NORMAL -> ENRAGED -> DESPERATE.
 * The "old_phase < new_phase" check ensures the transition message and
 * SFX only play once per phase change (not every frame).
 *
 * UI feedback on transition:
 *   ENRAGED:   "ENEMY POWERS UP!" + explosion SFX + brightness flash
 *   DESPERATE: "GOING ALL OUT!" + explosion SFX + brightness flash
 *
 * These visual cues alert the player that the boss is becoming more
 * dangerous and they should consider using items or defending.
 */
void bossUpdatePhase(void)
{
    u8 old_phase;
    s16 hp;
    s16 quarter;

    old_phase = g_boss.ai_phase;
    hp = battle.enemy.hp;
    /* Calculate 25% of max HP using shift (>>2 = divide by 4).
     * Avoids software division on the 65816. */
    quarter = battle.enemy.max_hp >> 2;

    /* Determine new phase based on HP thresholds */
    if (hp <= quarter) {
        g_boss.ai_phase = BOSS_AI_DESPERATE;   /* HP <= 25% */
    } else if (hp <= (quarter << 1)) {
        g_boss.ai_phase = BOSS_AI_ENRAGED;     /* HP <= 50% (quarter*2) */
    } else {
        g_boss.ai_phase = BOSS_AI_NORMAL;      /* HP > 50% */
    }

    /* Show transition message and play SFX only on upward phase changes.
     * The > comparison ensures this fires once per transition direction. */
    if (g_boss.ai_phase > old_phase) {
        if (g_boss.ai_phase == BOSS_AI_ENRAGED) {
            /* #184: Enrage timer visible - show estimated turns to desperate */
            {
                s16 hp_to_desperate;
                u8 est_turns;
                hp_to_desperate = battle.enemy.hp - quarter;
                if (hp_to_desperate < 10) hp_to_desperate = 10;
                /* Divide by 10 using subtraction loop (65816 safe) */
                est_turns = 0;
                while (hp_to_desperate >= 10) {
                    hp_to_desperate -= 10;
                    est_turns++;
                }
                if (est_turns < 1) est_turns = 1;
                if (est_turns > 9) est_turns = 9;
                /* Build message: "POWERS UP! ~Nt LEFT" */
                boss_msg_buf[0] = 'P'; boss_msg_buf[1] = 'O'; boss_msg_buf[2] = 'W';
                boss_msg_buf[3] = 'E'; boss_msg_buf[4] = 'R'; boss_msg_buf[5] = 'S';
                boss_msg_buf[6] = ' '; boss_msg_buf[7] = 'U'; boss_msg_buf[8] = 'P';
                boss_msg_buf[9] = '!'; boss_msg_buf[10] = ' ';
                boss_msg_buf[11] = '~'; boss_msg_buf[12] = '0' + est_turns;
                boss_msg_buf[13] = 'T'; boss_msg_buf[14] = 0;
                battleUIDrawMessage(boss_msg_buf);
            }

            /* #198: Award 25% of XP at ENRAGED transition */
            if (g_boss.xp_phases_awarded < 1) {
                u16 partial_xp;
                partial_xp = battle.xp_gained >> 2;
                rpgAddXP(partial_xp);
                g_boss.xp_phases_awarded = 1;
            }
        } else {
            battleUIDrawMessage("GOING ALL OUT!");

            /* #192: Boss revenge damage on DESPERATE transition */
            {
                s16 revenge;
                revenge = 15;
                if (battle.player.defending) {
                    revenge >>= 1;  /* Half damage if defending */
                }
                battle.player.hp -= revenge;
                if (battle.player.hp < 1) battle.player.hp = 1;
                battle.last_damage = revenge;
                battleUIDrawMessage("REVENGE STRIKE!");
                battleUIUpdatePlayerHP();
                soundPlaySFX(SFX_EXPLOSION);
            }

            /* #198: Award 25% of XP at DESPERATE transition */
            if (g_boss.xp_phases_awarded < 2) {
                u16 partial_xp;
                partial_xp = battle.xp_gained >> 2;
                rpgAddXP(partial_xp);
                g_boss.xp_phases_awarded = 2;
            }
        }
        soundPlaySFX(SFX_EXPLOSION);
        setBrightness(15); /* Flash to full brightness for visual punch */

        /* #169: Boss phase shield - auto-defend player on phase transition.
         * Prevents unfair burst damage at phase change moments. */
        battle.player.defending = 1;
    }

    /* #154: Boss enrage timer - +2 ATK every 4 turns, stacking.
     * Forces aggressive play; turtling means the boss hits harder.
     * Cap at original ATK + 20 to prevent overflow. */
    if ((battle.turn_number & 3) == 0 && battle.turn_number > 0) {
        s16 max_atk;
        max_atk = boss_types[g_boss.type].atk + 20;
        if (battle.enemy.atk < max_atk) {
            battle.enemy.atk += 2;
            if (battle.enemy.atk > max_atk) {
                battle.enemy.atk = max_atk;
            }
            buildBossMsg("POWER SURGES!");
            battleUIDrawMessage(boss_msg_buf);
        }
    }
}

/*===========================================================================*/
/* Boss AI Action Selection                                                  */
/*===========================================================================*/

/*
 * bossChooseAction
 * ----------------
 * Select the boss's action for this turn based on current AI phase.
 * Uses weighted pseudo-random selection with different probability tables
 * per phase, creating increasingly aggressive behavior as HP drops.
 *
 * Special mechanics:
 *   - CHARGE auto-release: if is_charging is set from last turn, this
 *     function returns BOSS_ACT_HEAVY immediately and clears the flag.
 *     The stored charge_bonus is added to the HEAVY attack's damage.
 *   - Heal cooldown: REPAIR is only available when turns_since_heal >= 3,
 *     preventing the boss from spamming self-heals every turn.
 *   - SP gating: SPECIAL, MULTI, and DRAIN require SP > 0.
 *
 * Randomness source:
 *   Uses frame counter and turn number combined via shift-add multiplication.
 *   The 65816 has no hardware multiply, so x*7 and x*13 are computed as:
 *     x * 7  = (x << 3) - x       = x*8 - x
 *     x * 13 = (x << 4) - (x << 2) + x = x*16 - x*4 + x
 *   The final value is masked to 0-15 (& 0x0F) for a 16-bucket distribution.
 *   This isn't truly random, but provides sufficient variety for gameplay.
 *
 * Returns:
 *   BACT_* or BOSS_ACT_* action ID.
 *
 * Action probability breakdown per phase:
 *   NORMAL (balanced):
 *     0-5:  ATTACK (37.5%)
 *     6-8:  SPECIAL if SP (18.75%)
 *     9-10: HEAVY (12.5%)
 *     11-12: DEFEND (12.5%)
 *     13-15: ATTACK (18.75%)
 *
 *   ENRAGED (aggressive):
 *     0-3:  ATTACK (25%)
 *     4-6:  MULTI if SP (18.75%)
 *     7-9:  HEAVY (18.75%)
 *     10-11: SPECIAL if SP (12.5%)
 *     12-13: REPAIR if cooldown (12.5%)
 *     14-15: ATTACK (12.5%)
 *
 *   DESPERATE (all-out):
 *     0-2:  DRAIN if SP (18.75%)
 *     3-4:  CHARGE (12.5%)
 *     5-7:  MULTI if SP (18.75%)
 *     8-10: HEAVY (18.75%)
 *     11-12: REPAIR if cooldown (12.5%)
 *     13-15: ATTACK (18.75%)
 */
u8 bossChooseAction(void)
{
    u8 r;
    u16 fc;
    u16 tn;
    u16 fc7;
    u16 tn13;

    /* If charging from last turn, automatically release with a HEAVY attack.
     * The stored charge_bonus will be added in bossResolveAction(). */
    if (g_boss.is_charging) {
        g_boss.is_charging = 0;
        return BOSS_ACT_HEAVY;
    }

    /* Track turns since last heal for REPAIR cooldown */
    g_boss.turns_since_heal++;

    /* Pseudo-random number generation using frame counter + turn number.
     * Shift-add replaces expensive software multiply on the 65816:
     *   x*7  = (x<<3) - x            (8x - x = 7x)
     *   x*13 = (x<<4) - (x<<2) + x   (16x - 4x + x = 13x)
     * The prime-like multipliers (7, 13) provide better distribution
     * than simple addition. Final mask to 0x0F gives range 0-15. */
    fc = (u16)g_frame_count;
    tn = (u16)battle.turn_number;
    fc7 = (fc << 3) - fc;              /* frame_count * 7 */
    tn13 = (tn << 4) - (tn << 2) + tn; /* turn_number * 13 */
    r = (u8)(fc7 + tn13) & 0x0F;       /* Combine and mask to 0-15 */

    switch (g_boss.ai_phase) {
        case BOSS_AI_NORMAL:
            /* Balanced: basic attacks are most common, with occasional specials
             * and heavies. Defending is possible to teach the player that bosses
             * can also guard. */
            if (r < 6) return BACT_ATTACK;                              /* 37.5% */
            if (r < 9 && battle.enemy.sp > 0) return BACT_SPECIAL;     /* 18.75% */
            if (r < 11) return BOSS_ACT_HEAVY;                         /* 12.5% */
            if (r < 13) return BACT_DEFEND;                             /* 12.5% */
            return BACT_ATTACK;                                         /* 18.75% fallback */

        case BOSS_AI_ENRAGED:
            /* Aggressive: multi-hits replace single attacks, specials are more
             * frequent, and the boss starts self-healing. No more defending --
             * the boss is too aggressive to guard. */
            if (r < 4) return BACT_ATTACK;                              /* 25% */
            if (r < 7 && battle.enemy.sp > 0) return BOSS_ACT_MULTI;   /* 18.75% */
            if (r < 10) return BOSS_ACT_HEAVY;                         /* 18.75% */
            if (r < 12 && battle.enemy.sp > 0) return BACT_SPECIAL;    /* 12.5% */
            if (r < 14 && g_boss.turns_since_heal >= 3) {
                return BOSS_ACT_REPAIR;                                 /* 12.5% (if cooldown met) */
            }
            return BACT_ATTACK;                                         /* 12.5% fallback */

        case BOSS_AI_DESPERATE:
            /* All-out: introduces DRAIN (damage + self-heal) and CHARGE
             * (setup for devastating HEAVY next turn). Multi-hits remain
             * frequent. REPAIR is still available as a desperation heal.
             * This phase is designed to be a race: can the player finish
             * the boss before it heals back or charges a killing blow? */
            if (r < 3 && battle.enemy.sp > 0) return BOSS_ACT_DRAIN;   /* 18.75% */
            if (r < 5) return BOSS_ACT_CHARGE;                         /* 12.5% */
            if (r < 8 && battle.enemy.sp > 0) return BOSS_ACT_MULTI;   /* 18.75% */
            if (r < 11) return BOSS_ACT_HEAVY;                         /* 18.75% */
            if (r < 13 && g_boss.turns_since_heal >= 3) {
                return BOSS_ACT_REPAIR;                                 /* 12.5% (if cooldown met) */
            }
            return BACT_ATTACK;                                         /* 18.75% fallback */
    }

    return BACT_ATTACK; /* Default fallback (unreachable with valid phase values) */
}

/*===========================================================================*/
/* Boss Attack Resolution                                                    */
/*===========================================================================*/

/*
 * bossResolveAction
 * -----------------
 * Resolve a boss-specific attack (BOSS_ACT_* actions with IDs >= 10).
 * Called from resolveAction() in battle.c when a boss uses a unique attack.
 *
 * Each boss attack:
 *   1. Calculates damage using battleCalcDamageRaw() (shared formula)
 *   2. Applies damage/healing to combatants
 *   3. Handles SP costs
 *   4. Draws UI message and damage numbers
 *   5. Triggers shake animation and sound effects
 *   6. Updates both HP bar displays
 *
 * Parameters:
 *   action - BOSS_ACT_* action ID (HEAVY, MULTI, DRAIN, CHARGE, REPAIR)
 *
 * Damage formula base: uses the same ATK^2/(ATK+DEF) formula as normal
 * attacks, with modifications per attack type:
 *   HEAVY:  base * 2 + charge_bonus
 *   MULTI:  base * 0.75 per hit, 2-3 hits
 *   DRAIN:  base (to player) + base/2 (heal to self)
 *   CHARGE: 0 (no damage this turn)
 *   REPAIR: 0 (heal only)
 *
 * The defender's DEF is doubled if they chose DEFEND this turn,
 * same as with normal attacks (handled by def_val calculation here).
 */
/* #129: Helper to build boss-specific attack messages.
 * Concatenates the boss name with an action string into a static buffer.
 * Example: "COMMANDER STRIKES!" for boss name "COMMANDER" + action "STRIKES!"
 * Max output: 11 (name) + 1 (space) + 11 (action) + 1 (null) = 24 chars.
 * (boss_msg_buf declared at top of file as forward declaration) */
static void buildBossMsg(const char *action)
{
    u8 i, j;
    /* Copy boss name */
    for (i = 0; i < 11 && g_boss.name[i]; i++) {
        boss_msg_buf[i] = g_boss.name[i];
    }
    boss_msg_buf[i++] = ' ';
    /* Copy action string */
    for (j = 0; j < 11 && action[j]; j++) {
        boss_msg_buf[i + j] = action[j];
    }
    boss_msg_buf[i + j] = 0;
}

void bossResolveAction(u8 action)
{
    BattleCombatant *plr;
    BattleCombatant *ene;
    s16 base_dmg;
    s16 def_val;
    s16 damage;
    s16 heal;
    u8 hits;
    u8 h;
    s16 per_hit;
    s16 total;

    plr = &battle.player;
    ene = &battle.enemy;

    /* Calculate base damage: boss ATK vs player DEF (with defend doubling) */
    def_val = plr->def;
    if (plr->defending) {
        def_val = def_val << 1;  /* Defending doubles DEF (shift = fast *2) */
    }
    base_dmg = battleCalcDamageRaw(ene->atk, def_val);

    switch (action) {
        case BOSS_ACT_HEAVY:
            /* HEAVY STRIKE: 2x base damage, plus any stored charge bonus.
             * <<1 = multiply by 2 (shift instead of multiply for 65816).
             * If a CHARGE was used last turn, charge_bonus adds extra damage
             * and is then cleared. */
            damage = base_dmg << 1;
            if (g_boss.charge_bonus > 0) {
                damage += g_boss.charge_bonus;
                g_boss.charge_bonus = 0; /* Consume the stored charge */
            }
            plr->hp -= damage;
            if (plr->hp < 0) plr->hp = 0;
            battle.last_damage = damage;
            /* #129: Boss-specific name in attack message */
            buildBossMsg("STRIKES!");
            battleUIDrawMessage(boss_msg_buf);
            soundPlaySFX(SFX_HIT);
            battleUIStartShake(1); /* Shake player sprite */
            break;

        case BOSS_ACT_MULTI:
            /* RAPID FIRE: 2-3 hits at 75% damage each.
             * Hit count: 2 + (frame_count & 1) gives either 2 or 3 hits
             * based on frame parity (simple pseudo-random).
             * Per-hit damage: base * 3/4 = (base * 3) >> 2. Using
             * multiply-then-shift is faster than division on the 65816.
             * Minimum 1 damage per hit to prevent zero-damage multi-hits.
             * Total damage is accumulated across all hits for UI display.
             * Costs 1 SP to use (checked by bossChooseAction before selecting). */
            hits = 2 + (u8)(g_frame_count & 1);    /* 2 or 3 hits */
            per_hit = (base_dmg * 3) >> 2;          /* 75% of base damage per hit */
            if (per_hit < 1) per_hit = 1;            /* Minimum 1 per hit */
            total = 0;
            for (h = 0; h < hits; h++) {
                plr->hp -= per_hit;
                if (plr->hp < 0) plr->hp = 0;
                total += per_hit;
            }
            battle.last_damage = total;              /* Display total damage dealt */
            /* #129: Boss-specific name in multi-hit message */
            if (hits == 3) {
                buildBossMsg("FIRE x3!");
                battleUIDrawMessage(boss_msg_buf);
            } else {
                buildBossMsg("FIRE x2!");
                battleUIDrawMessage(boss_msg_buf);
            }
            soundPlaySFX(SFX_HIT);
            battleUIStartShake(1); /* Shake player sprite */
            /* SP cost: 1 SP consumed */
            if (ene->sp > 0) ene->sp--;
            break;

        case BOSS_ACT_DRAIN:
            /* ENERGY DRAIN: deal base damage to player AND heal self for half.
             * This is the boss's sustain ability -- it deals damage while
             * recovering HP, making the fight harder to end.
             * Heal = damage/2 (>>1 = shift divide by 2). Minimum heal of 1.
             * Costs 1 SP. */
            damage = base_dmg;
            heal = damage >> 1;      /* Heal self for 50% of damage dealt */
            if (heal < 1) heal = 1;  /* Minimum 1 HP healed */
            plr->hp -= damage;
            if (plr->hp < 0) plr->hp = 0;
            ene->hp += heal;
            if (ene->hp > ene->max_hp) {
                ene->hp = ene->max_hp; /* Cap heal at max HP */
            }
            battle.last_damage = damage; /* Display damage to player */
            buildBossMsg("DRAINS!");
            battleUIDrawMessage(boss_msg_buf);
            soundPlaySFX(SFX_HIT);
            battleUIStartShake(1); /* Shake player sprite */
            /* SP cost: 1 SP consumed */
            if (ene->sp > 0) ene->sp--;
            break;

        case BOSS_ACT_CHARGE:
            /* CHARGING: skip damage this turn, store base_dmg as bonus.
             * Sets is_charging flag so that bossChooseAction() will
             * automatically return BOSS_ACT_HEAVY next turn.
             * The stored charge_bonus is added to the HEAVY attack's
             * damage, creating a devastating 2-turn combo:
             *   Turn N:   CHARGE (store base_dmg as bonus)
             *   Turn N+1: HEAVY  (2x base_dmg + charge_bonus = ~3x damage)
             * No SP cost. No shake effect (no damage dealt). */
            g_boss.is_charging = 1;
            g_boss.charge_bonus = base_dmg;  /* Store base damage as bonus */
            battle.last_damage = 0;           /* No damage this turn */
            buildBossMsg("CHARGES!");
            battleUIDrawMessage(boss_msg_buf);
            soundPlaySFX(SFX_ENEMY_SHOOT);  /* #208: Warning SFX for boss charge */
            break;

        case BOSS_ACT_REPAIR:
            /* SELF-REPAIR: heal ~25% of max HP.
             * Uses >>2 (shift divide by 4) instead of division.
             * Minimum heal of 1 HP.
             * Resets turns_since_heal to 0, which starts the 3-turn cooldown
             * before REPAIR can be selected again by bossChooseAction().
             * No SP cost. Plays heal SFX instead of hit SFX. */
            heal = (ene->max_hp >> 3) + (ene->max_hp >> 4);  /* ~18.75% of max HP (#225) */
            if (heal < 1) heal = 1;
            ene->hp += heal;
            if (ene->hp > ene->max_hp) {
                ene->hp = ene->max_hp;       /* Cap at max HP */
            }
            battle.last_damage = 0;           /* No damage display */
            g_boss.turns_since_heal = 0;      /* Reset heal cooldown */
            buildBossMsg("REPAIRS!");
            battleUIDrawMessage(boss_msg_buf);
            soundPlaySFX(SFX_HEAL);
            break;
    }

    /* Update all UI elements after attack resolution:
     * damage number display and both HP bars */
    battleUIDrawDamage(battle.last_damage);
    battleUIUpdateEnemyHP();
    battleUIUpdatePlayerHP();
}
