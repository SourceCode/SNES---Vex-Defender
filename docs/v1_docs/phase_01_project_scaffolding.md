# Phase 1: Project Scaffolding & Build System Setup

## Objective
Create the complete project directory structure, Makefile build system, ROM header configuration, and environment setup so that subsequent phases can immediately compile and link code into a working SNES ROM.

## Prerequisites
- None (this is the first phase)
- SNES Build Tools installed at `J:/code/snes/snes-build-tools/`
- PVSnesLib available at `J:/code/snes/snes-build-tools/tools/pvsneslib/`

## Detailed Tasks

### 1. Create Project Directory Structure
```
J:/code/snes/snes-rpg-test/
├── assets/
│   ├── backgrounds/       # Converted background tiles/maps
│   ├── sprites/           # Converted sprite data
│   │   ├── player/
│   │   ├── enemies/
│   │   ├── bullets/
│   │   ├── items/
│   │   └── npcs/
│   ├── fonts/             # Font bitmap data
│   ├── ui/                # UI element graphics
│   ├── music/             # IT module files for music
│   └── sfx/               # WAV files for sound effects
├── src/
│   ├── main.c             # Entry point
│   ├── game.h             # Master game header (shared types/defines)
│   ├── game.c             # Game state machine
│   ├── player.h / .c      # Player ship logic
│   ├── enemy.h / .c       # Enemy system
│   ├── battle.h / .c      # Turn-based battle engine
│   ├── bullet.h / .c      # Projectile system
│   ├── scroll.h / .c      # Scrolling engine
│   ├── ui.h / .c          # UI/menu rendering
│   ├── dialog.h / .c      # Story/dialog system
│   ├── items.h / .c       # Item/equipment system
│   ├── stats.h / .c       # RPG stats/leveling
│   ├── sound.h / .c       # Sound engine wrapper
│   └── collision.h / .c   # Collision detection
├── include/
│   └── config.h           # Global configuration constants
├── build/                 # Build output directory
├── data/
│   ├── hdr.asm            # ROM header definition
│   ├── data.asm           # Asset include directives
│   └── linkfile           # Linker configuration
├── tools/
│   └── convert_assets.bat # Asset conversion batch script
├── docs/
│   └── v1_docs/           # Phase documentation
└── Makefile               # Master build file
```

### 2. Create the Makefile

The Makefile must integrate with PVSnesLib's `snes_rules` and handle:
- C compilation via 816-tcc
- Asset conversion via gfx4snes
- Sound conversion via smconv
- Linking via wlalink

### 3. Create ROM Header (hdr.asm)

Standard LoROM header with game metadata.

### 4. Create Linker Configuration (linkfile)

### 5. Create Environment Setup Script

### 6. Create Initial main.c Stub

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `Makefile` | CREATE | Master build system |
| `data/hdr.asm` | CREATE | ROM header |
| `data/data.asm` | CREATE | Asset includes |
| `data/linkfile` | CREATE | Linker config |
| `src/main.c` | CREATE | Entry point stub |
| `src/game.h` | CREATE | Master header |
| `include/config.h` | CREATE | Global config |
| `tools/convert_assets.bat` | CREATE | Asset pipeline |
| `env.bat` | CREATE | Environment vars |

## Technical Specifications

### Makefile Content
```makefile
# SNES RPG - Vertical Shooter Turn-Based RPG
# Build System Configuration

# PVSnesLib path - adjust if needed
PVSNESLIB_HOME := J:/code/snes/snes-build-tools/tools/pvsneslib

ifeq ($(strip $(PVSNESLIB_HOME)),)
$(error "PVSNESLIB_HOME is not set")
endif

include ${PVSNESLIB_HOME}/devkitsnes/snes_rules

#---------------------------------------------------------------------------------
export ROMNAME := vex_defender

# Source files
export OFILES := src/main.obj

.PHONY: bitmaps all clean

all: bitmaps $(ROMNAME).sfc

clean: cleanBuildRes cleanRom cleanGfx

#---------------------------------------------------------------------------------
# Asset conversion rules will be added in Phase 3
#---------------------------------------------------------------------------------
bitmaps:
	@echo "No assets to convert yet"
```

### ROM Header (hdr.asm)
```asm
;== == == == == == == == == == == == == == == == ==
; SNES ROM Header - VEX DEFENDER
;== == == == == == == == == == == == == == == == ==
.MEMORYMAP
    SLOTSIZE $8000
    DEFAULTSLOT 0
    SLOT 0 $8000
.ENDME

.ROMBANKSIZE $8000
.ROMBANKS 8              ; 256KB ROM (expandable)

.SNESHEADER
    ID "SNES"
    NAME "VEX DEFENDER       "  ; 21 chars exactly
    LOROM
    SLOWROM
    CARTRIDGETYPE $00     ; ROM only (change to $02 for SRAM)
    ROMSIZE $08           ; 256KB
    SRAMSIZE $00          ; No SRAM initially (Phase 20 adds save)
    COUNTRY $01           ; USA
    LICENSEECODE $00
    VERSION $00
.ENDSNES

.SNESNATIVEVECTOR
    COP EmptyHandler
    BRK EmptyHandler
    ABORT EmptyHandler
    NMI VBlank
    IRQ EmptyHandler
.ENDNATIVEVECTOR

.SNESEMUVECTOR
    COP EmptyHandler
    ABORT EmptyHandler
    NMI VBlank
    RESET tcc__start
    IRQBRK EmptyHandler
.ENDEMUVECTOR
```

### data.asm
```asm
;== == == == == == == == == == == == == == == == ==
; Data Section - Asset Includes
; Assets will be added as phases progress
;== == == == == == == == == == == == == == == == ==
.SECTION ".rodata1" SUPERFREE

; Phase 3+ will add asset .incbin directives here

.ENDS
```

### linkfile
```
[objects]
data/hdr.obj
data/data.obj
src/main.obj
```

### config.h
```c
#ifndef CONFIG_H
#define CONFIG_H

/*=============================================================
 * VEX DEFENDER - Global Configuration
 * SNES Vertical Shooter Turn-Based RPG
 *=============================================================*/

/* Screen dimensions */
#define SCREEN_WIDTH    256
#define SCREEN_HEIGHT   224

/* Game timing */
#define FRAMES_PER_SEC  60
#define GAME_SPEED      1

/* VRAM Address Map (Word addresses - multiply by 2 for bytes)
 * Total VRAM: 64KB = 32K words
 *
 * $0000-$1FFF : BG1 Tiles - Starfield/Space background  (16KB)
 * $2000-$2FFF : BG2 Tiles - UI/Battle background         (8KB)
 * $3000-$3FFF : BG3 Tiles - Font/Text                    (8KB)
 * $4000-$47FF : Sprite Tiles (player, enemies, bullets)  (4KB)
 * $4800-$4FFF : Sprite Tiles (items, effects)            (4KB)
 * $5000-$57FF : BG1 Map                                  (4KB)
 * $5800-$5FFF : BG2 Map                                  (4KB)
 * $6000-$67FF : BG3 Map                                  (4KB)
 * $6800-$7FFF : Reserved / Extra sprite tiles           (12KB)
 */
#define VRAM_BG1_TILES   0x0000
#define VRAM_BG2_TILES   0x2000
#define VRAM_BG3_TILES   0x3000
#define VRAM_SPR_TILES   0x4000
#define VRAM_SPR_EXTRA   0x4800
#define VRAM_BG1_MAP     0x5000
#define VRAM_BG2_MAP     0x5800
#define VRAM_BG3_MAP     0x6000
#define VRAM_SPR_BATTLE  0x6800

/* Palette allocation (CGRAM)
 * 256 colors = 16 palettes x 16 colors each
 * Palette 0:  BG1 - Starfield/background
 * Palette 1:  BG1 - Background alt
 * Palette 2:  BG2 - UI elements
 * Palette 3:  BG3 - Font (white text)
 * Palette 4:  BG3 - Font (yellow highlight)
 * Palette 5:  Sprites - Player ship
 * Palette 6:  Sprites - Enemy type A
 * Palette 7:  Sprites - Enemy type B
 * Palette 8:  Sprites - Bullets/projectiles
 * Palette 9:  Sprites - Items/pickups
 * Palette 10: Sprites - Boss
 * Palette 11: Sprites - Effects/explosions
 * Palette 12: Battle UI elements
 * Palette 13: NPC sprites
 * Palette 14: Reserved
 * Palette 15: Reserved
 */
#define PAL_BG1_MAIN     0
#define PAL_BG1_ALT      1
#define PAL_BG2_UI       2
#define PAL_BG3_FONT     3
#define PAL_BG3_HILITE   4
#define PAL_SPR_PLAYER   5
#define PAL_SPR_ENEMY_A  6
#define PAL_SPR_ENEMY_B  7
#define PAL_SPR_BULLET   8
#define PAL_SPR_ITEM     9
#define PAL_SPR_BOSS     10
#define PAL_SPR_FX       11
#define PAL_BATTLE_UI    12
#define PAL_SPR_NPC      13

/* Game states */
#define STATE_BOOT       0
#define STATE_TITLE      1
#define STATE_STORY      2
#define STATE_FLIGHT     3
#define STATE_BATTLE     4
#define STATE_MENU       5
#define STATE_GAMEOVER   6
#define STATE_VICTORY    7
#define STATE_DIALOG     8

/* Zone definitions */
#define ZONE_DEBRIS      0
#define ZONE_ASTEROID    1
#define ZONE_FLAGSHIP    2
#define ZONE_COUNT       3

/* Player constants */
#define PLAYER_START_X   120
#define PLAYER_START_Y   180
#define PLAYER_SPEED     2
#define PLAYER_MAX_HP    100
#define PLAYER_MAX_MP    50

/* Enemy constants */
#define MAX_ENEMIES      8
#define MAX_BULLETS      16
#define MAX_ITEMS        4

/* Battle constants */
#define BATTLE_MAX_TURNS 99
#define BATTLE_PARTY_SIZE 1

#endif /* CONFIG_H */
```

### main.c (Stub)
```c
/*=============================================================
 * VEX DEFENDER - SNES Vertical Shooter Turn-Based RPG
 * Main Entry Point
 *=============================================================*/
#include <snes.h>
#include "config.h"

int main(void) {
    /* Initialize SNES hardware */
    consoleInit();

    /* Set Mode 1 (4bpp BG1, 4bpp BG2, 2bpp BG3) */
    setMode(BG_MODE1, 0);

    /* Turn on screen */
    setScreenOn();

    /* Main loop - placeholder */
    while(1) {
        WaitForVBlank();
    }

    return 0;
}
```

### game.h (Master Header)
```c
#ifndef GAME_H
#define GAME_H

#include <snes.h>
#include "config.h"

/*--- Global Game State ---*/
typedef struct {
    u8 current_state;      /* STATE_* enum */
    u8 current_zone;       /* ZONE_* enum */
    u8 paused;
    u16 frame_counter;
    u16 score;
    u8 story_flags;        /* Bitfield for story progression */
} GameState;

extern GameState g_game;

/*--- Function Prototypes (filled in by later phases) ---*/
/* Phase 2 */ void game_init(void);
/* Phase 6 */ void input_update(void);
/* Phase 15 */ void game_update(void);
/* Phase 15 */ void game_render(void);

#endif /* GAME_H */
```

### env.bat
```batch
@echo off
REM VEX DEFENDER - Environment Setup
SET PVSNESLIB_HOME=J:\code\snes\snes-build-tools\tools\pvsneslib
SET PATH=%PVSNESLIB_HOME%\devkitsnes\bin;%PVSNESLIB_HOME%\devkitsnes\tools;%PATH%
echo Environment configured for VEX DEFENDER build.
```

## Asset Requirements
- No assets needed for this phase
- Directory structure must be ready to receive assets in Phase 3

## Acceptance Criteria
1. Running `make` in the project root produces `vex_defender.sfc`
2. The ROM file is a valid SNES ROM (opens in Mesen/snes9x without errors)
3. The ROM shows a black screen (no crash, no garbage)
4. ROM header shows "VEX DEFENDER" as the title
5. All directories in the project structure exist
6. `make clean` removes all build artifacts

## SNES-Specific Constraints
- ROM name in header MUST be exactly 21 characters (pad with spaces)
- LoROM mapping selected (standard for homebrew)
- SlowROM timing (2.68MHz - safer default)
- No SRAM initially (added in Phase 20 for save system)
- The linkfile object order matters: hdr.obj must come first

## Memory Budget (This Phase)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~1KB | 256KB     | ~255KB    |
| WRAM     | ~50B | 128KB     | ~128KB    |
| VRAM     | 0    | 64KB      | 64KB      |
| CGRAM    | 0    | 512B      | 512B      |

## Estimated Complexity
**Simple** - This is boilerplate project setup. The critical detail is getting the Makefile and linker configuration correct for PVSnesLib.

## Agent Instructions
1. Create all directories first using `mkdir -p`
2. Write env.bat and source it before building
3. Copy `snes_rules` path reference from PVSnesLib
4. Build and verify the ROM opens in an emulator
5. If `make` fails, check that PVSNESLIB_HOME path is correct and snes_rules exists
6. The stub main.c should produce a black screen - that's correct behavior
