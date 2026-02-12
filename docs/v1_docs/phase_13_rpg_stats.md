# Phase 13: RPG Stats, Leveling & Damage Formulas

## Objective
Implement the complete RPG progression system: experience points, leveling up with stat increases, damage/defense formulas refined for balanced gameplay, and status effects. This makes the 10-minute game feel like a proper RPG with meaningful progression across three zones.

## Prerequisites
- Phase 11 (Battle System Core) complete
- Phase 12 (Battle UI) complete

## Detailed Tasks

### 1. Design XP/Level Curve
For a 10-minute game with ~15 battles, the player should reach level 5-7 by the final boss.

### 2. Define Stat Growth Tables
Each level increases HP, MP, Attack, Defense, Speed by predetermined amounts.

### 3. Implement Level-Up Check and Processing
After each battle, check XP threshold and apply stat gains.

### 4. Refine Damage Formula
Balance base damage, defense reduction, level scaling, and random variance.

### 5. Implement Status Effects (Simple)
Poison (HP drain per turn) and Stun (skip turn) for variety.

### 6. Create Level-Up Notification UI

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/stats.h` | CREATE | Stats/leveling system header |
| `src/stats.c` | CREATE | Stats/leveling implementation |
| `src/battle.c` | MODIFY | Integrate enhanced damage formula |
| `src/ui.c` | MODIFY | Level-up display |

## Technical Specifications

### XP/Level Table
```c
/* XP required to reach each level (cumulative) */
/* Designed for ~15 battles across 3 zones */
/* Zone 1: levels 1-2, Zone 2: levels 3-4, Zone 3: levels 5-7 */
const u16 xp_table[8] = {
    0,      /* Level 1 (starting) */
    30,     /* Level 2: ~3 scout battles */
    80,     /* Level 3: ~5 zone 1 battles */
    160,    /* Level 4: ~3 zone 2 battles after zone 1 */
    280,    /* Level 5: ~4 zone 2 battles */
    440,    /* Level 6: ~3 zone 3 battles */
    640,    /* Level 7: ~4 zone 3 battles (before boss) */
    999,    /* Level 8: max (unlikely to reach) */
};

#define MAX_LEVEL 7
```

### Stat Growth Per Level
```c
/* Stat increases per level [level] = {hp, mp, atk, def, spd} */
typedef struct {
    u16 hp_bonus;
    u16 mp_bonus;
    u16 atk_bonus;
    u16 def_bonus;
    u16 spd_bonus;
} LevelUpBonus;

const LevelUpBonus level_bonuses[8] = {
    {  0,  0, 0, 0, 0},  /* Level 1 base (not used) */
    { 15,  5, 3, 2, 1},  /* Level 2 */
    { 20,  5, 4, 2, 1},  /* Level 3 */
    { 20, 10, 4, 3, 2},  /* Level 4 */
    { 25, 10, 5, 3, 2},  /* Level 5 */
    { 30, 10, 6, 4, 2},  /* Level 6 */
    { 35, 15, 7, 5, 3},  /* Level 7 */
    {  0,  0, 0, 0, 0},  /* Level 8 (cap) */
};

/* Player base stats at level 1 */
#define BASE_HP     100
#define BASE_MP      50
#define BASE_ATK     10
#define BASE_DEF      5
#define BASE_SPD      8
```

### Enhanced Damage Formula
```c
/*
 * Damage Formula (balanced for 10-minute RPG):
 *
 * Physical Attack:
 *   base_damage = attacker.attack * 2 - defender.defense
 *   variance = base_damage * (rand 0.875 to 1.125)
 *   final = max(1, variance)
 *
 * Special Attack:
 *   base_damage = attacker.attack * 3 - defender.defense
 *   mp_cost = 10
 *   variance = base_damage * (rand 0.875 to 1.125)
 *   final = max(1, variance)
 *
 * Defending:
 *   defense_multiplier = 2x (double defense for one turn)
 *
 * Critical Hit (12.5% chance):
 *   damage *= 2
 *
 * Level scaling:
 *   If attacker level > defender level: +10% per level difference
 *   If attacker level < defender level: -5% per level difference
 */
```

### stats.h
```c
#ifndef STATS_H
#define STATS_H

#include <snes.h>
#include "config.h"

/* Status effect flags */
#define STATUS_NONE    0x00
#define STATUS_POISON  0x01  /* Lose 5% max HP per turn */
#define STATUS_STUN    0x02  /* Skip next turn */
#define STATUS_BOOST   0x04  /* Attack +25% for 3 turns */
#define STATUS_SHIELD  0x08  /* Defense +50% for 3 turns */

extern const u16 xp_table[8];
extern const LevelUpBonus level_bonuses[8];

/*--- Functions ---*/
void stats_init(void);
void stats_check_levelup(void);
void stats_apply_levelup(void);
u16  stats_calc_damage(u16 atk, u16 def, u8 is_special, u8 atk_level, u8 def_level);
void stats_apply_status(u8 *status, u8 effect);
void stats_process_turn_status(BattleCombatant *c);
u8   stats_get_player_level(void);

#endif /* STATS_H */
```

### stats.c
```c
#include "stats.h"
#include "player.h"
#include "battle.h"
#include "ui.h"

void stats_init(void) {
    g_player.level = 1;
    g_player.xp = 0;
    g_player.hp = BASE_HP;
    g_player.max_hp = BASE_HP;
    g_player.mp = BASE_MP;
    g_player.max_mp = BASE_MP;
    g_player.attack = BASE_ATK;
    g_player.defense = BASE_DEF;
    g_player.speed_stat = BASE_SPD;
}

void stats_check_levelup(void) {
    while (g_player.level < MAX_LEVEL &&
           g_player.xp >= xp_table[g_player.level]) {
        stats_apply_levelup();
    }
}

void stats_apply_levelup(void) {
    u8 new_level = g_player.level + 1;
    const LevelUpBonus *bonus = &level_bonuses[new_level];

    g_player.level = new_level;

    /* Apply stat increases */
    g_player.max_hp += bonus->hp_bonus;
    g_player.max_mp += bonus->mp_bonus;
    g_player.attack += bonus->atk_bonus;
    g_player.defense += bonus->def_bonus;
    g_player.speed_stat += bonus->spd_bonus;

    /* Heal on level up (full restore) */
    g_player.hp = g_player.max_hp;
    g_player.mp = g_player.max_mp;

    /* Show level up notification */
    /* ui_show_levelup(new_level); */ /* Implemented in Phase 12 UI */
}

/* Enhanced damage calculation */
u16 stats_calc_damage(u16 atk, u16 def, u8 is_special, u8 atk_level, u8 def_level) {
    u16 base;
    u16 damage;
    u8 rand_val;

    /* Base damage calculation */
    if (is_special) {
        base = atk * 3;
    } else {
        base = atk * 2;
    }

    /* Subtract defense */
    if (base > def) {
        damage = base - def;
    } else {
        damage = 1;
    }

    /* Level scaling */
    if (atk_level > def_level) {
        u8 diff = atk_level - def_level;
        /* +10% per level: damage = damage * (10 + diff) / 10 */
        damage = (damage * (10 + diff)) / 10;
    } else if (def_level > atk_level) {
        u8 diff = def_level - atk_level;
        /* -5% per level: damage = damage * (20 - diff) / 20 */
        damage = (damage * (20 - diff)) / 20;
    }

    /* Random variance: 87.5% to 112.5% */
    /* Multiply by (7 + rand(0..2)) / 8 */
    rand_val = (u8)(rand() & 0x03); /* 0-3 */
    if (rand_val > 2) rand_val = 2;
    damage = (damage * (7 + rand_val)) >> 3;

    /* Critical hit check (12.5% = 1/8 chance) */
    if ((rand() & 0x07) == 0) {
        damage <<= 1; /* Double damage */
        /* TODO: show "CRITICAL!" in UI */
    }

    /* Minimum 1 damage */
    if (damage == 0) damage = 1;

    return damage;
}

/* Apply status effect */
void stats_apply_status(u8 *status, u8 effect) {
    *status |= effect;
}

/* Process status effects at start of turn */
void stats_process_turn_status(BattleCombatant *c) {
    /* Poison: lose 5% max HP */
    if (c->status_effects & STATUS_POISON) {
        u16 poison_dmg = c->max_hp / 20;
        if (poison_dmg < 1) poison_dmg = 1;
        if (c->hp > poison_dmg)
            c->hp -= poison_dmg;
        else
            c->hp = 0;
    }

    /* Stun: handled in battle turn logic (skip turn if stunned) */
    /* Clear stun after one turn */
    if (c->status_effects & STATUS_STUN) {
        c->status_effects &= ~STATUS_STUN;
    }
}

u8 stats_get_player_level(void) {
    return g_player.level;
}
```

### Battle Integration Updates
In `battle_state_player_action()` and `battle_state_enemy_action()`, replace the old damage calculation with:
```c
u16 dmg = stats_calc_damage(
    g_battle.player.attack,
    g_battle.enemy.defense,
    (g_battle.current_action.action == BACT_SPECIAL),
    g_player.level,
    1 /* enemy "level" - based on zone */
);
```

In `battle_end()`, add:
```c
/* Check for level up after XP award */
stats_check_levelup();
```

### Expected Progression (10-minute game)
```
Zone 1 - Debris Field (~3 minutes):
  Battles: 4-5 (scouts, raiders)
  XP earned: ~60-80
  Level reached: 2
  Player stats: HP 115, ATK 13, DEF 7

Zone 2 - Asteroid Belt (~3 minutes):
  Battles: 4-5 (interceptors, destroyers, mini-boss)
  XP earned: ~160-200
  Level reached: 4
  Player stats: HP 155, ATK 21, DEF 12

Zone 3 - Flagship (~3 minutes):
  Battles: 3-4 (elites, commanders, final boss)
  XP earned: ~200-250
  Level reached: 6
  Player stats: HP 215, ATK 32, DEF 19

Final Boss:
  Player level: 5-6
  Boss HP: 200, ATK: 25, DEF: 15
  Expected turns to win: 5-8
```

## Acceptance Criteria
1. XP accumulates correctly after each battle
2. Level up triggers at correct XP thresholds
3. All stats increase by the correct amounts on level up
4. HP/MP fully restore on level up
5. Damage formula produces balanced numbers (not too high, not too low)
6. Critical hits deal double damage ~12.5% of the time
7. Level scaling gives meaningful advantage (+10% per level)
8. Defending doubles effective defense
9. Poison status deals 5% max HP per turn
10. Player reaches approximately level 5-6 by final boss

## SNES-Specific Constraints
- Division is slow on 65816 - minimize divisions in damage formula
- Use shifts for powers of 2: `>> 1` for /2, `>> 3` for /8, `<< 1` for *2
- No floating point - all percentages done with integer fraction math
- u16 overflow: max damage should stay under 65535 (not a concern at these scales)
- const tables stored in ROM, not WRAM (declared as `const`)

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~54KB | 256KB    | ~202KB    |
| WRAM     | ~960B | 128KB   | ~127KB    |
| VRAM     | ~14KB | 64KB    | ~50KB     |
| CGRAM    | 160B  | 512B    | 352B      |

## Estimated Complexity
**Medium** - The math is straightforward integer arithmetic. The main design challenge is balancing the numbers for a satisfying 10-minute progression curve.

## Agent Instructions
1. Create `src/stats.h` and `src/stats.c`
2. Update Makefile and linkfile
3. Call `stats_init()` in game_init() to set starting stats
4. Replace old `battle_calc_damage()` calls with `stats_calc_damage()`
5. Add `stats_check_levelup()` call in `battle_end()`
6. Test: fight 3 scout battles, verify XP accumulates and level 2 triggers
7. Verify stat increases match the growth table
8. Test damage numbers at different levels - they should scale noticeably
9. Verify full HP/MP restore on level up
10. Playtest the full progression curve with a 10-minute run
