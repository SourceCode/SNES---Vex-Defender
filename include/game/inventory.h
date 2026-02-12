/*==============================================================================
 * Inventory System - Phase 14
 *
 * Simple consumable item system for battle use:
 *   - 8-slot inventory with max stack of 9 per item type
 *   - 6 consumable item types (HP potions, SP charge, stat boosts, full restore)
 *   - Items are used during turn-based battle via the ITEM sub-menu
 *   - Enemy kills roll the loot table for random drops (invRollDrop)
 *
 * The inventory is kept "compacted": all occupied slots are contiguous
 * starting from index 0, so encountering ITEM_NONE means "no more items".
 * This allows early-exit optimizations in search loops.
 *
 * No equipment or weapon items this phase; the bullet system already has
 * weapon cycling via L/R shoulder buttons.
 *
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#ifndef INVENTORY_H
#define INVENTORY_H

#include "game.h"

/*=== Inventory Limits ===*/
#define INV_SIZE       8   /* Maximum number of distinct item slots */
#define INV_MAX_STACK  9   /* Maximum quantity per slot (single digit for UI) */

/*=== Item IDs (consumables only) ===*/
/* Each ID corresponds to an index into the item_names[] and item_effects[]
 * lookup tables in inventory.c.  Values must be contiguous starting from 0. */
#define ITEM_NONE          0   /* Empty slot sentinel */
#define ITEM_HP_POTION_S   1   /* Restore 30 HP (common early-game drop) */
#define ITEM_HP_POTION_L   2   /* Restore 80 HP (heavier enemies drop this) */
#define ITEM_SP_CHARGE     3   /* Restore 1 SP (enables special attacks in battle) */
#define ITEM_ATK_BOOST     4   /* +5 ATK for current battle only (temporary buff) */
#define ITEM_DEF_BOOST     5   /* +5 DEF for current battle only (temporary buff) */
#define ITEM_FULL_RESTORE  6   /* Full HP + SP restore (rare, from elite enemies) */
#define ITEM_COUNT         7   /* Total number of item types including NONE */

/*=== Inventory Slot ===*/
/* Each slot holds one item type and its stack count. Empty slots have
 * item_id == ITEM_NONE.  The inventory is always compacted so that
 * empty slots are only at the tail end of the array. */
typedef struct {
    u8 item_id;     /* ITEM_* ID (0 = empty) */
    u8 quantity;    /* Stack count (1-9 when occupied, 0 when empty) */
} InvSlot;

/* Global inventory array, defined in inventory.c.  Accessed by battle.c
 * for item use and by save.c for serialization. */
extern InvSlot g_inventory[INV_SIZE];

/*
 * invInit - Reset inventory to starting state.
 * Clears all 8 slots, then grants the player 2x HP Potion S as starter items.
 * Called at new game start and on save-load (load overwrites immediately after).
 */
void invInit(void);

/*
 * invAdd - Add qty units of item_id to inventory.
 * If the item already exists, stacks are merged (capped at INV_MAX_STACK).
 * If new, the first empty slot is used.
 * Returns 1 on success, 0 if inventory is full and item is not stackable.
 */
u8 invAdd(u8 item_id, u8 qty);

/*
 * invRemove - Remove qty units of item_id from inventory.
 * If quantity drops to zero, the slot is cleared and subsequent items
 * are shifted down to maintain compaction.
 * Returns 1 if the item was found and removed, 0 if not found.
 */
u8 invRemove(u8 item_id, u8 qty);

/*
 * invCount - Return the current quantity of item_id in inventory.
 * Uses compacted-inventory early exit: returns 0 as soon as an empty slot
 * is encountered.
 */
u8 invCount(u8 item_id);

/*
 * invRollDrop - Roll the loot table for a random item drop.
 * Uses g_frame_count as a simple PRNG seed combined with enemy_type
 * to produce a pseudo-random roll.  Different enemy types have different
 * drop probability distributions (scouts drop common items, elites drop rare).
 * Returns an ITEM_* ID on a successful roll, or ITEM_NONE for no drop.
 */
u8 invRollDrop(u8 enemy_type);

/*
 * invGetName - Return the display name string for an item ID.
 * Returns a non-const char* for PVSnesLib's consoleDrawText() compatibility.
 * Returns "" for ITEM_NONE or out-of-range IDs.
 */
char *invGetName(u8 item_id);

/*
 * invGetEffect - Return the primary numeric effect value for an item ID.
 * For potions, this is HP restored. For boosts, this is the stat delta.
 * ITEM_FULL_RESTORE returns 0 because its effect is handled specially
 * (restores both HP and SP to max).
 */
s16 invGetEffect(u8 item_id);

/*
 * invResetPityTimer - Reset the drop pity timer to 0.
 * Called on save/load to reset the consecutive miss counter. (#149)
 */
void invResetPityTimer(void);

#endif /* INVENTORY_H */
