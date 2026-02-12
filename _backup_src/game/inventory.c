/*==============================================================================
 * Inventory System - Phase 14
 *
 * Consumable items for battle use. 8-slot inventory, max stack 9.
 * Items: HP Potions (small/large), SP Charge, ATK/DEF Boost, Full Restore.
 * Loot table rolls drops per enemy type using frame counter as PRNG.
 *
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#include "game/inventory.h"
#include "engine/vblank.h"

InvSlot g_inventory[INV_SIZE];

/*=== Item Names (non-const for PVSnesLib API compat) ===*/
static char *item_names[ITEM_COUNT] = {
    "",           /* NONE */
    "HP POT S",   /* 1 */
    "HP POT L",   /* 2 */
    "SP CHARGE",  /* 3 */
    "ATK BOOST",  /* 4 */
    "DEF BOOST",  /* 5 */
    "FULL REST",  /* 6 */
};

/*=== Item Effect Values ===*/
static const s16 item_effects[ITEM_COUNT] = {
    0,    /* NONE */
    30,   /* HP Potion S: +30 HP */
    80,   /* HP Potion L: +80 HP */
    1,    /* SP Charge: +1 SP */
    5,    /* ATK Boost: +5 ATK */
    5,    /* DEF Boost: +5 DEF */
    0,    /* Full Restore: special (full HP+SP) */
};

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

void invInit(void)
{
    u8 i;
    for (i = 0; i < INV_SIZE; i++) {
        g_inventory[i].item_id = ITEM_NONE;
        g_inventory[i].quantity = 0;
    }
    /* Starting items: 2 small HP potions */
    invAdd(ITEM_HP_POTION_S, 2);
}

u8 invAdd(u8 item_id, u8 qty)
{
    u8 i;
    if (item_id == ITEM_NONE) return 0;

    /* Try to stack with existing */
    for (i = 0; i < INV_SIZE; i++) {
        if (g_inventory[i].item_id == item_id) {
            g_inventory[i].quantity += qty;
            if (g_inventory[i].quantity > INV_MAX_STACK)
                g_inventory[i].quantity = INV_MAX_STACK;
            return 1;
        }
    }
    /* Find empty slot */
    for (i = 0; i < INV_SIZE; i++) {
        if (g_inventory[i].item_id == ITEM_NONE) {
            g_inventory[i].item_id = item_id;
            g_inventory[i].quantity = qty;
            return 1;
        }
    }
    return 0; /* Inventory full */
}

u8 invRemove(u8 item_id, u8 qty)
{
    u8 i;
    for (i = 0; i < INV_SIZE; i++) {
        if (g_inventory[i].item_id == item_id) {
            if (g_inventory[i].quantity <= qty) {
                g_inventory[i].item_id = ITEM_NONE;
                g_inventory[i].quantity = 0;
            } else {
                g_inventory[i].quantity -= qty;
            }
            return 1;
        }
    }
    return 0;
}

u8 invCount(u8 item_id)
{
    u8 i;
    for (i = 0; i < INV_SIZE; i++) {
        if (g_inventory[i].item_id == item_id)
            return g_inventory[i].quantity;
    }
    return 0;
}

char *invGetName(u8 item_id)
{
    if (item_id >= ITEM_COUNT) return item_names[0];
    return item_names[item_id];
}

s16 invGetEffect(u8 item_id)
{
    if (item_id >= ITEM_COUNT) return 0;
    return item_effects[item_id];
}

/*===========================================================================*/
/* Loot Table: Roll for item drop per enemy type                             */
/* Uses g_frame_count as PRNG seed. Returns ITEM_* or ITEM_NONE.            */
/*===========================================================================*/

u8 invRollDrop(u8 enemy_type)
{
    u8 roll;

    /* Pseudo-random: frame_count * 31 + enemy_type * 17 */
    roll = (u8)((g_frame_count * 31 + ((u16)enemy_type << 4) + enemy_type) & 0xFF);

    switch (enemy_type) {
        case 0: /* SCOUT: ~30% HP Pot S */
            if (roll < 77) return ITEM_HP_POTION_S;
            break;
        case 1: /* FIGHTER: ~25% HP Pot S, ~25% SP Charge */
            if (roll < 64) return ITEM_HP_POTION_S;
            if (roll < 128) return ITEM_SP_CHARGE;
            break;
        case 2: /* HEAVY: ~20% HP Pot L, ~20% ATK Boost, ~31% SP Charge */
            if (roll < 50) return ITEM_HP_POTION_L;
            if (roll < 100) return ITEM_ATK_BOOST;
            if (roll < 180) return ITEM_SP_CHARGE;
            break;
        case 3: /* ELITE: ~31% HP Pot L, ~20% Full Rest, ~27% DEF Boost */
            if (roll < 80) return ITEM_HP_POTION_L;
            if (roll < 130) return ITEM_FULL_RESTORE;
            if (roll < 200) return ITEM_DEF_BOOST;
            break;
    }
    return ITEM_NONE;
}
