# Phase 13: RPG Stats, Leveling & Damage Formulas

## Objective
Implement the RPG progression system: player stats that persist across battles, an XP/leveling system with stat growth curves, and refined damage formulas that replace the placeholders from Phase 11. Define the level cap (level 10 for a 10-minute game), XP requirements per level, and stat growth tables.

## Prerequisites
- Phase 11 (Battle Engine uses stats), Phase 10 (Collision awards XP).

## Detailed Tasks

1. Create `src/game/rpg_stats.c` - Persistent player stats (HP, ATK, DEF, SPD, SP) that carry between battles.
2. Implement XP accumulation and level-up system. Level range: 1-10.
3. Define stat growth table: at each level, how much each stat increases.
4. Implement refined damage formula: `damage = (ATK * ATK) / (ATK + DEF) + variance`.
5. Implement special attack power scaling with level.
6. Integrate stats into battle engine: battleStart() reads from persistent stats.
7. After battle victory, apply XP gain and check for level-up.
8. Implement level-up notification for the UI.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/game/rpg_stats.h
```c
#ifndef RPG_STATS_H
#define RPG_STATS_H

#include "game.h"

#define MAX_LEVEL 10
#define BASE_HP   80
#define BASE_ATK  12
#define BASE_DEF  6
#define BASE_SPD  10
#define BASE_SP   2

/* Persistent player RPG data */
typedef struct {
    u8  level;
    u16 xp;
    u16 xp_to_next;     /* XP needed for next level */
    s16 max_hp;
    s16 current_hp;
    s16 atk;
    s16 def;
    s16 spd;
    s16 max_sp;
    s16 current_sp;
    u16 credits;         /* Currency for shop/items */
    u16 total_kills;     /* Lifetime enemy kills */
} PlayerRPGStats;

extern PlayerRPGStats rpg_stats;

/* Initialize stats to level 1 defaults */
void rpgStatsInit(void);

/* Add XP and check for level-up. Returns 1 if leveled up. */
u8 rpgAddXP(u16 xp);

/* Add credits */
void rpgAddCredits(u16 amount);

/* Restore HP by amount (clamped to max) */
void rpgHealHP(s16 amount);

/* Restore SP by amount */
void rpgHealSP(s16 amount);

/* Reduce current HP (from defeat penalty) */
void rpgReduceHP(s16 amount);

/* Get XP required for a specific level */
u16 rpgGetXPForLevel(u8 level);

/* Populate a BattleCombatant from current stats */
void rpgPopulateBattleStats(void *combatant);

/* Apply post-battle results to persistent stats */
void rpgApplyBattleResult(void *result);

/* Refined damage formula */
s16 rpgCalcDamage(s16 attackerATK, s16 defenderDEF, u8 isSpecial, u8 defenderGuarding);

#endif
```

### J:/code/snes/snes-rpg-test/src/game/rpg_stats.c
```c
#include "game/rpg_stats.h"
#include "battle/battle_engine.h"

PlayerRPGStats rpg_stats;

/* XP required for each level (cumulative) */
static const u16 xp_table[MAX_LEVEL + 1] = {
    0,      /* Level 1 (start) */
    30,     /* Level 2 */
    80,     /* Level 3 */
    160,    /* Level 4 */
    280,    /* Level 5 */
    450,    /* Level 6 */
    680,    /* Level 7 */
    1000,   /* Level 8 */
    1400,   /* Level 9 */
    2000,   /* Level 10 (max) */
    0xFFFF  /* Sentinel (unreachable) */
};

/* Stat growth per level: HP, ATK, DEF, SPD, SP_max */
static const s16 growth_table[MAX_LEVEL][5] = {
    /* L1->L2 */ { 15,  2,  1,  1, 0 },
    /* L2->L3 */ { 15,  2,  2,  1, 1 },
    /* L3->L4 */ { 20,  3,  2,  1, 0 },
    /* L4->L5 */ { 20,  3,  2,  2, 1 },
    /* L5->L6 */ { 25,  3,  3,  1, 0 },
    /* L6->L7 */ { 25,  4,  3,  2, 1 },
    /* L7->L8 */ { 30,  4,  3,  1, 0 },
    /* L8->L9 */ { 30,  5,  4,  2, 1 },
    /* L9->L10*/{ 35,  5,  4,  2, 1 },
    /* padding*/{ 0,   0,  0,  0, 0 },
};

void rpgStatsInit(void)
{
    rpg_stats.level = 1;
    rpg_stats.xp = 0;
    rpg_stats.xp_to_next = xp_table[1];
    rpg_stats.max_hp = BASE_HP;
    rpg_stats.current_hp = BASE_HP;
    rpg_stats.atk = BASE_ATK;
    rpg_stats.def = BASE_DEF;
    rpg_stats.spd = BASE_SPD;
    rpg_stats.max_sp = BASE_SP;
    rpg_stats.current_sp = BASE_SP;
    rpg_stats.credits = 0;
    rpg_stats.total_kills = 0;
}

static void applyLevelUp(void)
{
    u8 lv = rpg_stats.level - 1; /* Index into growth table */
    if (lv >= MAX_LEVEL) return;

    rpg_stats.max_hp += growth_table[lv][0];
    rpg_stats.atk += growth_table[lv][1];
    rpg_stats.def += growth_table[lv][2];
    rpg_stats.spd += growth_table[lv][3];
    rpg_stats.max_sp += growth_table[lv][4];

    /* Full heal on level up */
    rpg_stats.current_hp = rpg_stats.max_hp;
    rpg_stats.current_sp = rpg_stats.max_sp;
}

u8 rpgAddXP(u16 xp)
{
    u8 leveled = 0;
    rpg_stats.xp += xp;

    while (rpg_stats.level < MAX_LEVEL &&
           rpg_stats.xp >= xp_table[rpg_stats.level]) {
        rpg_stats.level++;
        applyLevelUp();
        leveled = 1;
    }

    if (rpg_stats.level < MAX_LEVEL) {
        rpg_stats.xp_to_next = xp_table[rpg_stats.level] - rpg_stats.xp;
    } else {
        rpg_stats.xp_to_next = 0;
    }

    return leveled;
}

void rpgAddCredits(u16 amount)
{
    rpg_stats.credits += amount;
    if (rpg_stats.credits > 9999) rpg_stats.credits = 9999;
}

void rpgHealHP(s16 amount)
{
    rpg_stats.current_hp += amount;
    if (rpg_stats.current_hp > rpg_stats.max_hp)
        rpg_stats.current_hp = rpg_stats.max_hp;
}

void rpgHealSP(s16 amount)
{
    rpg_stats.current_sp += amount;
    if (rpg_stats.current_sp > rpg_stats.max_sp)
        rpg_stats.current_sp = rpg_stats.max_sp;
}

void rpgReduceHP(s16 amount)
{
    rpg_stats.current_hp -= amount;
    if (rpg_stats.current_hp < 1) rpg_stats.current_hp = 1; /* Don't die outside battle */
}

u16 rpgGetXPForLevel(u8 level)
{
    if (level > MAX_LEVEL) return 0xFFFF;
    return xp_table[level];
}

void rpgPopulateBattleStats(void *combatant)
{
    BattleCombatant *c = (BattleCombatant *)combatant;
    c->hp = rpg_stats.current_hp;
    c->max_hp = rpg_stats.max_hp;
    c->atk = rpg_stats.atk;
    c->def = rpg_stats.def;
    c->spd = rpg_stats.spd;
    c->sp = rpg_stats.current_sp;
    c->max_sp = rpg_stats.max_sp;
    c->level = rpg_stats.level;
    c->defending = 0;
    c->is_player = 1;
    c->name[0]='V'; c->name[1]='E'; c->name[2]='X'; c->name[3]=0;
}

void rpgApplyBattleResult(void *result)
{
    BattleResult *r = (BattleResult *)result;
    if (r->outcome == 1) {
        /* Victory */
        rpgAddXP(r->xp_gained);
        rpgAddCredits(r->credits_gained);
        rpg_stats.total_kills++;
        /* Sync HP/SP from battle (player may have taken damage) */
        rpg_stats.current_hp = battle.player.hp;
        rpg_stats.current_sp = battle.player.sp;
    } else {
        /* Defeat: lose 20% current HP as penalty, keep at least 1 */
        rpgReduceHP(rpg_stats.current_hp / 5);
    }
}

s16 rpgCalcDamage(s16 attackerATK, s16 defenderDEF, u8 isSpecial, u8 defenderGuarding)
{
    s16 effectiveDEF;
    s16 damage;
    s16 variance;

    effectiveDEF = defenderDEF;
    if (defenderGuarding) effectiveDEF = effectiveDEF * 2;

    /* Formula: ATK^2 / (ATK + DEF)
     * This ensures damage scales with ATK but is meaningfully reduced by DEF.
     * At equal ATK and DEF: damage = ATK/2 */
    damage = (attackerATK * attackerATK) / (attackerATK + effectiveDEF);

    if (isSpecial) {
        damage = damage + (damage >> 1); /* 1.5x for special */
    }

    /* Add variance: -1 to +2 */
    variance = (s16)(g_frame_count & 3) - 1;
    damage += variance;

    if (damage < 1) damage = 1;
    return damage;
}
```

## Technical Specifications

### Level Progression Table
```
Level  Total XP  Battles*  HP   ATK  DEF  SPD  SP
-----  --------  --------  ---  ---  ---  ---  --
  1         0       0       80   12    6   10   2
  2        30       1-2    95   14    7   11   2
  3        80       3-4   110   16    9   12   3
  4       160       5-7   130   19   11   13   3
  5       280       8-10  150   22   13   15   4
  6       450      11-14  175   25   16   16   4
  7       680      15-18  200   29   19   18   5
  8      1000      19-23  230   33   22   19   5
  9      1400      24-28  260   38   26   21   6
 10      2000      29-35  295   43   30   23   7

* Approximate battles assuming 30-75 XP per battle.
  A 10-minute game with ~25-30 battles reaches level 7-9.
  Skilled players who fight more battles can reach level 10.
```

### Damage Formula Analysis
```
ATK^2 / (ATK + DEF)

Examples:
  L1 player (ATK=12) vs L1 scout (DEF=3):   12*12/(12+3) = 9.6 -> 9
  L1 player (ATK=12) vs L2 fighter (DEF=8):  12*12/(12+8) = 7.2 -> 7
  L5 player (ATK=22) vs L3 heavy (DEF=15):   22*22/(22+15) = 13.1 -> 13
  L5 player (ATK=22) vs L3 heavy GUARDING:   22*22/(22+30) = 9.3 -> 9

Special: damage * 1.5 = 13 * 1.5 = 19.5 -> 19

This formula avoids:
  - Division by zero (ATK+DEF is always > 0)
  - Negative damage (ATK^2 is always positive)
  - Extreme scaling (logarithmic growth)

Integer-only: ATK*ATK is max 43*43 = 1849, fits in s16 (max 32767).
Division ATK+DEF is max 43+30 = 73. 1849/73 = 25. Reasonable range.
```

## Acceptance Criteria
1. Player starts at level 1 with correct base stats.
2. XP accumulates across battles and persists.
3. Level-up occurs at correct XP thresholds.
4. Stats increase correctly on level-up per the growth table.
5. Full HP/SP heal on level-up.
6. Battle engine uses persistent stats (not hardcoded placeholders).
7. Damage formula produces reasonable values at all level ranges.
8. Defeat penalty reduces HP by 20% but never to 0.
9. Credits accumulate and are tracked.
10. Level cap at 10 prevents overflow.

## SNES-Specific Constraints
- ATK*ATK fits in s16 (max 43*43=1849). Safe.
- Division by (ATK+DEF) is integer division. Remainder is truncated. This is acceptable.
- 816-tcc generates slow code for division. The formula is only called once per battle action, so performance is fine.
- All stat values fit in s16. No 32-bit math needed.

## Estimated Complexity
**Medium** - Math is straightforward. Balancing stat growth requires playtesting iteration.
