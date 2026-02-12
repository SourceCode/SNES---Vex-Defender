# Phase 19: Boss Battles & Special Encounters

## Objective
Implement the two boss encounters: the Zone 2 mini-boss "Sentinel" and the Zone 3 final boss (either "Ark Defense System" on the truth path or "Commander Zyx's Flagship" on the loyalty path). Bosses use multi-phase patterns, special attacks, and dramatic presentation.

## Prerequisites
- Phase 11 (Battle System Core) complete
- Phase 13 (RPG Stats) complete
- Phase 16 (Dialog System) complete
- Phase 18 (Zone Progression) complete

## Detailed Tasks

### 1. Design Boss Encounter Data
Each boss has multiple phases with different attack patterns.

### 2. Implement Multi-Phase Boss Logic
Bosses change behavior at HP thresholds (75%, 50%, 25%).

### 3. Create Boss-Specific Attacks
Special moves only bosses can use (multi-hit, AoE, status effects).

### 4. Implement Boss Battle UI Enhancements
Large HP bar, phase indicators, special attack animations.

### 5. Create Boss Intro Sequences
Dramatic entry with dialog, music change, and visual effects.

### 6. Implement Victory Rewards and Story Triggers

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/boss.h` | CREATE | Boss battle system header |
| `src/boss.c` | CREATE | Boss battle implementation |
| `src/battle.c` | MODIFY | Support boss battle mode |
| `src/zones.c` | MODIFY | Trigger boss encounters |

## Technical Specifications

### Boss Definitions
```c
/* Boss IDs */
#define BOSS_NONE       0
#define BOSS_SENTINEL   1  /* Zone 2 mini-boss */
#define BOSS_ARK_DEF    2  /* Zone 3 boss (Truth path - Ark defense system) */
#define BOSS_ZYX        3  /* Zone 3 boss (Loyalty path - Commander Zyx) */

/* Boss phase thresholds (% of max HP) */
#define BOSS_PHASE_2_THRESHOLD  75  /* Switch to phase 2 at 75% HP */
#define BOSS_PHASE_3_THRESHOLD  50  /* Phase 3 at 50% */
#define BOSS_PHASE_4_THRESHOLD  25  /* Phase 4 (desperation) at 25% */

/* Boss special attacks */
#define BOSS_ATK_NORMAL     0  /* Standard attack */
#define BOSS_ATK_HEAVY      1  /* 2x damage */
#define BOSS_ATK_MULTI      2  /* Hit 2-3 times */
#define BOSS_ATK_DRAIN      3  /* Damage + heal self */
#define BOSS_ATK_STATUS     4  /* Apply poison or stun */
#define BOSS_ATK_CHARGE     5  /* Skip turn, next attack 3x damage */
#define BOSS_ATK_REPAIR     6  /* Heal 20% max HP */

typedef struct {
    char name[12];
    u16  max_hp;
    u16  attack;
    u16  defense;
    u16  speed;
    u8   sprite_tile;
    u8   sprite_pal;
    u16  xp_reward;

    /* Phase-specific attack patterns */
    /* Each phase has a weighted list of attacks */
    u8   phase1_attacks[4]; /* Attack IDs for phase 1 */
    u8   phase2_attacks[4]; /* Attack IDs for phase 2 */
    u8   phase3_attacks[4]; /* Phase 3 */
    u8   phase4_attacks[4]; /* Desperation phase */
} BossTemplate;

typedef struct {
    u8  boss_id;
    u8  current_phase;        /* 1-4 */
    u8  is_charging;          /* Charge attack active */
    u16 charge_damage;        /* Stored damage for charged attack */
    u8  turns_since_heal;     /* Prevent heal spam */
    u8  intro_complete;       /* Intro sequence finished */
    u8  death_anim_timer;     /* Death animation countdown */
} BossState;
```

### boss.h
```c
#ifndef BOSS_H
#define BOSS_H

#include <snes.h>
#include "config.h"
#include "battle.h"

/* ... (types from above) ... */

extern BossState g_boss;
extern const BossTemplate boss_templates[4];

/*--- Functions ---*/
void boss_init(u8 boss_id);
void boss_update_phase(void);
u8   boss_select_attack(void);
void boss_execute_attack(u8 attack_id);
void boss_intro_sequence(u8 boss_id);
void boss_death_sequence(void);
void boss_trigger(u8 boss_id);

#endif /* BOSS_H */
```

### boss.c
```c
#include "boss.h"
#include "battle.h"
#include "player.h"
#include "stats.h"
#include "dialog.h"
#include "sound.h"
#include "ui.h"
#include "game.h"

BossState g_boss;

const BossTemplate boss_templates[4] = {
    /* BOSS_NONE */
    {"",          0,   0,  0,  0, 0, 0,   0,
     {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}},

    /* BOSS_SENTINEL (Zone 2 Mini-Boss) */
    {"SENTINEL",  120, 15, 10, 5, 48, PAL_SPR_BOSS, 100,
     {BOSS_ATK_NORMAL, BOSS_ATK_NORMAL, BOSS_ATK_HEAVY, BOSS_ATK_NORMAL},
     {BOSS_ATK_NORMAL, BOSS_ATK_HEAVY, BOSS_ATK_HEAVY, BOSS_ATK_STATUS},
     {BOSS_ATK_HEAVY, BOSS_ATK_MULTI, BOSS_ATK_HEAVY, BOSS_ATK_STATUS},
     {BOSS_ATK_MULTI, BOSS_ATK_MULTI, BOSS_ATK_CHARGE, BOSS_ATK_REPAIR}},

    /* BOSS_ARK_DEF (Truth Path Final Boss) */
    {"ARK DEFENS", 200, 22, 15, 4, 52, PAL_SPR_BOSS, 250,
     {BOSS_ATK_NORMAL, BOSS_ATK_NORMAL, BOSS_ATK_HEAVY, BOSS_ATK_NORMAL},
     {BOSS_ATK_HEAVY, BOSS_ATK_MULTI, BOSS_ATK_NORMAL, BOSS_ATK_REPAIR},
     {BOSS_ATK_MULTI, BOSS_ATK_HEAVY, BOSS_ATK_STATUS, BOSS_ATK_CHARGE},
     {BOSS_ATK_CHARGE, BOSS_ATK_MULTI, BOSS_ATK_DRAIN, BOSS_ATK_MULTI}},

    /* BOSS_ZYX (Loyalty Path Final Boss) */
    {"CMD. ZYX",  180, 20, 12, 6, 56, PAL_SPR_BOSS, 250,
     {BOSS_ATK_NORMAL, BOSS_ATK_NORMAL, BOSS_ATK_HEAVY, BOSS_ATK_STATUS},
     {BOSS_ATK_HEAVY, BOSS_ATK_STATUS, BOSS_ATK_NORMAL, BOSS_ATK_MULTI},
     {BOSS_ATK_MULTI, BOSS_ATK_CHARGE, BOSS_ATK_STATUS, BOSS_ATK_HEAVY},
     {BOSS_ATK_DRAIN, BOSS_ATK_MULTI, BOSS_ATK_CHARGE, BOSS_ATK_DRAIN}},
};

void boss_trigger(u8 boss_id) {
    /* Set up boss state */
    g_boss.boss_id = boss_id;
    g_boss.current_phase = 1;
    g_boss.is_charging = 0;
    g_boss.charge_damage = 0;
    g_boss.turns_since_heal = 0;
    g_boss.intro_complete = 0;
    g_boss.death_anim_timer = 0;

    /* Play boss music */
    sound_play_music(MUSIC_BOSS);

    /* Start battle with boss stats */
    const BossTemplate *bt = &boss_templates[boss_id];

    /* Override normal battle init for boss */
    g_game.current_state = STATE_BATTLE;
    battle_init_boss(boss_id);
}

/* Initialize battle specifically for boss encounter */
void battle_init_boss(u8 boss_id) {
    const BossTemplate *bt = &boss_templates[boss_id];

    /* Stop scrolling */
    bg_stop_scroll();
    bullets_clear_all();

    g_battle.state = BSTATE_TRANSITION_IN;
    g_battle.turn_number = 1;
    g_battle.menu_cursor = 0;
    g_battle.anim_timer = 0;
    g_battle.result = 0;
    g_battle.trigger_enemy_type = 0;
    g_battle.xp_reward = bt->xp_reward;

    /* Player combatant */
    g_battle.player.hp = g_player.hp;
    g_battle.player.max_hp = g_player.max_hp;
    g_battle.player.mp = g_player.mp;
    g_battle.player.max_mp = g_player.max_mp;
    g_battle.player.attack = g_player.attack;
    g_battle.player.defense = g_player.defense;
    g_battle.player.speed = g_player.speed_stat;
    g_battle.player.is_player = 1;
    g_battle.player.is_defending = 0;
    g_battle.player.status_effects = STATUS_NONE;

    /* Boss combatant */
    u8 i;
    for (i = 0; i < 11 && bt->name[i]; i++) {
        g_battle.enemy.name[i] = bt->name[i];
    }
    g_battle.enemy.name[i] = 0;
    g_battle.enemy.hp = bt->max_hp;
    g_battle.enemy.max_hp = bt->max_hp;
    g_battle.enemy.mp = 50;
    g_battle.enemy.max_mp = 50;
    g_battle.enemy.attack = bt->attack;
    g_battle.enemy.defense = bt->defense;
    g_battle.enemy.speed = bt->speed;
    g_battle.enemy.is_player = 0;
    g_battle.enemy.is_defending = 0;
    g_battle.enemy.status_effects = STATUS_NONE;
    g_battle.enemy.sprite_tile = bt->sprite_tile;
    g_battle.enemy.sprite_pal = bt->sprite_pal;
}

/* Check HP thresholds and advance phase */
void boss_update_phase(void) {
    u16 hp = g_battle.enemy.hp;
    u16 max_hp = g_battle.enemy.max_hp;
    u8 hp_pct = (u8)((u32)hp * 100 / max_hp);

    u8 new_phase = 1;
    if (hp_pct <= BOSS_PHASE_4_THRESHOLD) new_phase = 4;
    else if (hp_pct <= BOSS_PHASE_3_THRESHOLD) new_phase = 3;
    else if (hp_pct <= BOSS_PHASE_2_THRESHOLD) new_phase = 2;

    if (new_phase > g_boss.current_phase) {
        g_boss.current_phase = new_phase;
        /* Phase change visual feedback */
        ui_draw_battle_message("ENEMY POWERS UP!");
        /* Flash screen or change sprite palette */
        /* sound_play_sfx(SFX_POWERUP); */
    }
}

/* Boss AI: select attack based on current phase */
u8 boss_select_attack(void) {
    const BossTemplate *bt = &boss_templates[g_boss.boss_id];
    const u8 *attacks;

    /* If charging, release the charged attack */
    if (g_boss.is_charging) {
        g_boss.is_charging = 0;
        return BOSS_ATK_HEAVY; /* Charged attack deals big damage */
    }

    /* Select attack pool based on phase */
    switch(g_boss.current_phase) {
        case 1: attacks = bt->phase1_attacks; break;
        case 2: attacks = bt->phase2_attacks; break;
        case 3: attacks = bt->phase3_attacks; break;
        case 4: attacks = bt->phase4_attacks; break;
        default: attacks = bt->phase1_attacks; break;
    }

    /* Random selection from phase attack pool */
    u8 idx = (u8)(rand() & 0x03);

    /* Prevent heal spam (max once per 3 turns) */
    if (attacks[idx] == BOSS_ATK_REPAIR && g_boss.turns_since_heal < 3) {
        return BOSS_ATK_NORMAL;
    }

    return attacks[idx];
}

/* Execute boss special attack */
void boss_execute_attack(u8 attack_id) {
    u16 base_dmg = stats_calc_damage(
        g_battle.enemy.attack, g_battle.player.defense,
        0, 3, g_player.level);

    g_boss.turns_since_heal++;

    switch(attack_id) {
        case BOSS_ATK_NORMAL:
            /* Standard attack */
            if (g_battle.player.is_defending) base_dmg >>= 1;
            if (g_battle.player.hp > base_dmg)
                g_battle.player.hp -= base_dmg;
            else
                g_battle.player.hp = 0;
            ui_draw_battle_message("ENEMY ATTACKS!");
            ui_show_damage(60, 140, base_dmg);
            break;

        case BOSS_ATK_HEAVY:
            /* Heavy attack: 2x damage */
            base_dmg <<= 1;
            if (g_boss.charge_damage > 0) {
                base_dmg = g_boss.charge_damage;
                g_boss.charge_damage = 0;
            }
            if (g_battle.player.is_defending) base_dmg >>= 1;
            if (g_battle.player.hp > base_dmg)
                g_battle.player.hp -= base_dmg;
            else
                g_battle.player.hp = 0;
            ui_draw_battle_message("HEAVY STRIKE!");
            ui_show_damage(60, 140, base_dmg);
            sound_play_sfx(SFX_BIG_EXPLOSION);
            break;

        case BOSS_ATK_MULTI:
            /* Multi-hit: 2-3 hits at 60% damage each */
            {
                u8 hits = 2 + (rand() & 0x01);
                u16 per_hit = (base_dmg * 3) >> 2; /* 75% per hit */
                u8 h;
                u16 total = 0;
                for (h = 0; h < hits; h++) {
                    u16 d = per_hit;
                    if (g_battle.player.is_defending) d >>= 1;
                    if (g_battle.player.hp > d)
                        g_battle.player.hp -= d;
                    else
                        g_battle.player.hp = 0;
                    total += d;
                }
                ui_draw_battle_message("RAPID FIRE!");
                ui_show_damage(60, 140, total);
            }
            break;

        case BOSS_ATK_DRAIN:
            /* Drain: deal damage and heal self */
            {
                u16 drain = base_dmg >> 1; /* Half damage as drain */
                if (g_battle.player.is_defending) base_dmg >>= 1;
                if (g_battle.player.hp > base_dmg)
                    g_battle.player.hp -= base_dmg;
                else
                    g_battle.player.hp = 0;
                g_battle.enemy.hp += drain;
                if (g_battle.enemy.hp > g_battle.enemy.max_hp)
                    g_battle.enemy.hp = g_battle.enemy.max_hp;
                ui_draw_battle_message("ENERGY DRAIN!");
            }
            break;

        case BOSS_ATK_STATUS:
            /* Status attack: damage + apply poison */
            {
                u16 d = base_dmg >> 1;
                if (g_battle.player.is_defending) d >>= 1;
                if (g_battle.player.hp > d)
                    g_battle.player.hp -= d;
                else
                    g_battle.player.hp = 0;
                stats_apply_status(&g_battle.player.status_effects, STATUS_POISON);
                ui_draw_battle_message("TOXIC STRIKE! POISONED!");
            }
            break;

        case BOSS_ATK_CHARGE:
            /* Charge: skip attack this turn, next attack is 3x */
            g_boss.is_charging = 1;
            g_boss.charge_damage = base_dmg * 3;
            ui_draw_battle_message("CHARGING POWER...");
            break;

        case BOSS_ATK_REPAIR:
            /* Self-repair: heal 20% max HP */
            {
                u16 heal = g_battle.enemy.max_hp / 5;
                g_battle.enemy.hp += heal;
                if (g_battle.enemy.hp > g_battle.enemy.max_hp)
                    g_battle.enemy.hp = g_battle.enemy.max_hp;
                g_boss.turns_since_heal = 0;
                ui_draw_battle_message("SELF-REPAIR!");
            }
            break;
    }
}

void boss_death_sequence(void) {
    g_boss.death_anim_timer++;

    /* Flash screen for dramatic effect */
    if (g_boss.death_anim_timer & 0x04) {
        setBrightness(15);
    } else {
        setBrightness(8);
    }

    if (g_boss.death_anim_timer > 60) {
        setBrightness(15);
        g_game.story_flags |= STORY_BOSS_DEFEATED;

        /* Trigger appropriate victory dialog */
        if (g_game.story_flags & STORY_CHOSE_TRUTH) {
            dialog_start(SEQ_VICTORY_TRUTH);
        } else {
            dialog_start(SEQ_VICTORY_LOYAL);
        }

        /* After dialog, transition to victory */
        game_change_state(STATE_VICTORY);
    }
}
```

### Battle System Modifications for Boss Mode
In `battle_state_enemy_turn()`, add boss check:
```c
void battle_state_enemy_turn(void) {
    if (g_boss.boss_id != BOSS_NONE) {
        /* Boss AI */
        boss_update_phase();
        u8 attack = boss_select_attack();
        g_battle.state = BSTATE_ENEMY_ACTION;
        g_battle.anim_timer = 0;
        g_battle.current_action.action = attack; /* Store boss attack ID */
        return;
    }
    /* ... normal enemy AI ... */
}
```

In `battle_state_enemy_action()`, add boss execution:
```c
if (g_boss.boss_id != BOSS_NONE) {
    if (g_battle.anim_timer == 0) {
        boss_execute_attack(g_battle.current_action.action);
    }
    /* ... animation timer ... */
}
```

In `battle_state_check()`, add boss death:
```c
if (g_battle.enemy.hp == 0) {
    if (g_boss.boss_id != BOSS_NONE) {
        g_battle.state = BSTATE_WIN;
        /* Boss-specific death sequence */
    }
}
```

## Acceptance Criteria
1. Sentinel (Zone 2 mini-boss) has 120 HP and escalating attack patterns
2. Final boss (Truth path) has 200 HP and uses drain/charge attacks
3. Final boss (Loyalty path) has 180 HP and uses speed/status attacks
4. Boss phases change at 75%, 50%, and 25% HP with visual feedback
5. Charge attack skips one turn then deals 3x damage
6. Self-repair heals boss for 20% max HP (limited frequency)
7. Multi-hit attacks deal 2-3 hits at 75% damage each
8. Drain attack heals boss for half damage dealt
9. Boss death triggers screen flash and victory dialog
10. Correct ending plays based on twist choice
11. XP reward is substantial (100-250 XP)

## SNES-Specific Constraints
- Boss sprite may be 64x64 (composed of 4x 32x32 sprites) or single 32x32
- setBrightness during boss death must not conflict with normal rendering
- Division for HP percentage: `hp * 100 / max_hp` is expensive - use comparison: `hp * 4 < max_hp` for 25%
- Boss attack animation timers should be consistent (30 frames per action)

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~92KB | 256KB    | ~164KB    |
| WRAM     | ~1.4KB| 128KB   | ~127KB    |
| VRAM     | ~15KB | 64KB    | ~49KB     |
| CGRAM    | 224B  | 512B    | 288B      |

## Estimated Complexity
**Complex** - Multi-phase boss AI with 7 attack types, integration with the battle system, and branching boss selection based on story choice. This is a high-value phase that makes the game feel complete.

## Agent Instructions
1. Create `src/boss.h` and `src/boss.c`
2. Update Makefile and linkfile
3. Add boss_trigger() calls in zones.c for Zone 2 and Zone 3 bosses
4. Modify battle.c to check g_boss.boss_id for boss-specific behavior
5. Test Sentinel: should be beatable at level 3-4 with items
6. Test final bosses: should be challenging but beatable at level 5-6
7. Test phase transitions: damage boss to 75%, 50%, 25% and verify messages
8. Test both story paths: truth path fights Ark Defense, loyalty fights Zyx
9. Verify boss death triggers correct ending dialog
10. Balance: if boss is too hard, reduce ATK or HP; if too easy, increase
