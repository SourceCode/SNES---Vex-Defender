/*==============================================================================
 * RPG Stats & Leveling System - Phase 13
 *
 * Persistent player stats that carry between battles and across zones:
 *   - 10 levels with cumulative XP thresholds tuned for a ~10-minute game
 *   - 5 stats grow each level via a hand-tuned growth table: HP, ATK, DEF, SPD, SP
 *   - Full HP/SP heal on level-up as a reward for progression
 *   - Defeat penalty: lose ~25% current HP (min 1), so the player is
 *     weakened but never killed outright by a loss
 *
 * Data flow with the battle system:
 *   battleStart() copies rpg_stats into a BattleCombatant struct for combat.
 *   After battle victory, HP/SP are synced back and XP is awarded via rpgAddXP().
 *   After defeat, rpgApplyDefeatPenalty() reduces persistent HP.
 *
 * The 65816 CPU has no hardware multiply or divide, so all stat calculations
 * use addition, subtraction, and bit shifts only.
 *============================================================================*/

#ifndef RPG_STATS_H
#define RPG_STATS_H

#include "game.h"

/*=== Level Cap and Base Stats ===*/
/* These are the starting stats for a brand-new level 1 character.
 * They were balanced so that Zone 1 scouts (ATK ~8-10) deal meaningful
 * but survivable damage against 80 HP / 6 DEF. */
#define RPG_MAX_LEVEL   10   /* Hard level cap; XP stops mattering at 10 */
#define RPG_BASE_HP     80   /* Starting maximum HP */
#define RPG_BASE_ATK    12   /* Starting attack power */
#define RPG_BASE_DEF    6    /* Starting defense */
#define RPG_BASE_SPD    10   /* Starting speed (affects turn order in battle) */
#define RPG_BASE_SP     2    /* Starting special points (for special attacks) */

/*=== Persistent Player RPG Data ===*/
/* This struct holds all RPG progression state.  A single global instance
 * (rpg_stats) is defined in rpg_stats.c and accessed by battle.c, save.c,
 * game_state.c, and the HUD renderer.
 *
 * Note: xp_to_next is derived (not saved) -- on load it is recalculated
 * from level and xp to avoid save-format bloat and desync risks. */
typedef struct {
    u8  level;          /* Current level (1-10); determines growth table index */
    u16 xp;             /* Total accumulated XP across all battles */
    u16 xp_to_next;     /* XP remaining until next level-up (derived, not saved) */
    s16 max_hp;         /* Maximum HP at current level (grows per growth table) */
    s16 hp;             /* Current HP; persists between battles, reduced on defeat */
    s16 atk;            /* Attack power; affects damage dealt in battle */
    s16 def;            /* Defense; reduces incoming damage in battle */
    s16 spd;            /* Speed; determines who acts first each battle round */
    u8  max_sp;         /* Maximum SP at current level (for special attacks) */
    u8  sp;             /* Current SP; persists between battles */
    u16 credits;        /* Currency earned from battles (reserved for future shop) */
    u16 total_kills;    /* Lifetime enemy kill count (shown on victory screen) */
    u8  defeat_streak;  /* Consecutive defeats for dynamic difficulty (#160) */
    u8  win_streak;     /* Consecutive battle wins, max 5 (#239) */
} PlayerRPGStats;

/* Global instance defined in rpg_stats.c */
extern PlayerRPGStats rpg_stats;

/*
 * rpgStatsInit - Reset all stats to level 1 defaults.
 * Called once for NEW GAME, and also when returning to title from game-over.
 */
void rpgStatsInit(void);

/*
 * rpgAddXP - Award XP and process any resulting level-ups.
 * Supports multi-level jumps from large XP awards (e.g., boss kills).
 * On each level-up: applies growth table bonuses and fully heals HP/SP.
 * Updates xp_to_next for the HUD display.
 *
 * xp:      Amount of XP to award.
 * Returns: 1 if at least one level-up occurred, 0 otherwise.
 */
u8 rpgAddXP(u16 xp);

/*
 * rpgApplyDefeatPenalty - Reduce HP after losing a battle.
 * Loses approximately 25% of current HP (computed via >>2 to avoid
 * division on the 65816).  HP never drops below 1.
 */
void rpgApplyDefeatPenalty(void);

/*
 * rpgGetXPForLevel - Look up the cumulative XP threshold for a level.
 * Returns the total XP needed to reach the given level.
 * Returns 0xFFFF for levels above RPG_MAX_LEVEL (sentinel value).
 */
u16 rpgGetXPForLevel(u8 level);

/*
 * rpgRegenSP - Passive SP regeneration during flight.
 * Regenerates 1 SP every 600 frames (10 seconds at 60fps).
 * Call once per frame from the flight update loop. (#144)
 */
void rpgRegenSP(void);

/*
 * rpgRegenResetCounter - Reset the SP regen frame counter.
 * Call when entering flight mode to ensure consistent regen timing.
 */
void rpgRegenResetCounter(void);

/*
 * rpgGetCatchUpBonus - Check if XP catch-up bonus is active.
 * Returns 1 if player level is below expected level for current zone.
 * Expected: zone 0 = L1-3, zone 1 = L4-6, zone 2 = L7-10. (#158)
 */
u8 rpgGetCatchUpBonus(void);

/*
 * rpgGetDifficultyAssist - Check if dynamic difficulty assist is active (#160).
 * Returns 1 if player has 2+ consecutive defeats (defeat_streak >= 2).
 * Used by battle.c to reduce enemy ATK by ~12.5%.
 */
u8 rpgGetDifficultyAssist(void);

/*
 * rpgGetGrowthStr - Format stat growth values for level-up display (#171).
 * Writes "+HP +ATK +DEF" string into buf (must be >= 24 chars).
 * level: the level just reached (reads growth_table[level-2]).
 */
void rpgGetGrowthStr(u8 level, char *buf);

#endif /* RPG_STATS_H */
