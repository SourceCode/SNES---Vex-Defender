/*==============================================================================
 * SRAM Save/Load System - Phase 20
 *
 * Save data stored in battery-backed SRAM at $70:0000 (LoROM).
 * PVSnesLib's consoleCopySram() / consoleLoadSram() handle bank
 * switching and DMA transfer between WRAM buffer and SRAM.
 *
 * Checksum: CRC-8 (polynomial 0x31) over all bytes after the 8-byte header.
 * Detects uninitialized SRAM (all 0x00 or 0xFF) and corrupted data.
 *
 * Bank 0 is full; this file auto-overflows via WLA-DX linker.
 *============================================================================*/

#include "game/save.h"
#include "game/rpg_stats.h"
#include "game/inventory.h"
#include "game/game_state.h"
#include "engine/bullets.h"
#include "engine/collision.h"

/* WRAM buffer for save data transfers.
 * Data is packed into this struct, then the entire struct is DMA'd to SRAM.
 * On load, SRAM contents are DMA'd into this buffer, validated, then
 * unpacked into the live game state. */
static SaveData save_buf;

/*===========================================================================*/
/* Checksum                                                                  */
/*===========================================================================*/

/*
 * calcChecksum - Compute CRC-8 checksum over save data payload.
 *
 * Uses the CRC-8/MAXIM variant with polynomial 0x31 (x^8 + x^5 + x^4 + 1).
 * The checksum covers bytes [8..SAVE_DATA_SIZE), skipping the 8-byte header
 * (magic, version, reserved, and the checksum field itself) so that the
 * checksum does not include itself.
 *
 * The inner loop is 2x unrolled: instead of 8 iterations processing 1 bit
 * each, it uses 4 iterations processing 2 bits each.  This reduces loop
 * overhead on the 65816, which has relatively expensive branch instructions.
 *
 * data:    Pointer to the SaveData struct to checksum.
 * Returns: The computed CRC-8 value (only low 8 bits are meaningful,
 *          returned in a u16 for struct field compatibility).
 */
static u16 calcChecksum(SaveData *data)
{
    u8 *ptr;
    u8 crc;
    u16 i;
    u8 j;
    u8 byte;

    ptr = (u8 *)data;
    crc = 0xFF;  /* Initial seed value (standard for CRC-8/MAXIM) */

    /* Process each byte of the payload (skip 8-byte header) */
    for (i = 8; i < SAVE_DATA_SIZE; i++) {
        byte = ptr[i];
        crc ^= byte;
        /* 2x unrolled bit processing: 4 iterations x 2 bits = 8 bits total */
        for (j = 0; j < 4; j++) {
            /* Process bit (high bit of CRC determines XOR with polynomial) */
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = crc << 1;
            }
            /* Process next bit (second half of unrolled pair) */
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = crc << 1;
            }
        }
    }

    return (u16)crc;
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

/*
 * saveInit - Initialize the save system.
 * SRAM is battery-backed and retains its contents across power cycles,
 * so there is nothing to initialize at runtime.  This exists as a
 * placeholder for potential future SRAM bank configuration.
 */
void saveInit(void)
{
    /* SRAM is battery-backed and persistent. Nothing to initialize. */
}

/*
 * saveGame - Serialize all game state into SRAM.
 *
 * Packs the current rpg_stats, g_inventory, and g_game progress fields
 * into the save_buf struct, writes the magic/version header, computes
 * the CRC-8 checksum, and DMA-copies the buffer to SRAM.
 *
 * Called automatically by gsFlightEnter() on zone entry (auto-save).
 */
void saveGame(void)
{
    u8 i;

    /* Pack header: magic signature + version for validation on load */
    save_buf.magic1 = SAVE_MAGIC_1;
    save_buf.magic2 = SAVE_MAGIC_2;
    save_buf.version = SAVE_VERSION;
    save_buf.reserved = 0;

    /* Pack player RPG stats from the global rpg_stats struct */
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

    /* Pack inventory as parallel arrays (item IDs and quantities) */
    for (i = 0; i < 8; i++) {
        save_buf.inv_ids[i] = g_inventory[i].item_id;
        save_buf.inv_qty[i] = g_inventory[i].quantity;
    }

    /* Pack game progress fields */
    save_buf.current_zone     = g_game.current_zone;
    save_buf.zones_cleared    = g_game.zones_cleared;
    save_buf.story_flags      = g_game.story_flags;
    save_buf.play_time_seconds = g_game.play_time_seconds;

    /* #150: Pack weapon mastery kill counts */
    save_buf.weapon_kills[0] = g_weapon_kills[0];
    save_buf.weapon_kills[1] = g_weapon_kills[1];
    save_buf.weapon_kills[2] = g_weapon_kills[2];

    /* #156: Update high score if current score exceeds it */
    if (g_score > save_buf.high_score) {
        save_buf.high_score = g_score;
    }

    /* #174: Pack max combo */
    save_buf.max_combo = g_game.max_combo;

    /* #199: Pack per-zone ranks */
    save_buf.zone_ranks[0] = g_game.zone_ranks[0];
    save_buf.zone_ranks[1] = g_game.zone_ranks[1];
    save_buf.zone_ranks[2] = g_game.zone_ranks[2];

    /* #239: Pack win streak */
    save_buf.win_streak = rpg_stats.win_streak;

    /* Calculate CRC-8 over payload and store in header */
    save_buf.checksum = calcChecksum(&save_buf);

    /* DMA-copy the entire buffer to battery-backed SRAM.
     * consoleCopySram() handles 65816 bank switching to $70:0000. */
    consoleCopySram((u8 *)&save_buf, SAVE_DATA_SIZE);
}

/*
 * loadGame - Restore game state from SRAM.
 *
 * Reads the SRAM contents into save_buf, validates the magic signature,
 * version, and checksum, then unpacks all fields into the live game state.
 * Performs bounds checking on zone and level values to guard against
 * partially-corrupted data that passes the checksum.
 *
 * Returns 1 on successful load, 0 on any validation failure.
 * On failure, rpg_stats and g_game may be partially modified (inventory
 * was reset by invInit), so the caller should treat failure as "start new game".
 */
u8 loadGame(void)
{
    u16 cs;
    u8 i;

    /* DMA-read SRAM contents into the WRAM buffer */
    consoleLoadSram((u8 *)&save_buf, SAVE_DATA_SIZE);

    /* Validate 4-byte magic signature.
     * Uninitialized SRAM (all 0x00 or 0xFF) will fail here. */
    if (save_buf.magic1 != SAVE_MAGIC_1) return 0;
    if (save_buf.magic2 != SAVE_MAGIC_2) return 0;

    /* Validate save format version.
     * Rejects saves from incompatible builds. */
    if (save_buf.version != SAVE_VERSION) return 0;

    /* Validate CRC-8 checksum over the payload.
     * Detects bit-rot, partial writes, or other corruption. */
    cs = calcChecksum(&save_buf);
    if (cs != save_buf.checksum) return 0;

    /* --- All validation passed; restore game state --- */

    /* Restore player RPG stats */
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

    /* #125: Clamp HP and SP to their maximums after restore.
     * Guards against save data where HP > max_hp or SP > max_sp
     * due to stat changes between game versions or subtle corruption
     * that passes the CRC check. */
    if (rpg_stats.hp > rpg_stats.max_hp) rpg_stats.hp = rpg_stats.max_hp;
    if (rpg_stats.sp > rpg_stats.max_sp) rpg_stats.sp = rpg_stats.max_sp;

    /* Recalculate xp_to_next from level and XP.
     * This derived field is NOT saved to avoid desync risks and save bloat. */
    if (rpg_stats.level < RPG_MAX_LEVEL) {
        rpg_stats.xp_to_next = rpgGetXPForLevel(rpg_stats.level) - rpg_stats.xp;
    } else {
        rpg_stats.xp_to_next = 0;  /* At max level, no more XP needed */
    }

    /* Restore inventory: reset first to clear stale data, then overwrite
     * with saved slot contents.  invInit() provides safe defaults in case
     * some slots are empty/invalid. */
    invInit();
    for (i = 0; i < 8; i++) {
        g_inventory[i].item_id  = save_buf.inv_ids[i];
        g_inventory[i].quantity = save_buf.inv_qty[i];

        /* #125: Validate item IDs and quantities on load.
         * Invalid item IDs (>= ITEM_COUNT) or excessive quantities
         * (> INV_MAX_STACK) are reset to empty slots. */
        if (g_inventory[i].item_id >= ITEM_COUNT) {
            g_inventory[i].item_id = ITEM_NONE;
            g_inventory[i].quantity = 0;
        }
        if (g_inventory[i].quantity > INV_MAX_STACK) {
            g_inventory[i].quantity = INV_MAX_STACK;
        }
    }

    /* Restore game progress with bounds validation.
     * Even though the checksum passed, clamp zone/cleared values to
     * prevent out-of-bounds array access if data is subtly wrong. */
    g_game.current_zone     = save_buf.current_zone;
    if (g_game.current_zone >= ZONE_COUNT) g_game.current_zone = 0;
    g_game.zones_cleared    = save_buf.zones_cleared;
    if (g_game.zones_cleared > ZONE_COUNT) g_game.zones_cleared = 0;
    g_game.story_flags      = save_buf.story_flags;
    g_game.play_time_seconds = save_buf.play_time_seconds;

    /* #150: Restore weapon mastery kill counts */
    g_weapon_kills[0] = save_buf.weapon_kills[0];
    g_weapon_kills[1] = save_buf.weapon_kills[1];
    g_weapon_kills[2] = save_buf.weapon_kills[2];

    /* #174: Restore max combo */
    g_game.max_combo = save_buf.max_combo;

    /* #199: Restore per-zone ranks */
    g_game.zone_ranks[0] = save_buf.zone_ranks[0];
    g_game.zone_ranks[1] = save_buf.zone_ranks[1];
    g_game.zone_ranks[2] = save_buf.zone_ranks[2];

    /* #239: Restore win streak */
    rpg_stats.win_streak = save_buf.win_streak;
    if (rpg_stats.win_streak > 5) rpg_stats.win_streak = 5;

    /* #149: Reset pity timer on load */
    invResetPityTimer();

    /* Final sanity check on player level.
     * Level 0 or level > 10 means the save data is nonsensical. */
    if (rpg_stats.level == 0 || rpg_stats.level > RPG_MAX_LEVEL) {
        return 0;  /* Corrupt data */
    }

    return 1;
}

/*
 * saveExists - Check for valid save data without modifying game state.
 *
 * Performs the same validation as loadGame() (magic + version + checksum)
 * but does NOT unpack any data into live state.  Used by the title screen
 * to determine whether the "CONTINUE" option should be available.
 *
 * Returns 1 if valid save exists, 0 otherwise.
 */
u8 saveExists(void)
{
    u16 cs;

    /* Read SRAM into buffer */
    consoleLoadSram((u8 *)&save_buf, SAVE_DATA_SIZE);

    /* Validate magic signature */
    if (save_buf.magic1 != SAVE_MAGIC_1) return 0;
    if (save_buf.magic2 != SAVE_MAGIC_2) return 0;

    /* Validate version */
    if (save_buf.version != SAVE_VERSION) return 0;

    /* Validate checksum */
    cs = calcChecksum(&save_buf);
    if (cs != save_buf.checksum) return 0;

    return 1;
}

/*
 * saveGetLevel - Read saved level from save_buf without side effects.
 * Must call saveExists() first to populate save_buf from SRAM.
 */
u8 saveGetLevel(void)
{
    return save_buf.level;
}

/*
 * saveGetZone - Read saved zone from save_buf without side effects.
 * Must call saveExists() first to populate save_buf from SRAM.
 */
u8 saveGetZone(void)
{
    return save_buf.current_zone;
}

/*
 * saveErase - Destroy all save data by zeroing SRAM.
 *
 * Fills the save buffer with 0x00 bytes and writes it to SRAM.
 * This invalidates the magic signature so saveExists() returns 0.
 * Called from gsVictoryEnter() because the game is considered "complete"
 * after the final boss is defeated.
 */
void saveErase(void)
{
    u8 *ptr;
    u16 i;

    /* Zero out the entire save buffer */
    ptr = (u8 *)&save_buf;
    for (i = 0; i < SAVE_DATA_SIZE; i++) {
        ptr[i] = 0;
    }
    /* Write zeroed buffer to SRAM, overwriting any existing save */
    consoleCopySram((u8 *)&save_buf, SAVE_DATA_SIZE);
}
