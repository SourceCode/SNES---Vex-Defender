/*==============================================================================
 * SRAM Save/Load System - Phase 20
 *
 * Saves player progress to battery-backed SRAM at $70:0000 (LoROM mapping).
 * Uses PVSnesLib's consoleCopySram()/consoleLoadSram() for transfers.
 * Those routines handle the 65816 bank switching and DMA transfer between
 * the WRAM buffer (save_buf in save.c) and the physical SRAM chip.
 *
 * Save data includes:
 *   - Player RPG stats (level, XP, HP, ATK, DEF, SPD, SP, credits, kills)
 *   - Inventory (8 slots x 2 bytes each: item_id + quantity)
 *   - Game progress (zone, zones cleared, story flags, play time)
 *
 * Validation uses a three-layer scheme:
 *   1. 4-byte magic number ("VEXD" split into two u16 values) to detect
 *      uninitialized SRAM (all 0x00 or 0xFF after fresh battery install).
 *   2. 1-byte version tag for forward compatibility if the save format changes.
 *   3. CRC-8 checksum (polynomial 0x31) over all bytes after the 8-byte header
 *      to detect bit-rot or partial writes.
 *
 * Total save size: ~48 bytes (well under the 2KB SRAM available with
 * SRAMSIZE $01 in hdr.asm).  The ROM header also sets CARTRIDGETYPE $02
 * which tells the emulator/hardware that SRAM with battery backup is present.
 *
 * Auto-save triggers on zone entry (called from gsFlightEnter in game_state.c).
 *============================================================================*/

#ifndef SAVE_H
#define SAVE_H

#include "game.h"

/*=== Save Validation Constants ===*/
/* ASCII "VEXD" split across two 16-bit words.  Uninitialized SRAM will
 * contain 0x0000 or 0xFFFF, neither of which matches this signature. */
#define SAVE_MAGIC_1    0x5645  /* 'V' 'E' in little-endian */
#define SAVE_MAGIC_2    0x5844  /* 'X' 'D' in little-endian */

/*=== Save Format Version ===*/
/* Increment this whenever the SaveData struct layout changes so that
 * an old save from a previous build is rejected rather than mis-parsed. */
#define SAVE_VERSION  5

/*=== Save Data Structure (packed for SRAM) ===*/
/* This struct is memcpy'd to/from SRAM as a flat byte blob.
 * Field order matters: the header must come first so that magic/version
 * checks can be done before touching any gameplay fields. */
typedef struct {
    /* --- Header (8 bytes) ---
     * These are checked first on load; if any fail the save is rejected. */
    u16 magic1;             /* Must be SAVE_MAGIC_1 ("VE") */
    u16 magic2;             /* Must be SAVE_MAGIC_2 ("XD") */
    u8  version;            /* Must equal SAVE_VERSION */
    u8  reserved;           /* Padding byte for 16-bit alignment */
    u16 checksum;           /* CRC-8 (stored in u16 for alignment) over bytes [8..end) */

    /* --- Player RPG stats (22 bytes) ---
     * Mirrors the fields of PlayerRPGStats (rpg_stats.h).
     * xp_to_next is NOT saved; it is re-derived from level + xp on load. */
    u8  level;              /* Player level 1-10 */
    u16 xp;                 /* Total accumulated XP */
    s16 max_hp;             /* Maximum HP at saved level */
    s16 hp;                 /* Current HP at time of save */
    s16 atk;                /* Attack stat */
    s16 def;                /* Defense stat */
    s16 spd;                /* Speed stat (turn order in battle) */
    u8  max_sp;             /* Maximum SP at saved level */
    u8  sp;                 /* Current SP at time of save */
    u16 credits;            /* Currency (for potential shop feature) */
    u16 total_kills;        /* Lifetime enemy kills (shown on victory screen) */

    /* --- Inventory (16 bytes: 8 slots x 2 bytes) ---
     * Stored as parallel arrays rather than an array-of-structs to keep
     * the binary layout simple and avoid potential padding issues. */
    u8  inv_ids[8];         /* ITEM_* ID per slot (ITEM_NONE = empty) */
    u8  inv_qty[8];         /* Stack count per slot (1-9) */

    /* --- Game progress (6 bytes) --- */
    u8  current_zone;       /* ZONE_DEBRIS / ZONE_ASTEROID / ZONE_FLAGSHIP */
    u8  zones_cleared;      /* Number of zones completed (0-3) */
    u16 story_flags;        /* Bitmask of STORY_* and SFLAG_* flags */
    u16 play_time_seconds;  /* Total elapsed play time in seconds */

    /* --- Weapon mastery (6 bytes, #150) --- */
    u16 weapon_kills[3];    /* Per-weapon-type kill counts for mastery bonus */

    /* --- High score (2 bytes, #156) --- */
    u16 high_score;         /* Best score achieved (persists across save erase) */

    /* --- Max combo (1 byte, #174) --- */
    u8  max_combo;          /* Persistent best combo (#174) */

    /* --- Per-zone ranks (3 bytes, #199) --- */
    u8  zone_ranks[3];      /* Per-zone rank 0=D,1=C,2=B,3=A,4=S */

    /* --- Win streak (1 byte, #239) --- */
    u8  win_streak;         /* Consecutive battle wins, max 5 */
} SaveData;

/* Convenience: compile-time size used for SRAM transfer length */
#define SAVE_DATA_SIZE  sizeof(SaveData)

/*
 * saveInit - Initialize save system (no-op, SRAM is battery-backed and persistent).
 * Called once during gsInit().
 */
void saveInit(void);

/*
 * saveGame - Serialize current game state into SRAM.
 * Packs rpg_stats, g_inventory, and g_game fields into save_buf,
 * computes CRC-8 checksum, then writes to SRAM via consoleCopySram().
 */
void saveGame(void);

/*
 * loadGame - Deserialize SRAM contents back into live game state.
 * Validates magic, version, and checksum before restoring any data.
 * Returns 1 on successful restore, 0 on any validation failure.
 * On failure, live game state is NOT modified (except inventory reset).
 */
u8 loadGame(void);

/*
 * saveExists - Non-destructive check for valid save data in SRAM.
 * Reads SRAM and checks magic + version + checksum.
 * Returns 1 if a valid save is present, 0 otherwise.
 * Used by title screen to enable/disable the CONTINUE menu option.
 */
u8 saveExists(void);

/*
 * saveErase - Overwrite SRAM with zeroes, destroying all save data.
 * Called after the victory screen because the game is "complete".
 */
void saveErase(void);

/*
 * saveGetLevel - Read the saved player level without modifying game state.
 * Must be called AFTER saveExists() (which populates save_buf).
 * Returns: Saved level (1-10), or 0 if no valid save.
 */
u8 saveGetLevel(void);

/*
 * saveGetZone - Read the saved zone without modifying game state.
 * Must be called AFTER saveExists() (which populates save_buf).
 * Returns: Saved zone index (0-2).
 */
u8 saveGetZone(void);

#endif /* SAVE_H */
