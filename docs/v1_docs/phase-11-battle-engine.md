# Phase 11: Turn-Based Battle System - Core Engine

## Objective
Implement the turn-based battle engine that activates when triggered by collision events. The battle system uses a classic JRPG format: player and enemy take turns selecting actions (Attack, Defend, Special, Item). Each action resolves with damage calculations, HP tracking, and win/loss conditions. This phase implements the engine logic only; the UI is in Phase 12.

## Prerequisites
- Phase 10 (Collision triggers battles), Phase 13 provides formulas (but this phase uses placeholder math).

## Detailed Tasks

1. Create `src/battle/battle_engine.c` - Core battle state machine with states: INIT, PLAYER_TURN, PLAYER_ACTION, ENEMY_TURN, ENEMY_ACTION, RESOLVE, VICTORY, DEFEAT, EXIT.

2. Define battle combatant structures for both player and enemy with: HP, max HP, ATK, DEF, SPD, special ability charges.

3. Implement turn order based on SPD stat (higher goes first).

4. Implement 4 player actions:
   - ATTACK: Deal ATK-based damage to enemy (reduced by enemy DEF)
   - DEFEND: Halve incoming damage for one enemy turn
   - SPECIAL: Powerful attack using SP charges (limited resource)
   - ITEM: Use a consumable (HP restore or stat buff) (Phase 14 expands this)

5. Implement enemy AI decision-making: weighted random choice between attack, defend, and special.

6. Implement battle outcome: Victory awards XP, credits, and possibly items. Defeat reduces player HP and returns to flight with invincibility.

7. Create the battle transition: freeze flight mode, swap graphics to battle scene, run battle, then transition back.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/battle/battle_engine.h
```c
#ifndef BATTLE_ENGINE_H
#define BATTLE_ENGINE_H

#include "game.h"

/* Battle states */
#define BSTATE_NONE         0
#define BSTATE_INIT         1
#define BSTATE_PLAYER_TURN  2
#define BSTATE_PLAYER_ACT   3
#define BSTATE_ENEMY_TURN   4
#define BSTATE_ENEMY_ACT    5
#define BSTATE_RESOLVE      6
#define BSTATE_VICTORY      7
#define BSTATE_DEFEAT       8
#define BSTATE_EXIT         9
#define BSTATE_ANIM_WAIT    10  /* Waiting for attack animation */

/* Battle actions */
#define BACT_ATTACK   0
#define BACT_DEFEND   1
#define BACT_SPECIAL  2
#define BACT_ITEM     3
#define BACT_COUNT    4

/* Battle combatant stats */
typedef struct {
    char name[12];      /* Display name (max 11 chars + null) */
    s16 hp;             /* Current HP */
    s16 max_hp;         /* Maximum HP */
    s16 atk;            /* Attack power */
    s16 def;            /* Defense */
    s16 spd;            /* Speed (determines turn order) */
    s16 sp;             /* Special Points (ability charges) */
    s16 max_sp;         /* Maximum SP */
    u8  level;          /* Level */
    u8  defending;      /* 1 if defending this turn (halves damage) */
    u8  enemy_type;     /* For enemies: ENEMY_TYPE_* (for rendering) */
    u8  is_player;      /* 1 if this is the player combatant */
} BattleCombatant;

/* Battle result */
typedef struct {
    u8  outcome;        /* 0=defeat, 1=victory, 2=fled */
    u16 xp_gained;      /* Experience points earned */
    u16 credits_gained;  /* Currency earned */
    u8  item_dropped;   /* Item ID dropped (0=none) */
} BattleResult;

/* Battle context (complete state) */
typedef struct {
    u8 state;                   /* Current BSTATE_* */
    u8 turn_number;             /* Which turn we're on */
    BattleCombatant player;     /* Player combatant */
    BattleCombatant enemy;      /* Enemy combatant */
    u8 player_action;           /* Selected action for this turn */
    u8 enemy_action;            /* AI-chosen action */
    s16 last_damage;            /* Damage dealt in last action (for display) */
    u8 player_goes_first;       /* 1 if player is faster */
    u8 anim_timer;              /* Animation/display timer */
    u8 message_id;              /* ID of current battle message to display */
    BattleResult result;        /* Outcome after battle ends */
} BattleContext;

/* Global battle context */
extern BattleContext battle;

/* Initialize the battle engine */
void battleInit(void);

/* Start a new battle against the specified enemy type
 * Freezes the flight mode and enters battle state */
void battleStart(u8 enemyType);

/* Update battle logic one frame
 * Returns 1 while battle is active, 0 when battle has ended */
u8 battleUpdate(void);

/* Get the battle result (valid after battleUpdate returns 0) */
BattleResult* battleGetResult(void);

/* Called from the UI when player selects an action (Phase 12) */
void battleSelectAction(u8 action);

/* Calculate damage: attacker ATK vs defender DEF
 * Returns damage dealt (minimum 1) */
s16 battleCalcDamage(BattleCombatant *attacker, BattleCombatant *defender);

/* Apply damage to a combatant */
void battleApplyDamage(BattleCombatant *target, s16 damage);

/* Check if battle is over (someone at 0 HP) */
u8 battleCheckEnd(void);

/* Get current battle message text */
const char* battleGetMessage(void);

#endif
```

### J:/code/snes/snes-rpg-test/src/battle/battle_engine.c
```c
/*==============================================================================
 * Turn-Based Battle Engine
 *
 * State machine flow:
 *   INIT -> determine turn order
 *   PLAYER_TURN -> wait for UI input (Phase 12)
 *   PLAYER_ACT -> resolve player action + animation
 *   ENEMY_TURN -> AI selects action
 *   ENEMY_ACT -> resolve enemy action + animation
 *   RESOLVE -> check win/loss
 *   VICTORY / DEFEAT -> award XP or handle loss
 *   EXIT -> return to flight mode
 *============================================================================*/

#include "battle/battle_engine.h"

BattleContext battle;

/* Enemy stat tables by type */
static const s16 enemy_battle_stats[ENEMY_TYPE_COUNT][6] = {
    /* HP, ATK, DEF, SPD, SP, MaxSP */
    { 30,  8,  3,  5, 0, 0 },   /* SCOUT */
    { 60, 14,  8, 10, 2, 2 },   /* FIGHTER */
    { 100, 20, 15,  6, 3, 3 },  /* HEAVY */
    { 80, 18, 10, 14, 4, 4 },   /* ELITE */
};

/* Enemy names */
static const char *enemy_names[ENEMY_TYPE_COUNT] = {
    "SCOUT", "FIGHTER", "CRUISER", "ELITE"
};

/* XP awards per enemy type */
static const u16 enemy_xp[ENEMY_TYPE_COUNT] = { 15, 30, 50, 75 };

/* Credit awards per enemy type */
static const u16 enemy_credits[ENEMY_TYPE_COUNT] = { 10, 25, 40, 60 };

/* Battle messages */
#define MSG_NONE          0
#define MSG_BATTLE_START  1
#define MSG_PLAYER_ATK    2
#define MSG_PLAYER_DEF    3
#define MSG_PLAYER_SPC    4
#define MSG_PLAYER_ITEM   5
#define MSG_ENEMY_ATK     6
#define MSG_ENEMY_DEF     7
#define MSG_ENEMY_SPC     8
#define MSG_VICTORY       9
#define MSG_DEFEAT        10
#define MSG_DAMAGE        11

static const char *battle_messages[] = {
    "",                         /* 0: none */
    "ENEMY APPEARED!",          /* 1 */
    "VEX ATTACKS!",             /* 2 */
    "VEX DEFENDS!",             /* 3 */
    "VEX USES SPECIAL!",        /* 4 */
    "VEX USES ITEM!",           /* 5 */
    "ENEMY ATTACKS!",           /* 6 */
    "ENEMY DEFENDS!",           /* 7 */
    "ENEMY USES SPECIAL!",      /* 8 */
    "VICTORY!",                 /* 9 */
    "DEFEATED...",              /* 10 */
    "",                         /* 11: damage (dynamic) */
};

void battleInit(void)
{
    battle.state = BSTATE_NONE;
}

void battleStart(u8 enemyType)
{
    u8 et = enemyType;
    if (et >= ENEMY_TYPE_COUNT) et = 0;

    battle.state = BSTATE_INIT;
    battle.turn_number = 0;

    /* Initialize player combatant from global player stats */
    /* Phase 13 will populate these from the RPG stat system */
    /* For now, use placeholder stats */
    battle.player.hp = 100;
    battle.player.max_hp = 100;
    battle.player.atk = 15;
    battle.player.def = 8;
    battle.player.spd = 12;
    battle.player.sp = 3;
    battle.player.max_sp = 3;
    battle.player.level = 1;
    battle.player.defending = 0;
    battle.player.is_player = 1;
    /* Copy name */
    battle.player.name[0] = 'V'; battle.player.name[1] = 'E';
    battle.player.name[2] = 'X'; battle.player.name[3] = 0;

    /* Initialize enemy combatant from type table */
    battle.enemy.hp = enemy_battle_stats[et][0];
    battle.enemy.max_hp = enemy_battle_stats[et][0];
    battle.enemy.atk = enemy_battle_stats[et][1];
    battle.enemy.def = enemy_battle_stats[et][2];
    battle.enemy.spd = enemy_battle_stats[et][3];
    battle.enemy.sp = enemy_battle_stats[et][4];
    battle.enemy.max_sp = enemy_battle_stats[et][5];
    battle.enemy.level = et + 1;
    battle.enemy.defending = 0;
    battle.enemy.enemy_type = et;
    battle.enemy.is_player = 0;
    /* Copy enemy name (simple byte copy) */
    {
        u8 ni;
        const char *ename = enemy_names[et];
        for (ni = 0; ni < 11 && ename[ni]; ni++) {
            battle.enemy.name[ni] = ename[ni];
        }
        battle.enemy.name[ni] = 0;
    }

    /* Determine turn order */
    battle.player_goes_first = (battle.player.spd >= battle.enemy.spd) ? 1 : 0;

    battle.message_id = MSG_BATTLE_START;
    battle.anim_timer = 60;  /* Show "ENEMY APPEARED!" for 1 second */
    battle.last_damage = 0;

    battle.result.outcome = 0;
    battle.result.xp_gained = 0;
    battle.result.credits_gained = 0;
    battle.result.item_dropped = 0;
}

s16 battleCalcDamage(BattleCombatant *attacker, BattleCombatant *defender)
{
    s16 raw_damage;
    s16 defense;

    raw_damage = attacker->atk;

    /* Subtract defense */
    defense = defender->def;
    if (defender->defending) {
        defense = defense * 2; /* Double defense when guarding */
    }

    raw_damage = raw_damage - (defense >> 1); /* ATK - DEF/2 */

    /* Add some variance: +/- 10% using frame counter as "random" */
    /* Simple: raw_damage += (g_frame_count & 3) - 1 */
    raw_damage += (s16)((g_frame_count & 3)) - 1;

    /* Minimum 1 damage */
    if (raw_damage < 1) raw_damage = 1;

    return raw_damage;
}

void battleApplyDamage(BattleCombatant *target, s16 damage)
{
    target->hp -= damage;
    if (target->hp < 0) target->hp = 0;
}

u8 battleCheckEnd(void)
{
    if (battle.enemy.hp <= 0) return 1;  /* Enemy defeated */
    if (battle.player.hp <= 0) return 2; /* Player defeated */
    return 0; /* Battle continues */
}

static void resolveAction(BattleCombatant *actor, BattleCombatant *target, u8 action)
{
    s16 damage;

    switch (action) {
        case BACT_ATTACK:
            damage = battleCalcDamage(actor, target);
            battleApplyDamage(target, damage);
            battle.last_damage = damage;
            battle.message_id = actor->is_player ? MSG_PLAYER_ATK : MSG_ENEMY_ATK;
            break;

        case BACT_DEFEND:
            actor->defending = 1;
            battle.last_damage = 0;
            battle.message_id = actor->is_player ? MSG_PLAYER_DEF : MSG_ENEMY_DEF;
            break;

        case BACT_SPECIAL:
            if (actor->sp > 0) {
                actor->sp--;
                /* Special does 2x ATK damage */
                damage = battleCalcDamage(actor, target);
                damage = damage * 2;
                battleApplyDamage(target, damage);
                battle.last_damage = damage;
                battle.message_id = actor->is_player ? MSG_PLAYER_SPC : MSG_ENEMY_SPC;
            } else {
                /* No SP: fallback to normal attack */
                damage = battleCalcDamage(actor, target);
                battleApplyDamage(target, damage);
                battle.last_damage = damage;
                battle.message_id = actor->is_player ? MSG_PLAYER_ATK : MSG_ENEMY_ATK;
            }
            break;

        case BACT_ITEM:
            /* Heal 30% of max HP */
            damage = actor->max_hp * 3 / 10;
            actor->hp += damage;
            if (actor->hp > actor->max_hp) actor->hp = actor->max_hp;
            battle.last_damage = -damage; /* Negative = healing */
            battle.message_id = actor->is_player ? MSG_PLAYER_ITEM : MSG_NONE;
            break;
    }
}

static u8 enemyChooseAction(void)
{
    u8 r = (u8)(g_frame_count * 7 + battle.turn_number * 13) & 0x0F;

    /* HP below 30%: higher chance to defend or use special */
    if (battle.enemy.hp < battle.enemy.max_hp * 3 / 10) {
        if (r < 4 && battle.enemy.sp > 0) return BACT_SPECIAL;
        if (r < 8) return BACT_DEFEND;
        return BACT_ATTACK;
    }

    /* Normal: mostly attack, sometimes special */
    if (r < 10) return BACT_ATTACK;
    if (r < 13 && battle.enemy.sp > 0) return BACT_SPECIAL;
    return BACT_DEFEND;
}

u8 battleUpdate(void)
{
    u8 end_check;

    switch (battle.state) {
        case BSTATE_NONE:
            return 0;

        case BSTATE_INIT:
            /* Wait for intro message timer */
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            /* Start first turn */
            battle.turn_number = 1;
            battle.player.defending = 0;
            battle.enemy.defending = 0;
            if (battle.player_goes_first) {
                battle.state = BSTATE_PLAYER_TURN;
            } else {
                battle.state = BSTATE_ENEMY_TURN;
            }
            return 1;

        case BSTATE_PLAYER_TURN:
            /* Wait for UI to call battleSelectAction() */
            /* Phase 12 handles the menu UI */
            return 1;

        case BSTATE_PLAYER_ACT:
            /* Show action message */
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            /* Resolve player action */
            battle.player.defending = 0;
            resolveAction(&battle.player, &battle.enemy, battle.player_action);
            battle.anim_timer = 30; /* Show result for 0.5 sec */
            battle.state = BSTATE_RESOLVE;
            return 1;

        case BSTATE_ENEMY_TURN:
            /* AI chooses action instantly */
            battle.enemy_action = enemyChooseAction();
            battle.enemy.defending = 0;
            battle.anim_timer = 15; /* Brief pause before enemy acts */
            battle.state = BSTATE_ENEMY_ACT;
            return 1;

        case BSTATE_ENEMY_ACT:
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            resolveAction(&battle.enemy, &battle.player, battle.enemy_action);
            battle.anim_timer = 30;
            battle.state = BSTATE_RESOLVE;
            return 1;

        case BSTATE_RESOLVE:
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            /* Check for battle end */
            end_check = battleCheckEnd();
            if (end_check == 1) {
                /* Enemy defeated */
                battle.state = BSTATE_VICTORY;
                battle.message_id = MSG_VICTORY;
                battle.result.outcome = 1;
                battle.result.xp_gained = enemy_xp[battle.enemy.enemy_type];
                battle.result.credits_gained = enemy_credits[battle.enemy.enemy_type];
                battle.anim_timer = 90; /* Victory display 1.5 sec */
                return 1;
            }
            if (end_check == 2) {
                /* Player defeated */
                battle.state = BSTATE_DEFEAT;
                battle.message_id = MSG_DEFEAT;
                battle.result.outcome = 0;
                battle.anim_timer = 90;
                return 1;
            }
            /* Battle continues - next combatant's turn */
            battle.turn_number++;
            /* Alternate turns based on who went first */
            if (battle.state == BSTATE_RESOLVE) {
                /* After player acted: enemy turn. After enemy: player turn. */
                if (battle.player_goes_first) {
                    /* Player went first this round. After player_act->resolve, go to enemy */
                    /* After enemy_act->resolve, start new round with player */
                    /* Use turn_number parity */
                    if (battle.turn_number & 1) {
                        battle.state = BSTATE_ENEMY_TURN;
                    } else {
                        battle.state = BSTATE_PLAYER_TURN;
                    }
                } else {
                    if (battle.turn_number & 1) {
                        battle.state = BSTATE_PLAYER_TURN;
                    } else {
                        battle.state = BSTATE_ENEMY_TURN;
                    }
                }
            }
            return 1;

        case BSTATE_VICTORY:
        case BSTATE_DEFEAT:
            if (battle.anim_timer > 0) {
                battle.anim_timer--;
                return 1;
            }
            battle.state = BSTATE_EXIT;
            return 1;

        case BSTATE_EXIT:
            battle.state = BSTATE_NONE;
            return 0; /* Battle is over */
    }

    return 0;
}

void battleSelectAction(u8 action)
{
    if (battle.state != BSTATE_PLAYER_TURN) return;
    battle.player_action = action;
    battle.anim_timer = 15; /* Brief pause before action resolves */
    battle.state = BSTATE_PLAYER_ACT;
}

BattleResult* battleGetResult(void)
{
    return &battle.result;
}

const char* battleGetMessage(void)
{
    if (battle.message_id < 12) {
        return battle_messages[battle.message_id];
    }
    return "";
}
```

## Technical Specifications

### Battle State Machine Flow
```
INIT (show "ENEMY APPEARED!", 1 sec)
  |
  v
[speed check: who goes first?]
  |
  +-> PLAYER_TURN (wait for UI menu input)
  |     |
  |     v
  |   PLAYER_ACT (resolve action, show message 0.5 sec)
  |     |
  |     v
  |   RESOLVE (check HP, either VICTORY/DEFEAT or continue)
  |     |
  |     v
  +-> ENEMY_TURN (AI picks action, 0.25 sec pause)
  |     |
  |     v
  |   ENEMY_ACT (resolve action, show message 0.5 sec)
  |     |
  |     v
  |   RESOLVE (check HP, continue or end)
  |     |
  |     +-> Loop back to PLAYER_TURN / ENEMY_TURN
  |
  v
VICTORY -> show "VICTORY!", award XP/credits (1.5 sec)
DEFEAT  -> show "DEFEATED..." (1.5 sec)
  |
  v
EXIT -> return 0 from battleUpdate(), caller resumes flight mode
```

### Damage Formula (Placeholder - Phase 13 refines)
```
damage = ATK - (DEF / 2) + random(-1, +2)
If defender is DEFENDING: DEF is doubled before calculation.
Special attack: damage * 2
Minimum damage: 1

Example:
  Player ATK=15, Enemy DEF=8
  damage = 15 - (8/2) + rand = 15 - 4 + 1 = 12
  With defend: 15 - (16/2) + 1 = 15 - 8 + 1 = 8
```

### Battle Duration Target
```
Average battle length: 4-6 turns per combatant.
At ~45 frames per action (message + animation):
  6 turns * 2 combatants * 45 frames = 540 frames = 9 seconds
Plus intro (60f) + victory (90f) = 750 frames = 12.5 seconds

Target: each battle takes 10-15 seconds.
```

## Acceptance Criteria
1. battleStart() correctly initializes both combatants from the enemy type table.
2. Turn order respects SPD stat (higher SPD goes first).
3. Player can select all 4 actions (ATTACK, DEFEND, SPECIAL, ITEM) via battleSelectAction().
4. Damage calculation produces reasonable values (not always 1, not instant kills).
5. DEFEND action halves incoming damage on the next enemy attack.
6. SPECIAL attack costs 1 SP and deals double damage.
7. Battle ends in VICTORY when enemy HP reaches 0.
8. Battle ends in DEFEAT when player HP reaches 0.
9. XP and credits are correctly set in the BattleResult.
10. Enemy AI makes reasonable choices (attacks mostly, defends when low HP).

## SNES-Specific Constraints
- No floating point in damage calculations. Use integer math with shifts.
- The "random" factor uses g_frame_count as a pseudo-random source. Not cryptographically random, but sufficient for gameplay variety.
- Battle message strings are const char* stored in ROM. Do not attempt to sprintf into ROM pointers.
- BattleCombatant struct is 32 bytes. Two combatants = 64 bytes. BattleContext total ~80 bytes WRAM.

## Estimated Complexity
**Complex** - The state machine has many states with timing dependencies. Turn order alternation logic and damage formula tuning require iteration. This is the core RPG mechanic.
