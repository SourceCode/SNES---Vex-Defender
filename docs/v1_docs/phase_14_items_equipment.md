# Phase 14: Item & Equipment System

## Objective
Implement a simple inventory system with consumable items (healing, buffs) and passive equipment (weapon upgrades) that drop from enemies or are found during flight. Items add strategic depth to battles and reward exploration.

## Prerequisites
- Phase 11 (Battle System Core) complete
- Phase 12 (Battle UI) complete
- Phase 13 (RPG Stats) complete

## Detailed Tasks

### 1. Define Item Database
All items defined in a const ROM table.

### 2. Create Inventory System
Fixed-size array of item slots with quantity tracking.

### 3. Implement Item Usage in Battle
"ITEM" menu opens sub-menu listing available items.

### 4. Implement Equipment System
Simple passive equipment that modifies stats.

### 5. Create Item Drop System
Enemies drop items based on drop chance from enemy template.

### 6. Create Item Pickup in Flight Mode
Items appear as sprites during flight, collectible by flying over them.

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/items.h` | CREATE | Item system header |
| `src/items.c` | CREATE | Item system implementation |
| `src/battle.c` | MODIFY | Add BSTATE_ITEM_SELECT handling |
| `src/enemy.c` | MODIFY | Add item drops on kill |
| `src/ui.c` | MODIFY | Add item sub-menu rendering |

## Technical Specifications

### Item Definitions
```c
/* Item type categories */
#define ITEM_TYPE_NONE      0
#define ITEM_TYPE_HEAL_HP   1  /* Restores HP */
#define ITEM_TYPE_HEAL_MP   2  /* Restores MP */
#define ITEM_TYPE_BUFF_ATK  3  /* Temporary attack boost */
#define ITEM_TYPE_BUFF_DEF  4  /* Temporary defense boost */
#define ITEM_TYPE_CURE      5  /* Remove status effects */
#define ITEM_TYPE_REVIVE    6  /* Full heal (rare) */
#define ITEM_TYPE_EQUIP_WPN 7  /* Weapon equipment */
#define ITEM_TYPE_EQUIP_ARM 8  /* Armor equipment */

/* Item database (ROM constant) */
typedef struct {
    char name[10];         /* Display name (9 chars + null) */
    u8   type;             /* ITEM_TYPE_* */
    u16  value;            /* Effect magnitude (HP healed, ATK boost, etc.) */
    u8   duration;         /* Duration in turns for buffs (0 = instant) */
    u8   rarity;           /* 0=common, 1=uncommon, 2=rare */
} ItemTemplate;

#define ITEM_COUNT 10

const ItemTemplate item_db[ITEM_COUNT] = {
    /* name,         type,            value, dur, rarity */
    {"REPAIR KT",   ITEM_TYPE_HEAL_HP,   30,  0, 0},  /* 0: Repair Kit - heals 30 HP */
    {"ENRG CELL",   ITEM_TYPE_HEAL_MP,   20,  0, 0},  /* 1: Energy Cell - restores 20 MP */
    {"MED REPAI",   ITEM_TYPE_HEAL_HP,   60,  0, 1},  /* 2: Medium Repair - heals 60 HP */
    {"OVERDRIV",    ITEM_TYPE_BUFF_ATK,  10,  3, 1},  /* 3: Overdrive - +10 ATK for 3 turns */
    {"SHLD BOST",   ITEM_TYPE_BUFF_DEF,  10,  3, 1},  /* 4: Shield Boost - +10 DEF for 3 turns */
    {"PURIFIER",    ITEM_TYPE_CURE,       0,  0, 1},  /* 5: Purifier - cure all status */
    {"FULL REPR",   ITEM_TYPE_REVIVE,     0,  0, 2},  /* 6: Full Repair - restore all HP/MP */
    {"ION BLADE",   ITEM_TYPE_EQUIP_WPN,  5,  0, 1},  /* 7: Ion Blade - +5 ATK permanent */
    {"PLT ARMOR",   ITEM_TYPE_EQUIP_ARM,  5,  0, 1},  /* 8: Plating - +5 DEF permanent */
    {"PWR CORE",    ITEM_TYPE_EQUIP_WPN, 10,  0, 2},  /* 9: Power Core - +10 ATK permanent */
};

/* Enemy drop table: which items each enemy can drop */
/* Index by enemy type, value is item_db index (-1 = no drop) */
const s8 enemy_drops[8] = {
    -1,  /* ENEMY_NONE */
     0,  /* SCOUT: Repair Kit */
     0,  /* RAIDER: Repair Kit */
     1,  /* INTERCEPTOR: Energy Cell */
     3,  /* DESTROYER: Overdrive */
     2,  /* ELITE: Medium Repair */
     4,  /* COMMANDER: Shield Boost */
    -1,  /* ASTEROID: nothing */
};
```

### Inventory System
```c
/* Inventory slot */
typedef struct {
    u8 item_id;       /* Index into item_db (-1 = empty) */
    u8 quantity;       /* Stack count */
} InvSlot;

#define INVENTORY_SIZE 8  /* Max 8 different items */

typedef struct {
    InvSlot slots[INVENTORY_SIZE];
    u8 count;           /* Number of unique items */

    /* Equipment slots */
    s8 equipped_weapon; /* item_id or -1 */
    s8 equipped_armor;  /* item_id or -1 */
    u16 weapon_bonus;   /* ATK bonus from weapon */
    u16 armor_bonus;    /* DEF bonus from armor */
} Inventory;

extern Inventory g_inventory;
```

### items.h
```c
#ifndef ITEMS_H
#define ITEMS_H

#include <snes.h>
#include "config.h"

/* ... (types from above) ... */

extern Inventory g_inventory;
extern const ItemTemplate item_db[ITEM_COUNT];

/*--- Functions ---*/
void items_init(void);
u8   items_add(u8 item_id, u8 quantity);
u8   items_remove(u8 item_id, u8 quantity);
u8   items_get_count(u8 item_id);
void items_use_in_battle(u8 item_id);
void items_equip(u8 item_id);
void items_try_drop(u8 enemy_type);
void items_spawn_pickup(s16 x, s16 y, u8 item_id);
void items_update_pickups(void);
void items_render_pickups(void);

#endif /* ITEMS_H */
```

### items.c
```c
#include "items.h"
#include "player.h"
#include "battle.h"

Inventory g_inventory;

/* Active pickups in flight mode */
typedef struct {
    s16 x, y;
    u8  item_id;
    u8  active;
    u8  oam_id;
    u8  timer;        /* Despawn timer */
} ItemPickup;

#define MAX_PICKUPS 4
static ItemPickup pickups[MAX_PICKUPS];

#define OAM_ITEM_START 33  /* After enemies */

void items_init(void) {
    u8 i;
    g_inventory.count = 0;
    g_inventory.equipped_weapon = -1;
    g_inventory.equipped_armor = -1;
    g_inventory.weapon_bonus = 0;
    g_inventory.armor_bonus = 0;

    for (i = 0; i < INVENTORY_SIZE; i++) {
        g_inventory.slots[i].item_id = 0xFF;
        g_inventory.slots[i].quantity = 0;
    }

    /* Give player starting items */
    items_add(0, 3);  /* 3x Repair Kit */
    items_add(1, 2);  /* 2x Energy Cell */

    for (i = 0; i < MAX_PICKUPS; i++) {
        pickups[i].active = 0;
        pickups[i].oam_id = OAM_ITEM_START + i;
    }
}

u8 items_add(u8 item_id, u8 quantity) {
    u8 i;

    /* Check if already in inventory (stack) */
    for (i = 0; i < INVENTORY_SIZE; i++) {
        if (g_inventory.slots[i].item_id == item_id) {
            g_inventory.slots[i].quantity += quantity;
            if (g_inventory.slots[i].quantity > 99)
                g_inventory.slots[i].quantity = 99;
            return 1;
        }
    }

    /* Find empty slot */
    for (i = 0; i < INVENTORY_SIZE; i++) {
        if (g_inventory.slots[i].item_id == 0xFF) {
            g_inventory.slots[i].item_id = item_id;
            g_inventory.slots[i].quantity = quantity;
            g_inventory.count++;
            return 1;
        }
    }

    return 0; /* Inventory full */
}

u8 items_remove(u8 item_id, u8 quantity) {
    u8 i;
    for (i = 0; i < INVENTORY_SIZE; i++) {
        if (g_inventory.slots[i].item_id == item_id) {
            if (g_inventory.slots[i].quantity >= quantity) {
                g_inventory.slots[i].quantity -= quantity;
                if (g_inventory.slots[i].quantity == 0) {
                    g_inventory.slots[i].item_id = 0xFF;
                    g_inventory.count--;
                }
                return 1;
            }
            return 0; /* Not enough quantity */
        }
    }
    return 0; /* Item not found */
}

void items_use_in_battle(u8 item_id) {
    const ItemTemplate *item = &item_db[item_id];

    switch(item->type) {
        case ITEM_TYPE_HEAL_HP:
            g_battle.player.hp += item->value;
            if (g_battle.player.hp > g_battle.player.max_hp)
                g_battle.player.hp = g_battle.player.max_hp;
            break;

        case ITEM_TYPE_HEAL_MP:
            g_battle.player.mp += item->value;
            if (g_battle.player.mp > g_battle.player.max_mp)
                g_battle.player.mp = g_battle.player.max_mp;
            break;

        case ITEM_TYPE_BUFF_ATK:
            stats_apply_status(&g_battle.player.status_effects, STATUS_BOOST);
            g_battle.player.attack += item->value;
            break;

        case ITEM_TYPE_BUFF_DEF:
            stats_apply_status(&g_battle.player.status_effects, STATUS_SHIELD);
            g_battle.player.defense += item->value;
            break;

        case ITEM_TYPE_CURE:
            g_battle.player.status_effects = STATUS_NONE;
            break;

        case ITEM_TYPE_REVIVE:
            g_battle.player.hp = g_battle.player.max_hp;
            g_battle.player.mp = g_battle.player.max_mp;
            break;
    }

    /* Remove one from inventory */
    items_remove(item_id, 1);
}

void items_equip(u8 item_id) {
    const ItemTemplate *item = &item_db[item_id];

    if (item->type == ITEM_TYPE_EQUIP_WPN) {
        g_inventory.equipped_weapon = item_id;
        g_inventory.weapon_bonus = item->value;
        g_player.attack += item->value;
    } else if (item->type == ITEM_TYPE_EQUIP_ARM) {
        g_inventory.equipped_armor = item_id;
        g_inventory.armor_bonus = item->value;
        g_player.defense += item->value;
    }

    items_remove(item_id, 1);
}

/* Called when an enemy dies - roll for item drop */
void items_try_drop(u8 enemy_type) {
    const EnemyTemplate *et = &enemy_templates[enemy_type];
    s8 drop_id = enemy_drops[enemy_type];

    if (drop_id < 0) return; /* No drop possible */

    /* Roll drop chance */
    u8 roll = (u8)(rand() & 0xFF);
    if (roll < et->drop_chance) {
        /* Drop item! In flight mode, spawn a pickup sprite */
        /* In battle mode, directly add to inventory */
        if (g_game.current_state == STATE_BATTLE) {
            items_add((u8)drop_id, 1);
        }
        /* Flight mode drops handled by items_spawn_pickup */
    }
}

void items_spawn_pickup(s16 x, s16 y, u8 item_id) {
    u8 i;
    for (i = 0; i < MAX_PICKUPS; i++) {
        if (!pickups[i].active) {
            pickups[i].x = x;
            pickups[i].y = y;
            pickups[i].item_id = item_id;
            pickups[i].active = 1;
            pickups[i].timer = 180; /* 3 seconds to collect */
            return;
        }
    }
}

void items_update_pickups(void) {
    u8 i;
    for (i = 0; i < MAX_PICKUPS; i++) {
        if (!pickups[i].active) continue;

        /* Drift downward (with scroll) */
        pickups[i].y += 1;

        /* Despawn timer */
        pickups[i].timer--;
        if (pickups[i].timer == 0 || pickups[i].y > SCREEN_HEIGHT + 16) {
            pickups[i].active = 0;
            continue;
        }

        /* Check collection by player (simple distance check) */
        s16 dx = pickups[i].x - g_player.x;
        s16 dy = pickups[i].y - g_player.y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < 24 && dy < 24) {
            /* Collected! */
            items_add(pickups[i].item_id, 1);
            pickups[i].active = 0;
            /* Play collect sound (Phase 17) */
        }
    }
}

void items_render_pickups(void) {
    u8 i;
    for (i = 0; i < MAX_PICKUPS; i++) {
        u16 oam_offset = pickups[i].oam_id * 4;
        if (!pickups[i].active) {
            oamSetEx(oam_offset, OBJ_SMALL, OBJ_HIDE);
            continue;
        }

        /* Blink when about to expire */
        if (pickups[i].timer < 60 && (pickups[i].timer & 0x04)) {
            oamSetEx(oam_offset, OBJ_SMALL, OBJ_HIDE);
            continue;
        }

        oamSet(oam_offset, pickups[i].x, pickups[i].y, 3, 0, 0,
               44, PAL_SPR_ITEM); /* Item sprite tile */
        oamSetEx(oam_offset, OBJ_SMALL, OBJ_SHOW);
    }
}
```

## Acceptance Criteria
1. Player starts with 3 Repair Kits and 2 Energy Cells
2. Items stack correctly in inventory (adding duplicates increases count)
3. Using Repair Kit in battle heals 30 HP
4. Using Energy Cell restores 20 MP
5. Equipment items permanently boost stats
6. Enemy drops appear based on drop_chance roll
7. Items collected during flight mode add to inventory
8. Item pickups blink and despawn after 3 seconds
9. Inventory max is 8 unique items (additional drops are lost)
10. Battle item sub-menu shows available items with quantities

## SNES-Specific Constraints
- Item names limited to 9 chars to fit in battle menu
- Inventory stored in WRAM - 8 slots x 2 bytes = 16 bytes
- Item templates in ROM (const) - 10 items x ~14 bytes = 140 bytes
- Item pickup sprites share the ITEM OAM slots and palettes

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~56KB | 256KB    | ~200KB    |
| WRAM     | ~1KB  | 128KB   | ~127KB    |
| VRAM     | ~14KB | 64KB    | ~50KB     |
| CGRAM    | 192B  | 512B    | 320B      |

## Estimated Complexity
**Medium** - Inventory management is standard array operations. The main complexity is integrating item drops between flight and battle modes.

## Agent Instructions
1. Create `src/items.h` and `src/items.c`
2. Update Makefile and linkfile
3. Call `items_init()` in game_init()
4. Add BSTATE_ITEM_SELECT handling in battle.c
5. In enemy_kill(), call `items_try_drop(e->type)`
6. Add `items_update_pickups()` and `items_render_pickups()` to flight mode loop
7. Test: use Repair Kit in battle, verify HP increases by 30
8. Test: kill enemies, verify items drop based on chance
9. Test: collect item pickup in flight mode, verify added to inventory
10. Test: fill inventory to 8 items, verify 9th item is rejected gracefully
