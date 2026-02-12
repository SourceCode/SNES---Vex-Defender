# Phase 19: Boss Battles & Special Encounters

## Objective
Implement 3 boss encounters, one per zone, that serve as climactic gameplay events. Each boss is a large, multi-phase enemy with unique attack patterns, a visible HP bar, and a mandatory turn-based battle component. Bosses differ from regular enemies: they are visually larger (composed of multiple sprites), have multiple attack phases, and defeating them advances the zone. The boss fight combines both a brief flight-mode dodging phase and a full turn-based RPG battle.

## Prerequisites
- Phase 9 (Enemy system), Phase 11-12 (Battle engine + UI), Phase 13 (RPG stats for boss scaling), Phase 18 (Zone system triggers boss spawns).

## Detailed Tasks

1. Create `src/game/boss.c` - Boss entity manager separate from the regular enemy pool.
2. Define 3 boss types with stats, attack patterns, and visual configurations:
   - Zone 1 Boss: Scout Commander (medium difficulty, teaches boss mechanics)
   - Zone 2 Boss: Heavy Cruiser (high HP, multi-phase attacks)
   - Zone 3 Boss: Flagship Core (final boss, complex attack patterns)
3. Implement boss sprite rendering using multiple OAM slots (44-51) to create a visually larger enemy (up to 64x64 from four 32x32 sprites).
4. Implement boss HP bar on BG3 (visible during the flight-mode approach phase).
5. Implement the boss encounter sequence: scroll stops, boss enters from top of screen, brief flight-mode dodging phase (30-60 seconds), then mandatory transition to turn-based battle.
6. Implement boss-specific battle stats that scale with zone difficulty.
7. Implement boss-specific enemy AI for turn-based battles: unique action patterns, special attacks, phase transitions at HP thresholds.
8. Wire boss defeat to zone progression: defeating the boss calls zoneBossDefeated() and triggers zone advance.
9. Implement loot rewards: bosses drop guaranteed rare items and large XP/credit bonuses.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/game/boss.h
```c
#ifndef BOSS_H
#define BOSS_H

#include "game.h"

/* Boss type IDs */
#define BOSS_SCOUT_COMMANDER   0  /* Zone 1 boss */
#define BOSS_HEAVY_CRUISER     1  /* Zone 2 boss */
#define BOSS_FLAGSHIP_CORE     2  /* Zone 3 final boss */
#define BOSS_TYPE_COUNT        3

/* Boss encounter phases */
#define BOSS_PHASE_NONE        0  /* No boss active */
#define BOSS_PHASE_ENTERING    1  /* Boss scrolling onto screen */
#define BOSS_PHASE_FLIGHT      2  /* Flight-mode dodging phase */
#define BOSS_PHASE_TRANSITION  3  /* Transitioning to turn-based */
#define BOSS_PHASE_BATTLE      4  /* In turn-based battle */
#define BOSS_PHASE_DEFEATED    5  /* Boss destroyed */
#define BOSS_PHASE_EXPLOSION   6  /* Death explosion sequence */

/* Boss battle AI phases (within turn-based combat) */
#define BOSS_AI_NORMAL         0  /* Standard attacks */
#define BOSS_AI_ENRAGED        1  /* Below 50% HP: more specials */
#define BOSS_AI_DESPERATE      2  /* Below 25% HP: all-out attacks */

/* Boss visual composition (how many 32x32 sprites) */
#define BOSS_SPRITES_SMALL     1  /* 32x32: Scout Commander */
#define BOSS_SPRITES_MEDIUM    2  /* 64x32: Heavy Cruiser */
#define BOSS_SPRITES_LARGE     4  /* 64x64: Flagship Core */

/* Boss type definition (ROM data) */
typedef struct {
    /* Flight-mode stats */
    u16 flight_hp;          /* HP for flight-mode damage phase */
    u8  flight_fire_rate;   /* Bullet fire rate in flight mode */
    u8  flight_pattern;     /* Movement pattern during flight */
    u8  flight_duration;    /* Frames before forced battle transition */
    u8  sprite_count;       /* Number of 32x32 sprites composing boss */

    /* Battle stats */
    s16 battle_hp;          /* HP in turn-based battle */
    s16 battle_atk;         /* Attack power */
    s16 battle_def;         /* Defense */
    s16 battle_spd;         /* Speed (turn order) */
    s16 battle_sp;          /* Special attack charges */
    s16 battle_max_sp;      /* Max SP */

    /* Rewards */
    u16 xp_reward;          /* XP on defeat */
    u16 credit_reward;      /* Credits on defeat */
    u8  drop_item;          /* Guaranteed item drop */

    /* Tile/palette */
    u16 tile_offset;        /* OBJ VRAM tile offset */
    u8  palette;            /* OBJ palette slot */

    /* Display name */
    char name[12];
} BossTypeDef;

/* Boss runtime state */
typedef struct {
    u8  active;             /* 1 if boss is currently in play */
    u8  type;               /* BOSS_* type ID */
    u8  phase;              /* BOSS_PHASE_* */
    u8  ai_phase;           /* BOSS_AI_* (for battle) */
    s16 x, y;              /* Screen position (center of boss) */
    s16 flight_hp;          /* Current HP in flight phase */
    u8  fire_timer;         /* Countdown to next shot */
    u8  pattern_timer;      /* Movement pattern timer */
    s8  pattern_dir;        /* Current movement direction */
    u16 phase_timer;        /* General-purpose phase timer */
    u8  flash_timer;        /* Damage flash */
    u8  explosion_count;    /* Explosions shown during defeat */
} BossState;

extern BossState boss;

/* Initialize boss system */
void bossInit(void);

/* Spawn a boss of the specified type */
void bossSpawn(u8 bossType);

/* Update boss each frame (movement, firing, phase transitions) */
void bossUpdate(void);

/* Render boss sprites */
void bossRender(void);

/* Render boss HP bar on BG3 */
void bossRenderHPBar(void);

/* Apply damage to boss in flight mode. Returns 1 if HP depleted. */
u8 bossDamage(u8 damage);

/* Is a boss currently active? */
u8 bossIsActive(void);

/* Get boss type definition */
const BossTypeDef* bossGetTypeDef(u8 type);

/* Boss-specific battle AI: choose action based on HP phase */
u8 bossChooseBattleAction(void);

/* Load boss graphics into OBJ VRAM */
void bossLoadGraphics(u8 bossType);

#endif
```

### J:/code/snes/snes-rpg-test/src/game/boss.c
```c
/*==============================================================================
 * Boss Battle System
 *
 * Boss encounter flow:
 * 1. Zone scroll reaches boss trigger position
 * 2. Scroll stops, boss enters from top of screen (ENTERING)
 * 3. Flight-mode dodging phase begins (FLIGHT)
 *    - Boss moves in a pattern and fires bullets
 *    - Player can damage boss with bullets (reduces flight_hp)
 *    - After flight_duration frames OR flight_hp depleted,
 *      transition to turn-based battle
 * 4. Screen transitions to battle mode (TRANSITION)
 * 5. Full turn-based RPG battle (BATTLE)
 *    - Boss has unique AI with phase transitions at HP thresholds
 * 6. Boss defeated (DEFEATED)
 *    - Explosion sequence plays
 *    - Rewards applied
 *    - Zone advance triggered
 *============================================================================*/

#include "game/boss.h"
#include "engine/sprites.h"
#include "engine/scroll.h"
#include "engine/sound.h"
#include "engine/fade.h"
#include "engine/bullets.h"
#include "game/player.h"
#include "game/rpg_stats.h"
#include "game/inventory.h"
#include "game/zone.h"
#include "game/game_state.h"
#include "battle/battle_engine.h"
#include "ui/battle_ui.h"

BossState boss;

/* Extern asset labels for boss sprites */
extern char boss_mini_tiles, boss_mini_tiles_end;
extern char boss_mini_pal, boss_mini_pal_end;
extern char boss_final_tiles, boss_final_tiles_end;
extern char boss_final_pal, boss_final_pal_end;

/* Boss OAM slots: 44-51 (8 slots for up to 4 large sprites) */
#define BOSS_OAM_START   44
#define BOSS_OAM_COUNT   8

/* Boss type definitions (ROM data) */
static const BossTypeDef boss_types[BOSS_TYPE_COUNT] = {
    /* Zone 1: Scout Commander
     * Medium difficulty. Teaches player boss mechanics.
     * Single 32x32 sprite. Moderate stats. */
    {
        60,             /* flight_hp */
        40,             /* fire_rate (frames) */
        AI_HOVER,       /* flight_pattern: strafe left/right */
        1800,           /* flight_duration: 30 seconds */
        BOSS_SPRITES_SMALL,
        120,            /* battle_hp */
        18,             /* battle_atk */
        10,             /* battle_def */
        8,              /* battle_spd */
        3,              /* battle_sp */
        3,              /* battle_max_sp */
        100,            /* xp_reward */
        75,             /* credit_reward */
        ITEM_HP_POTION_L, /* guaranteed drop */
        0, 4,           /* tile_offset, palette */
        "COMMANDER"
    },
    /* Zone 2: Heavy Cruiser
     * High HP tank. Two 32x32 sprites (64x32).
     * Fires spreads of bullets. High DEF. */
    {
        100,            /* flight_hp */
        30,             /* fire_rate */
        AI_HOVER,       /* strafe pattern */
        2100,           /* flight_duration: 35 seconds */
        BOSS_SPRITES_MEDIUM,
        200,            /* battle_hp */
        22,             /* battle_atk */
        18,             /* battle_def */
        6,              /* battle_spd */
        4,              /* battle_sp */
        4,              /* battle_max_sp */
        200,            /* xp_reward */
        150,            /* credit_reward */
        ITEM_SP_CHARGE,
        0, 4,
        "CRUISER"
    },
    /* Zone 3: Flagship Core
     * Final boss. Four 32x32 sprites (64x64).
     * Complex attack patterns. Three AI phases. */
    {
        150,            /* flight_hp */
        25,             /* fire_rate */
        AI_HOVER,       /* strafe pattern */
        2400,           /* flight_duration: 40 seconds */
        BOSS_SPRITES_LARGE,
        350,            /* battle_hp */
        30,             /* battle_atk */
        22,             /* battle_def */
        12,             /* battle_spd */
        6,              /* battle_sp */
        6,              /* battle_max_sp */
        400,            /* xp_reward */
        300,            /* credit_reward */
        ITEM_FULL_RESTORE,
        0, 4,
        "FLAGSHIP"
    },
};

void bossInit(void)
{
    boss.active = 0;
    boss.type = 0;
    boss.phase = BOSS_PHASE_NONE;
}

void bossLoadGraphics(u8 bossType)
{
    /* Load boss sprite tiles into OBJ VRAM at the battle sprite area */
    /* Boss sprites use OAM slots 44-51, OBJ tiles at OBJ_UI_OFFSET area */
    switch (bossType) {
        case BOSS_SCOUT_COMMANDER:
            /* Reuses enemy scout graphics at larger scale */
            /* Or load dedicated boss_mini asset */
            spriteLoadTiles(&boss_mini_tiles,
                (u16)(&boss_mini_tiles_end - &boss_mini_tiles),
                OBJ_UI_OFFSET);
            spriteLoadPalette(&boss_mini_pal,
                (u16)(&boss_mini_pal_end - &boss_mini_pal), 4);
            break;

        case BOSS_HEAVY_CRUISER:
            /* Load cruiser sprite (larger) */
            spriteLoadTiles(&boss_mini_tiles,
                (u16)(&boss_mini_tiles_end - &boss_mini_tiles),
                OBJ_UI_OFFSET);
            spriteLoadPalette(&boss_mini_pal,
                (u16)(&boss_mini_pal_end - &boss_mini_pal), 4);
            break;

        case BOSS_FLAGSHIP_CORE:
            spriteLoadTiles(&boss_final_tiles,
                (u16)(&boss_final_tiles_end - &boss_final_tiles),
                OBJ_UI_OFFSET);
            spriteLoadPalette(&boss_final_pal,
                (u16)(&boss_final_pal_end - &boss_final_pal), 4);
            break;
    }
}

void bossSpawn(u8 bossType)
{
    const BossTypeDef *def;

    if (bossType >= BOSS_TYPE_COUNT) return;
    def = &boss_types[bossType];

    boss.active = 1;
    boss.type = bossType;
    boss.phase = BOSS_PHASE_ENTERING;
    boss.ai_phase = BOSS_AI_NORMAL;
    boss.x = 128 - 16;  /* Center of screen */
    boss.y = -48;        /* Start above screen */
    boss.flight_hp = (s16)def->flight_hp;
    boss.fire_timer = def->flight_fire_rate;
    boss.pattern_timer = 0;
    boss.pattern_dir = 1;
    boss.phase_timer = 0;
    boss.flash_timer = 0;
    boss.explosion_count = 0;

    /* Load boss graphics */
    bossLoadGraphics(bossType);

    /* Stop scrolling during boss fight */
    scrollSetSpeed(SCROLL_SPEED_STOP);

    /* Play boss music */
    soundPlayMusic(MUSIC_BOSS);
}

static void updateEntering(void)
{
    /* Boss descends to position y=30 */
    boss.y += 1;
    if (boss.y >= 30) {
        boss.y = 30;
        boss.phase = BOSS_PHASE_FLIGHT;
        boss.phase_timer = boss_types[boss.type].flight_duration;
    }
}

static void updateFlight(void)
{
    const BossTypeDef *def = &boss_types[boss.type];

    /* Strafe movement */
    boss.pattern_timer++;
    boss.x += boss.pattern_dir * 1;

    /* Bounce off screen edges */
    if (boss.x <= 16) {
        boss.pattern_dir = 1;
    } else if (boss.x >= 200) {
        boss.pattern_dir = -1;
    }

    /* Firing */
    boss.fire_timer--;
    if (boss.fire_timer == 0) {
        boss.fire_timer = def->flight_fire_rate;

        /* Boss fires multiple bullets based on type */
        switch (boss.type) {
            case BOSS_SCOUT_COMMANDER:
                /* Single aimed shot */
                bulletEnemyFire(boss.x + 16, boss.y + 32,
                               player.x + 16, player.y + 16,
                               BULLET_TYPE_ENEMY_AIMED);
                break;

            case BOSS_HEAVY_CRUISER:
                /* Three-spread shot */
                bulletEnemyFireDown(boss.x, boss.y + 32);
                bulletEnemyFireDown(boss.x + 16, boss.y + 32);
                bulletEnemyFireDown(boss.x + 32, boss.y + 32);
                break;

            case BOSS_FLAGSHIP_CORE:
                /* Aimed + spread combination */
                bulletEnemyFire(boss.x + 16, boss.y + 48,
                               player.x + 16, player.y + 16,
                               BULLET_TYPE_ENEMY_AIMED);
                bulletEnemyFireDown(boss.x - 8, boss.y + 48);
                bulletEnemyFireDown(boss.x + 40, boss.y + 48);
                break;
        }
    }

    /* Countdown to mandatory battle transition */
    boss.phase_timer--;
    if (boss.phase_timer == 0 || boss.flight_hp <= 0) {
        boss.phase = BOSS_PHASE_TRANSITION;
        boss.phase_timer = 60; /* 1 second transition delay */
    }

    /* Damage flash */
    if (boss.flash_timer > 0) boss.flash_timer--;
}

static void updateTransition(void)
{
    boss.phase_timer--;
    if (boss.phase_timer == 0) {
        /* Enter turn-based battle */
        boss.phase = BOSS_PHASE_BATTLE;

        /* Set up battle with boss stats */
        gameStateSetParam((u16)(boss.type + 100)); /* 100+ = boss type */
        gameStateChange(GS_BATTLE, TRANS_FADE);
    }
}

static void updateDefeated(void)
{
    const BossTypeDef *def = &boss_types[boss.type];

    /* Explosion sequence */
    boss.phase_timer++;

    /* Spawn visual explosions at random offsets */
    if ((boss.phase_timer & 0x07) == 0 && boss.explosion_count < 8) {
        /* Flash effect using palette swap */
        boss.flash_timer = 4;
        boss.explosion_count++;
        soundPlaySFX(SFX_EXPLOSION);
    }

    /* After 120 frames (2 seconds), clean up */
    if (boss.phase_timer >= 120) {
        boss.active = 0;
        boss.phase = BOSS_PHASE_NONE;

        /* Hide boss sprites */
        {
            u8 i;
            for (i = 0; i < BOSS_OAM_COUNT; i++) {
                oamSetVisible((BOSS_OAM_START + i) * 4, OBJ_HIDE);
            }
        }

        /* Apply rewards */
        rpgAddXP(def->xp_reward);
        rpgAddCredits(def->credit_reward);

        /* Guaranteed item drop */
        if (def->drop_item != ITEM_NONE) {
            inventoryAdd(def->drop_item, 1);
        }

        /* Level up SFX if applicable */
        soundPlaySFX(SFX_LEVEL_UP);

        /* Notify zone system */
        zoneBossDefeated();
    }
}

void bossUpdate(void)
{
    if (!boss.active) return;

    switch (boss.phase) {
        case BOSS_PHASE_ENTERING:
            updateEntering();
            break;
        case BOSS_PHASE_FLIGHT:
            updateFlight();
            break;
        case BOSS_PHASE_TRANSITION:
            updateTransition();
            break;
        case BOSS_PHASE_DEFEATED:
            updateDefeated();
            break;
        case BOSS_PHASE_EXPLOSION:
            updateDefeated();  /* Same logic */
            break;
    }
}

void bossRender(void)
{
    const BossTypeDef *def;
    u8 pal;
    u16 oam_id;

    if (!boss.active || boss.phase == BOSS_PHASE_NONE) return;
    if (boss.phase == BOSS_PHASE_BATTLE) return; /* Hidden during battle */

    def = &boss_types[boss.type];
    pal = (boss.flash_timer > 0) ? 7 : def->palette;

    /* Render boss sprites based on composition */
    switch (def->sprite_count) {
        case BOSS_SPRITES_SMALL: /* 1 sprite: 32x32 */
            oam_id = BOSS_OAM_START * 4;
            oamSet(oam_id, (u16)boss.x, (u16)boss.y,
                   2, 0, 0,
                   OBJ_UI_OFFSET >> 4, pal);
            oamSetEx(oam_id, OBJ_LARGE, OBJ_SHOW);
            break;

        case BOSS_SPRITES_MEDIUM: /* 2 sprites: 64x32 */
            oam_id = BOSS_OAM_START * 4;
            oamSet(oam_id, (u16)boss.x, (u16)boss.y,
                   2, 0, 0,
                   OBJ_UI_OFFSET >> 4, pal);
            oamSetEx(oam_id, OBJ_LARGE, OBJ_SHOW);

            oam_id = (BOSS_OAM_START + 1) * 4;
            oamSet(oam_id, (u16)(boss.x + 32), (u16)boss.y,
                   2, 0, 0,
                   (OBJ_UI_OFFSET >> 4) + 4, pal);
            oamSetEx(oam_id, OBJ_LARGE, OBJ_SHOW);
            break;

        case BOSS_SPRITES_LARGE: /* 4 sprites: 64x64 */
            oam_id = BOSS_OAM_START * 4;
            oamSet(oam_id, (u16)boss.x, (u16)boss.y,
                   2, 0, 0,
                   OBJ_UI_OFFSET >> 4, pal);
            oamSetEx(oam_id, OBJ_LARGE, OBJ_SHOW);

            oam_id = (BOSS_OAM_START + 1) * 4;
            oamSet(oam_id, (u16)(boss.x + 32), (u16)boss.y,
                   2, 0, 0,
                   (OBJ_UI_OFFSET >> 4) + 4, pal);
            oamSetEx(oam_id, OBJ_LARGE, OBJ_SHOW);

            oam_id = (BOSS_OAM_START + 2) * 4;
            oamSet(oam_id, (u16)boss.x, (u16)(boss.y + 32),
                   2, 0, 0,
                   (OBJ_UI_OFFSET >> 4) + 8, pal);
            oamSetEx(oam_id, OBJ_LARGE, OBJ_SHOW);

            oam_id = (BOSS_OAM_START + 3) * 4;
            oamSet(oam_id, (u16)(boss.x + 32), (u16)(boss.y + 32),
                   2, 0, 0,
                   (OBJ_UI_OFFSET >> 4) + 12, pal);
            oamSetEx(oam_id, OBJ_LARGE, OBJ_SHOW);
            break;
    }
}

void bossRenderHPBar(void)
{
    const BossTypeDef *def;
    u8 filled;

    if (!boss.active || boss.phase == BOSS_PHASE_BATTLE) return;
    if (boss.phase == BOSS_PHASE_ENTERING) return;

    def = &boss_types[boss.type];

    /* Draw boss name and HP bar at top of screen */
    consoleDrawText(2, 0, "%s", def->name);

    /* HP bar: 20 characters wide */
    if (def->flight_hp > 0) {
        filled = (u8)((u16)boss.flight_hp * 20 / def->flight_hp);
    } else {
        filled = 0;
    }

    consoleDrawText(2, 1, "HP:");
    {
        u8 i;
        for (i = 0; i < 20; i++) {
            if (i < filled) {
                consoleDrawText(5 + i, 1, "=");
            } else {
                consoleDrawText(5 + i, 1, "-");
            }
        }
    }
}

u8 bossDamage(u8 damage)
{
    if (!boss.active || boss.phase != BOSS_PHASE_FLIGHT) return 0;

    if (boss.flight_hp <= (s16)damage) {
        boss.flight_hp = 0;
        return 1; /* HP depleted, will transition to battle */
    }

    boss.flight_hp -= (s16)damage;
    boss.flash_timer = 4;
    soundPlaySFX(SFX_HIT_DAMAGE);
    return 0;
}

u8 bossIsActive(void)
{
    return boss.active;
}

const BossTypeDef* bossGetTypeDef(u8 type)
{
    if (type >= BOSS_TYPE_COUNT) return &boss_types[0];
    return &boss_types[type];
}

u8 bossChooseBattleAction(void)
{
    u8 r = (u8)(g_frame_count * 7 + battle.turn_number * 13) & 0x0F;
    const BossTypeDef *def = &boss_types[boss.type];

    /* Determine AI phase based on current battle HP */
    if (battle.enemy.hp < battle.enemy.max_hp / 4) {
        boss.ai_phase = BOSS_AI_DESPERATE;
    } else if (battle.enemy.hp < battle.enemy.max_hp / 2) {
        boss.ai_phase = BOSS_AI_ENRAGED;
    } else {
        boss.ai_phase = BOSS_AI_NORMAL;
    }

    switch (boss.ai_phase) {
        case BOSS_AI_NORMAL:
            /* Standard: mostly attack, occasional special */
            if (r < 10) return BACT_ATTACK;
            if (r < 13 && battle.enemy.sp > 0) return BACT_SPECIAL;
            return BACT_DEFEND;

        case BOSS_AI_ENRAGED:
            /* Aggressive: more specials, no defending */
            if (r < 7) return BACT_ATTACK;
            if (r < 12 && battle.enemy.sp > 0) return BACT_SPECIAL;
            return BACT_ATTACK;  /* Attack if no SP */

        case BOSS_AI_DESPERATE:
            /* All-out: special whenever possible */
            if (battle.enemy.sp > 0) return BACT_SPECIAL;
            if (r < 6) return BACT_ATTACK;
            return BACT_DEFEND;  /* Occasionally defend to prolong */
    }

    return BACT_ATTACK;
}
```

### Integration with battle_engine.c
```c
/* Modify battleStart() to handle boss battles (param >= 100): */

void battleStart(u8 enemyType)
{
    u8 isBoss = 0;
    u8 bossType = 0;

    if (enemyType >= 100) {
        /* Boss battle */
        isBoss = 1;
        bossType = enemyType - 100;
        if (bossType >= BOSS_TYPE_COUNT) bossType = 0;
    }

    battle.state = BSTATE_INIT;
    battle.turn_number = 0;

    /* Initialize player combatant from RPG stats */
    rpgPopulateBattleStats(&battle.player);

    if (isBoss) {
        /* Initialize enemy from boss type */
        const BossTypeDef *bdef = bossGetTypeDef(bossType);
        battle.enemy.hp = bdef->battle_hp;
        battle.enemy.max_hp = bdef->battle_hp;
        battle.enemy.atk = bdef->battle_atk;
        battle.enemy.def = bdef->battle_def;
        battle.enemy.spd = bdef->battle_spd;
        battle.enemy.sp = bdef->battle_sp;
        battle.enemy.max_sp = bdef->battle_max_sp;
        battle.enemy.level = (bossType + 1) * 3;  /* L3, L6, L9 */
        battle.enemy.defending = 0;
        battle.enemy.enemy_type = bossType;
        battle.enemy.is_player = 0;
        /* Copy boss name */
        {
            u8 ni;
            for (ni = 0; ni < 11 && bdef->name[ni]; ni++) {
                battle.enemy.name[ni] = bdef->name[ni];
            }
            battle.enemy.name[ni] = 0;
        }
        battle.result.xp_gained = bdef->xp_reward;
        battle.result.credits_gained = bdef->credit_reward;
    } else {
        /* Standard enemy initialization (existing code) */
        /* ... existing enemyType lookup ... */
    }

    /* Determine turn order */
    battle.player_goes_first =
        (battle.player.spd >= battle.enemy.spd) ? 1 : 0;

    battle.message_id = MSG_BATTLE_START;
    battle.anim_timer = 60;
    battle.last_damage = 0;
    battle.result.outcome = 0;
}
```

### Integration with battle_engine.c enemyChooseAction()
```c
/* Replace or extend enemyChooseAction() to use boss AI: */

static u8 enemyChooseAction(void)
{
    /* Check if this is a boss battle */
    if (boss.active && boss.phase == BOSS_PHASE_BATTLE) {
        return bossChooseBattleAction();
    }

    /* Standard enemy AI (existing code) */
    u8 r = (u8)(g_frame_count * 7 + battle.turn_number * 13) & 0x0F;
    /* ... rest of existing code ... */
}
```

### Integration with game_state.c updateBattle()
```c
/* After battle ends, check if it was a boss battle: */

if (!battleUpdate()) {
    BattleResult *result = battleGetResult();
    rpgApplyBattleResult(result);

    if (result->outcome == 1) {
        /* Victory */
        u8 drop = inventoryRollDrop(battle.enemy.enemy_type);
        if (drop != ITEM_NONE) inventoryAdd(drop, 1);

        /* Check if this was a boss battle */
        if (boss.active && boss.phase == BOSS_PHASE_BATTLE) {
            boss.phase = BOSS_PHASE_DEFEATED;
            boss.phase_timer = 0;
        }
    }

    battleUIExit();
    gameStateChange(GS_FLIGHT, TRANS_FADE);
    return;
}
```

### Integration with collision.c
```c
/* Add boss collision check to collisionCheckAll(): */

/* Check player bullets vs boss */
if (bossIsActive()) {
    for (b = 0; b < MAX_BULLETS; b++) {
        if (!bullets[b].active || !bullets[b].is_player) continue;
        /* Simple AABB: boss hitbox at (boss.x, boss.y, width, height) */
        u8 bw = boss_types[boss.type].sprite_count >= 2 ? 64 : 32;
        u8 bh = boss_types[boss.type].sprite_count >= 4 ? 64 : 32;
        if (collisionCheckAABB(
                bullets[b].x, bullets[b].y, 8, 8,
                boss.x, boss.y, bw, bh)) {
            bossDamage(10);  /* Player bullet does 10 flight damage */
            bullets[b].active = 0;
        }
    }
}
```

### Integration with updateFlight() in game_state.c
```c
/* Add boss update and render calls to updateFlight(): */

/* After other updates: */
if (bossIsActive()) {
    bossUpdate();
}

/* After other renders: */
if (bossIsActive()) {
    bossRender();
    bossRenderHPBar();
}
```

### data.asm Additions
```asm
;----------------------------------------------------------------------
; Boss Sprites
;----------------------------------------------------------------------
.section ".rodata_spr_boss" superfree

boss_mini_tiles:
.incbin "assets/sprites/boss_mini.pic"
boss_mini_tiles_end:

boss_mini_pal:
.incbin "assets/sprites/boss_mini.pal"
boss_mini_pal_end:

boss_final_tiles:
.incbin "assets/sprites/boss_final.pic"
boss_final_tiles_end:

boss_final_pal:
.incbin "assets/sprites/boss_final.pal"
boss_final_pal_end:

.ends
```

## Technical Specifications

### Boss Stat Scaling
```
Boss     Zone  FlightHP  BattleHP  ATK  DEF  SPD  SP  XP   Credits
------   ----  --------  --------  ---  ---  ---  --  ---  -------
Cmdr     1     60        120       18   10   8    3   100  75
Cruiser  2     100       200       22   18   6    4   200  150
Core     3     150       350       30   22   12   6   400  300

Comparison with player stats at expected level:
  Zone 1 boss vs Level 3 player: Player HP=110, ATK=16, DEF=9
    Damage to boss: 16*16/(16+10) = 9.8 -> 10/turn
    Boss HP 120 / 10 damage = 12 turns
    Boss damage to player: 18*18/(18+9) = 12/turn
    Player HP 110 / 12 damage = 9 turns (tight, player needs defend/heal)

  Zone 3 boss vs Level 8 player: Player HP=230, ATK=33, DEF=22
    Damage to boss: 33*33/(33+22) = 19.8 -> 20/turn
    Boss HP 350 / 20 damage = 17.5 turns
    Boss damage to player: 30*30/(30+22) = 17.3/turn
    Player HP 230 / 17 damage = 13.5 turns

These are approximate. Defending, specials, and items shift the balance.
Boss battles should take 1-2 minutes each.
```

### Boss Flight-Mode Phase
```
Duration: 30-40 seconds (1800-2400 frames)
Purpose: Build tension before the RPG battle
Mechanics:
  - Boss strafes left/right at the top of the screen
  - Boss fires bullets at regular intervals
  - Player can shoot the boss, reducing flight_hp
  - Reducing flight_hp to 0 forces early battle transition
    (boss enters battle at full battle_hp regardless)
  - If timer expires, battle transition happens automatically

The flight phase is a skill check:
  - Good players take less chip damage before the battle
  - The boss's flight HP does NOT affect its battle HP
    (flight phase is a dodging challenge, not a DPS check)
```

### Multi-Sprite Boss Rendering
```
BOSS_SPRITES_SMALL (1x1 = 32x32):
  OAM slot 44: (x, y)

BOSS_SPRITES_MEDIUM (2x1 = 64x32):
  OAM slot 44: (x, y)         - left half
  OAM slot 45: (x+32, y)      - right half

BOSS_SPRITES_LARGE (2x2 = 64x64):
  OAM slot 44: (x, y)         - top-left
  OAM slot 45: (x+32, y)      - top-right
  OAM slot 46: (x, y+32)      - bottom-left
  OAM slot 47: (x+32, y+32)   - bottom-right

All sprites use the same palette (slot 4: boss palette).
Tile numbers are offset by 4 per sprite (each 32x32 = 4x4 = 16 tiles).

Boss sprites use the UI OAM range (44-51), which does not
conflict with regular enemies (4-11) or bullets (12-35).
```

### Boss Collision Hitbox
```
The boss hitbox matches its visual size:
  Scout Commander: 32x32 hitbox at (boss.x, boss.y)
  Heavy Cruiser:   64x32 hitbox
  Flagship Core:   64x64 hitbox

Player bullets that hit the boss in flight mode deal 10 damage each.
A maxed bullet rate of 1 shot per 8 frames = 7.5 shots/second.
Scout Commander flight HP 60: depleted in ~8 seconds of continuous fire.
Flagship Core flight HP 150: depleted in ~20 seconds.
```

### Memory Budget
```
Boss state: 24 bytes WRAM
Boss type defs (ROM): 3 * 38 bytes = 114 bytes
Boss AI logic (ROM): ~1KB code
Total WRAM: 24 bytes
Total ROM: ~1.2KB
```

## Asset Requirements
| Asset | Source | Size | Colors | Boss |
|-------|--------|------|--------|------|
| Mini-boss | ship070.png | 32x32 | 16 | Scout Commander / Cruiser |
| Final boss | ship090.png | 32x32 | 16 | Flagship Core (tiled 2x2) |

### Makefile Additions
```makefile
assets/sprites/boss_mini.png: tools/convert_sprite.py
	$(PYTHON) tools/convert_sprite.py \
		"G:/2024-unity/0-GameAssets/shooter/ship070.png" $@ --size 32 --colors 15

assets/sprites/boss_mini.pic: assets/sprites/boss_mini.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -R -i $<

assets/sprites/boss_final.png: tools/convert_sprite.py
	$(PYTHON) tools/convert_sprite.py \
		"G:/2024-unity/0-GameAssets/shooter/ship090.png" $@ --size 32 --colors 15

assets/sprites/boss_final.pic: assets/sprites/boss_final.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -R -i $<
```

## Acceptance Criteria
1. Reaching the boss trigger position in each zone spawns the correct boss.
2. Boss enters from the top of the screen with a smooth descent animation.
3. Boss HP bar is visible at the top of the screen during the flight phase.
4. Player bullets damage the boss (HP bar decreases, damage flash occurs).
5. Boss fires bullets at the player during the flight phase.
6. After the flight phase timer expires, battle transition begins automatically.
7. The turn-based battle uses the correct boss stats (HP, ATK, DEF, etc.).
8. Boss AI changes behavior at HP thresholds (normal/enraged/desperate).
9. Defeating the boss in battle triggers the explosion sequence.
10. Boss rewards (XP, credits, item) are correctly applied after defeat.
11. Zone 1 and Zone 2 boss defeat leads to zone advance.
12. Zone 3 boss defeat triggers GS_VICTORY (game complete).
13. Multi-sprite bosses (cruiser, flagship) render as cohesive entities.
14. Boss battle takes 1-2 minutes, creating a satisfying climactic fight.

## SNES-Specific Constraints
- Multi-sprite boss uses OAM slots 44-47 (4 large sprites). The 32-sprite-per-scanline limit could be an issue if the boss occupies the same scanlines as many bullets. In practice, bullets are small (16x16) and the boss is at the top of screen while bullets are mid-screen.
- Boss explosion sequence reuses the damage flash palette trick (palette 7 = all white) rather than loading actual explosion sprites. This saves VRAM.
- The boss HP bar uses consoleDrawText on BG3 row 0-1. This conflicts with nothing during flight mode (dialog uses rows 20-25, pause uses rows 10-17).
- Boss tile data loaded into OBJ_UI_OFFSET ($0C00). For the 64x64 boss, this is 4 * 512 bytes = 2KB of OBJ VRAM, fitting within the allocated space.

## Estimated Complexity
**Complex** - Boss encounters integrate flight combat, turn-based battles, multi-sprite rendering, AI phase transitions, and zone progression. The boss battle must feel challenging but fair, requiring careful stat balancing. The transition between flight and battle mode for bosses is the most complex game state transition in the entire game.
