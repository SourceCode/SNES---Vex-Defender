/*==============================================================================
 * Inventory System - Phase 14
 *
 * Consumable items for battle use. 8-slot inventory, max stack 9.
 * Items: HP Potions (small/large), SP Charge, ATK/DEF Boost, Full Restore.
 * Loot table rolls drops per enemy type using frame counter as PRNG.
 *
 * The inventory is always kept "compacted": occupied slots are contiguous
 * from index 0.  When a slot is emptied, all subsequent slots shift down
 * to fill the gap.  This means encountering item_id == ITEM_NONE while
 * iterating guarantees there are no more items, enabling early exits.
 *
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#include "game/inventory.h"
#include "game/rpg_stats.h"
#include "engine/vblank.h"

/* Global inventory array: 8 slots, accessible by battle.c and save.c */
InvSlot g_inventory[INV_SIZE];

/*=== Item Names (non-const for PVSnesLib API compat) ===*/
/* PVSnesLib's consoleDrawText() takes a non-const char*, so these must
 * not be declared const.  Indexed by ITEM_* ID. */
static char *item_names[ITEM_COUNT] = {
    "",           /* ITEM_NONE (empty slot) */
    "HP POT S",   /* ITEM_HP_POTION_S: Small healing potion */
    "HP POT L",   /* ITEM_HP_POTION_L: Large healing potion */
    "SP CHARGE",  /* ITEM_SP_CHARGE: Restores special points */
    "ATK BOOST",  /* ITEM_ATK_BOOST: Temporary attack buff */
    "DEF BOOST",  /* ITEM_DEF_BOOST: Temporary defense buff */
    "FULL REST",  /* ITEM_FULL_RESTORE: Full HP + SP recovery */
};

/*=== Item Effect Values ===*/
/* Primary numeric effect per item, used by battle.c when an item is consumed.
 * For HP potions, this is the amount of HP restored.
 * For SP Charge, this is SP points restored (1).
 * For ATK/DEF Boost, this is the stat bonus applied for the current battle.
 * Full Restore has effect 0 because it is handled as a special case
 * (restores HP to max_hp AND SP to max_sp). */
static const s16 item_effects[ITEM_COUNT] = {
    0,    /* ITEM_NONE */
    30,   /* HP Potion S: +30 HP */
    80,   /* HP Potion L: +80 HP */
    1,    /* SP Charge: +1 SP */
    5,    /* ATK Boost: +5 ATK for current battle */
    5,    /* DEF Boost: +5 DEF for current battle */
    0,    /* Full Restore: special case (full HP+SP, handled in battle.c) */
};

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

/*
 * invInit - Reset inventory to the new-game starting state.
 * Clears all 8 slots to empty, then adds 2 small HP potions as starter items
 * so the player has some healing available for early battles.
 */
void invInit(void)
{
    u8 i;
    for (i = 0; i < INV_SIZE; i++) {
        g_inventory[i].item_id = ITEM_NONE;
        g_inventory[i].quantity = 0;
    }
    /* Starting items: 2 small HP potions for early-game survivability */
    invAdd(ITEM_HP_POTION_S, 2);
}

/*
 * invAdd - Add qty units of item_id to the inventory.
 *
 * First attempts to stack with an existing slot of the same item type.
 * If the item is already present, its quantity increases (capped at
 * INV_MAX_STACK = 9 to keep the UI single-digit).
 *
 * If the item is not found, a new empty slot is used.
 *
 * The first loop exploits the compacted-inventory invariant: if we hit
 * ITEM_NONE, there are no more occupied slots to check, so we break
 * early and fall through to the empty-slot search.
 *
 * item_id: The ITEM_* type to add (must not be ITEM_NONE).
 * qty:     Number of units to add.
 * Returns: 1 on success, 0 if inventory is completely full.
 */
u8 invAdd(u8 item_id, u8 qty)
{
    u8 i;
    if (item_id == ITEM_NONE) return 0;

    /* Try to stack with existing slot (compacted: ITEM_NONE = end of items) */
    for (i = 0; i < INV_SIZE; i++) {
        if (g_inventory[i].item_id == ITEM_NONE) break;  /* No more items to check */
        if (g_inventory[i].item_id == item_id) {
            /* Found existing stack: merge quantities, cap at max */
            g_inventory[i].quantity += qty;
            if (g_inventory[i].quantity > INV_MAX_STACK)
                g_inventory[i].quantity = INV_MAX_STACK;
            return 1;
        }
    }
    /* Item not found in existing slots; find first empty slot */
    for (i = 0; i < INV_SIZE; i++) {
        if (g_inventory[i].item_id == ITEM_NONE) {
            g_inventory[i].item_id = item_id;
            g_inventory[i].quantity = qty;
            return 1;
        }
    }
    /* #190: Inventory full - convert to 10 credits instead of losing item */
    rpg_stats.credits += 10;
    return 2; /* 2 = converted to credits */
}

/*
 * invRemove - Remove qty units of item_id from the inventory.
 *
 * Searches for the item.  If found and quantity would drop to zero (or
 * below), the slot is cleared and all subsequent slots shift down by one
 * to maintain the compacted-inventory invariant.  The shift loop uses an
 * early exit when it encounters ITEM_NONE (no more items to move).
 *
 * If the item has more than qty remaining, only the quantity is decremented.
 *
 * item_id: The ITEM_* type to remove.
 * qty:     Number of units to remove.
 * Returns: 1 if the item was found, 0 if not present.
 */
u8 invRemove(u8 item_id, u8 qty)
{
    u8 i, j;
    for (i = 0; i < INV_SIZE; i++) {
        if (g_inventory[i].item_id == item_id) {
            if (g_inventory[i].quantity <= qty) {
                /* Quantity exhausted: clear this slot and compact the array.
                 * Shift all items after index i down by one position.
                 * Early exit when ITEM_NONE reached (compacted invariant). */
                for (j = i; j < INV_SIZE - 1; j++) {
                    g_inventory[j] = g_inventory[j + 1];
                    if (g_inventory[j].item_id == ITEM_NONE) break;
                }
                /* Ensure the last slot is explicitly cleared */
                g_inventory[INV_SIZE - 1].item_id = ITEM_NONE;
                g_inventory[INV_SIZE - 1].quantity = 0;
            } else {
                /* Still has remaining quantity after removal */
                g_inventory[i].quantity -= qty;
            }
            return 1;
        }
    }
    return 0;  /* Item not found in inventory */
}

/*
 * invCount - Get the current quantity of a specific item.
 *
 * Uses the compacted-inventory early exit: returns 0 immediately when
 * ITEM_NONE is encountered, since no items exist past that point.
 *
 * item_id: The ITEM_* type to query.
 * Returns: Quantity of that item (0 if not present).
 */
u8 invCount(u8 item_id)
{
    u8 i;
    for (i = 0; i < INV_SIZE; i++) {
        if (g_inventory[i].item_id == ITEM_NONE) return 0;  /* Compacted: no more items */
        if (g_inventory[i].item_id == item_id)
            return g_inventory[i].quantity;
    }
    return 0;
}

/*
 * invGetName - Look up the display name for an item ID.
 *
 * Returns a pointer to a static string for rendering with consoleDrawText().
 * Out-of-range IDs return the empty string (same as ITEM_NONE).
 *
 * item_id: The ITEM_* type to look up.
 * Returns: Non-const char* to the item name string.
 */
char *invGetName(u8 item_id)
{
    if (item_id >= ITEM_COUNT) return item_names[0];
    return item_names[item_id];
}

/*
 * invGetEffect - Look up the primary effect value for an item ID.
 *
 * item_id: The ITEM_* type to look up.
 * Returns: The numeric effect (e.g., 30 for HP_POTION_S), or 0 for
 *          invalid/special-case items.
 */
s16 invGetEffect(u8 item_id)
{
    if (item_id >= ITEM_COUNT) return 0;
    return item_effects[item_id];
}

/*===========================================================================*/
/* Loot Table                                                                */
/*                                                                           */
/* Roll for an item drop after defeating an enemy.                           */
/* Uses g_frame_count (VBlank counter) as a simple PRNG seed because the     */
/* exact frame on which an enemy dies is effectively random from the         */
/* player's perspective.  No need for a proper PRNG on SNES given the        */
/* limited entropy requirements of a loot table.                             */
/*                                                                           */
/* Each enemy type has a different probability distribution:                  */
/*   Scout (type 0):  30% HP Pot S, 70% nothing                             */
/*   Fighter (type 1): 25% HP Pot S, 25% SP Charge, 50% nothing             */
/*   Heavy (type 2):  ~20% HP Pot L, ~20% ATK Boost, ~31% SP Charge         */
/*   Elite (type 3):  ~31% HP Pot L, ~20% Full Rest, ~27% DEF Boost         */
/*                                                                           */
/* Harder enemies drop better and more frequent items to reward progression. */
/*===========================================================================*/

/*
 * invRollDrop - Roll the loot table for a random item drop.
 *
 * The pseudo-random roll is computed as:
 *   roll = (frame_count * 31 + enemy_type * 17) & 0xFF
 * The multiplication by primes and masking to 8 bits produces a decently
 * distributed value in [0, 255].  The (enemy_type << 4) + enemy_type
 * computes enemy_type * 17 without a multiply instruction (optimization
 * for the 65816 which has no MUL opcode).
 *
 * enemy_type: 0=Scout, 1=Fighter, 2=Heavy, 3=Elite.
 * Returns:    An ITEM_* ID on a successful drop, or ITEM_NONE for no drop.
 */
/* #149: Drop pity timer - consecutive battles with no drop */
static u8 drop_miss_streak = 0;

u8 invRollDrop(u8 enemy_type)
{
    u8 roll;
    u8 result;

    /* Pseudo-random: frame_count * 31 + enemy_type * 17. */
    roll = (u8)((g_frame_count * 31 + ((u16)enemy_type << 4) + enemy_type) & 0xFF);

    result = ITEM_NONE;
    switch (enemy_type) {
        case 0:
            if (roll < 77) result = ITEM_HP_POTION_S;
            break;
        case 1:
            if (roll < 64) result = ITEM_HP_POTION_S;
            else if (roll < 128) result = ITEM_SP_CHARGE;
            break;
        case 2:
            if (roll < 50) result = ITEM_HP_POTION_L;
            else if (roll < 100) result = ITEM_ATK_BOOST;
            else if (roll < 180) result = ITEM_SP_CHARGE;
            break;
        case 3:
            if (roll < 80) result = ITEM_HP_POTION_L;
            else if (roll < 130) result = ITEM_FULL_RESTORE;
            else if (roll < 200) result = ITEM_DEF_BOOST;
            break;
    }

    /* #149: Pity timer - guaranteed drop after 3 consecutive misses */
    if (result == ITEM_NONE) {
        drop_miss_streak++;
        if (drop_miss_streak >= 3) {
            /* Force a drop from type-appropriate table */
            if (enemy_type >= 2) result = ITEM_HP_POTION_L;
            else result = ITEM_HP_POTION_S;
            drop_miss_streak = 0;
        }
    } else {
        drop_miss_streak = 0;
    }

    return result;
}

/*
 * invResetPityTimer - Reset the drop pity timer (#149).
 * Called on save/load to reset the consecutive miss counter.
 */
void invResetPityTimer(void)
{
    drop_miss_streak = 0;
}
