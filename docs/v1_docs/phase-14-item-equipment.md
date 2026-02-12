# Phase 14: Item & Equipment System

## Objective
Implement a simple inventory system with consumable items (healing, buffs) and equippable weapon upgrades. Items are found as drops from defeated enemies or purchased between zones. Equipment modifies player ship stats. Consumables can be used in battle (ITEM action) or from a pause menu.

## Prerequisites
- Phase 13 (RPG Stats), Phase 11-12 (Battle system to use items in).

## Detailed Tasks

1. Create `src/game/inventory.c` - Inventory with 16 item slots, each holding an item ID and quantity.
2. Define 8 consumable item types: HP Potion (small/large), SP Charge, ATK Boost, DEF Boost, Shield, Full Restore, Revive.
3. Define 4 equippable weapon types: Basic Laser, Spread Cannon, Homing Missiles, Plasma Beam. Each modifies ATK and changes the flight-mode weapon behavior.
4. Implement item use in battle (Phase 12 ITEM action): consume from inventory, apply effect.
5. Implement equipment slots: one weapon slot. Equipping a weapon changes ATK bonus and flight weapon type.
6. Implement item drop table: each enemy type has a % chance to drop specific items on defeat.
7. Create item definitions as ROM data (name, type, effect value, description).

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/game/inventory.h
```c
#ifndef INVENTORY_H
#define INVENTORY_H

#include "game.h"

#define INVENTORY_SIZE 16
#define MAX_STACK 9

/* Item categories */
#define ICAT_NONE       0
#define ICAT_CONSUMABLE 1
#define ICAT_WEAPON     2

/* Consumable item IDs (1-8) */
#define ITEM_NONE          0
#define ITEM_HP_POTION_S   1  /* Restore 30 HP */
#define ITEM_HP_POTION_L   2  /* Restore 80 HP */
#define ITEM_SP_CHARGE     3  /* Restore 1 SP */
#define ITEM_ATK_BOOST     4  /* +5 ATK for one battle */
#define ITEM_DEF_BOOST     5  /* +5 DEF for one battle */
#define ITEM_SHIELD        6  /* Negate next hit in battle */
#define ITEM_FULL_RESTORE  7  /* Full HP + SP */
#define ITEM_REVIVE        8  /* Prevent game over once */

/* Weapon item IDs (10-13) */
#define WEAPON_BASIC_LASER  10
#define WEAPON_SPREAD       11
#define WEAPON_HOMING       12
#define WEAPON_PLASMA       13

/* Item definition (ROM data) */
typedef struct {
    u8 id;
    u8 category;    /* ICAT_CONSUMABLE or ICAT_WEAPON */
    char name[12];  /* Display name */
    s16 effect;     /* HP restored, ATK bonus, etc. */
    u8 weapon_type; /* For weapons: which flight weapon it maps to */
} ItemDef;

/* Inventory slot */
typedef struct {
    u8 item_id;
    u8 quantity;
} InventorySlot;

/* Equipment state */
typedef struct {
    u8 weapon_id;   /* Equipped weapon item ID (0=none=basic) */
    s16 atk_bonus;  /* ATK bonus from weapon */
} Equipment;

extern InventorySlot inventory[INVENTORY_SIZE];
extern Equipment equipment;

void inventoryInit(void);
u8 inventoryAdd(u8 itemId, u8 quantity);  /* Returns 1 on success */
u8 inventoryRemove(u8 itemId, u8 quantity);
u8 inventoryCount(u8 itemId);
u8 inventoryGetSlotItem(u8 slot);
void inventoryUseItem(u8 itemId);  /* Use consumable (applies effect) */
void inventoryEquipWeapon(u8 itemId);
const ItemDef* inventoryGetItemDef(u8 itemId);

/* Loot table: roll for item drop from enemy type. Returns item ID or 0 */
u8 inventoryRollDrop(u8 enemyType);

#endif
```

### J:/code/snes/snes-rpg-test/src/game/inventory.c
```c
#include "game/inventory.h"
#include "game/rpg_stats.h"
#include "engine/bullets.h"

InventorySlot inventory[INVENTORY_SIZE];
Equipment equipment;

static const ItemDef item_defs[] = {
    { ITEM_NONE, ICAT_NONE, "EMPTY", 0, 0 },
    { ITEM_HP_POTION_S, ICAT_CONSUMABLE, "HP POT S", 30, 0 },
    { ITEM_HP_POTION_L, ICAT_CONSUMABLE, "HP POT L", 80, 0 },
    { ITEM_SP_CHARGE, ICAT_CONSUMABLE, "SP CHARGE", 1, 0 },
    { ITEM_ATK_BOOST, ICAT_CONSUMABLE, "ATK BOOST", 5, 0 },
    { ITEM_DEF_BOOST, ICAT_CONSUMABLE, "DEF BOOST", 5, 0 },
    { ITEM_SHIELD, ICAT_CONSUMABLE, "SHIELD", 1, 0 },
    { ITEM_FULL_RESTORE, ICAT_CONSUMABLE, "FULL REST", 0, 0 },
    { ITEM_REVIVE, ICAT_CONSUMABLE, "REVIVE", 0, 0 },
    { 9, ICAT_NONE, "", 0, 0 }, /* padding */
    { WEAPON_BASIC_LASER, ICAT_WEAPON, "LASER", 0, 0 },
    { WEAPON_SPREAD, ICAT_WEAPON, "SPREAD", 3, 1 },
    { WEAPON_HOMING, ICAT_WEAPON, "HOMING", 2, 2 },
    { WEAPON_PLASMA, ICAT_WEAPON, "PLASMA", 5, 2 },
};

#define ITEM_DEF_COUNT (sizeof(item_defs) / sizeof(ItemDef))

void inventoryInit(void)
{
    u8 i;
    for (i = 0; i < INVENTORY_SIZE; i++) {
        inventory[i].item_id = ITEM_NONE;
        inventory[i].quantity = 0;
    }
    /* Start with 2 small HP potions */
    inventoryAdd(ITEM_HP_POTION_S, 2);

    equipment.weapon_id = 0;
    equipment.atk_bonus = 0;
}

u8 inventoryAdd(u8 itemId, u8 quantity)
{
    u8 i;
    /* Try to stack with existing */
    for (i = 0; i < INVENTORY_SIZE; i++) {
        if (inventory[i].item_id == itemId && inventory[i].quantity < MAX_STACK) {
            inventory[i].quantity += quantity;
            if (inventory[i].quantity > MAX_STACK)
                inventory[i].quantity = MAX_STACK;
            return 1;
        }
    }
    /* Find empty slot */
    for (i = 0; i < INVENTORY_SIZE; i++) {
        if (inventory[i].item_id == ITEM_NONE) {
            inventory[i].item_id = itemId;
            inventory[i].quantity = quantity;
            return 1;
        }
    }
    return 0; /* Inventory full */
}

u8 inventoryRemove(u8 itemId, u8 quantity)
{
    u8 i;
    for (i = 0; i < INVENTORY_SIZE; i++) {
        if (inventory[i].item_id == itemId) {
            if (inventory[i].quantity <= quantity) {
                inventory[i].item_id = ITEM_NONE;
                inventory[i].quantity = 0;
            } else {
                inventory[i].quantity -= quantity;
            }
            return 1;
        }
    }
    return 0;
}

u8 inventoryCount(u8 itemId)
{
    u8 i;
    for (i = 0; i < INVENTORY_SIZE; i++) {
        if (inventory[i].item_id == itemId)
            return inventory[i].quantity;
    }
    return 0;
}

void inventoryUseItem(u8 itemId)
{
    if (inventoryCount(itemId) == 0) return;
    inventoryRemove(itemId, 1);

    switch (itemId) {
        case ITEM_HP_POTION_S: rpgHealHP(30); break;
        case ITEM_HP_POTION_L: rpgHealHP(80); break;
        case ITEM_SP_CHARGE:   rpgHealSP(1); break;
        case ITEM_FULL_RESTORE:
            rpgHealHP(rpg_stats.max_hp);
            rpgHealSP(rpg_stats.max_sp);
            break;
        /* ATK_BOOST, DEF_BOOST, SHIELD handled in battle engine */
    }
}

void inventoryEquipWeapon(u8 itemId)
{
    const ItemDef *def = inventoryGetItemDef(itemId);
    if (!def || def->category != ICAT_WEAPON) return;
    equipment.weapon_id = itemId;
    equipment.atk_bonus = def->effect;
    /* Change flight weapon type */
    player_weapon.weapon_type = def->weapon_type;
}

const ItemDef* inventoryGetItemDef(u8 itemId)
{
    if (itemId < ITEM_DEF_COUNT) return &item_defs[itemId];
    return &item_defs[0];
}

u8 inventoryRollDrop(u8 enemyType)
{
    u8 roll = (u8)((g_frame_count * 31 + enemyType * 17) & 0xFF);
    /* ~30% drop rate for scouts, ~50% for fighters, ~70% for heavy/elite */
    switch (enemyType) {
        case 0: if (roll < 77) return ITEM_HP_POTION_S; break;
        case 1: if (roll < 64) return ITEM_HP_POTION_S;
                if (roll < 128) return ITEM_SP_CHARGE; break;
        case 2: if (roll < 50) return ITEM_HP_POTION_L;
                if (roll < 100) return ITEM_ATK_BOOST;
                if (roll < 180) return ITEM_SP_CHARGE; break;
        case 3: if (roll < 80) return ITEM_HP_POTION_L;
                if (roll < 130) return ITEM_FULL_RESTORE;
                if (roll < 200) return ITEM_DEF_BOOST; break;
    }
    return ITEM_NONE;
}

u8 inventoryGetSlotItem(u8 slot)
{
    if (slot >= INVENTORY_SIZE) return ITEM_NONE;
    return inventory[slot].item_id;
}
```

## Technical Specifications

### Item Effect Summary
```
ID  Name          Category    Effect
--  ----          --------    ------
1   HP POT S      Consumable  Restore 30 HP
2   HP POT L      Consumable  Restore 80 HP
3   SP CHARGE     Consumable  Restore 1 SP
4   ATK BOOST     Consumable  +5 ATK for current battle
5   DEF BOOST     Consumable  +5 DEF for current battle
6   SHIELD        Consumable  Block next damage instance
7   FULL REST     Consumable  Full HP + SP restore
8   REVIVE        Consumable  Prevent game over (auto-use)
10  LASER         Weapon      +0 ATK, basic weapon
11  SPREAD        Weapon      +3 ATK, spread shot
12  HOMING        Weapon      +2 ATK, homing missiles (future)
13  PLASMA        Weapon      +5 ATK, laser weapon type
```

### Memory Budget
```
Inventory: 16 slots * 2 bytes = 32 bytes
Equipment: 4 bytes
Item defs (ROM): 14 items * ~16 bytes = 224 bytes
Total WRAM: 36 bytes
```

## Acceptance Criteria
1. Player starts with 2 HP Potion S items.
2. inventoryAdd correctly stacks items up to MAX_STACK (9).
3. Using HP POT S in battle restores 30 HP to the player combatant.
4. Items are consumed (quantity decreases) when used.
5. Equipping SPREAD weapon changes flight weapon type to spread shot.
6. Enemy drops appear correctly based on the loot table.
7. Inventory full is handled gracefully (drop is lost, no crash).
8. Equipment ATK bonus is reflected in battle stats.

## Estimated Complexity
**Medium** - Inventory management is well-understood. Integration with battle ITEM action and equipment stat modification requires coordination across phases.
