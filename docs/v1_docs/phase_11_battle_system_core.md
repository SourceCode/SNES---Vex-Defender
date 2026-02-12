# Phase 11: Turn-Based Battle System - Core Engine

## Objective
Implement the core turn-based battle engine that activates when the player encounters specific enemies during flight. The game pauses scrolling, transitions to a battle screen, and players select actions (Attack, Special, Item, Defend) in classic RPG style. This is the heart of the RPG-shooter hybrid.

## Prerequisites
- Phase 5 (Sprite Engine) complete
- Phase 9 (Enemy System) complete
- Phase 10 (Collision Detection) complete

## Detailed Tasks

### 1. Design Battle Flow State Machine
```
FLIGHT → Enemy Encounter →
  BATTLE_INIT (stop scroll, transition screen) →
  BATTLE_START (show "ENCOUNTER!" text) →
  BATTLE_PLAYER_TURN (show menu, wait for input) →
  BATTLE_PLAYER_ACTION (animate player attack) →
  BATTLE_ENEMY_TURN (enemy AI selects action) →
  BATTLE_ENEMY_ACTION (animate enemy attack) →
  BATTLE_CHECK (check win/lose conditions) →
  [Loop back to PLAYER_TURN or...]
  BATTLE_WIN (award XP, items) →
  BATTLE_END (transition back to flight) →
FLIGHT
```

### 2. Implement Battle Trigger System
Certain enemies (or scroll triggers) initiate battles instead of real-time combat.

### 3. Create Battle Data Structures
Track combatants, turn order, action queue.

### 4. Implement Turn Resolution
Calculate damage, apply effects, check win/lose.

### 5. Create Battle Transition Effects
Visual transition from flight to battle and back.

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/battle.h` | CREATE | Battle system header |
| `src/battle.c` | CREATE | Battle system implementation |
| `src/game.c` | MODIFY | Add STATE_BATTLE handling |
| `src/main.c` | MODIFY | Integrate battle state |
| `Makefile` | MODIFY | Add battle.obj |
| `data/linkfile` | MODIFY | Add battle.obj |

## Technical Specifications

### Battle State Machine
```c
/* Battle sub-states */
#define BSTATE_NONE           0
#define BSTATE_INIT           1
#define BSTATE_TRANSITION_IN  2
#define BSTATE_START          3
#define BSTATE_PLAYER_TURN    4
#define BSTATE_PLAYER_ACTION  5
#define BSTATE_ENEMY_TURN     6
#define BSTATE_ENEMY_ACTION   7
#define BSTATE_CHECK          8
#define BSTATE_WIN            9
#define BSTATE_LOSE          10
#define BSTATE_TRANSITION_OUT 11
#define BSTATE_ITEM_SELECT   12

/* Battle actions */
#define BACT_ATTACK   0
#define BACT_SPECIAL  1
#define BACT_ITEM     2
#define BACT_DEFEND   3
#define BACT_COUNT    4

/* Battle action names (for UI) */
const char* battle_action_names[BACT_COUNT] = {
    "ATTACK",
    "SPECIAL",
    "ITEM",
    "DEFEND"
};
```

### Battle Data Structures
```c
/* Combatant stats (used during battle) */
typedef struct {
    char name[12];         /* Display name */
    u16 hp;
    u16 max_hp;
    u16 mp;
    u16 max_mp;
    u16 attack;
    u16 defense;
    u16 speed;
    u8  is_player;         /* 1 = player, 0 = enemy */
    u8  is_defending;      /* Defend stance active */
    u8  status_effects;    /* Bitfield: poison, stun, etc. */
    u8  sprite_tile;       /* Sprite tile for battle display */
    u8  sprite_pal;        /* Sprite palette */
} BattleCombatant;

/* Pending action */
typedef struct {
    u8  action;            /* BACT_* */
    u8  actor_idx;         /* Index of acting combatant */
    u8  target_idx;        /* Index of target combatant */
    u8  item_id;           /* Item ID if action is ITEM */
} BattleAction;

/* Main battle state */
typedef struct {
    u8  state;             /* BSTATE_* sub-state */
    u8  turn_number;
    u8  menu_cursor;       /* Current menu selection */
    u8  anim_timer;        /* Animation frame counter */
    u8  result;            /* 0=ongoing, 1=win, 2=lose */

    BattleCombatant player;
    BattleCombatant enemy;

    BattleAction current_action;

    /* Enemy from the overworld that triggered this battle */
    u8  trigger_enemy_type;
    u16 xp_reward;
    u8  item_drop;

    /* UI state */
    u8  message_timer;     /* How long to show result messages */
    char message[32];      /* Current battle message */
} BattleState;
```

### battle.h
```c
#ifndef BATTLE_H
#define BATTLE_H

#include <snes.h>
#include "config.h"

/* ... (state defines and structs from above) ... */

extern BattleState g_battle;

/*--- Functions ---*/
void battle_init(u8 enemy_type);
void battle_update(void);
void battle_render(void);
void battle_end(void);

/* Internal state handlers */
void battle_state_player_turn(void);
void battle_state_player_action(void);
void battle_state_enemy_turn(void);
void battle_state_enemy_action(void);
void battle_state_check(void);

/* Damage calculation */
u16 battle_calc_damage(BattleCombatant *attacker, BattleCombatant *defender, u8 is_special);

/* Battle trigger (called from flight mode) */
void battle_trigger(u8 enemy_type);

#endif /* BATTLE_H */
```

### battle.c (Core Implementation)
```c
#include "battle.h"
#include "player.h"
#include "enemy.h"
#include "input.h"
#include "scroll.h"

BattleState g_battle;

/* Start a battle against an enemy type */
void battle_trigger(u8 enemy_type) {
    /* Transition game state */
    g_game.current_state = STATE_BATTLE;

    /* Initialize battle */
    battle_init(enemy_type);
}

void battle_init(u8 enemy_type) {
    const EnemyTemplate *et = &enemy_templates[enemy_type];

    /* Stop background scrolling */
    bg_stop_scroll();

    /* Clear all flight-mode bullets */
    bullets_clear_all();

    /* Set up battle state */
    g_battle.state = BSTATE_TRANSITION_IN;
    g_battle.turn_number = 1;
    g_battle.menu_cursor = 0;
    g_battle.anim_timer = 0;
    g_battle.result = 0;
    g_battle.trigger_enemy_type = enemy_type;
    g_battle.xp_reward = et->xp_reward;
    g_battle.item_drop = 0;
    g_battle.message_timer = 0;

    /* Copy player stats into battle combatant */
    /* strncpy not available, manual copy */
    g_battle.player.name[0] = 'V'; g_battle.player.name[1] = 'E';
    g_battle.player.name[2] = 'X'; g_battle.player.name[3] = 0;
    g_battle.player.hp = g_player.hp;
    g_battle.player.max_hp = g_player.max_hp;
    g_battle.player.mp = g_player.mp;
    g_battle.player.max_mp = g_player.max_mp;
    g_battle.player.attack = g_player.attack;
    g_battle.player.defense = g_player.defense;
    g_battle.player.speed = g_player.speed_stat;
    g_battle.player.is_player = 1;
    g_battle.player.is_defending = 0;
    g_battle.player.status_effects = 0;

    /* Set up enemy combatant from template */
    g_battle.enemy.name[0] = 'E'; g_battle.enemy.name[1] = 'N';
    g_battle.enemy.name[2] = 'M'; g_battle.enemy.name[3] = 'Y';
    g_battle.enemy.name[4] = 0;
    g_battle.enemy.hp = et->hp;
    g_battle.enemy.max_hp = et->hp;
    g_battle.enemy.mp = 20;
    g_battle.enemy.max_mp = 20;
    g_battle.enemy.attack = et->attack;
    g_battle.enemy.defense = et->defense;
    g_battle.enemy.speed = et->speed * 3;
    g_battle.enemy.is_player = 0;
    g_battle.enemy.is_defending = 0;
    g_battle.enemy.status_effects = 0;
    g_battle.enemy.sprite_tile = et->tile_offset;
    g_battle.enemy.sprite_pal = et->palette;
}

/* Main battle update - dispatches to sub-state handlers */
void battle_update(void) {
    switch(g_battle.state) {
        case BSTATE_TRANSITION_IN:
            /* Fade/transition effect */
            g_battle.anim_timer++;
            if (g_battle.anim_timer > 30) {
                g_battle.state = BSTATE_START;
                g_battle.anim_timer = 0;
            }
            break;

        case BSTATE_START:
            /* Show "ENCOUNTER!" message briefly */
            g_battle.anim_timer++;
            if (g_battle.anim_timer > 60) {
                g_battle.state = BSTATE_PLAYER_TURN;
                g_battle.anim_timer = 0;
            }
            break;

        case BSTATE_PLAYER_TURN:
            battle_state_player_turn();
            break;

        case BSTATE_PLAYER_ACTION:
            battle_state_player_action();
            break;

        case BSTATE_ENEMY_TURN:
            battle_state_enemy_turn();
            break;

        case BSTATE_ENEMY_ACTION:
            battle_state_enemy_action();
            break;

        case BSTATE_CHECK:
            battle_state_check();
            break;

        case BSTATE_WIN:
            g_battle.anim_timer++;
            if (g_battle.anim_timer > 90) {
                battle_end();
            }
            break;

        case BSTATE_LOSE:
            g_battle.anim_timer++;
            if (g_battle.anim_timer > 90) {
                g_game.current_state = STATE_GAMEOVER;
            }
            break;

        case BSTATE_TRANSITION_OUT:
            g_battle.anim_timer++;
            if (g_battle.anim_timer > 30) {
                g_game.current_state = STATE_FLIGHT;
                bg_resume_scroll();
            }
            break;
    }
}

/* Player's turn - menu navigation */
void battle_state_player_turn(void) {
    /* Navigate menu with D-pad */
    if (input_is_pressed(KEY_UP)) {
        if (g_battle.menu_cursor > 0) g_battle.menu_cursor--;
    }
    if (input_is_pressed(KEY_DOWN)) {
        if (g_battle.menu_cursor < BACT_COUNT - 1) g_battle.menu_cursor++;
    }

    /* Confirm action */
    if (input_is_pressed(KEY_A)) {
        g_battle.current_action.action = g_battle.menu_cursor;
        g_battle.current_action.actor_idx = 0; /* Player */
        g_battle.current_action.target_idx = 1; /* Enemy */

        /* Validate action */
        if (g_battle.menu_cursor == BACT_SPECIAL && g_battle.player.mp < 10) {
            /* Not enough MP - flash message and stay in menu */
            return;
        }
        if (g_battle.menu_cursor == BACT_ITEM) {
            g_battle.state = BSTATE_ITEM_SELECT;
            return;
        }

        g_battle.state = BSTATE_PLAYER_ACTION;
        g_battle.anim_timer = 0;
    }
}

/* Execute player's chosen action */
void battle_state_player_action(void) {
    if (g_battle.anim_timer == 0) {
        /* Execute on first frame */
        switch(g_battle.current_action.action) {
            case BACT_ATTACK: {
                u16 dmg = battle_calc_damage(&g_battle.player, &g_battle.enemy, 0);
                if (g_battle.enemy.hp > dmg)
                    g_battle.enemy.hp -= dmg;
                else
                    g_battle.enemy.hp = 0;
                break;
            }
            case BACT_SPECIAL: {
                u16 dmg = battle_calc_damage(&g_battle.player, &g_battle.enemy, 1);
                if (g_battle.enemy.hp > dmg)
                    g_battle.enemy.hp -= dmg;
                else
                    g_battle.enemy.hp = 0;
                g_battle.player.mp -= 10;
                break;
            }
            case BACT_DEFEND:
                g_battle.player.is_defending = 1;
                break;
        }
    }

    /* Animation delay */
    g_battle.anim_timer++;
    if (g_battle.anim_timer > 30) {
        g_battle.state = BSTATE_CHECK;
        g_battle.anim_timer = 0;
    }
}

/* Enemy AI selects and executes action */
void battle_state_enemy_turn(void) {
    /* Simple AI: mostly attack, occasionally use special */
    u8 r = (u8)(rand() & 0xFF);

    if (r < 200) {
        /* Normal attack (78% chance) */
        g_battle.current_action.action = BACT_ATTACK;
    } else if (r < 240 && g_battle.enemy.mp >= 10) {
        /* Special attack (16% chance if has MP) */
        g_battle.current_action.action = BACT_SPECIAL;
    } else {
        /* Defend (6% chance) */
        g_battle.current_action.action = BACT_DEFEND;
    }

    g_battle.current_action.actor_idx = 1; /* Enemy */
    g_battle.current_action.target_idx = 0; /* Player */
    g_battle.state = BSTATE_ENEMY_ACTION;
    g_battle.anim_timer = 0;
}

void battle_state_enemy_action(void) {
    if (g_battle.anim_timer == 0) {
        switch(g_battle.current_action.action) {
            case BACT_ATTACK: {
                u16 dmg = battle_calc_damage(&g_battle.enemy, &g_battle.player, 0);
                /* Defending halves damage */
                if (g_battle.player.is_defending) dmg >>= 1;
                if (g_battle.player.hp > dmg)
                    g_battle.player.hp -= dmg;
                else
                    g_battle.player.hp = 0;
                break;
            }
            case BACT_SPECIAL: {
                u16 dmg = battle_calc_damage(&g_battle.enemy, &g_battle.player, 1);
                if (g_battle.player.is_defending) dmg >>= 1;
                if (g_battle.player.hp > dmg)
                    g_battle.player.hp -= dmg;
                else
                    g_battle.player.hp = 0;
                g_battle.enemy.mp -= 10;
                break;
            }
            case BACT_DEFEND:
                g_battle.enemy.is_defending = 1;
                break;
        }
    }

    g_battle.anim_timer++;
    if (g_battle.anim_timer > 30) {
        /* Clear defend status at end of turn */
        g_battle.player.is_defending = 0;
        g_battle.enemy.is_defending = 0;
        g_battle.state = BSTATE_CHECK;
        g_battle.anim_timer = 0;
    }
}

/* Check win/lose conditions */
void battle_state_check(void) {
    if (g_battle.enemy.hp == 0) {
        g_battle.state = BSTATE_WIN;
        g_battle.anim_timer = 0;
        return;
    }
    if (g_battle.player.hp == 0) {
        g_battle.state = BSTATE_LOSE;
        g_battle.anim_timer = 0;
        return;
    }

    /* Next turn */
    g_battle.turn_number++;

    /* Determine turn order by speed */
    if (g_battle.player.speed >= g_battle.enemy.speed) {
        g_battle.state = BSTATE_PLAYER_TURN;
    } else {
        g_battle.state = BSTATE_ENEMY_TURN;
    }
}

/* Damage formula */
u16 battle_calc_damage(BattleCombatant *attacker, BattleCombatant *defender, u8 is_special) {
    u16 base = attacker->attack;
    u16 def = defender->defense;

    if (is_special) {
        base = (base * 3) >> 1; /* 1.5x damage for special */
    }

    /* Damage = Attack - Defense/2, minimum 1 */
    u16 damage = base - (def >> 1);
    if (damage == 0 || damage > 1000) damage = 1; /* Underflow protection + min 1 */

    /* Random variance: +/- 12.5% (multiply by 7/8 to 9/8) */
    u8 r = (u8)(rand() & 0x03); /* 0-3 */
    damage = (damage * (7 + r)) >> 3;

    if (damage == 0) damage = 1;
    return damage;
}

/* End battle, return to flight */
void battle_end(void) {
    /* Copy battle HP/MP back to player */
    g_player.hp = g_battle.player.hp;
    g_player.mp = g_battle.player.mp;

    /* Award XP */
    g_player.xp += g_battle.xp_reward;

    /* Restore MP partially after battle */
    g_player.mp += 5;
    if (g_player.mp > g_player.max_mp) g_player.mp = g_player.max_mp;

    /* Transition back to flight */
    g_battle.state = BSTATE_TRANSITION_OUT;
    g_battle.anim_timer = 0;
}
```

## Acceptance Criteria
1. Encountering a battle-trigger enemy transitions smoothly to battle mode
2. Battle menu shows 4 options: ATTACK, SPECIAL, ITEM, DEFEND
3. D-pad navigates menu, A confirms selection
4. Attack deals damage to enemy based on damage formula
5. Special deals 1.5x damage and costs 10 MP
6. Defend halves incoming damage for one turn
7. Enemy AI selects actions with weighted randomness
8. Battle ends when either combatant reaches 0 HP
9. Winning awards XP and returns to flight mode
10. Losing transitions to game over screen
11. Player HP/MP persist between flight and battle

## SNES-Specific Constraints
- No floating-point in damage formula - use shift operations for multiply/divide
- String handling is limited - use fixed-size char arrays, no strlen/strcmp
- rand() quality is low on SNES - acceptable for enemy AI
- Battle screen may need to reconfigure BG layers (hide BG1 starfield, show BG2 battle bg)
- All state transition animations should be frame-counted, not time-based

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~48KB | 256KB    | ~208KB    |
| WRAM     | ~900B | 128KB   | ~127KB    |
| VRAM     | ~10KB | 64KB    | ~54KB     |
| CGRAM    | 128B  | 512B    | 384B      |

## Estimated Complexity
**Complex** - This is the largest single system. The state machine has many states and transitions. Damage calculation, turn order, and the flight-to-battle transition all need careful implementation.

## Agent Instructions
1. Create `src/battle.h` and `src/battle.c`
2. Update Makefile and linkfile
3. In game.c, add STATE_BATTLE case that calls `battle_update()`
4. For testing, add a keyboard shortcut (Select button) to force-trigger a test battle
5. Test the full battle flow: menu → attack → enemy turn → check → repeat
6. Verify damage formula produces reasonable numbers (not 0, not thousands)
7. Test win condition: enemy HP reaches 0, XP awarded, return to flight
8. Test lose condition: player HP reaches 0, game over triggered
9. Verify HP/MP carry over correctly between flight and battle modes
