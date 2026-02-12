# Phase 20: Polish, Title Screen, Save System & Final Integration

## Objective
Finalize the game by implementing the title screen with menu options, an SRAM-based save/load system, a proper game over sequence, the victory ending sequence, a flight-mode HUD, visual polish effects (screen shake, particle explosions, HDMA color gradient), and thorough integration testing of all systems working together from boot to victory. This phase turns the assembled systems into a complete, shippable game.

## Prerequisites
- All previous phases (1-19) must be complete. This is the capstone integration phase.

## Detailed Tasks

1. Create the full title screen: background graphic, game logo, animated starfield, menu options (New Game, Continue, Options).
2. Implement the SRAM save/load system: save player progress (stats, inventory, zone, story flags) to battery-backed SRAM with checksum validation.
3. Implement the Continue option: detect valid save data, load and resume from the saved zone.
4. Create the flight-mode HUD on BG3: player HP bar (top-right), current weapon name, SP charge indicator, zone progress indicator.
5. Create the Game Over sequence: fade to red tint, "GAME OVER" text, option to continue from last save or return to title.
6. Create the Victory sequence: explosion of final boss, victory dialog (Phase 16), credit scroll showing stats (kills, level, time), fade to title.
7. Implement screen shake effect for big explosions and boss hits.
8. Implement particle/explosion sprites for enemy and boss destruction.
9. Implement HDMA color gradient on BG backdrop for atmospheric depth.
10. Perform full integration testing: boot -> title -> new game -> zone 1 -> boss 1 -> zone 2 -> boss 2 -> zone 3 -> final boss -> victory.
11. Fix any state machine edge cases: double-trigger prevention, VRAM corruption on rapid transitions, stack overflow in nested overlays.
12. Final ROM size and performance optimization pass.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/game/save.h
```c
#ifndef SAVE_H
#define SAVE_H

#include "game.h"
#include "game/rpg_stats.h"
#include "game/inventory.h"
#include "game/story.h"
#include "game/zone.h"

/* Save slot magic number for validation */
#define SAVE_MAGIC_1    0x564F  /* "VO" */
#define SAVE_MAGIC_2    0x4944  /* "ID" */

/* Save data structure (packed for SRAM) */
typedef struct {
    /* Header (6 bytes) */
    u16 magic1;             /* Must be SAVE_MAGIC_1 */
    u16 magic2;             /* Must be SAVE_MAGIC_2 */
    u16 checksum;           /* XOR checksum of data[] */

    /* Player stats (20 bytes) */
    u8  level;
    u16 xp;
    s16 max_hp;
    s16 current_hp;
    s16 atk;
    s16 def;
    s16 spd;
    s16 max_sp;
    s16 current_sp;
    u16 credits;

    /* Equipment (2 bytes) */
    u8  weapon_id;
    u8  pad1;

    /* Inventory (32 bytes: 16 slots * 2 bytes) */
    u8  inv_items[INVENTORY_SIZE];
    u8  inv_counts[INVENTORY_SIZE];

    /* Game progress (6 bytes) */
    u8  current_zone;
    u16 story_flags;
    u16 total_kills;
    u8  pad2;

} SaveData;

#define SAVE_DATA_SIZE  sizeof(SaveData)

/* Initialize save system */
void saveInit(void);

/* Save current game state to SRAM. Returns 1 on success. */
u8 saveGame(void);

/* Load game state from SRAM. Returns 1 on success (valid save found). */
u8 loadGame(void);

/* Check if a valid save exists in SRAM. Returns 1 if valid. */
u8 saveExists(void);

/* Erase save data */
void saveErase(void);

/* Calculate XOR checksum over the data portion of SaveData */
u16 saveCalcChecksum(SaveData *data);

#endif
```

### J:/code/snes/snes-rpg-test/src/game/save.c
```c
/*==============================================================================
 * SRAM Save/Load System
 *
 * Save data is stored in battery-backed SRAM at $70:0000.
 * Uses PVSnesLib's consoleCopySram()/consoleLoadSram() which
 * handle the bank switching and DMA transfer.
 *
 * Save format:
 *   Bytes 0-1:   Magic number 1 (0x564F = "VO")
 *   Bytes 2-3:   Magic number 2 (0x4944 = "ID")
 *   Bytes 4-5:   XOR checksum of remaining data
 *   Bytes 6+:    Game state data
 *
 * Checksum validation prevents loading corrupted or uninitialized SRAM.
 *============================================================================*/

#include "game/save.h"

/* Temporary buffer for save data (WRAM) */
static SaveData save_buffer;

u16 saveCalcChecksum(SaveData *data)
{
    u16 checksum = 0;
    u8 *ptr = (u8 *)data;
    u16 i;
    /* Checksum covers everything after the header (skip magic + checksum = 6 bytes) */
    for (i = 6; i < SAVE_DATA_SIZE; i++) {
        checksum ^= (u16)ptr[i] << ((i & 1) ? 8 : 0);
    }
    return checksum;
}

void saveInit(void)
{
    /* Nothing to initialize. SRAM is battery-backed and persistent. */
}

u8 saveGame(void)
{
    u8 i;

    /* Pack current state into save buffer */
    save_buffer.magic1 = SAVE_MAGIC_1;
    save_buffer.magic2 = SAVE_MAGIC_2;

    /* Player stats */
    save_buffer.level = rpg_stats.level;
    save_buffer.xp = rpg_stats.xp;
    save_buffer.max_hp = rpg_stats.max_hp;
    save_buffer.current_hp = rpg_stats.current_hp;
    save_buffer.atk = rpg_stats.atk;
    save_buffer.def = rpg_stats.def;
    save_buffer.spd = rpg_stats.spd;
    save_buffer.max_sp = rpg_stats.max_sp;
    save_buffer.current_sp = rpg_stats.current_sp;
    save_buffer.credits = rpg_stats.credits;

    /* Equipment */
    save_buffer.weapon_id = equipment.weapon_id;
    save_buffer.pad1 = 0;

    /* Inventory */
    for (i = 0; i < INVENTORY_SIZE; i++) {
        save_buffer.inv_items[i] = inventory[i].item_id;
        save_buffer.inv_counts[i] = inventory[i].quantity;
    }

    /* Progress */
    save_buffer.current_zone = zone_state.current_zone;
    save_buffer.story_flags = story_flags;
    save_buffer.total_kills = rpg_stats.total_kills;
    save_buffer.pad2 = 0;

    /* Calculate checksum */
    save_buffer.checksum = saveCalcChecksum(&save_buffer);

    /* Write to SRAM using PVSnesLib */
    consoleCopySram((u8 *)&save_buffer, SAVE_DATA_SIZE);

    return 1;
}

u8 loadGame(void)
{
    u16 checksum;
    u8 i;

    /* Read from SRAM */
    consoleLoadSram((u8 *)&save_buffer, SAVE_DATA_SIZE);

    /* Validate magic numbers */
    if (save_buffer.magic1 != SAVE_MAGIC_1 ||
        save_buffer.magic2 != SAVE_MAGIC_2) {
        return 0; /* No valid save */
    }

    /* Validate checksum */
    checksum = saveCalcChecksum(&save_buffer);
    if (checksum != save_buffer.checksum) {
        return 0; /* Corrupted save */
    }

    /* Restore player stats */
    rpgStatsInit();  /* Reset to defaults first */
    rpg_stats.level = save_buffer.level;
    rpg_stats.xp = save_buffer.xp;
    rpg_stats.max_hp = save_buffer.max_hp;
    rpg_stats.current_hp = save_buffer.current_hp;
    rpg_stats.atk = save_buffer.atk;
    rpg_stats.def = save_buffer.def;
    rpg_stats.spd = save_buffer.spd;
    rpg_stats.max_sp = save_buffer.max_sp;
    rpg_stats.current_sp = save_buffer.current_sp;
    rpg_stats.credits = save_buffer.credits;
    rpg_stats.total_kills = save_buffer.total_kills;

    /* Restore equipment */
    equipment.weapon_id = save_buffer.weapon_id;
    if (equipment.weapon_id != 0) {
        const ItemDef *def = inventoryGetItemDef(equipment.weapon_id);
        equipment.atk_bonus = def ? def->effect : 0;
    }

    /* Restore inventory */
    inventoryInit();
    for (i = 0; i < INVENTORY_SIZE; i++) {
        inventory[i].item_id = save_buffer.inv_items[i];
        inventory[i].quantity = save_buffer.inv_counts[i];
    }

    /* Restore progress */
    story_flags = save_buffer.story_flags;

    return 1;
}

u8 saveExists(void)
{
    u16 checksum;

    consoleLoadSram((u8 *)&save_buffer, SAVE_DATA_SIZE);

    if (save_buffer.magic1 != SAVE_MAGIC_1 ||
        save_buffer.magic2 != SAVE_MAGIC_2) {
        return 0;
    }

    checksum = saveCalcChecksum(&save_buffer);
    return (checksum == save_buffer.checksum) ? 1 : 0;
}

void saveErase(void)
{
    u8 i;
    u8 *ptr = (u8 *)&save_buffer;
    for (i = 0; i < SAVE_DATA_SIZE; i++) {
        ptr[i] = 0;
    }
    consoleCopySram((u8 *)&save_buffer, SAVE_DATA_SIZE);
}
```

### J:/code/snes/snes-rpg-test/include/ui/hud.h
```c
#ifndef HUD_H
#define HUD_H

#include "game.h"

/* HUD layout on BG3 (tile positions) */
#define HUD_HP_X        22  /* Top-right area */
#define HUD_HP_Y        1
#define HUD_WEAPON_X    22
#define HUD_WEAPON_Y    2
#define HUD_SP_X        22
#define HUD_SP_Y        3
#define HUD_ZONE_X      1
#define HUD_ZONE_Y      0

/* Initialize HUD */
void hudInit(void);

/* Update HUD each frame (only redraws when values change) */
void hudUpdate(void);

/* Force full HUD redraw */
void hudRedraw(void);

/* Show/hide HUD */
void hudShow(void);
void hudHide(void);

#endif
```

### J:/code/snes/snes-rpg-test/src/ui/hud.c
```c
/*==============================================================================
 * Flight Mode HUD
 *
 * Displays on BG3 (text layer):
 *   Top-left:  Zone name
 *   Top-right: HP bar, weapon name, SP indicator
 *
 * Layout:
 *   ZONE 1                  HP:[=====]
 *                           LASER
 *                           SP: ** _
 *============================================================================*/

#include "ui/hud.h"
#include "game/rpg_stats.h"
#include "game/inventory.h"
#include "game/zone.h"
#include "engine/bullets.h"

static u8 hud_visible;
static s16 last_hp;         /* Cached to avoid unnecessary redraws */
static s16 last_max_hp;
static u8  last_sp;
static u8  last_weapon;
static u8  last_zone;

static const char *zone_names[ZONE_COUNT] = {
    "ZONE 1", "ZONE 2", "ZONE 3"
};

static const char *weapon_names[4] = {
    "LASER", "SPREAD", "HOMING", "PLASMA"
};

void hudInit(void)
{
    hud_visible = 0;
    last_hp = -1;   /* Force first draw */
    last_max_hp = -1;
    last_sp = 0xFF;
    last_weapon = 0xFF;
    last_zone = 0xFF;
}

void hudShow(void)
{
    hud_visible = 1;
    hudRedraw();
}

void hudHide(void)
{
    u8 x;
    hud_visible = 0;

    /* Clear HUD text areas */
    for (x = 0; x < 10; x++) {
        consoleDrawText(HUD_ZONE_X + x, HUD_ZONE_Y, " ");
    }
    for (x = 0; x < 10; x++) {
        consoleDrawText(HUD_HP_X + x, HUD_HP_Y, " ");
        consoleDrawText(HUD_WEAPON_X + x, HUD_WEAPON_Y, " ");
        consoleDrawText(HUD_SP_X + x, HUD_SP_Y, " ");
    }
}

void hudRedraw(void)
{
    last_hp = -1;
    last_max_hp = -1;
    last_sp = 0xFF;
    last_weapon = 0xFF;
    last_zone = 0xFF;
    hudUpdate();
}

void hudUpdate(void)
{
    u8 hp_filled;
    u8 i;

    if (!hud_visible) return;

    /* Zone name (only redraw on change) */
    if (last_zone != zone_state.current_zone) {
        last_zone = zone_state.current_zone;
        consoleDrawText(HUD_ZONE_X, HUD_ZONE_Y, "%s",
                        zone_names[last_zone]);
    }

    /* HP bar (redraw on change) */
    if (last_hp != rpg_stats.current_hp ||
        last_max_hp != rpg_stats.max_hp) {
        last_hp = rpg_stats.current_hp;
        last_max_hp = rpg_stats.max_hp;

        /* 6-character HP bar */
        if (rpg_stats.max_hp > 0) {
            hp_filled = (u8)((u16)rpg_stats.current_hp * 6 /
                             rpg_stats.max_hp);
        } else {
            hp_filled = 0;
        }

        consoleDrawText(HUD_HP_X, HUD_HP_Y, "HP:");
        for (i = 0; i < 6; i++) {
            if (i < hp_filled) {
                consoleDrawText(HUD_HP_X + 3 + i, HUD_HP_Y, "=");
            } else {
                consoleDrawText(HUD_HP_X + 3 + i, HUD_HP_Y, "-");
            }
        }
    }

    /* Weapon name */
    if (last_weapon != player_weapon.weapon_type) {
        last_weapon = player_weapon.weapon_type;
        /* Clear old name */
        consoleDrawText(HUD_WEAPON_X, HUD_WEAPON_Y, "        ");
        if (last_weapon < 4) {
            consoleDrawText(HUD_WEAPON_X, HUD_WEAPON_Y, "%s",
                            weapon_names[last_weapon]);
        }
    }

    /* SP indicator (show dots for charges) */
    if (last_sp != (u8)rpg_stats.current_sp) {
        last_sp = (u8)rpg_stats.current_sp;
        consoleDrawText(HUD_SP_X, HUD_SP_Y, "SP:");
        for (i = 0; i < 4; i++) {
            if (i < last_sp) {
                consoleDrawText(HUD_SP_X + 3 + i, HUD_SP_Y, "*");
            } else {
                consoleDrawText(HUD_SP_X + 3 + i, HUD_SP_Y, "_");
            }
        }
    }
}
```

### J:/code/snes/snes-rpg-test/include/engine/effects.h
```c
#ifndef EFFECTS_H
#define EFFECTS_H

#include "game.h"

/* Screen shake */
void effectScreenShake(u8 intensity, u8 duration);
void effectUpdateShake(void);

/* Get current shake offset (apply to BG scroll) */
s8 effectGetShakeX(void);
s8 effectGetShakeY(void);

/* Explosion particle system */
#define MAX_PARTICLES 8

typedef struct {
    s16 x, y;
    s8  vx, vy;
    u8  life;       /* Frames remaining */
    u8  active;
} Particle;

void effectParticleInit(void);
void effectSpawnExplosion(s16 x, s16 y, u8 count);
void effectUpdateParticles(void);
void effectRenderParticles(void);

/* HDMA backdrop gradient (space depth effect) */
void effectInitHDMAGradient(void);
void effectUpdateHDMAGradient(void);

#endif
```

### J:/code/snes/snes-rpg-test/src/engine/effects.c
```c
/*==============================================================================
 * Visual Effects
 *
 * Screen shake: offsets BG1/BG2 scroll by random amounts for N frames.
 * Particles: simple sprite-based explosions.
 * HDMA gradient: per-scanline backdrop color change for depth.
 *============================================================================*/

#include "engine/effects.h"
#include "engine/scroll.h"
#include "engine/sprites.h"

/* Screen shake state */
static u8 shake_active;
static u8 shake_intensity;
static u8 shake_duration;
static u8 shake_timer;
static s8 shake_x;
static s8 shake_y;

/* Particle state */
static Particle particles[MAX_PARTICLES];

/* Particle OAM slots: 36-43 (allocated in Phase 1 for particles) */
#define PARTICLE_OAM_START  36

/* HDMA gradient table (224 scanlines * 2 bytes = 448 bytes) */
/* Format: each entry is an RGB value for the backdrop color */
static u8 hdma_gradient_table[224 * 3 + 1];
static u8 hdma_gradient_active;

void effectScreenShake(u8 intensity, u8 duration)
{
    shake_active = 1;
    shake_intensity = intensity;
    shake_duration = duration;
    shake_timer = duration;
}

void effectUpdateShake(void)
{
    if (!shake_active) return;

    shake_timer--;
    if (shake_timer == 0) {
        shake_active = 0;
        shake_x = 0;
        shake_y = 0;
        return;
    }

    /* Generate pseudo-random shake offsets */
    shake_x = (s8)((g_frame_count * 7) & (shake_intensity * 2 - 1))
              - (s8)shake_intensity;
    shake_y = (s8)((g_frame_count * 13) & (shake_intensity * 2 - 1))
              - (s8)shake_intensity;

    /* Reduce intensity over time */
    if ((shake_timer & 0x03) == 0 && shake_intensity > 1) {
        shake_intensity--;
    }
}

s8 effectGetShakeX(void) { return shake_active ? shake_x : 0; }
s8 effectGetShakeY(void) { return shake_active ? shake_y : 0; }

/* --- Particle System --- */

void effectParticleInit(void)
{
    u8 i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        particles[i].active = 0;
    }
}

void effectSpawnExplosion(s16 x, s16 y, u8 count)
{
    u8 i, spawned;
    spawned = 0;

    for (i = 0; i < MAX_PARTICLES && spawned < count; i++) {
        if (!particles[i].active) {
            particles[i].active = 1;
            particles[i].x = x;
            particles[i].y = y;
            /* Random-ish velocities using frame counter */
            particles[i].vx = (s8)(((g_frame_count + i * 37) & 7) - 4);
            particles[i].vy = (s8)(((g_frame_count + i * 23) & 7) - 4);
            particles[i].life = 15 + (i & 7);  /* 15-22 frames */
            spawned++;
        }
    }
}

void effectUpdateParticles(void)
{
    u8 i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        if (!particles[i].active) continue;

        particles[i].x += particles[i].vx;
        particles[i].y += particles[i].vy;
        particles[i].life--;

        /* Gravity-like effect */
        if ((particles[i].life & 3) == 0) {
            particles[i].vy++;
        }

        if (particles[i].life == 0) {
            particles[i].active = 0;
        }
    }
}

void effectRenderParticles(void)
{
    u8 i;
    u16 oam_id;

    for (i = 0; i < MAX_PARTICLES; i++) {
        oam_id = (PARTICLE_OAM_START + i) * 4;

        if (!particles[i].active) {
            oamSetVisible(oam_id, OBJ_HIDE);
            continue;
        }

        /* Render as small 16x16 sprite using bullet tile */
        oamSet(oam_id,
               (u16)particles[i].x, (u16)particles[i].y,
               3,  /* Priority: on top */
               0, 0,
               OBJ_BULLET_OFFSET >> 4,
               7);  /* Palette 7: special effects (white flash) */
        oamSetEx(oam_id, OBJ_SMALL, OBJ_SHOW);

        /* Fade: hide sprite for alternating frames near end of life */
        if (particles[i].life < 5 && (particles[i].life & 1)) {
            oamSetVisible(oam_id, OBJ_HIDE);
        }
    }
}

/* --- HDMA Backdrop Gradient --- */

void effectInitHDMAGradient(void)
{
    u8 i;
    u16 color;
    u8 r, g, b;

    /* Build a gradient: deep blue at top -> darker blue at bottom */
    for (i = 0; i < 224; i++) {
        /* Blue decreases from top to bottom for depth effect */
        r = 0;
        g = 0;
        b = (u8)(8 - (i >> 5));  /* 8 to 1 over 224 scanlines */
        if (b > 31) b = 0;

        /* SNES color format: 0bbbbbgggggrrrrr */
        color = ((u16)b << 10) | ((u16)g << 5) | (u16)r;

        /* HDMA table format depends on HDMA register configuration */
        /* For backdrop color ($2132), we write 3 bytes per entry:
         * scanline_count, color_lo, color_hi */
        hdma_gradient_table[i * 3] = 1;           /* 1 scanline */
        hdma_gradient_table[i * 3 + 1] = (u8)(color & 0xFF);
        hdma_gradient_table[i * 3 + 2] = (u8)(color >> 8);
    }
    /* Terminate table */
    hdma_gradient_table[224 * 3] = 0;

    hdma_gradient_active = 0;  /* Disabled by default, enable in flight mode */
}

void effectUpdateHDMAGradient(void)
{
    if (!hdma_gradient_active) return;
    /* HDMA table is static, no per-frame updates needed.
     * The HDMA hardware reads the table automatically each frame. */
}
```

### Updated Title Screen (game_state.c)
```c
/* Full title screen with menu options */

static u8 title_cursor;       /* 0=New Game, 1=Continue */
static u8 title_continue_ok;  /* 1 if valid save exists */

static void enterTitle(void)
{
    systemResetVideo();

    /* Load title background (if available) */
    /* For now, use text-only title */
    consoleInitText(0, BG_4COLORS, 0, 0);
    bgSetEnable(2);

    /* Draw title screen */
    consoleDrawText(8, 5, "VOIDRUNNER");
    consoleDrawText(5, 7, "DEFEND THE ARK");

    /* Menu options */
    consoleDrawText(10, 14, "NEW GAME");

    /* Check for save data */
    title_continue_ok = saveExists();
    if (title_continue_ok) {
        consoleDrawText(10, 16, "CONTINUE");
    } else {
        consoleDrawText(10, 16, "--------");  /* Grayed out */
    }

    consoleDrawText(5, 22, "2024 VOIDRUNNER PROJECT");

    title_cursor = 0;

    /* Draw initial cursor */
    consoleDrawText(8, 14 + title_cursor * 2, ">");

    setScreenOn();

    soundPlayMusic(MUSIC_TITLE);
}

static void updateTitle(void)
{
    u16 pressed = inputPressed();

    /* Clear old cursor */
    consoleDrawText(8, 14, " ");
    consoleDrawText(8, 16, " ");

    /* Navigate menu */
    if (pressed & ACTION_UP) {
        if (title_cursor > 0) {
            title_cursor--;
            soundPlaySFX(SFX_MENU_MOVE);
        }
    }
    if (pressed & ACTION_DOWN) {
        if (title_cursor < 1 && title_continue_ok) {
            title_cursor++;
            soundPlaySFX(SFX_MENU_MOVE);
        }
    }

    /* Draw cursor */
    consoleDrawText(8, 14 + title_cursor * 2, ">");

    /* Confirm selection */
    if ((pressed & ACTION_CONFIRM) || (pressed & ACTION_PAUSE)) {
        soundPlaySFX(SFX_MENU_SELECT);

        if (title_cursor == 0) {
            /* New Game */
            gameStateChange(GS_FLIGHT, TRANS_FADE);
        } else if (title_cursor == 1 && title_continue_ok) {
            /* Continue: load save and start at saved zone */
            if (loadGame()) {
                /* Set game to resume from saved zone */
                gameStateSetParam((u16)save_buffer.current_zone);
                gameStateChange(GS_FLIGHT, TRANS_FADE);
            }
        }
    }
}

static void exitTitle(void)
{
    soundFadeMusicOut(15);
}
```

### Updated Game Over Screen
```c
static void enterGameOver(void)
{
    systemResetVideo();
    consoleInitText(0, BG_4COLORS, 0, 0);
    bgSetEnable(2);

    consoleDrawText(9, 8, "GAME OVER");
    consoleDrawText(4, 12, "YOUR JOURNEY ENDS HERE");

    /* Stats summary */
    consoleDrawText(4, 15, "LEVEL: %d", rpg_stats.level);
    consoleDrawText(4, 16, "KILLS: %d", rpg_stats.total_kills);

    /* Options */
    consoleDrawText(8, 20, "> CONTINUE");
    consoleDrawText(8, 22, "  TITLE");

    setScreenOn();
    soundPlayMusic(MUSIC_GAME_OVER);
}

static void updateGameOver(void)
{
    u16 pressed = inputPressed();
    static u8 go_cursor = 0;

    /* Clear cursors */
    consoleDrawText(8, 20, " ");
    consoleDrawText(8, 22, " ");

    if (pressed & ACTION_UP && go_cursor > 0) { go_cursor--; soundPlaySFX(SFX_MENU_MOVE); }
    if (pressed & ACTION_DOWN && go_cursor < 1) { go_cursor++; soundPlaySFX(SFX_MENU_MOVE); }

    consoleDrawText(8, 20 + go_cursor * 2, ">");

    if (pressed & ACTION_CONFIRM) {
        soundPlaySFX(SFX_MENU_SELECT);
        if (go_cursor == 0) {
            /* Continue from current zone (lose some HP as penalty) */
            rpgReduceHP(rpg_stats.current_hp / 5);
            gameStateChange(GS_FLIGHT, TRANS_FADE);
        } else {
            gameStateChange(GS_TITLE, TRANS_FADE);
        }
    }
}
```

### Updated Victory Screen
```c
static u8 victory_phase;
static u16 victory_timer;

static void enterVictory(void)
{
    systemResetVideo();
    consoleInitText(0, BG_4COLORS, 0, 0);
    bgSetEnable(2);

    victory_phase = 0;
    victory_timer = 0;

    /* Phase 0: Display victory message */
    consoleDrawText(8, 4, "VICTORY!");
    consoleDrawText(3, 6, "THE FLAGSHIP IS DESTROYED");
    consoleDrawText(5, 7, "THE ARK IS SAVED");

    setScreenOn();
    soundPlayMusic(MUSIC_VICTORY);
}

static void updateVictory(void)
{
    u16 elapsed_sec;

    victory_timer++;

    switch (victory_phase) {
        case 0:
            /* Show stats after 3 seconds */
            if (victory_timer >= 180) {
                victory_phase = 1;

                /* Calculate approximate play time */
                elapsed_sec = g_session_frames / 60;

                consoleDrawText(6, 11, "= MISSION STATS =");
                consoleDrawText(6, 13, "LEVEL:    %d", rpg_stats.level);
                consoleDrawText(6, 14, "XP:       %d", rpg_stats.xp);
                consoleDrawText(6, 15, "KILLS:    %d", rpg_stats.total_kills);
                consoleDrawText(6, 16, "CREDITS:  %d", rpg_stats.credits);
                consoleDrawText(6, 17, "TIME:     %d:%02d",
                    elapsed_sec / 60, elapsed_sec % 60);

                consoleDrawText(5, 21, "PRESS START");
            }
            break;

        case 1:
            /* Wait for player input */
            if (inputPressed() & ACTION_PAUSE) {
                /* Erase save (game complete) and return to title */
                saveErase();
                gameStateChange(GS_TITLE, TRANS_FADE);
            }
            break;
    }
}
```

### Updated enterFlight() with Save and Continue Support
```c
static void enterFlight(void)
{
    u8 startZone;
    u16 param;

    systemResetVideo();

    /* Check if we're resuming from a save (param = zone ID) */
    param = gameStateGetParam();

    if (param == 0 || param > ZONE_COUNT) {
        /* New game */
        rpgStatsInit();
        inventoryInit();
        storyInit();
        zoneInit();
        startZone = ZONE_DEBRIS;
    } else {
        /* Continue from save - stats already loaded by loadGame() */
        zoneInit();
        startZone = (u8)param;
    }

    spriteSystemInit();
    bulletInit();
    enemyInit();
    collisionInit();
    scrollInit();
    dialogInit();
    bossInit();
    effectParticleInit();
    hudInit();

    /* Load the starting zone */
    zoneLoad(startZone);

    /* Initialize player */
    playerInit();
    bulletLoadGraphics();

    /* Enable display layers */
    bgSetEnable(0);
    bgSetEnable(1);
    bgSetEnable(2);  /* BG3 for HUD text */
    setScreenOn();

    /* Show HUD */
    hudShow();

    /* Auto-save on zone entry */
    saveGame();

    gameStateSetParam(0); /* Clear param */
}
```

### Integration with updateFlight() - Effects
```c
/* Add to updateFlight(): */

/* Update visual effects */
effectUpdateShake();
effectUpdateParticles();

/* Apply screen shake to BG scroll offset */
{
    s8 sx = effectGetShakeX();
    s8 sy = effectGetShakeY();
    if (sx != 0 || sy != 0) {
        /* Offset the BG scroll by shake amount */
        bgSetScroll(0, (u16)sx, scrollGetY() + (u16)sy);
    }
}

/* Update HUD */
hudUpdate();

/* Render particles */
effectRenderParticles();
```

### Auto-Save Trigger Integration
```c
/* Save game at key moments:
 * - Zone entry (in enterFlight / zoneLoad)
 * - After boss defeat (in boss.c updateDefeated)
 * - Before zone transition (in zoneAdvance) */

/* In boss.c updateDefeated(), before zoneBossDefeated(): */
saveGame();

/* In zone.c zoneAdvance(), before fadeOutBlocking(): */
saveGame();
```

## Technical Specifications

### Save Data Layout in SRAM
```
Offset  Size   Field
------  ----   -----
0x0000  2      Magic number 1 (0x564F)
0x0002  2      Magic number 2 (0x4944)
0x0004  2      XOR checksum
0x0006  1      Player level
0x0007  2      Player XP
0x0009  2      Max HP
0x000B  2      Current HP
0x000D  2      ATK
0x000F  2      DEF
0x0011  2      SPD
0x0013  2      Max SP
0x0015  2      Current SP
0x0017  2      Credits
0x0019  1      Equipped weapon ID
0x001A  1      Padding
0x001B  16     Inventory item IDs (16 slots)
0x002B  16     Inventory quantities (16 slots)
0x003B  1      Current zone
0x003C  2      Story flags
0x003E  2      Total kills
0x0040  1      Padding
------
Total: 65 bytes

SRAM budget: 65 bytes / 8192 bytes available = 0.8%
Leaves room for 3 save slots if desired in the future.
```

### Title Screen Layout
```
Row   Content
---   -------
5     VOIDRUNNER
7     DEFEND THE ARK
14    > NEW GAME
16      CONTINUE (or --------)
22    2024 VOIDRUNNER PROJECT
```

### HUD Layout During Flight
```
Top-left:   ZONE 1
Top-right:  HP:[======]
            LASER
            SP: ** _
```

### Screen Shake Parameters
```
Boss explosion: intensity=4, duration=20 (0.33 seconds)
Player hit:     intensity=2, duration=8  (0.13 seconds)
Heavy explosion: intensity=3, duration=12

Shake offset formula: pseudo-random in [-intensity, +intensity]
Applied to BG1 and BG2 scroll registers.
BG3 (HUD text) is NOT shaken (keeps text readable).
```

### Particle Explosion Parameters
```
Enemy destroyed: 3 particles, life=15-22 frames
Boss explosion:  6-8 particles, life=15-22 frames

Particles reuse bullet tiles (palette 7 = white flash).
Max 8 simultaneous particles (OAM slots 36-43).
Velocity: random in [-4, +3] pixels/frame.
Gravity: +1 vy every 4 frames.
```

### HDMA Gradient Specification
```
224 scanlines of backdrop color gradient:
  Top of screen:    dark blue (0, 0, 8)
  Bottom of screen: near black (0, 0, 1)

This creates a subtle depth effect in space.
Uses HDMA channel 7 (reserved in Phase 2).
Table size: 224 * 3 + 1 = 673 bytes.

Note: HDMA gradient is only active during flight mode.
Disabled during battle (flat backdrop) and menus.
```

### Final Integration Checklist
```
[ ] Boot -> PVSnesLib splash -> fade in title screen
[ ] Title: "NEW GAME" starts fresh, "CONTINUE" loads save
[ ] Zone 1: background, enemies, scrolling, HUD all working
[ ] Story dialogs trigger at correct scroll positions
[ ] Pause menu shows stats, resumes correctly
[ ] Battle transitions: fade out flight, load battle scene
[ ] Battle: HP bars, menu, damage, animations
[ ] Battle victory: XP, credits, items applied
[ ] Level up: stats increase, full heal, notification
[ ] Boss encounter: approach, flight phase, battle transition
[ ] Boss defeat: explosion sequence, zone advance
[ ] Zone transition: graphics swap, new enemies, new music
[ ] Zone 2 and Zone 3 play through completely
[ ] Final boss defeat -> victory screen with stats
[ ] Game over -> continue or return to title
[ ] Save/load cycle: save on zone entry, load on continue
[ ] No crashes during any state transition
[ ] No VRAM corruption visible in any scene
[ ] Performance: consistent 60fps in flight mode
[ ] Audio: music changes with scenes, SFX trigger correctly
[ ] Total playtime: approximately 10 minutes
```

### Performance Budget
```
Flight mode frame budget (16.67ms at 60fps):
  Input processing:     0.2ms
  Player update:        0.1ms
  Enemy AI (8x):        0.8ms
  Bullet update (24x):  0.4ms
  Collision checks:     1.2ms
  Scroll update:        0.1ms
  Zone/Boss update:     0.2ms
  HUD update:           0.3ms
  Effects update:       0.2ms
  Sprite render:        0.5ms
  BG update:            0.2ms
  spcProcess:           0.3ms
  ---
  Total:                4.5ms / 16.67ms = 27% CPU

Comfortable headroom. Even with worst-case collision
(8 enemies + 24 bullets), we stay under 50% CPU.
```

### Final ROM Size Estimate
```
Component            Size
---------            ----
Code (all .c files)  ~32KB
Asset data:
  BG tiles (3 zones) ~24KB (3 * 8KB)
  BG maps (3 zones)  ~6KB
  BG palettes         ~0.2KB
  Player sprite       ~0.5KB
  Enemy sprites (4)   ~4KB
  Boss sprites (2)    ~2KB
  Bullet sprites      ~0.5KB
  Font data           ~2KB
Music (soundbank)     ~16KB
SFX (BRR samples)     ~6KB
Dialog text           ~3KB
Tables/definitions    ~2KB
---
Total:               ~99KB / 1024KB ROM = 9.7%

ROM is far from full. Plenty of room for additional
content, animations, or more complex music.
```

## Acceptance Criteria
1. Title screen displays with "NEW GAME" and "CONTINUE" options.
2. "CONTINUE" is grayed out when no valid save data exists.
3. Selecting "NEW GAME" starts the game at Zone 1 with default stats.
4. Selecting "CONTINUE" restores player stats, inventory, and zone from save.
5. HUD displays HP, weapon, SP, and zone name during flight mode.
6. HUD updates in real-time as stats change (damage, weapon switch, etc.).
7. Screen shake triggers on boss hits and enemy explosions.
8. Particle explosions appear when enemies are destroyed.
9. Game Over screen shows stats and offers Continue/Title options.
10. Victory screen shows completion stats (level, kills, time).
11. Save data persists across power cycles (SRAM + battery).
12. Corrupted/missing save data is correctly detected and rejected.
13. Full playthrough from title to victory completes without crashes.
14. No VRAM corruption during any scene transition.
15. Game runs at consistent 60fps throughout all gameplay.
16. Audio plays correctly: music per scene, SFX per event.
17. Total game duration is approximately 10 minutes.
18. All story dialogs display and advance correctly.
19. All 3 bosses are fightable and defeatable.
20. Auto-save occurs at zone entry and boss defeat.

## SNES-Specific Constraints
- consoleCopySram()/consoleLoadSram() transfer data between WRAM buffer and SRAM. The SRAM address is fixed at $70:0000 by the ROM header (CARTRIDGETYPE $02).
- SRAM writes may fail on emulators that do not support SRAM saving. Test with Mesen (good SRAM support) and snes9x.
- The XOR checksum is simple but sufficient to detect uninitialized SRAM (all 0xFF or 0x00).
- Screen shake modifies BG scroll registers, which must be set during VBlank. The scroll VBlank callback handles this.
- HDMA gradient uses HDMA channel 7. Only one HDMA effect can use a channel simultaneously. Do not enable gradient during battle (it conflicts with battle BG setup).
- Particle sprites reuse existing tile data (bullet tiles) to avoid loading additional graphics. Palette 7 (white/effect palette) is used for the flash appearance.
- The total save data (65 bytes) is well under the 8KB SRAM limit.

## Estimated Complexity
**Complex** - This phase touches every system in the game. Integration testing alone requires playing through the entire game multiple times. The save system requires careful checksum logic and state restoration. The title screen menu and game over flow add new state machine paths that must work correctly with all existing states.
