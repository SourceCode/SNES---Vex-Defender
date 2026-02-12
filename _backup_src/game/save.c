/*==============================================================================
 * SRAM Save/Load System - Phase 20
 *
 * Save data stored in battery-backed SRAM at $70:0000 (LoROM).
 * PVSnesLib's consoleCopySram() / consoleLoadSram() handle bank
 * switching and DMA transfer between WRAM buffer and SRAM.
 *
 * Checksum: XOR over all bytes after the 6-byte header.
 * Detects uninitialized SRAM (all 0x00 or 0xFF) and corrupted data.
 *
 * Bank 0 is full; this file auto-overflows via WLA-DX linker.
 *============================================================================*/

#include "game/save.h"
#include "game/rpg_stats.h"
#include "game/inventory.h"
#include "game/game_state.h"

/* WRAM buffer for save data transfers */
static SaveData save_buf;

/*===========================================================================*/
/* Checksum                                                                  */
/*===========================================================================*/

static u16 calcChecksum(SaveData *data)
{
    u8 *ptr;
    u16 sum;
    u16 i;

    ptr = (u8 *)data;
    sum = 0;

    /* Simple XOR checksum over all bytes after the 6-byte header */
    for (i = 6; i < SAVE_DATA_SIZE; i++) {
        sum ^= (u16)ptr[i];
    }

    return sum;
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

void saveInit(void)
{
    /* SRAM is battery-backed and persistent. Nothing to initialize. */
}

void saveGame(void)
{
    u8 i;

    /* Pack header */
    save_buf.magic1 = SAVE_MAGIC_1;
    save_buf.magic2 = SAVE_MAGIC_2;

    /* Pack player stats from rpg_stats */
    save_buf.level       = rpg_stats.level;
    save_buf.xp          = rpg_stats.xp;
    save_buf.max_hp      = rpg_stats.max_hp;
    save_buf.hp          = rpg_stats.hp;
    save_buf.atk         = rpg_stats.atk;
    save_buf.def         = rpg_stats.def;
    save_buf.spd         = rpg_stats.spd;
    save_buf.max_sp      = rpg_stats.max_sp;
    save_buf.sp          = rpg_stats.sp;
    save_buf.credits     = rpg_stats.credits;
    save_buf.total_kills = rpg_stats.total_kills;

    /* Pack inventory */
    for (i = 0; i < 8; i++) {
        save_buf.inv_ids[i] = g_inventory[i].item_id;
        save_buf.inv_qty[i] = g_inventory[i].quantity;
    }

    /* Pack game progress */
    save_buf.current_zone     = g_game.current_zone;
    save_buf.zones_cleared    = g_game.zones_cleared;
    save_buf.story_flags      = g_game.story_flags;
    save_buf.play_time_seconds = g_game.play_time_seconds;

    /* Calculate and set checksum */
    save_buf.checksum = calcChecksum(&save_buf);

    /* Write to SRAM */
    consoleCopySram((u8 *)&save_buf, SAVE_DATA_SIZE);
}

u8 loadGame(void)
{
    u16 cs;
    u8 i;

    /* Read from SRAM into buffer */
    consoleLoadSram((u8 *)&save_buf, SAVE_DATA_SIZE);

    /* Validate magic */
    if (save_buf.magic1 != SAVE_MAGIC_1) return 0;
    if (save_buf.magic2 != SAVE_MAGIC_2) return 0;

    /* Validate checksum */
    cs = calcChecksum(&save_buf);
    if (cs != save_buf.checksum) return 0;

    /* Restore player stats */
    rpg_stats.level       = save_buf.level;
    rpg_stats.xp          = save_buf.xp;
    rpg_stats.max_hp      = save_buf.max_hp;
    rpg_stats.hp          = save_buf.hp;
    rpg_stats.atk         = save_buf.atk;
    rpg_stats.def         = save_buf.def;
    rpg_stats.spd         = save_buf.spd;
    rpg_stats.max_sp      = save_buf.max_sp;
    rpg_stats.sp          = save_buf.sp;
    rpg_stats.credits     = save_buf.credits;
    rpg_stats.total_kills = save_buf.total_kills;

    /* Recalculate xp_to_next from level */
    if (rpg_stats.level < RPG_MAX_LEVEL) {
        rpg_stats.xp_to_next = rpgGetXPForLevel(rpg_stats.level) - rpg_stats.xp;
    } else {
        rpg_stats.xp_to_next = 0;
    }

    /* Restore inventory */
    invInit();  /* Reset first, then overwrite */
    for (i = 0; i < 8; i++) {
        g_inventory[i].item_id  = save_buf.inv_ids[i];
        g_inventory[i].quantity = save_buf.inv_qty[i];
    }

    /* Restore game progress */
    g_game.current_zone     = save_buf.current_zone;
    g_game.zones_cleared    = save_buf.zones_cleared;
    g_game.story_flags      = save_buf.story_flags;
    g_game.play_time_seconds = save_buf.play_time_seconds;

    return 1;
}

u8 saveExists(void)
{
    u16 cs;

    /* Read from SRAM */
    consoleLoadSram((u8 *)&save_buf, SAVE_DATA_SIZE);

    /* Validate magic */
    if (save_buf.magic1 != SAVE_MAGIC_1) return 0;
    if (save_buf.magic2 != SAVE_MAGIC_2) return 0;

    /* Validate checksum */
    cs = calcChecksum(&save_buf);
    if (cs != save_buf.checksum) return 0;

    return 1;
}

void saveErase(void)
{
    u8 *ptr;
    u16 i;

    ptr = (u8 *)&save_buf;
    for (i = 0; i < SAVE_DATA_SIZE; i++) {
        ptr[i] = 0;
    }
    consoleCopySram((u8 *)&save_buf, SAVE_DATA_SIZE);
}
