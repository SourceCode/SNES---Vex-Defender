# Phase 20: Polish, Title Screen, Save System & Final Integration

## Objective
Final polish phase: create a complete title screen with menu, implement SRAM save/load for mid-game saves, add visual effects (screen shake, flash, explosions), perform integration testing, optimize performance, fix bugs, and produce the final shippable ROM.

## Prerequisites
- ALL previous phases (1-19) complete
- Full game playable from start to finish

## Detailed Tasks

### 1. Enhanced Title Screen
```
+----------------------------------+
|                                  |
|        [GAME LOGO AREA]         |
|                                  |
|        VEX DEFENDER              |
|     VERTICAL ASSAULT RPG         |
|                                  |
|       > NEW GAME                 |
|         CONTINUE                 |
|                                  |
|                                  |
|    (C) 2026 HOMEBREW             |
|                                  |
+----------------------------------+
```

### 2. Implement SRAM Save System
Save player progress (zone, stats, items, story flags) to SNES SRAM for persistence.

### 3. Visual Effects System
- Screen shake (boss attacks, explosions)
- Screen flash (critical hits, level up)
- Explosion sprite animations (enemy death)
- Palette cycling effects

### 4. Game Over Improvements
Retry from last zone start (not full restart).

### 5. Performance Optimization
Profile and optimize hot paths, reduce VRAM bandwidth.

### 6. Final Integration Testing
Full playthrough testing, bug fixing, balance tuning.

### 7. ROM Metadata and Finalization

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/save.h` | CREATE | Save system header |
| `src/save.c` | CREATE | Save system implementation |
| `src/effects.h` | CREATE | Visual effects header |
| `src/effects.c` | CREATE | Visual effects implementation |
| `src/game.c` | MODIFY | Enhanced title, game over, integrate effects |
| `data/hdr.asm` | MODIFY | Enable SRAM in ROM header |
| `Makefile` | MODIFY | Add new obj files |
| `data/linkfile` | MODIFY | Add new obj files |
| ALL source files | MODIFY | Bug fixes and optimization |

## Technical Specifications

### SRAM Save System

**ROM Header Update** (hdr.asm):
```asm
CARTRIDGETYPE $02     ; ROM + SRAM (was $00)
SRAMSIZE $01          ; 2KB SRAM
```

**Save Data Structure**:
```c
/* Save data layout - must fit in 2KB SRAM */
/* SRAM is mapped at $70:0000-$70:07FF in LoROM */

#define SAVE_MAGIC   0xBEEF  /* Validity marker */
#define SAVE_VERSION 1

typedef struct {
    u16 magic;              /* Must be SAVE_MAGIC to be valid */
    u8  version;            /* Save format version */

    /* Player state */
    u8  player_level;
    u16 player_hp;
    u16 player_max_hp;
    u16 player_mp;
    u16 player_max_mp;
    u16 player_attack;
    u16 player_defense;
    u16 player_speed;
    u16 player_xp;

    /* Game progress */
    u8  current_zone;
    u8  story_flags;
    u16 score;
    u8  zones_cleared;

    /* Inventory */
    u8  inventory_count;
    u8  inventory_items[16]; /* 8 slots x 2 bytes (id + qty) */
    s8  equipped_weapon;
    s8  equipped_armor;

    /* Checksum */
    u16 checksum;
} SaveData;

#define SAVE_SIZE sizeof(SaveData)  /* ~48 bytes */
```

### save.h
```c
#ifndef SAVE_H
#define SAVE_H

#include <snes.h>
#include "config.h"

/*--- Functions ---*/
void save_init(void);
u8   save_exists(void);
void save_write(void);
void save_read(void);
void save_erase(void);
u16  save_calc_checksum(SaveData *data);

#endif /* SAVE_H */
```

### save.c
```c
#include "save.h"
#include "player.h"
#include "items.h"
#include "game.h"

/* SRAM access - PVSnesLib provides sram functions */
/* sramWrite(offset, data, size) / sramRead(offset, data, size) */

static SaveData save_buffer;

void save_init(void) {
    /* Initialize SRAM access */
    /* sramEnableAccess(); -- if needed */
}

u16 save_calc_checksum(SaveData *data) {
    u16 sum = 0;
    u8 *ptr = (u8*)data;
    u16 i;
    /* Sum all bytes except the checksum field itself */
    for (i = 0; i < SAVE_SIZE - 2; i++) {
        sum += ptr[i];
    }
    return sum;
}

u8 save_exists(void) {
    /* Read save header from SRAM */
    u16 magic;
    /* Read first 2 bytes */
    sramGet((u8*)&save_buffer, SAVE_SIZE);

    if (save_buffer.magic != SAVE_MAGIC) return 0;
    if (save_buffer.version != SAVE_VERSION) return 0;

    /* Validate checksum */
    u16 expected = save_calc_checksum(&save_buffer);
    if (save_buffer.checksum != expected) return 0;

    return 1;
}

void save_write(void) {
    save_buffer.magic = SAVE_MAGIC;
    save_buffer.version = SAVE_VERSION;

    /* Copy player state */
    save_buffer.player_level = g_player.level;
    save_buffer.player_hp = g_player.hp;
    save_buffer.player_max_hp = g_player.max_hp;
    save_buffer.player_mp = g_player.mp;
    save_buffer.player_max_mp = g_player.max_mp;
    save_buffer.player_attack = g_player.attack;
    save_buffer.player_defense = g_player.defense;
    save_buffer.player_speed = g_player.speed_stat;
    save_buffer.player_xp = g_player.xp;

    /* Copy game progress */
    save_buffer.current_zone = g_game.current_zone;
    save_buffer.story_flags = g_game.story_flags;
    save_buffer.score = g_game.score;
    save_buffer.zones_cleared = g_game.zones_cleared;

    /* Copy inventory */
    save_buffer.inventory_count = g_inventory.count;
    u8 i;
    for (i = 0; i < INVENTORY_SIZE; i++) {
        save_buffer.inventory_items[i * 2] = g_inventory.slots[i].item_id;
        save_buffer.inventory_items[i * 2 + 1] = g_inventory.slots[i].quantity;
    }
    save_buffer.equipped_weapon = g_inventory.equipped_weapon;
    save_buffer.equipped_armor = g_inventory.equipped_armor;

    /* Calculate and set checksum */
    save_buffer.checksum = save_calc_checksum(&save_buffer);

    /* Write to SRAM */
    sramPut((u8*)&save_buffer, SAVE_SIZE);
}

void save_read(void) {
    sramGet((u8*)&save_buffer, SAVE_SIZE);

    if (save_buffer.magic != SAVE_MAGIC) return;

    /* Restore player state */
    g_player.level = save_buffer.player_level;
    g_player.hp = save_buffer.player_hp;
    g_player.max_hp = save_buffer.player_max_hp;
    g_player.mp = save_buffer.player_mp;
    g_player.max_mp = save_buffer.player_max_mp;
    g_player.attack = save_buffer.player_attack;
    g_player.defense = save_buffer.player_defense;
    g_player.speed_stat = save_buffer.player_speed;
    g_player.xp = save_buffer.player_xp;

    /* Restore game progress */
    g_game.current_zone = save_buffer.current_zone;
    g_game.story_flags = save_buffer.story_flags;
    g_game.score = save_buffer.score;
    g_game.zones_cleared = save_buffer.zones_cleared;

    /* Restore inventory */
    u8 i;
    for (i = 0; i < INVENTORY_SIZE; i++) {
        g_inventory.slots[i].item_id = save_buffer.inventory_items[i * 2];
        g_inventory.slots[i].quantity = save_buffer.inventory_items[i * 2 + 1];
    }
    g_inventory.count = save_buffer.inventory_count;
    g_inventory.equipped_weapon = save_buffer.equipped_weapon;
    g_inventory.equipped_armor = save_buffer.equipped_armor;
}

void save_erase(void) {
    save_buffer.magic = 0;
    sramPut((u8*)&save_buffer, 2); /* Just overwrite magic bytes */
}
```

### Visual Effects System

### effects.h
```c
#ifndef EFFECTS_H
#define EFFECTS_H

#include <snes.h>
#include "config.h"

/* Effect types */
#define FX_NONE          0
#define FX_SCREEN_SHAKE  1
#define FX_SCREEN_FLASH  2
#define FX_EXPLOSION     3
#define FX_PALETTE_PULSE 4

typedef struct {
    u8  active;
    u8  type;
    u8  timer;
    u8  intensity;
    s16 param_x;
    s16 param_y;
} Effect;

#define MAX_EFFECTS 4

typedef struct {
    Effect pool[MAX_EFFECTS];
    s8 shake_offset_x;
    s8 shake_offset_y;
    u8 flash_brightness;
} EffectsState;

extern EffectsState g_effects;

/*--- Functions ---*/
void effects_init(void);
void effects_update(void);
void effects_apply(void);
void effects_trigger_shake(u8 intensity, u8 duration);
void effects_trigger_flash(u8 brightness, u8 duration);
void effects_trigger_explosion(s16 x, s16 y);

#endif /* EFFECTS_H */
```

### effects.c
```c
#include "effects.h"

EffectsState g_effects;

void effects_init(void) {
    u8 i;
    for (i = 0; i < MAX_EFFECTS; i++) {
        g_effects.pool[i].active = 0;
    }
    g_effects.shake_offset_x = 0;
    g_effects.shake_offset_y = 0;
    g_effects.flash_brightness = 15;
}

void effects_trigger_shake(u8 intensity, u8 duration) {
    u8 i;
    for (i = 0; i < MAX_EFFECTS; i++) {
        if (!g_effects.pool[i].active) {
            g_effects.pool[i].active = 1;
            g_effects.pool[i].type = FX_SCREEN_SHAKE;
            g_effects.pool[i].timer = duration;
            g_effects.pool[i].intensity = intensity;
            return;
        }
    }
}

void effects_trigger_flash(u8 brightness, u8 duration) {
    u8 i;
    for (i = 0; i < MAX_EFFECTS; i++) {
        if (!g_effects.pool[i].active) {
            g_effects.pool[i].active = 1;
            g_effects.pool[i].type = FX_SCREEN_FLASH;
            g_effects.pool[i].timer = duration;
            g_effects.pool[i].intensity = brightness;
            return;
        }
    }
}

void effects_trigger_explosion(s16 x, s16 y) {
    u8 i;
    for (i = 0; i < MAX_EFFECTS; i++) {
        if (!g_effects.pool[i].active) {
            g_effects.pool[i].active = 1;
            g_effects.pool[i].type = FX_EXPLOSION;
            g_effects.pool[i].timer = 20;
            g_effects.pool[i].param_x = x;
            g_effects.pool[i].param_y = y;
            return;
        }
    }
}

void effects_update(void) {
    u8 i;
    g_effects.shake_offset_x = 0;
    g_effects.shake_offset_y = 0;
    g_effects.flash_brightness = 15;

    for (i = 0; i < MAX_EFFECTS; i++) {
        Effect *fx = &g_effects.pool[i];
        if (!fx->active) continue;

        fx->timer--;
        if (fx->timer == 0) {
            fx->active = 0;
            continue;
        }

        switch(fx->type) {
            case FX_SCREEN_SHAKE:
                /* Random shake offset based on intensity */
                g_effects.shake_offset_x = (s8)((rand() & (fx->intensity * 2)) - fx->intensity);
                g_effects.shake_offset_y = (s8)((rand() & (fx->intensity * 2)) - fx->intensity);
                break;

            case FX_SCREEN_FLASH:
                /* Alternate between bright and normal */
                if (fx->timer & 0x02) {
                    g_effects.flash_brightness = fx->intensity;
                }
                break;

            case FX_EXPLOSION:
                /* Use a sprite to show explosion animation */
                {
                    u8 frame = 20 - fx->timer; /* 0-19 */
                    u8 oam_slot = (40 + i) * 4; /* Use high OAM slots */
                    if (frame < 20) {
                        oamSet(oam_slot, fx->param_x, fx->param_y, 3, 0, 0,
                               60 + (frame >> 2), PAL_SPR_FX);
                        oamSetEx(oam_slot, OBJ_LARGE, OBJ_SHOW);
                    }
                    if (fx->timer == 1) {
                        oamSetEx(oam_slot, OBJ_LARGE, OBJ_HIDE);
                    }
                }
                break;
        }
    }
}

void effects_apply(void) {
    /* Apply screen shake to BG scroll */
    if (g_effects.shake_offset_x != 0 || g_effects.shake_offset_y != 0) {
        /* Add shake offset to current scroll positions */
        /* This is applied AFTER normal scroll update */
    }

    /* Apply flash brightness */
    if (g_effects.flash_brightness != 15) {
        setBrightness(g_effects.flash_brightness);
    }
}
```

### Enhanced Title Screen (game.c additions)
```c
void state_title_update(void) {
    static u8 menu_cursor = 0;
    u8 has_save = save_exists();

    /* Draw title elements */
    ui_draw_text(7, 6, "VEX DEFENDER");
    ui_draw_text(3, 8, "VERTICAL ASSAULT RPG");

    /* Menu options */
    ui_draw_text(10, 14, "NEW GAME");
    if (has_save) {
        ui_draw_text(10, 16, "CONTINUE");
    } else {
        ui_draw_text(10, 16, "--------"); /* Grayed out */
    }

    /* Cursor */
    ui_draw_text(8, 14, (menu_cursor == 0) ? ">" : " ");
    ui_draw_text(8, 16, (menu_cursor == 1) ? ">" : " ");

    /* Navigate */
    if (input_is_pressed(KEY_UP) || input_is_pressed(KEY_DOWN)) {
        menu_cursor = !menu_cursor;
        if (menu_cursor == 1 && !has_save) menu_cursor = 0;
        sound_play_sfx(SFX_MENU_SELECT);
    }

    /* Select */
    if (input_is_pressed(KEY_A) || input_is_pressed(KEY_START)) {
        if (menu_cursor == 0) {
            /* New Game */
            save_erase();
            stats_init();
            items_init();
            g_game.story_flags = 0;
            g_game.score = 0;
            g_game.zones_cleared = 0;
            screen_fade_out();
            dialog_start(SEQ_INTRO);
        } else if (menu_cursor == 1 && has_save) {
            /* Continue */
            save_read();
            screen_fade_out();
            game_start_zone(g_game.current_zone);
        }
    }

    /* Copyright */
    ui_draw_text(5, 24, "(C) 2026 HOMEBREW STUDIO");
}
```

### Enhanced Game Over
```c
void state_gameover_update(void) {
    ui_draw_text(10, 8, "GAME OVER");
    ui_draw_text(5, 12, "THE ARK FALLS SILENT...");
    ui_draw_text(5, 16, "> RETRY FROM ZONE START");
    ui_draw_text(5, 18, "  RETURN TO TITLE");

    static u8 go_cursor = 0;
    if (input_is_pressed(KEY_UP) || input_is_pressed(KEY_DOWN)) {
        go_cursor = !go_cursor;
    }
    ui_draw_text(3, 16, (go_cursor == 0) ? ">" : " ");
    ui_draw_text(3, 18, (go_cursor == 1) ? ">" : " ");

    if (input_is_pressed(KEY_A) || input_is_pressed(KEY_START)) {
        if (go_cursor == 0) {
            /* Retry from zone start */
            g_player.hp = g_player.max_hp;
            g_player.mp = g_player.max_mp;
            screen_fade_out();
            game_start_zone(g_game.current_zone);
        } else {
            /* Return to title */
            game_change_state(STATE_TITLE);
        }
    }
}
```

### Auto-Save Points
```c
/* Save at zone transitions */
void game_start_zone(u8 zone) {
    /* ... existing zone setup ... */

    /* Auto-save at zone start */
    save_write();
}
```

### Final Makefile (Complete)
```makefile
PVSNESLIB_HOME := J:/code/snes/snes-build-tools/tools/pvsneslib

ifeq ($(strip $(PVSNESLIB_HOME)),)
$(error "PVSNESLIB_HOME is not set")
endif

include ${PVSNESLIB_HOME}/devkitsnes/snes_rules

export ROMNAME := vex_defender

export OFILES := \
    src/main.obj \
    src/game.obj \
    src/player.obj \
    src/input.obj \
    src/scroll.obj \
    src/bullet.obj \
    src/enemy.obj \
    src/collision.obj \
    src/battle.obj \
    src/ui.obj \
    src/stats.obj \
    src/items.obj \
    src/dialog.obj \
    src/sound.obj \
    src/zones.obj \
    src/boss.obj \
    src/save.obj \
    src/effects.obj

.PHONY: bitmaps all clean

all: bitmaps $(ROMNAME).sfc

clean: cleanBuildRes cleanRom cleanGfx

# ... (all bitmap rules from Phase 3) ...
bitmaps: $(BITMAP_TARGETS)
```

### Final linkfile
```
[objects]
data/hdr.obj
data/data.obj
src/main.obj
src/game.obj
src/player.obj
src/input.obj
src/scroll.obj
src/bullet.obj
src/enemy.obj
src/collision.obj
src/battle.obj
src/ui.obj
src/stats.obj
src/items.obj
src/dialog.obj
src/sound.obj
src/zones.obj
src/boss.obj
src/save.obj
src/effects.obj
```

## Integration Testing Checklist
```
[ ] Title screen displays correctly with menu
[ ] "NEW GAME" starts fresh game with intro dialog
[ ] "CONTINUE" loads saved game state
[ ] Intro dialog plays with typewriter effect
[ ] Zone 1 flight: enemies spawn, player can shoot them
[ ] Zone 1 battles: 3-4 battles trigger and resolve correctly
[ ] Player levels up during Zone 1 (reach level 2)
[ ] Zone 1 → Zone 2 transition: fade, new background, new enemies
[ ] Zone 2 enemies are noticeably harder
[ ] Mini-boss encounter: multi-phase battle works
[ ] Zone 2 → Zone 3 transition
[ ] Zone 3: story twist dialog plays
[ ] Choice system: both options set correct flags
[ ] Final boss: correct boss based on choice
[ ] Boss multi-phase patterns work
[ ] Victory dialog: correct ending based on choice
[ ] Victory screen with score
[ ] Game Over: retry from zone start works
[ ] Game Over: return to title works
[ ] Save/Load: progress persists after reset
[ ] Sound: SFX play on all events
[ ] Sound: music changes between zones
[ ] Visual: screen shake on big hits
[ ] Visual: explosions on enemy death
[ ] Performance: steady 60fps throughout
[ ] No crashes during any state transition
[ ] Total playtime: approximately 10 minutes
```

## Acceptance Criteria
1. Complete game plays from title to ending without crashes
2. Both story paths (truth/loyalty) lead to different endings
3. Save system persists data across ROM resets (in emulator)
4. Title screen has working New Game / Continue menu
5. Game Over allows retry from zone start or return to title
6. Screen shake and flash effects enhance dramatic moments
7. All 18 source files compile and link without errors
8. Final ROM size is under 256KB
9. Game runs at 60fps with no noticeable slowdown
10. The story twist is genuinely surprising and the choice feels meaningful

## SNES-Specific Constraints
- SRAM must be explicitly enabled in ROM header ($02 cartridge type)
- SRAM is battery-backed on real hardware - emulators save to .srm file
- SRAM writes should be minimized (auto-save at zone transitions only)
- ROM size may need to increase to 512KB if assets are large (change ROMBANKS)
- Final ROM should pass validation in emulators (Mesen, snes9x, bsnes)

## Final Memory Budget
| Resource | Used (est.) | Available | Remaining |
|----------|-------------|-----------|-----------|
| ROM      | ~100KB      | 256KB     | ~156KB    |
| WRAM     | ~1.5KB      | 128KB     | ~126KB    |
| VRAM     | ~16KB       | 64KB      | ~48KB     |
| CGRAM    | ~256B       | 512B      | ~256B     |
| SRAM     | ~48B        | 2KB       | ~2KB      |
| SPC RAM  | ~32KB       | 64KB      | ~32KB     |

## Estimated Complexity
**Complex** - This phase touches every system. Save/load requires careful serialization. Visual effects need integration points throughout the codebase. The integration testing checklist must be fully verified.

## Agent Instructions
1. Create `src/save.h`, `src/save.c`, `src/effects.h`, `src/effects.c`
2. Update `data/hdr.asm` to enable SRAM (CARTRIDGETYPE $02, SRAMSIZE $01)
3. Update Makefile with ALL object files and linkfile with ALL objects
4. Integrate effects: add `effects_update()` and `effects_apply()` to main loop
5. Integrate save: add `save_init()` to game_init(), auto-save in `game_start_zone()`
6. Enhance title screen with New Game / Continue menu
7. Enhance Game Over with retry / title options
8. Add effects triggers: `effects_trigger_shake(3, 10)` on boss attacks, `effects_trigger_explosion()` on enemy death
9. Run full integration test using the checklist above
10. Fix any bugs discovered during testing
11. Verify ROM opens in Mesen, snes9x, and bsnes without errors
12. **Final step**: do a complete 10-minute playthrough and verify the experience
