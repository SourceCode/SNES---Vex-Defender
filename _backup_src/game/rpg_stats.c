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
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#include "game/rpg_stats.h"

PlayerRPGStats rpg_stats;

/*=== Cumulative XP Required For Each Level ===*/
/* xp_table[N] = total XP needed to reach level N+1 */
/* At level L, check: rpg_stats.xp >= xp_table[L] to level up */
static const u16 xp_table[RPG_MAX_LEVEL + 1] = {
    0,      /* Level 1 (starting level) */
    30,     /* Level 2: ~1-2 scout battles */
    80,     /* Level 3: ~3-4 battles */
    160,    /* Level 4: ~5-7 battles */
    280,    /* Level 5: ~8-10 battles */
    450,    /* Level 6: ~11-14 battles */
    680,    /* Level 7: ~15-18 battles */
    1000,   /* Level 8: ~19-23 battles */
    1400,   /* Level 9: ~24-28 battles */
    2000,   /* Level 10 (max): ~29-35 battles */
    0xFFFF  /* Sentinel (unreachable) */
};

/*=== Stat Growth Per Level-Up: HP, ATK, DEF, SPD, SP_max ===*/
/* growth_table[0] = bonuses when going from L1 to L2, etc. */
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
 * Expected stats at each level:
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

static void applyLevelUp(void)
{
    u8 idx;

    /* growth_table[0] = L1->L2 bonuses, so index = new_level - 2 */
    idx = rpg_stats.level - 2;
    if (idx >= RPG_MAX_LEVEL) return;

    rpg_stats.max_hp += growth_table[idx][0];
    rpg_stats.atk    += growth_table[idx][1];
    rpg_stats.def    += growth_table[idx][2];
    rpg_stats.spd    += growth_table[idx][3];
    rpg_stats.max_sp += (u8)growth_table[idx][4];

    /* Full heal on level up */
    rpg_stats.hp = rpg_stats.max_hp;
    rpg_stats.sp = rpg_stats.max_sp;
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

void rpgStatsInit(void)
{
    rpg_stats.level    = 1;
    rpg_stats.xp       = 0;
    rpg_stats.xp_to_next = xp_table[1];
    rpg_stats.max_hp   = RPG_BASE_HP;
    rpg_stats.hp       = RPG_BASE_HP;
    rpg_stats.atk      = RPG_BASE_ATK;
    rpg_stats.def      = RPG_BASE_DEF;
    rpg_stats.spd      = RPG_BASE_SPD;
    rpg_stats.max_sp   = RPG_BASE_SP;
    rpg_stats.sp       = RPG_BASE_SP;
    rpg_stats.credits  = 0;
    rpg_stats.total_kills = 0;
}

u8 rpgAddXP(u16 xp)
{
    u8 leveled;

    leveled = 0;
    rpg_stats.xp += xp;

    /* Check for level-ups (can gain multiple levels from large XP awards) */
    while (rpg_stats.level < RPG_MAX_LEVEL &&
           rpg_stats.xp >= xp_table[rpg_stats.level]) {
        rpg_stats.level++;
        applyLevelUp();
        leveled = 1;
    }

    /* Update XP remaining to next level */
    if (rpg_stats.level < RPG_MAX_LEVEL) {
        rpg_stats.xp_to_next = xp_table[rpg_stats.level] - rpg_stats.xp;
    } else {
        rpg_stats.xp_to_next = 0;
    }

    return leveled;
}

void rpgApplyDefeatPenalty(void)
{
    s16 penalty;

    /* Lose ~25% of current HP (shift avoids division on 65816) */
    penalty = rpg_stats.hp >> 2;
    if (penalty < 1) penalty = 1;

    rpg_stats.hp -= penalty;
    if (rpg_stats.hp < 1) rpg_stats.hp = 1;
}

u16 rpgGetXPForLevel(u8 level)
{
    if (level > RPG_MAX_LEVEL) return 0xFFFF;
    return xp_table[level];
}
