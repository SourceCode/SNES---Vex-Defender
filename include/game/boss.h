/*==============================================================================
 * Boss Battle System - Phase 19
 *
 * One boss per zone, triggered at scroll distance 4800px (replacing the
 * zone-end auto-advance). Bosses are fought in the existing turn-based
 * battle engine with enhanced multi-phase AI and special attacks.
 *
 * Boss trigger values use the 0x80+ range to distinguish from regular
 * enemy types (0-3). battleStart() detects boss triggers and loads boss
 * stats from the BossTypeDef table.
 *
 * Boss AI has 3 phases determined by HP thresholds:
 *   NORMAL  (>50% HP):  Balanced attacks - basic offense, occasional specials
 *   ENRAGED (25-50% HP): Aggressive - multi-hits, heals, no defending
 *   DESPERATE (<25% HP): All-out - charge attacks, drain, heavy specials
 *
 * Phase transitions are checked at the start of each enemy turn by
 * bossUpdatePhase(). Transitions only go forward (NORMAL -> ENRAGED ->
 * DESPERATE), never back even if the boss heals above the threshold.
 * The "old_phase < new_phase" check ensures transition messages only
 * fire once per direction.
 *
 * Boss-specific attacks extend the standard BACT_* actions:
 *   HEAVY:  2x base damage (plus stored charge bonus if any)
 *   MULTI:  2-3 hits at 75% damage each, costs 1 SP
 *   DRAIN:  Normal damage to player + heal self for half, costs 1 SP
 *   CHARGE: Skip turn, store base_dmg as bonus for next HEAVY attack
 *   REPAIR: Heal 25% max HP, limited to once per 3 turns
 *
 * Defeating a boss triggers zone advancement (or final victory after Zone 3).
 * Boss victory exit differs from normal: no flight restoration, main.c
 * handles zone transition via gsZoneAdvance().
 *============================================================================*/

#ifndef BOSS_H
#define BOSS_H

#include "game.h"

/*=== Boss Type IDs (one per zone) ===*/
/* These index into the boss_types[] definition table in boss.c.
 * Each zone has exactly one boss, fought at the zone's scroll endpoint. */
#define BOSS_ZONE1          0   /* Zone 1 (Debris Field): Scout Commander - intro boss,
                                 * moderate stats, teaches basic battle mechanics */
#define BOSS_ZONE2          1   /* Zone 2 (Asteroid Belt): Heavy Cruiser - tanky boss,
                                 * high DEF and HP, emphasizes SPECIAL and item usage */
#define BOSS_ZONE3          2   /* Zone 3 (Flagship): Flagship Core - final boss,
                                 * highest stats across the board, aggressive desperate phase */
#define BOSS_TYPE_COUNT     3

/*=== Battle Trigger Values for Bosses ===*/
/* Boss trigger values start at 0x80 to avoid collision with ENEMY_TYPE_*
 * values (0-3). When g_battle_trigger is set to one of these, battleStart()
 * routes to boss setup instead of normal enemy setup. */
#define BOSS_TRIGGER_BASE   0x80
#define BOSS_TRIGGER_ZONE1  (BOSS_TRIGGER_BASE + BOSS_ZONE1)   /* 0x80 */
#define BOSS_TRIGGER_ZONE2  (BOSS_TRIGGER_BASE + BOSS_ZONE2)   /* 0x81 */
#define BOSS_TRIGGER_ZONE3  (BOSS_TRIGGER_BASE + BOSS_ZONE3)   /* 0x82 */

/*=== Macros to detect and extract boss type from trigger ===*/
/* IS_BOSS_TRIGGER: returns true if trigger value is in the boss range.
 * BOSS_TYPE_FROM_TRIGGER: extracts the BOSS_ZONE* index from the trigger. */
#define IS_BOSS_TRIGGER(t)       ((t) >= BOSS_TRIGGER_BASE && (t) < BOSS_TRIGGER_BASE + BOSS_TYPE_COUNT)
#define BOSS_TYPE_FROM_TRIGGER(t) ((t) - BOSS_TRIGGER_BASE)

/*=== Boss AI Phases (determined by HP percentage) ===*/
/* Phase escalation is one-directional: once a boss enters ENRAGED, it never
 * returns to NORMAL even if healed above 50%. This creates escalating tension.
 * Phase transitions trigger a UI message and SFX to alert the player. */
#define BOSS_AI_NORMAL      0   /* >50% HP: balanced mix of attack, special, defend */
#define BOSS_AI_ENRAGED     1   /* 25-50% HP: aggressive, multi-hits, begins healing */
#define BOSS_AI_DESPERATE   2   /* <25% HP: all-out offense, drain, charge combos,
                                 * sprite flickers every 4 frames as visual warning */

/*=== Boss-Specific Attack Actions (>= 10 to avoid BACT_* conflict) ===*/
/* These action IDs are returned by bossChooseAction() and resolved by
 * bossResolveAction(). They use IDs >= 10 to cleanly separate from the
 * standard BACT_* actions (0-3) used by both player and normal enemies.
 * resolveAction() in battle.c detects action >= 10 && is_boss and delegates
 * to bossResolveAction(). */
#define BOSS_ACT_HEAVY      10  /* Heavy strike: 2x base damage + stored charge bonus.
                                 * This is also the auto-release after a CHARGE turn. */
#define BOSS_ACT_MULTI      11  /* Rapid fire: 2-3 hits at 75% damage each, costs 1 SP.
                                 * Hit count is pseudo-random (2 or 3 based on frame parity). */
#define BOSS_ACT_DRAIN      12  /* Energy drain: deal normal damage + heal self for half
                                 * of damage dealt. Costs 1 SP. */
#define BOSS_ACT_CHARGE     13  /* Charge up: no damage this turn, stores base_dmg as bonus.
                                 * Next turn automatically uses HEAVY with the stored bonus. */
#define BOSS_ACT_REPAIR     14  /* Self-repair: heal 25% max HP (shift approximation).
                                 * Limited to once per 3 turns via turns_since_heal counter. */

/*=== Boss Type Definition (ROM data) ===*/
/* Static stat block stored in ROM. One entry per boss. These are the base
 * stats loaded into BattleCombatant.enemy at battle start. Boss stats are
 * significantly higher than regular enemies to create multi-turn fights. */
typedef struct {
    s16 hp;             /* Starting and max HP (same value; boss HP doesn't persist) */
    s16 atk;            /* Attack power (used in ATK^2/(ATK+DEF) damage formula) */
    s16 def;            /* Defense (reduces incoming player damage) */
    s16 spd;            /* Speed: compared to player SPD for turn order.
                         * Zone 3 boss has SPD 12 to often go first. */
    u8  sp;             /* Starting SP (ability charges for MULTI, DRAIN, SPECIAL) */
    u8  max_sp;         /* Maximum SP cap */
    u16 xp_reward;      /* XP awarded to player on defeat. Bosses give much more
                         * XP than regular enemies (100/200/400 vs 15-75). */
    u8  drop_item;      /* Guaranteed item drop (ITEM_* constant from inventory.h).
                         * Unlike regular enemies which use RNG drops, bosses always
                         * drop a specific item. */
    char name[12];      /* Display name shown in battle UI (max 11 chars + null).
                         * e.g., "COMMANDER", "CRUISER", "FLAGSHIP" */
    u8  weakness;       /* WEAPON_* type that deals bonus damage (#161) */
} BossTypeDef;

/*=== Boss Runtime State ===*/
/* Mutable state tracked during a boss battle. Initialized by bossSetup()
 * and mutated by bossChooseAction() / bossResolveAction() during combat.
 * Cleared by bossInit() at game startup. */
typedef struct {
    u8  active;         /* 1 if currently in a boss battle. Cleared on boss defeat.
                         * Checked by main.c to determine post-battle behavior. */
    u8  type;           /* BOSS_ZONE* type ID (0-2) of the current boss */
    u8  ai_phase;       /* Current BOSS_AI_* phase (NORMAL/ENRAGED/DESPERATE).
                         * Updated at the start of each enemy turn. */
    u8  is_charging;    /* 1 if CHARGE was used last turn. When set, the next call
                         * to bossChooseAction() returns HEAVY automatically and
                         * clears this flag. */
    s16 charge_bonus;   /* Extra damage stored from CHARGE action. Added to the
                         * next HEAVY attack's damage, then cleared. */
    u8  turns_since_heal; /* Frame counter since last REPAIR action. REPAIR is only
                           * available when this reaches >= 3, preventing heal spam. */
    u8  drop_item;      /* Guaranteed item drop (copied from BossTypeDef at setup).
                         * Used by battle.c on victory to add to inventory. */
    char name[12];      /* Boss name for UI display (copied from BossTypeDef). */
    u8  xp_phases_awarded; /* 0/1/2 = phase XP milestones awarded (#198) */
} BossState;

/* Global boss state singleton. Accessed by battle.c and battle_ui.c
 * for boss-specific behavior (AI phase checks, name display, etc.). */
extern BossState g_boss;

/* Initialize boss state to default values. Call once at game start.
 * Sets active=0, clears all charging/heal state. */
void bossInit(void);

/* Set up boss for battle: copies stats from the BossTypeDef table into
 * g_boss runtime state (name, drop item, AI phase reset, charge state).
 * Returns pointer to the BossTypeDef so battleStart() can load stats
 * into the enemy combatant. Clamps bossType to valid range. */
const BossTypeDef* bossSetup(u8 bossType);

/* Update boss AI phase based on current HP vs max HP thresholds.
 * Called at the start of each enemy turn during boss battles.
 * Quarter HP = max_hp >> 2 (bitshift division, no expensive divide).
 * Shows "ENEMY POWERS UP!" or "GOING ALL OUT!" message and plays
 * explosion SFX on phase transitions. */
void bossUpdatePhase(void);

/* Boss AI: choose action for the current enemy turn.
 * Uses pseudo-random selection based on frame counter and turn number
 * (shift-add replaces multiply for 65816 performance).
 * Action probabilities change per AI phase:
 *   NORMAL: mostly basic attacks with occasional specials
 *   ENRAGED: frequent multi-hits and heals
 *   DESPERATE: drain, charge combos, heavy attacks
 * Returns BACT_* or BOSS_ACT_* action ID. */
u8 bossChooseAction(void);

/* Resolve a boss-specific attack (BOSS_ACT_* actions >= 10).
 * Called from resolveAction() in battle.c when the action ID is >= 10
 * and battle.is_boss is set. Handles damage calculation, HP modification,
 * SP costs, UI messages, shake effects, and stat display updates. */
void bossResolveAction(u8 action);

#endif /* BOSS_H */
