/*==============================================================================
 * RPG Stats & Leveling System - Phase 13
 *
 * XP/Level progression for a ~10 minute game:
 *   Level 1-2:  Zone 1 (scouts, fighters)    ~30-80 XP
 *   Level 3-5:  Zone 2 (heavies, elites)     ~160-450 XP
 *   Level 6-8:  Zone 3 (bosses)              ~680-1400 XP
 *   Level 9-10: Extended play                 ~1400-2000 XP
 *
 * Growth table gives meaningful stat increases each level.
 * Full HP/SP heal on level-up rewards progression.
 *
 * All arithmetic uses addition, subtraction, and shifts only because the
 * WDC 65816 CPU has no hardware multiply or divide instructions.
 *
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#include "game/rpg_stats.h"

/* Global player RPG stats instance, accessed by battle.c, save.c, HUD, etc. */
PlayerRPGStats rpg_stats;

/*=== Cumulative XP Required For Each Level ===*/
/* xp_table[N] = total XP that must be accumulated to transition FROM level N
 * TO level N+1.  The rpgAddXP() loop checks: rpg_stats.xp >= xp_table[level]
 * to determine if a level-up occurs.
 *
 * The curve is designed so that:
 *   - Early levels are quick (~2-3 battles per level) to give the player
 *     a sense of progression during the tutorial-like Zone 1
 *   - Mid levels require more battles, coinciding with Zone 2's harder enemies
 *   - Late levels are a stretch goal for completionists
 *
 * The sentinel value 0xFFFF at index 10 ensures the while loop in rpgAddXP()
 * naturally terminates at max level without a separate bounds check. */
static const u16 xp_table[RPG_MAX_LEVEL + 1] = {
    0,      /* Level 1 (starting level, no XP needed) */
    30,     /* Level 2: ~1-2 scout battles */
    80,     /* Level 3: ~3-4 battles */
    160,    /* Level 4: ~5-7 battles */
    280,    /* Level 5: ~8-10 battles */
    450,    /* Level 6: ~11-14 battles */
    680,    /* Level 7: ~15-18 battles */
    1000,   /* Level 8: ~19-23 battles */
    1400,   /* Level 9: ~24-28 battles */
    2000,   /* Level 10 (max): ~29-35 battles */
    0xFFFF  /* Sentinel: unreachable threshold to stop level-up loop */
};

/*=== Stat Growth Per Level-Up: HP, ATK, DEF, SPD, SP_max ===*/
/* growth_table[i] contains the stat bonuses applied when leveling from
 * level (i+1) to level (i+2).  So growth_table[0] is L1->L2 bonuses.
 *
 * Column order: { HP_bonus, ATK_bonus, DEF_bonus, SPD_bonus, SP_max_bonus }
 *
 * Design notes:
 *   - HP grows by 15-35 per level, scaling up at higher levels
 *   - ATK grows steadily (2-5 per level) to keep battles feeling faster
 *   - DEF grows slower than ATK so enemies remain threatening
 *   - SPD grows by 1-2 per level (small changes are significant in turn order)
 *   - SP increases only every other level to keep specials feeling rare
 *
 * The padding row at index 9 is never accessed because applyLevelUp()
 * uses index (level - 2), and level caps at 10. */
static const s16 growth_table[RPG_MAX_LEVEL][5] = {
    /* L1->L2  */ { 15,  2,  1,  1, 0 },
    /* L2->L3  */ { 15,  2,  2,  1, 1 },
    /* L3->L4  */ { 20,  3,  2,  1, 0 },
    /* L4->L5  */ { 20,  3,  2,  2, 1 },
    /* L5->L6  */ { 25,  3,  3,  1, 0 },
    /* L6->L7  */ { 25,  4,  3,  2, 1 },
    /* L7->L8  */ { 30,  4,  3,  1, 0 },
    /* L8->L9  */ { 30,  5,  4,  2, 1 },
    /* L9->L10 */ { 35,  5,  4,  2, 1 },
    /* padding */ {  0,  0,  0,  0, 0 },
};

/*
 * Expected cumulative stats at each level (for reference/balancing):
 *
 * Level  HP   ATK  DEF  SPD  SP
 * -----  ---  ---  ---  ---  --
 *   1     80   12    6   10   2
 *   2     95   14    7   11   2
 *   3    110   16    9   12   3
 *   4    130   19   11   13   3
 *   5    150   22   13   15   4
 *   6    175   25   16   16   4
 *   7    200   29   19   18   5
 *   8    230   33   22   19   5
 *   9    260   38   26   21   6
 *  10    295   43   30   23   7
 */

/*===========================================================================*/
/* Internal: Apply stat bonuses for one level-up                             */
/*===========================================================================*/

/*
 * applyLevelUp - Apply the growth table bonuses for the most recent level-up.
 *
 * Must be called AFTER rpg_stats.level has already been incremented.
 * The growth table is indexed by (level - 2) because:
 *   growth_table[0] = bonuses for L1->L2, which applies when level == 2.
 *
 * Also performs a full HP/SP heal as a reward for leveling up, which
 * creates a positive feedback loop where the player is incentivized
 * to fight battles rather than avoid them.
 */
static void applyLevelUp(void)
{
    u8 idx;

    /* Guard: level must be 2-10 for valid table access */
    if (rpg_stats.level < 2 || rpg_stats.level > RPG_MAX_LEVEL) return;
    idx = rpg_stats.level - 2;  /* Map level 2 -> index 0, level 10 -> index 8 */

    /* Apply stat bonuses from the growth table */
    rpg_stats.max_hp += growth_table[idx][0];  /* HP cap increase */
    rpg_stats.atk    += growth_table[idx][1];  /* Attack power increase */
    rpg_stats.def    += growth_table[idx][2];  /* Defense increase */
    rpg_stats.spd    += growth_table[idx][3];  /* Speed increase */
    rpg_stats.max_sp += (u8)growth_table[idx][4];  /* SP cap increase (0 or 1) */

    /* Full heal on level up: restore HP and SP to their new maximums.
     * This rewards the player for gaining a level and gives them a fresh
     * start for subsequent battles. */
    rpg_stats.hp = rpg_stats.max_hp;
    rpg_stats.sp = rpg_stats.max_sp;
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

/*
 * rpgStatsInit - Reset all player stats to the level 1 starting state.
 *
 * Initializes every field of rpg_stats with the base values defined in
 * rpg_stats.h.  xp_to_next is set to the XP threshold for level 2 (30 XP),
 * which the HUD can display as "XP needed" for the next level-up.
 *
 * Called at NEW GAME start, and also when returning to the title screen
 * from game-over (to ensure a clean slate for the next playthrough).
 */
void rpgStatsInit(void)
{
    rpg_stats.level    = 1;
    rpg_stats.xp       = 0;
    rpg_stats.xp_to_next = xp_table[1];  /* XP needed for level 2 = 30 */
    rpg_stats.max_hp   = RPG_BASE_HP;     /* 80 HP */
    rpg_stats.hp       = RPG_BASE_HP;     /* Start at full health */
    rpg_stats.atk      = RPG_BASE_ATK;    /* 12 ATK */
    rpg_stats.def      = RPG_BASE_DEF;    /* 6 DEF */
    rpg_stats.spd      = RPG_BASE_SPD;    /* 10 SPD */
    rpg_stats.max_sp   = RPG_BASE_SP;     /* 2 SP */
    rpg_stats.sp       = RPG_BASE_SP;     /* Start with full SP */
    rpg_stats.credits  = 0;
    rpg_stats.total_kills = 0;
    rpg_stats.defeat_streak = 0;  /* #160: Reset dynamic difficulty */
    rpg_stats.win_streak = 0;     /* #239: Reset win streak */
}

/*
 * rpgAddXP - Award XP to the player and process any resulting level-ups.
 *
 * The while loop handles multi-level jumps: if a boss awards 500 XP and
 * the player only needed 100 to level up, they might gain 2-3 levels at
 * once.  Each iteration increments the level and applies the corresponding
 * growth table bonuses.
 *
 * The XP table uses a sentinel value (0xFFFF at index RPG_MAX_LEVEL) to
 * naturally stop the loop at max level without an extra bounds check.
 *
 * After processing level-ups, xp_to_next is updated for the HUD display.
 * At max level, xp_to_next is set to 0 to indicate "max level reached".
 *
 * xp:      Amount of experience points to award.
 * Returns: 1 if at least one level-up occurred, 0 otherwise.
 */
u8 rpgAddXP(u16 xp)
{
    u8 leveled;

    leveled = 0;

    /* #131: XP saturating addition to prevent u16 overflow.
     * Without this guard, adding large XP near 0xFFFF could wrap to 0,
     * causing the player to lose all accumulated XP. */
    if (rpg_stats.xp > (u16)(0xFFFF - xp)) {
        rpg_stats.xp = 0xFFFF;
    } else {
        rpg_stats.xp += xp;
    }

    /* #160: Victory resets defeat streak for dynamic difficulty */
    rpg_stats.defeat_streak = 0;

    /* Process level-ups: can gain multiple levels from large XP awards.
     * The sentinel 0xFFFF at xp_table[RPG_MAX_LEVEL] ensures this loop
     * terminates even if XP somehow exceeds all thresholds. */
    while (rpg_stats.level < RPG_MAX_LEVEL &&
           rpg_stats.xp >= xp_table[rpg_stats.level]) {
        rpg_stats.level++;
        applyLevelUp();  /* Apply growth bonuses + full heal */
        leveled = 1;
    }

    /* Update the "XP remaining" counter for the HUD */
    if (rpg_stats.level < RPG_MAX_LEVEL) {
        rpg_stats.xp_to_next = xp_table[rpg_stats.level] - rpg_stats.xp;
    } else {
        rpg_stats.xp_to_next = 0;  /* Max level reached */
    }

    return leveled;
}

/*
 * rpgApplyDefeatPenalty - Reduce HP after losing a battle.
 *
 * The penalty is ~25% of current HP, computed as hp >> 2 (right shift
 * by 2 is equivalent to dividing by 4).  This uses a shift instead of
 * division because the 65816 CPU has no hardware divide instruction.
 *
 * A minimum penalty of 1 is enforced so that repeated defeats always
 * cost something.  HP is clamped to a minimum of 1 so the player is
 * never killed outright by the penalty (they can always retry).
 */
void rpgApplyDefeatPenalty(void)
{
    s16 penalty;

    /* #160: Increment defeat streak for dynamic difficulty assist */
    if (rpg_stats.defeat_streak < 255) {
        rpg_stats.defeat_streak++;
    }

    /* #138: Zone-scaled defeat penalty.
     * Higher zones impose harsher penalties to increase stakes:
     *   Zone 0 (Debris):   ~25% HP loss (hp >> 2)
     *   Zone 1 (Asteroid): ~37% HP loss (hp >> 2 + hp >> 3)
     *   Zone 2 (Flagship): ~50% HP loss (hp >> 1)
     * Uses shifts instead of multiply/divide for 65816 performance. */
    if (g_game.current_zone >= 2) {
        penalty = rpg_stats.hp >> 1;           /* 50% */
    } else if (g_game.current_zone == 1) {
        penalty = (rpg_stats.hp >> 2) + (rpg_stats.hp >> 3);  /* ~37% */
    } else {
        penalty = rpg_stats.hp >> 2;           /* 25% */
    }
    if (penalty < 1) penalty = 1;  /* Ensure at least 1 HP is lost */

    rpg_stats.hp -= penalty;
    if (rpg_stats.hp < 1) rpg_stats.hp = 1;  /* Never reduce to zero */
}

/*
 * rpgGetXPForLevel - Look up cumulative XP required for a specific level.
 *
 * Used by the save/load system to recalculate xp_to_next without saving
 * that derived value to SRAM.
 *
 * level:   Target level (1-10).
 * Returns: Cumulative XP needed to reach that level, or 0xFFFF if invalid.
 */
u16 rpgGetXPForLevel(u8 level)
{
    if (level > RPG_MAX_LEVEL) return 0xFFFF;  /* Invalid level */
    return xp_table[level];
}

/* File-scope regen counter so rpgRegenResetCounter() can clear it.
 * A static local inside rpgRegenSP() would persist across zone transitions
 * and game restarts, causing inconsistent SP regen timing. */
static u16 regen_counter;

/*
 * rpgRegenSP - Passive SP regeneration during flight (#144).
 * Regenerates 1 SP every 600 frames (10 seconds at 60fps), up to max_sp.
 * Call once per frame from the flight update loop.
 */
void rpgRegenSP(void)
{
    regen_counter++;
    if (regen_counter >= 600) {
        regen_counter = 0;
        if (rpg_stats.sp < rpg_stats.max_sp) {
            rpg_stats.sp++;
        }
    }
}

/*
 * rpgRegenResetCounter - Reset the SP regen counter.
 * Call when entering a new zone or starting a new game to ensure
 * consistent SP regen timing from the start of each flight section.
 */
void rpgRegenResetCounter(void)
{
    regen_counter = 0;
}

/*
 * rpgGetCatchUpBonus - Check if XP catch-up bonus is active (#158).
 * Returns 1 if player level is below expected level for current zone.
 * Expected: zone 0 = L1-3, zone 1 = L4-6, zone 2 = L7-10.
 */
u8 rpgGetCatchUpBonus(void)
{
    u8 expected_min;
    expected_min = g_game.current_zone * 3 + 1;
    if (rpg_stats.level < expected_min) return 1;
    return 0;
}

/*
 * rpgGetDifficultyAssist - Check if dynamic difficulty assist is active (#160).
 * Returns 1 if player has 2+ consecutive defeats in the same zone.
 * Battle.c uses this to reduce enemy ATK by ~12.5% (atk -= atk >> 3).
 */
u8 rpgGetDifficultyAssist(void)
{
    return (rpg_stats.defeat_streak >= 2) ? 1 : 0;
}

/*
 * rpgGetGrowthStr - Format stat growth values for level-up display (#171).
 * Reads growth_table[level-2] and builds "+HP +ATK +DEF" string.
 * Uses manual digit conversion (no sprintf on 65816).
 */
void rpgGetGrowthStr(u8 level, char *buf)
{
    u8 idx;
    u8 pos;
    s16 val;
    u8 d;

    pos = 0;
    if (level < 2 || level > RPG_MAX_LEVEL) {
        buf[0] = 0;
        return;
    }
    idx = level - 2;

    /* HP growth */
    buf[pos++] = '+';
    val = growth_table[idx][0];
    if (val >= 100) { d = 0; while (val >= 100) { val -= 100; d++; } buf[pos++] = '0' + d; }
    if (val >= 10 || growth_table[idx][0] >= 100) { d = 0; while (val >= 10) { val -= 10; d++; } buf[pos++] = '0' + d; }
    buf[pos++] = '0' + (u8)val;
    buf[pos++] = 'H';
    buf[pos++] = 'P';
    buf[pos++] = ' ';

    /* ATK growth */
    buf[pos++] = '+';
    val = growth_table[idx][1];
    if (val >= 10) { d = 0; while (val >= 10) { val -= 10; d++; } buf[pos++] = '0' + d; }
    buf[pos++] = '0' + (u8)val;
    buf[pos++] = 'A';
    buf[pos++] = 'T';
    buf[pos++] = 'K';
    buf[pos++] = ' ';

    /* DEF growth */
    buf[pos++] = '+';
    val = growth_table[idx][2];
    if (val >= 10) { d = 0; while (val >= 10) { val -= 10; d++; } buf[pos++] = '0' + d; }
    buf[pos++] = '0' + (u8)val;
    buf[pos++] = 'D';
    buf[pos++] = 'E';
    buf[pos++] = 'F';
    buf[pos] = 0;
}
