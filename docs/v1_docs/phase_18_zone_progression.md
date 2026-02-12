# Phase 18: Zone Progression & Level Design

## Objective
Design and implement the complete level layout for all three zones: enemy wave patterns, scroll trigger timings, battle encounter placements, item drop locations, and story trigger points. This defines the actual 10-minute gameplay experience from start to finish.

## Prerequisites
- Phase 7 (Vertical Scrolling with triggers) complete
- Phase 9 (Enemy System) complete
- Phase 11 (Battle System) complete
- Phase 15 (Game State Machine) complete
- Phase 16 (Dialog System) complete

## Detailed Tasks

### 1. Design Zone Timing Budget
```
Total game time: ~10 minutes (600 seconds / 36000 frames)

Zone 1 - Debris Field: ~3 minutes (180s / 10800 frames)
  Flight segments: ~2 minutes
  Battle encounters: ~1 minute (3-4 battles x 15-20 seconds each)
  Dialog: ~15 seconds (intro)

Zone 2 - Asteroid Belt: ~3 minutes (180s / 10800 frames)
  Flight segments: ~1.5 minutes
  Battle encounters: ~1 minute (3-4 battles)
  Mini-boss battle: ~30 seconds
  Dialog: ~15 seconds

Zone 3 - Flagship: ~4 minutes (240s / 14400 frames)
  Flight segments: ~1 minute
  Battle encounters: ~1 minute (2-3 battles)
  Story twist dialog: ~45 seconds
  Final boss battle: ~1 minute
  Victory dialog: ~15 seconds
```

### 2. Create Scroll Distance → Event Mapping
Each zone has a series of scroll triggers that spawn enemies, trigger battles, or play dialog.

### 3. Implement Zone Setup Functions
Functions that configure triggers, enemies, and background for each zone.

### 4. Design Enemy Wave Patterns Per Zone

### 5. Place Battle Encounter Triggers

### 6. Place Item/Equipment Rewards

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/zones.h` | CREATE | Zone layout definitions |
| `src/zones.c` | CREATE | Zone setup and management |
| `src/game.c` | MODIFY | Call zone setup on zone entry |
| `src/scroll.c` | MODIFY | Connect triggers to zone events |

## Technical Specifications

### Scroll Trigger Event Types
```c
/* Event types for scroll triggers */
#define EVT_SPAWN_WAVE      0  /* Spawn enemy wave (data = wave_id) */
#define EVT_SPAWN_SINGLE    1  /* Spawn single enemy (data = enemy_type) */
#define EVT_START_BATTLE    2  /* Force battle encounter (data = enemy_type) */
#define EVT_DIALOG          3  /* Trigger dialog sequence (data = sequence_id) */
#define EVT_ZONE_CLEAR      4  /* Zone complete, transition */
#define EVT_BOSS_APPROACH   5  /* Speed up scroll, dramatic entry */
#define EVT_ITEM_DROP       6  /* Spawn item pickup (data = item_id) */
#define EVT_CHANGE_SPEED    7  /* Change scroll speed */
#define EVT_PLAY_MUSIC      8  /* Change music track */
```

### Zone Layout Data
```c
/* Wave definition: what to spawn and how */
typedef struct {
    u8 enemy_type;
    u8 formation;     /* 0=line, 1=V, 2=column */
    u8 count;
} WaveDefinition;

/* Zone layout: sequence of triggers at specific scroll distances */
typedef struct {
    ScrollTrigger triggers[MAX_SCROLL_TRIGGERS];
    u8 trigger_count;
    u8 zone_bg;           /* Background ID for this zone */
    u16 zone_length;      /* Total scroll distance before zone end */
} ZoneLayout;
```

### zones.h
```c
#ifndef ZONES_H
#define ZONES_H

#include <snes.h>
#include "config.h"
#include "scroll.h"

/* Pre-defined wave IDs */
#define WAVE_Z1_SCOUTS_LINE    0
#define WAVE_Z1_SCOUTS_V       1
#define WAVE_Z1_RAIDERS_LINE   2
#define WAVE_Z1_MIXED          3
#define WAVE_Z2_INTERCEPT_V    4
#define WAVE_Z2_DESTROYERS     5
#define WAVE_Z2_MIXED          6
#define WAVE_Z3_ELITE_LINE     7
#define WAVE_Z3_COMMANDERS     8
#define WAVE_Z3_FINAL_WAVE     9

extern const WaveDefinition wave_defs[];
extern const ZoneLayout zone_layouts[ZONE_COUNT];

/*--- Functions ---*/
void zone_setup(u8 zone_id);
void zone_handle_event(u8 event_type, u8 event_data);
void zone_spawn_wave(u8 wave_id);
void zone_update(void);

#endif /* ZONES_H */
```

### zones.c
```c
#include "zones.h"
#include "enemy.h"
#include "battle.h"
#include "dialog.h"
#include "items.h"
#include "sound.h"
#include "game.h"
#include "scroll.h"

/* Wave definitions */
const WaveDefinition wave_defs[] = {
    /* WAVE_Z1_SCOUTS_LINE */    {ENEMY_SCOUT,       0, 3},
    /* WAVE_Z1_SCOUTS_V */       {ENEMY_SCOUT,       1, 5},
    /* WAVE_Z1_RAIDERS_LINE */   {ENEMY_RAIDER,      0, 3},
    /* WAVE_Z1_MIXED */          {ENEMY_SCOUT,       2, 2}, /* + manual raider */
    /* WAVE_Z2_INTERCEPT_V */    {ENEMY_INTERCEPTOR, 1, 4},
    /* WAVE_Z2_DESTROYERS */     {ENEMY_DESTROYER,   0, 2},
    /* WAVE_Z2_MIXED */          {ENEMY_INTERCEPTOR, 0, 3},
    /* WAVE_Z3_ELITE_LINE */     {ENEMY_ELITE,       0, 3},
    /* WAVE_Z3_COMMANDERS */     {ENEMY_COMMANDER,   1, 3},
    /* WAVE_Z3_FINAL_WAVE */     {ENEMY_ELITE,       2, 4},
};

/* =====================================================
 * ZONE 1: DEBRIS FIELD
 * Duration: ~3 minutes at SCROLL_SPEED_NORMAL (0.5 px/frame)
 * Total scroll: ~5400 pixels (~180 seconds * 30 pixels/sec)
 * =====================================================*/
const ScrollTrigger zone1_triggers[] = {
    /* dist, event,           data,  fired */
    {  50,  EVT_DIALOG,       SEQ_INTRO,        0},  /* Intro dialog */
    { 200,  EVT_SPAWN_WAVE,   WAVE_Z1_SCOUTS_LINE, 0},  /* First enemies */
    { 400,  EVT_SPAWN_WAVE,   WAVE_Z1_SCOUTS_V,    0},  /* V formation */
    { 600,  EVT_START_BATTLE, ENEMY_SCOUT,       0},  /* First battle */
    { 800,  EVT_ITEM_DROP,    0,                 0},  /* Repair Kit */
    {1000,  EVT_SPAWN_WAVE,   WAVE_Z1_RAIDERS_LINE, 0},
    {1200,  EVT_START_BATTLE, ENEMY_RAIDER,      0},  /* Raider battle */
    {1400,  EVT_SPAWN_WAVE,   WAVE_Z1_MIXED,     0},
    {1600,  EVT_START_BATTLE, ENEMY_SCOUT,       0},  /* Scout battle */
    {1800,  EVT_SPAWN_WAVE,   WAVE_Z1_SCOUTS_LINE, 0},
    {2000,  EVT_START_BATTLE, ENEMY_RAIDER,      0},  /* Raider battle 2 */
    {2100,  EVT_ITEM_DROP,    1,                 0},  /* Energy Cell */
    {2200,  EVT_DIALOG,       SEQ_ZONE1_CLEAR,   0},  /* Zone 1 clear dialog */
    {2300,  EVT_ZONE_CLEAR,   0,                 0},  /* Transition to Zone 2 */
};

/* =====================================================
 * ZONE 2: ASTEROID BELT
 * Higher difficulty, more battles
 * =====================================================*/
const ScrollTrigger zone2_triggers[] = {
    { 100,  EVT_SPAWN_WAVE,   WAVE_Z2_INTERCEPT_V,  0},
    { 300,  EVT_SPAWN_SINGLE, ENEMY_ASTEROID,    0},  /* Asteroid obstacles */
    { 400,  EVT_START_BATTLE, ENEMY_INTERCEPTOR, 0},
    { 500,  EVT_SPAWN_SINGLE, ENEMY_ASTEROID,    0},
    { 600,  EVT_SPAWN_WAVE,   WAVE_Z2_DESTROYERS,0},
    { 700,  EVT_ITEM_DROP,    2,                 0},  /* Medium Repair */
    { 800,  EVT_START_BATTLE, ENEMY_DESTROYER,   0},
    { 900,  EVT_SPAWN_WAVE,   WAVE_Z2_MIXED,     0},
    {1000,  EVT_SPAWN_SINGLE, ENEMY_ASTEROID,    0},
    {1100,  EVT_START_BATTLE, ENEMY_INTERCEPTOR, 0},
    {1300,  EVT_ITEM_DROP,    3,                 0},  /* Overdrive */
    {1400,  EVT_SPAWN_WAVE,   WAVE_Z2_DESTROYERS,0},
    {1500,  EVT_START_BATTLE, ENEMY_DESTROYER,   0},
    {1700,  EVT_BOSS_APPROACH, 0,                0},  /* Mini-boss approach */
    {1800,  EVT_DIALOG,       SEQ_ZONE2_CLEAR,   0},
    {1900,  EVT_ZONE_CLEAR,   0,                 0},
};

/* =====================================================
 * ZONE 3: FLAGSHIP ASSAULT
 * Hardest enemies, story twist, final boss
 * =====================================================*/
const ScrollTrigger zone3_triggers[] = {
    { 100,  EVT_SPAWN_WAVE,   WAVE_Z3_ELITE_LINE, 0},
    { 300,  EVT_START_BATTLE, ENEMY_ELITE,       0},
    { 400,  EVT_ITEM_DROP,    7,                 0},  /* Ion Blade (weapon) */
    { 500,  EVT_SPAWN_WAVE,   WAVE_Z3_COMMANDERS,0},
    { 600,  EVT_START_BATTLE, ENEMY_COMMANDER,   0},
    { 700,  EVT_ITEM_DROP,    6,                 0},  /* Full Repair */
    { 800,  EVT_SPAWN_WAVE,   WAVE_Z3_FINAL_WAVE,0},
    { 900,  EVT_START_BATTLE, ENEMY_ELITE,       0},
    {1000,  EVT_CHANGE_SPEED, 0,                 0},  /* Slow to stop */
    {1050,  EVT_DIALOG,       SEQ_TWIST,         0},  /* THE TWIST! */
    /* After twist dialog, choice determines next events */
    /* Zone update logic checks story_flags and triggers appropriate path */
    {1100,  EVT_BOSS_APPROACH, 0,                0},
    /* Boss battle triggered by zone_update based on choice */
};

/* Zone layout master table */
const ZoneLayout zone_layouts[ZONE_COUNT] = {
    {/* zone1 triggers copied in zone_setup */, 14, BG_DEBRIS,   2300},
    {/* zone2 triggers copied in zone_setup */, 16, BG_ASTEROID, 1900},
    {/* zone3 triggers copied in zone_setup */, 12, BG_FLAGSHIP, 1200},
};

void zone_setup(u8 zone_id) {
    const ScrollTrigger *src;
    u8 count;

    /* Clear existing triggers */
    g_bg.trigger_count = 0;
    g_bg.cumulative_scroll = 0;

    /* Copy zone triggers to scroll system */
    switch(zone_id) {
        case ZONE_DEBRIS:
            src = zone1_triggers;
            count = 14;
            break;
        case ZONE_ASTEROID:
            src = zone2_triggers;
            count = 16;
            break;
        case ZONE_FLAGSHIP:
            src = zone3_triggers;
            count = 12;
            break;
        default:
            return;
    }

    u8 i;
    for (i = 0; i < count && i < MAX_SCROLL_TRIGGERS; i++) {
        g_bg.triggers[i] = src[i];
    }
    g_bg.trigger_count = count;
}

void zone_handle_event(u8 event_type, u8 event_data) {
    switch(event_type) {
        case EVT_SPAWN_WAVE:
            zone_spawn_wave(event_data);
            break;

        case EVT_SPAWN_SINGLE:
            enemy_spawn(event_data,
                       (s16)(64 + (rand() & 0x7F)), /* Random X: 64-191 */
                       -32);
            break;

        case EVT_START_BATTLE:
            battle_trigger(event_data);
            break;

        case EVT_DIALOG:
            dialog_start(event_data);
            break;

        case EVT_ZONE_CLEAR:
            g_game.zones_cleared++;
            if (g_game.current_zone == ZONE_DEBRIS)
                g_game.story_flags |= STORY_ZONE1_CLEAR;
            else if (g_game.current_zone == ZONE_ASTEROID)
                g_game.story_flags |= STORY_ZONE2_CLEAR;
            game_change_state(STATE_ZONE_TRANS);
            break;

        case EVT_BOSS_APPROACH:
            bg_set_speed_gradual(SCROLL_SPEED_FAST, 0x0010);
            /* sound_play_music(MUSIC_BOSS); */
            break;

        case EVT_ITEM_DROP:
            items_spawn_pickup(
                (s16)(80 + (rand() & 0x3F)), /* Random X: 80-143 */
                -16,
                event_data);
            break;

        case EVT_CHANGE_SPEED:
            bg_stop_scroll();
            break;

        case EVT_PLAY_MUSIC:
            sound_play_music(event_data);
            break;
    }
}

void zone_spawn_wave(u8 wave_id) {
    if (wave_id >= 10) return;
    const WaveDefinition *w = &wave_defs[wave_id];
    enemy_spawn_wave(w->enemy_type, w->formation, w->count);
}

void zone_update(void) {
    /* Post-twist logic for Zone 3 */
    if (g_game.current_zone == ZONE_FLAGSHIP) {
        /* After twist dialog, trigger appropriate final boss */
        if (g_game.story_flags & STORY_TWIST_SEEN) {
            if (g_game.story_flags & STORY_CHOSE_TRUTH) {
                /* Fight Admiral's defense drones (boss = modified sentinel) */
                if (!(g_game.story_flags & STORY_BOSS_DEFEATED)) {
                    dialog_start(SEQ_TRUTH_PATH);
                    /* Then trigger boss_init with "ark defense" boss */
                }
            } else if (g_game.story_flags & STORY_CHOSE_LOYALTY) {
                /* Fight Commander Zyx */
                if (!(g_game.story_flags & STORY_BOSS_DEFEATED)) {
                    dialog_start(SEQ_LOYALTY_PATH);
                    /* Then trigger boss battle */
                }
            }
        }
    }
}
```

## Acceptance Criteria
1. Zone 1 plays through ~3 minutes with correct enemy spawns and battles
2. Zone 2 has increased difficulty with interceptors and destroyers
3. Zone 3 leads to the twist dialog and boss fight
4. All scroll triggers fire at correct distances
5. Enemy waves spawn in correct formations
6. Item drops appear at designated points
7. Dialog triggers pause gameplay and display correct text
8. Zone transitions fade smoothly between zones
9. The twist choice leads to the correct final boss path
10. Total playtime is approximately 10 minutes
11. Difficulty curve feels progressive (Zone 1 easy, Zone 3 hard)

## SNES-Specific Constraints
- const trigger arrays in ROM - don't modify at runtime (copy to WRAM first)
- 16 max scroll triggers per zone (limited by BackgroundState buffer)
- Enemy wave spawns should be spaced 200+ scroll pixels apart (prevent overlap)
- Scroll distance is tracked in 8.8 fixed-point - integer part used for triggers

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~85KB | 256KB    | ~171KB    |
| WRAM     | ~1.3KB| 128KB   | ~127KB    |
| VRAM     | ~14KB | 64KB    | ~50KB     |
| CGRAM    | 192B  | 512B    | 320B      |

## Estimated Complexity
**Complex** - This is game design + implementation. Balancing encounter timing, difficulty curve, and pacing requires extensive playtesting. The trigger system itself is simple, but the DATA (timing of events) is the hard part.

## Agent Instructions
1. Create `src/zones.h` and `src/zones.c`
2. Update Makefile and linkfile
3. Call `zone_setup(zone_id)` inside `game_start_zone()`
4. Modify `game_handle_scroll_event()` to call `zone_handle_event()`
5. Test Zone 1: verify all 14 triggers fire in correct order
6. Time Zone 1: should take ~3 minutes at normal scroll speed
7. Test Zone 2: verify higher difficulty and mini-boss approach
8. Test Zone 3: verify twist dialog triggers and choice works
9. Playtest all 3 zones back-to-back for the full 10-minute experience
10. Adjust scroll distances if pacing feels too fast/slow
11. **Key balancing**: if too hard, reduce enemy HP or increase XP rewards
