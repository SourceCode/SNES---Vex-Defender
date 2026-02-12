# Phase 18: Zone Progression & Level Design

## Objective
Implement the 3-zone game structure that forms the complete playthrough: Zone 1 (Debris Field), Zone 2 (Asteroid Belt), Zone 3 (Flagship Approach). Each zone has unique backgrounds, enemy compositions, scroll speeds, and difficulty curves. Zones transition seamlessly when the player defeats the zone boss or reaches the zone boundary. This phase also loads zone-specific graphics, configures enemy spawn waves, and manages the overall game progression from start to finish.

## Prerequisites
- Phase 4 (Background loading), Phase 7 (Scroll triggers), Phase 9 (Enemy spawning), Phase 15 (State machine), Phase 16 (Story triggers), Phase 17 (Zone music).

## Detailed Tasks

1. Create `src/game/zone.c` - Zone manager that handles loading, transitioning, and tracking the current zone.
2. Define zone data tables: each zone specifies its background asset, enemy roster, scroll speed, BG palette, star density, and total scroll distance.
3. Implement zone loading: swap BG tiles/palette/map in VRAM, load zone-appropriate enemy graphics, register scroll triggers for enemy waves and story events.
4. Implement zone transitions: when zone distance is reached, trigger a fade-to-white, load next zone, fade back in.
5. Design enemy wave patterns for each zone: 20-25 spawn events per zone, mixing enemy types and formations.
6. Implement difficulty curve: later zones use faster enemies, shorter fire timers, and more complex AI patterns.
7. Track game progress: current zone, distance within zone, enemies defeated per zone.
8. Implement the end-of-game trigger: after defeating Zone 3 boss, transition to GS_VICTORY.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/game/zone.h
```c
#ifndef ZONE_H
#define ZONE_H

#include "game.h"

/* Zone IDs */
#define ZONE_DEBRIS     0   /* Zone 1: Debris Field */
#define ZONE_ASTEROID   1   /* Zone 2: Asteroid Belt */
#define ZONE_FLAGSHIP   2   /* Zone 3: Flagship Approach */
#define ZONE_COUNT      3

/* Zone data structure (ROM constant per zone) */
typedef struct {
    u8  zone_id;
    u8  bg_zone_id;         /* Background asset ID for bgLoadZone() */
    u8  default_speed;      /* Default scroll speed (SCROLL_SPEED_*) */
    u8  enemy_count;        /* Number of regular enemy types available */
    u8  enemy_types[4];     /* Which ENEMY_TYPE_* appear in this zone */
    u16 zone_length;        /* Total scroll distance for this zone */
    u16 boss_trigger_pos;   /* Scroll distance at which boss appears */
    u8  boss_type;          /* Boss enemy type ID (from Phase 19) */
    u8  music_id;           /* MUSIC_* for this zone's flight theme */
} ZoneDef;

/* Zone runtime state */
typedef struct {
    u8  current_zone;       /* ZONE_DEBRIS, ZONE_ASTEROID, etc. */
    u16 zone_distance;      /* Distance scrolled within current zone */
    u8  boss_spawned;       /* 1 if zone boss has been spawned */
    u8  boss_defeated;      /* 1 if zone boss has been defeated */
    u16 enemies_killed;     /* Enemies killed in this zone */
    u8  zone_complete;      /* 1 when ready to transition */
} ZoneState;

extern ZoneState zone_state;

/* Initialize zone system */
void zoneInit(void);

/* Load a zone: swap graphics, configure enemies, register triggers */
void zoneLoad(u8 zoneId);

/* Update zone state each frame (check progression, transitions) */
void zoneUpdate(void);

/* Advance to the next zone */
void zoneAdvance(void);

/* Get the zone definition for a zone */
const ZoneDef* zoneGetDef(u8 zoneId);

/* Get current zone ID */
u8 zoneGetCurrent(void);

/* Called when a boss is defeated */
void zoneBossDefeated(void);

/* Register all enemy wave triggers for the current zone */
void zoneRegisterWaveTriggers(u8 zoneId);

/* Get total enemies killed across all zones */
u16 zoneGetTotalKills(void);

#endif
```

### J:/code/snes/snes-rpg-test/src/game/zone.c
```c
/*==============================================================================
 * Zone Progression & Level Design
 *
 * Game structure (3 zones, ~3.5 minutes each):
 *
 * Zone 1 - Debris Field (scroll 0 to 5400)
 *   Enemy roster: Scout, Fighter
 *   Difficulty: Easy. Scouts mostly, a few Fighters.
 *   Boss: Scout Commander (Phase 19)
 *
 * Zone 2 - Asteroid Belt (scroll 0 to 5400)
 *   Enemy roster: Fighter, Heavy
 *   Difficulty: Medium. More enemies, faster fire rates.
 *   Boss: Heavy Cruiser (Phase 19)
 *
 * Zone 3 - Flagship Approach (scroll 0 to 5400)
 *   Enemy roster: Heavy, Elite
 *   Difficulty: Hard. Fast enemies, aggressive AI.
 *   Boss: Flagship Core (Phase 19)
 *============================================================================*/

#include "game/zone.h"
#include "engine/scroll.h"
#include "engine/sound.h"
#include "engine/background.h"
#include "engine/sprites.h"
#include "engine/fade.h"
#include "game/enemies.h"
#include "game/player.h"
#include "game/story.h"
#include "game/game_state.h"

ZoneState zone_state;

/* Extern asset labels for zone-specific enemy graphics */
extern char enemy_scout_tiles, enemy_scout_tiles_end;
extern char enemy_scout_pal, enemy_scout_pal_end;
extern char enemy_fighter_tiles, enemy_fighter_tiles_end;
extern char enemy_fighter_pal, enemy_fighter_pal_end;
extern char enemy_heavy_tiles, enemy_heavy_tiles_end;
extern char enemy_heavy_pal, enemy_heavy_pal_end;
extern char enemy_elite_tiles, enemy_elite_tiles_end;
extern char enemy_elite_pal, enemy_elite_pal_end;

/* Zone definitions (ROM constant data) */
static const ZoneDef zone_defs[ZONE_COUNT] = {
    /* Zone 1: Debris Field */
    {
        ZONE_DEBRIS,
        ZONE_DEBRIS,            /* bg asset */
        SCROLL_SPEED_NORMAL,    /* 0.5 px/frame */
        2,                      /* 2 enemy types */
        { ENEMY_TYPE_SCOUT, ENEMY_TYPE_FIGHTER, 0, 0 },
        5400,                   /* zone length in scroll pixels */
        5000,                   /* boss trigger at 5000 */
        0,                      /* boss type (Phase 19) */
        MUSIC_FLIGHT_ZONE1
    },
    /* Zone 2: Asteroid Belt */
    {
        ZONE_ASTEROID,
        ZONE_ASTEROID,
        SCROLL_SPEED_NORMAL,
        2,
        { ENEMY_TYPE_FIGHTER, ENEMY_TYPE_HEAVY, 0, 0 },
        5400,
        5000,
        1,
        MUSIC_FLIGHT_ZONE2
    },
    /* Zone 3: Flagship Approach */
    {
        ZONE_FLAGSHIP,
        ZONE_FLAGSHIP,
        SCROLL_SPEED_FAST,      /* Faster scroll for intensity */
        2,
        { ENEMY_TYPE_HEAVY, ENEMY_TYPE_ELITE, 0, 0 },
        5400,
        5000,
        2,
        MUSIC_FLIGHT_ZONE3
    },
};

/* Cumulative kill counter */
static u16 total_kills;

/* ====================================================================
 * Enemy Wave Trigger Callbacks - Zone 1
 * ==================================================================== */

/* Wave naming: z1_wXX = zone 1, wave XX */
static void z1_w01(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 2, 60, -20, 60, 0); }
static void z1_w02(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 3, 40, -20, 50, 0); }
static void z1_w03(void) { enemySpawnFromLeft(ENEMY_TYPE_SCOUT, 30); }
static void z1_w04(void) { enemySpawnFromRight(ENEMY_TYPE_SCOUT, 50); }
static void z1_w05(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 4, 30, -20, 48, 0); }
static void z1_w06(void) { enemySpawn(ENEMY_TYPE_FIGHTER, 120, -32); }
static void z1_w07(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 3, 50, -30, 60, -10); }
static void z1_w08(void) {
    enemySpawnFromLeft(ENEMY_TYPE_SCOUT, 20);
    enemySpawnFromRight(ENEMY_TYPE_SCOUT, 20);
}
static void z1_w09(void) { enemySpawn(ENEMY_TYPE_FIGHTER, 60, -32); }
static void z1_w10(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 3, 80, -20, 40, 0); }
static void z1_w11(void) {
    enemySpawn(ENEMY_TYPE_FIGHTER, 80, -32);
    enemySpawn(ENEMY_TYPE_FIGHTER, 160, -32);
}
static void z1_w12(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 5, 20, -20, 44, 0); }
static void z1_w13(void) {
    enemySpawnFromLeft(ENEMY_TYPE_FIGHTER, 40);
    enemySpawnWave(ENEMY_TYPE_SCOUT, 2, 100, -20, 50, 0);
}
static void z1_w14(void) { enemySpawn(ENEMY_TYPE_FIGHTER, 120, -32); }
static void z1_w15(void) {
    /* Pre-boss wave: clear screen then slow scroll */
    scrollTransitionSpeed(SCROLL_SPEED_SLOW, 60);
}

/* ====================================================================
 * Enemy Wave Trigger Callbacks - Zone 2
 * ==================================================================== */

static void z2_w01(void) { enemySpawnWave(ENEMY_TYPE_FIGHTER, 2, 80, -20, 80, 0); }
static void z2_w02(void) {
    enemySpawnFromLeft(ENEMY_TYPE_FIGHTER, 30);
    enemySpawnFromRight(ENEMY_TYPE_FIGHTER, 50);
}
static void z2_w03(void) { enemySpawnWave(ENEMY_TYPE_FIGHTER, 3, 40, -20, 60, 0); }
static void z2_w04(void) { enemySpawn(ENEMY_TYPE_HEAVY, 120, -32); }
static void z2_w05(void) {
    enemySpawnWave(ENEMY_TYPE_FIGHTER, 2, 60, -20, 100, 0);
    enemySpawnFromLeft(ENEMY_TYPE_FIGHTER, 80);
}
static void z2_w06(void) { enemySpawnWave(ENEMY_TYPE_FIGHTER, 4, 30, -20, 50, 0); }
static void z2_w07(void) {
    enemySpawn(ENEMY_TYPE_HEAVY, 60, -32);
    enemySpawn(ENEMY_TYPE_HEAVY, 180, -32);
}
static void z2_w08(void) { enemySpawnWave(ENEMY_TYPE_FIGHTER, 3, 50, -30, 60, -10); }
static void z2_w09(void) {
    enemySpawnFromLeft(ENEMY_TYPE_HEAVY, 40);
    enemySpawnWave(ENEMY_TYPE_FIGHTER, 2, 120, -20, 50, 0);
}
static void z2_w10(void) { enemySpawnWave(ENEMY_TYPE_FIGHTER, 5, 20, -20, 44, 0); }
static void z2_w11(void) {
    enemySpawn(ENEMY_TYPE_HEAVY, 120, -32);
    enemySpawnFromRight(ENEMY_TYPE_FIGHTER, 20);
}
static void z2_w12(void) { scrollTransitionSpeed(SCROLL_SPEED_SLOW, 60); }

/* ====================================================================
 * Enemy Wave Trigger Callbacks - Zone 3
 * ==================================================================== */

static void z3_w01(void) {
    enemySpawn(ENEMY_TYPE_HEAVY, 80, -32);
    enemySpawn(ENEMY_TYPE_HEAVY, 160, -32);
}
static void z3_w02(void) { enemySpawnWave(ENEMY_TYPE_ELITE, 2, 60, -20, 120, 0); }
static void z3_w03(void) {
    enemySpawnFromLeft(ENEMY_TYPE_ELITE, 30);
    enemySpawnFromRight(ENEMY_TYPE_ELITE, 50);
}
static void z3_w04(void) { enemySpawnWave(ENEMY_TYPE_HEAVY, 3, 40, -20, 70, 0); }
static void z3_w05(void) {
    enemySpawn(ENEMY_TYPE_ELITE, 120, -32);
    enemySpawnWave(ENEMY_TYPE_HEAVY, 2, 40, -20, 140, 0);
}
static void z3_w06(void) { enemySpawnWave(ENEMY_TYPE_ELITE, 3, 40, -30, 70, -10); }
static void z3_w07(void) {
    enemySpawnFromLeft(ENEMY_TYPE_HEAVY, 30);
    enemySpawnFromRight(ENEMY_TYPE_HEAVY, 30);
    enemySpawn(ENEMY_TYPE_ELITE, 120, -32);
}
static void z3_w08(void) {
    enemySpawnWave(ENEMY_TYPE_ELITE, 2, 80, -20, 80, 0);
    enemySpawnWave(ENEMY_TYPE_HEAVY, 2, 40, -40, 160, 0);
}
static void z3_w09(void) { enemySpawnWave(ENEMY_TYPE_ELITE, 4, 30, -20, 50, 0); }
static void z3_w10(void) { scrollTransitionSpeed(SCROLL_SPEED_SLOW, 60); }

/* ====================================================================
 * Zone Loading & Management
 * ==================================================================== */

void zoneInit(void)
{
    zone_state.current_zone = ZONE_DEBRIS;
    zone_state.zone_distance = 0;
    zone_state.boss_spawned = 0;
    zone_state.boss_defeated = 0;
    zone_state.enemies_killed = 0;
    zone_state.zone_complete = 0;
    total_kills = 0;
}

static void loadZoneGraphics(u8 zoneId)
{
    const ZoneDef *def = &zone_defs[zoneId];

    /* Load background for this zone */
    bgLoadZone(def->bg_zone_id);

    /* Load enemy graphics for this zone's enemy roster */
    switch (zoneId) {
        case ZONE_DEBRIS:
            /* Scouts and Fighters */
            spriteLoadTiles(&enemy_scout_tiles,
                (u16)(&enemy_scout_tiles_end - &enemy_scout_tiles),
                OBJ_ENEMY_OFFSET);
            spriteLoadPalette(&enemy_scout_pal,
                (u16)(&enemy_scout_pal_end - &enemy_scout_pal), 1);
            spriteLoadTiles(&enemy_fighter_tiles,
                (u16)(&enemy_fighter_tiles_end - &enemy_fighter_tiles),
                OBJ_ENEMY_OFFSET + 0x0080);
            spriteLoadPalette(&enemy_fighter_pal,
                (u16)(&enemy_fighter_pal_end - &enemy_fighter_pal), 2);
            break;

        case ZONE_ASTEROID:
            /* Fighters and Heavies */
            spriteLoadTiles(&enemy_fighter_tiles,
                (u16)(&enemy_fighter_tiles_end - &enemy_fighter_tiles),
                OBJ_ENEMY_OFFSET);
            spriteLoadPalette(&enemy_fighter_pal,
                (u16)(&enemy_fighter_pal_end - &enemy_fighter_pal), 1);
            spriteLoadTiles(&enemy_heavy_tiles,
                (u16)(&enemy_heavy_tiles_end - &enemy_heavy_tiles),
                OBJ_ENEMY_OFFSET + 0x0080);
            spriteLoadPalette(&enemy_heavy_pal,
                (u16)(&enemy_heavy_pal_end - &enemy_heavy_pal), 2);
            break;

        case ZONE_FLAGSHIP:
            /* Heavies and Elites */
            spriteLoadTiles(&enemy_heavy_tiles,
                (u16)(&enemy_heavy_tiles_end - &enemy_heavy_tiles),
                OBJ_ENEMY_OFFSET);
            spriteLoadPalette(&enemy_heavy_pal,
                (u16)(&enemy_heavy_pal_end - &enemy_heavy_pal), 1);
            spriteLoadTiles(&enemy_elite_tiles,
                (u16)(&enemy_elite_tiles_end - &enemy_elite_tiles),
                OBJ_ENEMY_OFFSET + 0x0080);
            spriteLoadPalette(&enemy_elite_pal,
                (u16)(&enemy_elite_pal_end - &enemy_elite_pal), 2);
            break;
    }
}

void zoneLoad(u8 zoneId)
{
    const ZoneDef *def;

    if (zoneId >= ZONE_COUNT) return;
    def = &zone_defs[zoneId];

    zone_state.current_zone = zoneId;
    zone_state.zone_distance = 0;
    zone_state.boss_spawned = 0;
    zone_state.boss_defeated = 0;
    zone_state.enemies_killed = 0;
    zone_state.zone_complete = 0;

    /* Reset subsystems for new zone */
    enemyKillAll();
    scrollInit();

    /* Load zone-specific assets */
    loadZoneGraphics(zoneId);

    /* Set scroll speed */
    scrollSetSpeed(def->default_speed);

    /* Register enemy wave triggers */
    zoneRegisterWaveTriggers(zoneId);

    /* Register story dialog triggers */
    storyRegisterZoneTriggers(zoneId);

    /* Start zone music */
    soundPlayMusic(def->music_id);
}

void zoneUpdate(void)
{
    const ZoneDef *def = &zone_defs[zone_state.current_zone];

    /* Track zone scroll distance */
    zone_state.zone_distance = (u16)(scrollGetTotalDistance() & 0xFFFF);

    /* Check for boss trigger */
    if (!zone_state.boss_spawned &&
        zone_state.zone_distance >= def->boss_trigger_pos) {
        zone_state.boss_spawned = 1;
        /* Boss spawn is handled by Phase 19 */
        /* For now, mark zone as completing after boss position */
        scrollTransitionSpeed(SCROLL_SPEED_STOP, 30);
        soundPlayMusic(MUSIC_BOSS);
    }

    /* Check for zone completion (boss defeated) */
    if (zone_state.boss_defeated && !zone_state.zone_complete) {
        zone_state.zone_complete = 1;
        total_kills += zone_state.enemies_killed;

        if (zone_state.current_zone < ZONE_COUNT - 1) {
            /* Transition to next zone after a brief pause */
            scrollSetSpeed(SCROLL_SPEED_STOP);
        } else {
            /* Final zone boss defeated: VICTORY */
            gameStateChange(GS_VICTORY, TRANS_FADE);
        }
    }
}

void zoneAdvance(void)
{
    u8 nextZone = zone_state.current_zone + 1;
    if (nextZone >= ZONE_COUNT) {
        /* Game complete */
        gameStateChange(GS_VICTORY, TRANS_FADE);
        return;
    }

    /* Fade out, load next zone, fade in */
    fadeOutBlocking(20);
    setScreenOff();

    zoneLoad(nextZone);

    setScreenOn();
    fadeInBlocking(20);
}

const ZoneDef* zoneGetDef(u8 zoneId)
{
    if (zoneId >= ZONE_COUNT) return &zone_defs[0];
    return &zone_defs[zoneId];
}

u8 zoneGetCurrent(void)
{
    return zone_state.current_zone;
}

void zoneBossDefeated(void)
{
    zone_state.boss_defeated = 1;
}

void zoneRegisterWaveTriggers(u8 zoneId)
{
    /* Note: story triggers are registered separately in storyRegisterZoneTriggers.
     * This function registers only enemy wave spawns.
     * Both share the same scroll trigger system (Phase 7) which supports
     * up to MAX_SCROLL_TRIGGERS (32) total triggers. */

    switch (zoneId) {
        case ZONE_DEBRIS:
            /* Zone 1: gradual introduction of enemies */
            scrollAddTrigger(300,  z1_w01);   /* 2 scouts */
            scrollAddTrigger(600,  z1_w02);   /* 3 scouts */
            scrollAddTrigger(900,  z1_w03);   /* scout from left */
            scrollAddTrigger(1100, z1_w04);   /* scout from right */
            scrollAddTrigger(1400, z1_w05);   /* 4 scouts */
            scrollAddTrigger(1700, z1_w06);   /* first fighter */
            scrollAddTrigger(2000, z1_w07);   /* 3 scouts diagonal */
            scrollAddTrigger(2300, z1_w08);   /* pincer scouts */
            scrollAddTrigger(2700, z1_w09);   /* fighter */
            scrollAddTrigger(3100, z1_w10);   /* 3 scouts */
            scrollAddTrigger(3500, z1_w11);   /* 2 fighters */
            scrollAddTrigger(3900, z1_w12);   /* 5 scouts */
            scrollAddTrigger(4200, z1_w13);   /* fighter + scouts */
            scrollAddTrigger(4500, z1_w14);   /* fighter */
            scrollAddTrigger(4700, z1_w15);   /* pre-boss slow */
            break;

        case ZONE_ASTEROID:
            /* Zone 2: more fighters, introduce heavies */
            scrollAddTrigger(300,  z2_w01);
            scrollAddTrigger(600,  z2_w02);
            scrollAddTrigger(900,  z2_w03);
            scrollAddTrigger(1200, z2_w04);   /* first heavy */
            scrollAddTrigger(1600, z2_w05);
            scrollAddTrigger(2000, z2_w06);
            scrollAddTrigger(2400, z2_w07);   /* 2 heavies */
            scrollAddTrigger(2800, z2_w08);
            scrollAddTrigger(3200, z2_w09);
            scrollAddTrigger(3600, z2_w10);
            scrollAddTrigger(4200, z2_w11);
            scrollAddTrigger(4700, z2_w12);   /* pre-boss slow */
            break;

        case ZONE_FLAGSHIP:
            /* Zone 3: heavies and elites, aggressive */
            scrollAddTrigger(300,  z3_w01);
            scrollAddTrigger(700,  z3_w02);
            scrollAddTrigger(1100, z3_w03);
            scrollAddTrigger(1500, z3_w04);
            scrollAddTrigger(1900, z3_w05);
            scrollAddTrigger(2300, z3_w06);
            scrollAddTrigger(2800, z3_w07);
            scrollAddTrigger(3300, z3_w08);
            scrollAddTrigger(3800, z3_w09);
            scrollAddTrigger(4700, z3_w10);   /* pre-boss slow */
            break;
    }
}

u16 zoneGetTotalKills(void)
{
    return total_kills + zone_state.enemies_killed;
}
```

### Updates to game_state.c enterFlight()
```c
/* Replace the hardcoded zone setup in enterFlight with: */

static void enterFlight(void)
{
    systemResetVideo();
    rpgStatsInit();
    inventoryInit();
    spriteSystemInit();
    bulletInit();
    enemyInit();
    collisionInit();
    scrollInit();
    dialogInit();
    storyInit();
    zoneInit();

    /* Load first zone (handles graphics, music, triggers) */
    zoneLoad(ZONE_DEBRIS);

    /* Initialize player (loads player graphics) */
    playerInit();
    bulletLoadGraphics();

    /* Enable display layers */
    bgSetEnable(0);
    bgSetEnable(1);
    setScreenOn();
}
```

### Updates to game_state.c updateFlight()
```c
/* Add zone update to the flight loop: */

static void updateFlight(void)
{
    u16 held = inputHeld();
    u16 pressed = inputPressed();

    /* Pause check */
    if (pressed & ACTION_PAUSE) {
        gameStatePush(GS_PAUSE);
        enterPause();
        return;
    }

    /* Player movement and actions */
    playerHandleInput(held);
    if (held & ACTION_FIRE) bulletPlayerFire(player.x, player.y);
    if (pressed & ACTION_NEXT_WPN) bulletNextWeapon();
    if (pressed & ACTION_PREV_WPN) bulletPrevWeapon();

    /* Update all systems */
    scrollUpdate();
    playerUpdate();
    enemyUpdateAll();
    bulletUpdateAll();
    spriteUpdateAll();
    collisionCheckAll();

    /* Zone progression check */
    zoneUpdate();

    /* Check for zone transition */
    if (zone_state.zone_complete && zone_state.boss_defeated) {
        zoneAdvance();
    }

    /* Render */
    bgUpdate();
    spriteRenderAll();
    enemyRenderAll();
    bulletRenderAll();
}
```

### data.asm Additions for Zone Assets
```asm
;----------------------------------------------------------------------
; Zone 2 Background (Asteroid Belt)
;----------------------------------------------------------------------
.section ".rodata_bg_zone2" superfree

zone2_bg_tiles:
.incbin "assets/backgrounds/zone2_bg.pic"
zone2_bg_tiles_end:

zone2_bg_pal:
.incbin "assets/backgrounds/zone2_bg.pal"
zone2_bg_pal_end:

zone2_bg_map:
.incbin "assets/backgrounds/zone2_bg.map"
zone2_bg_map_end:

.ends

;----------------------------------------------------------------------
; Zone 3 Background (Flagship)
;----------------------------------------------------------------------
.section ".rodata_bg_zone3" superfree

zone3_bg_tiles:
.incbin "assets/backgrounds/zone3_bg.pic"
zone3_bg_tiles_end:

zone3_bg_pal:
.incbin "assets/backgrounds/zone3_bg.pal"
zone3_bg_pal_end:

zone3_bg_map:
.incbin "assets/backgrounds/zone3_bg.map"
zone3_bg_map_end:

.ends

;----------------------------------------------------------------------
; Additional Enemy Sprites (Fighter, Heavy, Elite)
;----------------------------------------------------------------------
.section ".rodata_spr_enemies" superfree

enemy_fighter_tiles:
.incbin "assets/sprites/enemy_fighter.pic"
enemy_fighter_tiles_end:

enemy_fighter_pal:
.incbin "assets/sprites/enemy_fighter.pal"
enemy_fighter_pal_end:

enemy_heavy_tiles:
.incbin "assets/sprites/enemy_heavy.pic"
enemy_heavy_tiles_end:

enemy_heavy_pal:
.incbin "assets/sprites/enemy_heavy.pal"
enemy_heavy_pal_end:

enemy_elite_tiles:
.incbin "assets/sprites/enemy_elite.pic"
enemy_elite_tiles_end:

enemy_elite_pal:
.incbin "assets/sprites/enemy_elite.pal"
enemy_elite_pal_end:

.ends
```

## Technical Specifications

### Zone Design Matrix
```
Property          Zone 1           Zone 2           Zone 3
--------          ------           ------           ------
Name              Debris Field     Asteroid Belt    Flagship
BG Source         background-01    background-05    background-09
Scroll Speed      0.5 px/f         0.5 px/f         1.0 px/f
Duration          ~3 min           ~3 min           ~2.5 min
Enemy Types       Scout, Fighter   Fighter, Heavy   Heavy, Elite
Wave Count        15               12               10
Enemy Per Wave    2-5              2-5              2-4
Spawn Spacing     300px            300-400px        300-500px
Boss Trigger      5000             5000             5000
Music             zone1.it         zone2.it         zone3.it
Story Events      3                3                3
```

### Difficulty Progression
```
Zone 1 (Easy):
  - Scouts: 10 HP, speed 2.0, fire every 60f, linear movement
  - Fighters: 25 HP, speed 1.0, fire every 45f, sine wave
  - Player should be level 1-3 by end of zone
  - Expected kills: 25-35 enemies

Zone 2 (Medium):
  - Fighters: 25 HP, speed 1.0, fire every 45f, sine wave
  - Heavies: 50 HP, speed 1.0, fire every 30f, hover+strafe
  - Player should be level 4-6 by end of zone
  - Expected kills: 20-30 enemies

Zone 3 (Hard):
  - Heavies: 50 HP, speed 1.0, fire every 30f, hover+strafe
  - Elites: 35 HP, speed 3.0, fire every 40f, chase player
  - Player should be level 7-9 by end of zone
  - Expected kills: 15-25 enemies

Total game kills: 60-90, yielding ~2000-3500 XP.
Level 10 requires 2000 XP, so skilled players can max out.
```

### Scroll Trigger Budget
```
MAX_SCROLL_TRIGGERS = 32 (defined in Phase 7)

Per zone allocation:
  Enemy wave triggers: 10-15
  Story dialog triggers: 3
  Speed change triggers: 1-2
  Boss trigger: 1 (handled by zoneUpdate, not scroll trigger)
  ---
  Total per zone: 15-21 triggers

This fits within the 32-trigger limit.
Both zoneRegisterWaveTriggers() and storyRegisterZoneTriggers()
add to the same trigger pool. scrollClearTriggers() is called
at the start of each zone to reset.
```

### Zone Transition Sequence
```
1. Boss defeated -> zoneBossDefeated() sets boss_defeated = 1
2. zoneUpdate() detects boss_defeated, sets zone_complete = 1
3. updateFlight() detects zone_complete, calls zoneAdvance()
4. zoneAdvance():
   a. fadeOutBlocking(20)           -- 0.33s fade to black
   b. setScreenOff()               -- force blank
   c. zoneLoad(nextZone):
      - enemyKillAll()             -- clear enemy pool
      - scrollInit()               -- reset scroll
      - loadZoneGraphics()         -- swap BG + enemy tiles
      - scrollSetSpeed()           -- set new zone speed
      - zoneRegisterWaveTriggers() -- new enemy waves
      - storyRegisterZoneTriggers() -- new story events
      - soundPlayMusic()           -- new zone music
   d. setScreenOn()                -- exit force blank
   e. fadeInBlocking(20)           -- 0.33s fade in
5. Flight resumes in new zone

Total transition time: ~0.7 seconds
Player position is preserved (same X,Y on screen).
```

### VRAM Swap During Zone Transition
```
During zoneLoad(), these VRAM regions are overwritten:
  BG1 tiles: $2000-$2FFF (8KB) -- new zone background tiles
  BG1 map:   $3C00-$3FFF (2KB) -- new zone background tilemap
  BG1 pal:   CGRAM 0-15 (32B)  -- new zone BG palette
  OBJ enemy: $0100-$01FF (varies) -- new enemy sprite tiles
  OBJ pal:   CGRAM 144-175 (64B) -- new enemy palettes

Regions NOT overwritten (preserved):
  OBJ player: $0000-$00FF      -- player ship tiles
  OBJ bullets: $0800-$0BFF     -- bullet tiles
  BG3 tiles: $3800-$3BFF       -- font (unchanged)
  BG3 map:   $4800-$4BFF       -- text tilemap

The transition happens during force blank, so no VRAM
corruption is visible.
```

### Memory Budget
```
Zone state: 12 bytes WRAM
Zone defs (ROM): 3 zones * 14 bytes = 42 bytes ROM
Wave callback functions: ~60 functions * ~20 bytes = ~1.2KB ROM
Total WRAM: 12 bytes
Total ROM: ~1.3KB (code) + asset data (varies)
```

## Asset Requirements

### Background Assets
| Asset | Source | Size | Colors | Zone |
|-------|--------|------|--------|------|
| Zone 1 BG | background-01.png | 256x256 | 16 | Debris Field |
| Zone 2 BG | background-05.png | 256x256 | 16 | Asteroid Belt |
| Zone 3 BG | background-09.png | 256x256 | 16 | Flagship |

### Enemy Sprite Assets
| Asset | Source | Size | Colors | Zone(s) |
|-------|--------|------|--------|---------|
| Scout | ship010.png | 16x16 | 16 | Zone 1 |
| Fighter | ship020.png | 32x32 | 16 | Zone 1-2 |
| Heavy | ship030.png | 32x32 | 16 | Zone 2-3 |
| Elite | ship050.png | 32x32 | 16 | Zone 3 |

### Makefile Additions
```makefile
# Zone 2 background
assets/backgrounds/zone2_bg.png: tools/convert_background.py
	$(PYTHON) tools/convert_background.py \
		"G:/2024-unity/0-GameAssets/shooter/background-05.png" $@ \
		--width 256 --height 256 --colors 16

assets/backgrounds/zone2_bg.pic: assets/backgrounds/zone2_bg.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -m -i $<

# Zone 3 background
assets/backgrounds/zone3_bg.png: tools/convert_background.py
	$(PYTHON) tools/convert_background.py \
		"G:/2024-unity/0-GameAssets/shooter/background-09.png" $@ \
		--width 256 --height 256 --colors 16

assets/backgrounds/zone3_bg.pic: assets/backgrounds/zone3_bg.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -m -i $<

# Additional enemy sprites
assets/sprites/enemy_fighter.png: tools/convert_sprite.py
	$(PYTHON) tools/convert_sprite.py \
		"G:/2024-unity/0-GameAssets/shooter/ship020.png" $@ --size 32 --colors 15

assets/sprites/enemy_fighter.pic: assets/sprites/enemy_fighter.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -R -i $<

assets/sprites/enemy_heavy.png: tools/convert_sprite.py
	$(PYTHON) tools/convert_sprite.py \
		"G:/2024-unity/0-GameAssets/shooter/ship030.png" $@ --size 32 --colors 15

assets/sprites/enemy_heavy.pic: assets/sprites/enemy_heavy.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -R -i $<

assets/sprites/enemy_elite.png: tools/convert_sprite.py
	$(PYTHON) tools/convert_sprite.py \
		"G:/2024-unity/0-GameAssets/shooter/ship050.png" $@ --size 32 --colors 15

assets/sprites/enemy_elite.pic: assets/sprites/enemy_elite.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -R -i $<

# Update bitmaps target
bitmaps: assets/sprites/player_ship.pic \
         assets/sprites/enemy_scout.pic \
         assets/sprites/enemy_fighter.pic \
         assets/sprites/enemy_heavy.pic \
         assets/sprites/enemy_elite.pic \
         assets/sprites/bullet_basic.pic \
         assets/sprites/bullet_enemy.pic \
         assets/backgrounds/zone1_bg.pic \
         assets/backgrounds/zone2_bg.pic \
         assets/backgrounds/zone3_bg.pic
```

## Acceptance Criteria
1. Game starts in Zone 1 with the correct background and music.
2. Enemy waves spawn at the correct scroll distances with expected enemy types.
3. Zone 1 uses only Scouts and Fighters; Zone 2 uses Fighters and Heavies; Zone 3 uses Heavies and Elites.
4. Reaching the boss trigger position stops scrolling and plays boss music.
5. After boss defeat, the game transitions to the next zone with a fade effect.
6. Zone transition swaps background tiles, enemy sprites, and music correctly.
7. No VRAM corruption during zone transition.
8. Player position, HP, inventory, and stats persist across zone transitions.
9. Story dialogs fire at the correct positions within each zone.
10. After defeating the Zone 3 boss, game transitions to GS_VICTORY.
11. Total game duration is approximately 10 minutes across all 3 zones.
12. Enemy density increases appropriately with zone difficulty.

## SNES-Specific Constraints
- Zone transition performs VRAM writes during force blank. All DMA transfers must complete before setScreenOn().
- Loading 8KB of BG tiles + 2KB tilemap + enemy tiles (~2-4KB) requires multiple DMA calls. Total transfer: ~14KB, which takes ~7ms via DMA (well within VBlank + force blank time).
- Each zone uses 2 OBJ palettes (slots 1-2) for enemies. The player palette (slot 0) and bullet palette (slot 3) are not swapped.
- The 8-enemy pool is cleared between zones. No enemy state carries over.
- Scroll triggers are reset between zones. The scroll distance counter resets to 0.

## Estimated Complexity
**Complex** - Designing balanced enemy wave patterns across 3 zones requires significant tuning. The zone transition logic must correctly manage graphics, music, scroll state, enemy pools, and story triggers in the right order. Many moving parts interact during transitions.
