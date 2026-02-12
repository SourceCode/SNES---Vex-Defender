/*==============================================================================
 * Turn-Based Battle Engine
 *
 * State machine for JRPG-style combat. Triggered by contact with non-scout
 * enemies or via debug key (SELECT). BG3 text UI with cursor menu.
 *
 * Battle states flow:
 *   INIT -> PLAYER_TURN / ENEMY_TURN (by SPD) ->
 *   PLAYER_ACT -> RESOLVE -> ENEMY_TURN ->
 *   ENEMY_ACT -> RESOLVE -> PLAYER_TURN -> (loop) ->
 *   VICTORY / DEFEAT -> EXIT -> back to flight
 *
 * The battle engine uses a classic JRPG turn-based model:
 *   - Turn order is determined once at battle start by comparing SPD stats
 *   - Each turn, combatants alternate: the faster one goes first each round
 *   - Actions are: Attack (base damage), Defend (2x DEF for one hit),
 *     Special (1.5x damage, costs 1 SP), Item (use consumable from inventory)
 *   - Victory awards XP and may trigger level-up; defeat applies a penalty
 *
 * Boss battles (Phase 19) use the same state machine but substitute the
 * boss AI (bossChooseAction) for the normal enemy AI, and boss attacks
 * (BOSS_ACT_* >= 10) are resolved by bossResolveAction() in boss.c.
 *
 * The battle screen reconfigures the SNES PPU:
 *   - BG1 is repurposed from game background to text display (4bpp font)
 *   - BG2 remains as star parallax backdrop
 *   - OBJ layer shows battle sprites at OAM slots 64-65
 *   - On exit, the zone background is reloaded to restore BG1 game tiles
 *
 * Phase 12 expands UI, Phase 13 connects real RPG stats.
 * Phase 14 adds item system integration.
 *============================================================================*/

#ifndef BATTLE_H
#define BATTLE_H

#include "game.h"

/*=== Battle States ===*/
/* These define the state machine progression. Each state handles one
 * phase of the battle flow, with anim_timer providing frame-based delays
 * between state transitions for visual pacing. */
#define BSTATE_NONE         0   /* No battle active; main loop skips battleUpdate() */
#define BSTATE_INIT         1   /* Show "ENCOUNTER!" message, wait 60 frames */
#define BSTATE_PLAYER_TURN  2   /* Player menu is visible, awaiting input */
#define BSTATE_PLAYER_ACT   3   /* Brief delay before resolving player's chosen action */
#define BSTATE_ENEMY_TURN   4   /* AI selects action instantly, sets up delay */
#define BSTATE_ENEMY_ACT    5   /* Brief delay before resolving enemy's chosen action */
#define BSTATE_RESOLVE      6   /* Show action result for 30 frames, then check HP */
#define BSTATE_VICTORY      7   /* Enemy HP <= 0: show victory, XP, item drops */
#define BSTATE_DEFEAT       8   /* Player HP <= 0: show defeat message */
#define BSTATE_EXIT         9   /* Transition out of battle (fade, restore BG) */
#define BSTATE_LEVELUP      10  /* Level-up notification with brightness flash */
#define BSTATE_ITEM_SELECT  11  /* Item sub-menu is visible, awaiting selection */

/*=== Battle Actions ===*/
/* Indices into the action menu. Also used as action IDs passed to
 * resolveAction(). Values 0-3 are standard actions; boss attacks use
 * IDs >= 10 (BOSS_ACT_* in boss.h) to avoid conflicts. */
#define BACT_ATTACK   0   /* Standard attack: ATK^2/(ATK+DEF) damage formula */
#define BACT_DEFEND   1   /* Guard: doubles DEF stat for next incoming attack */
#define BACT_SPECIAL  2   /* Special attack: 1.5x damage, costs 1 SP. Falls through
                           * to ATTACK if SP is 0 (no wasted turn). */
#define BACT_ITEM     3   /* Open item sub-menu to use a consumable */
#define BACT_COUNT    4   /* Number of standard actions (used for menu bounds) */

/*=== Battle Trigger Sentinel ===*/
/* g_battle_trigger is set to an ENEMY_TYPE_* value (0-3) to start a normal
 * battle, or a BOSS_TRIGGER_* value (0x80+) for boss battles. This sentinel
 * value means "no battle pending". */
#define BATTLE_TRIGGER_NONE  0xFF

/*=== Battle Combatant ===*/
/* Represents one fighter in battle (player or enemy). Both combatants use
 * the same struct so resolveAction() can operate generically on attacker
 * and target without branching on combatant type. */
typedef struct {
    s16 hp;             /* Current hit points; 0 = defeated */
    s16 max_hp;         /* Maximum HP cap (for heal clamping and HP bar display) */
    s16 atk;            /* Attack power: numerator in damage formula ATK^2/(ATK+DEF).
                         * Higher ATK means more damage dealt. */
    s16 def;            /* Defense: denominator contribution in damage formula.
                         * Higher DEF reduces incoming damage. Doubled when defending. */
    s16 spd;            /* Speed: determines turn order at battle start.
                         * Higher SPD goes first (ties favor the player). */
    u8  sp;             /* Special Points: consumed by BACT_SPECIAL (1 per use).
                         * Also used by some boss attacks (DRAIN, MULTI). */
    u8  max_sp;         /* Maximum SP cap (for SP restoration clamping) */
    u8  defending;      /* 1 if currently defending this turn. Reset at the start of
                         * each actor's turn. Doubles DEF in damage calculation. */
    u8  is_player;      /* 1 = player combatant, 0 = enemy combatant.
                         * Used to select UI messages ("VEX ATTACKS!" vs "ENEMY ATTACKS!")
                         * and to determine shake target direction. */
    u8  poison_turns;   /* Turns of poison remaining, deals 3 HP/turn (#182) */
} BattleCombatant;

/*=== Battle Context (complete state) ===*/
/* Singleton struct holding all state for one battle encounter. Initialized
 * by battleStart() and mutated by battleUpdate() each frame until the
 * battle ends and state returns to BSTATE_NONE. */
typedef struct {
    u8 state;               /* Current BSTATE_* (state machine position) */
    u8 turn_number;         /* Current round number (starts at 1, increments after
                             * both combatants have acted). Used in AI randomness. */
    BattleCombatant player; /* Player combatant - stats copied from rpg_stats at battle start,
                             * synced back on victory/defeat for persistence. */
    BattleCombatant enemy;  /* Enemy combatant - stats loaded from enemy_battle_stats[]
                             * or BossTypeDef based on encounter type. */
    u8 player_action;       /* Player's selected action (BACT_*) for current turn */
    u8 enemy_action;        /* AI-chosen action (BACT_* for normal, BOSS_ACT_* for boss) */
    s16 last_damage;        /* Damage dealt by the last resolved action.
                             * Positive = damage to target, negative = healing.
                             * Used by UI to display "045 DAMAGE!" or "025 HEALED!" */
    u8 player_goes_first;   /* 1 if player SPD >= enemy SPD (computed once at start).
                             * Determines who acts first each round. */
    u8 last_actor;          /* 0 = player acted last, 1 = enemy acted last.
                             * Used in RESOLVE state to determine whose turn is next. */
    u8 anim_timer;          /* General-purpose frame countdown for pacing between states.
                             * Decremented each frame; state transitions when it hits 0. */
    u8 menu_cursor;         /* Current action menu selection index (0-3, maps to BACT_*).
                             * Persists across turns so the cursor stays where player left it. */
    u8 enemy_type;          /* ENEMY_TYPE_* of current opponent (0-3).
                             * Used to select enemy name, sprite, and stat row. */
    u16 xp_gained;          /* XP awarded on victory. From enemy_xp[] table for normal
                             * enemies, or BossTypeDef.xp_reward for bosses. */
    u8 is_boss;             /* Phase 19: 1 if this is a boss battle. Changes AI source,
                             * allows BOSS_ACT_* actions, and alters exit behavior. */
    u8 boss_zone;           /* Phase 19: boss zone ID (0-2, BOSS_ZONE*). Only meaningful
                             * when is_boss == 1. Used to determine post-boss behavior. */
} BattleContext;

/* Global singleton battle context. Accessed by battle.c, battle_ui.c, and boss.c. */
extern BattleContext battle;

/* Battle trigger: set to ENEMY_TYPE_* (0-3) or BOSS_TRIGGER_* (0x80+) to start
 * a battle on the next frame. Set to BATTLE_TRIGGER_NONE (0xFF) when no battle
 * is pending. Checked by the main game loop each frame. */
extern u8 g_battle_trigger;

/* Raw damage formula shared between battle.c and boss.c.
 * Computes ATK^2 / (ATK + DEF) + variance(0-3), minimum 1.
 * Uses only integer math (no floating point on the 65816).
 * Max ATK=43 -> ATK^2=1849, safely within s16 range (32767). */
s16 battleCalcDamageRaw(s16 atk_val, s16 def_val);

/* Initialize the battle engine state. Clears battle context, sets trigger
 * to NONE, initializes battle UI and boss subsystems. Call once at game startup. */
void battleInit(void);

/* Start a battle against the specified enemy type (ENEMY_TYPE_* or BOSS_TRIGGER_*).
 * Performs a blocking fade transition: fades out, reconfigures PPU for battle
 * screen (repurpose BG1 for text, show battle sprites), draws initial UI,
 * fades in. Initializes both combatants' stats and determines turn order. */
void battleStart(u8 enemyType);

/* Update battle logic for one frame. Drives the state machine: processes
 * menu input during PLAYER_TURN, runs AI during ENEMY_TURN, resolves
 * actions, checks win/lose conditions.
 * pad_pressed = edge-triggered button presses (not held state) for menu nav.
 * Returns 1 while battle is active, 0 when battle has ended (state = NONE). */
u8 battleUpdate(u16 pad_pressed);

/* Lightweight HP-only redraws: redraw just the HP bar and number without
 * clearing and redrawing the labels. Used after damage/heal when the
 * stat labels haven't changed. Saves BG3 tilemap write bandwidth. */
void battleUIUpdateEnemyHP(void);
void battleUIUpdatePlayerHP(void);

#endif /* BATTLE_H */
