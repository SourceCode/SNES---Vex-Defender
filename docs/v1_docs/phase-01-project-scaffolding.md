# Phase 1: Project Scaffolding & Build System Setup

## Objective
Create the complete project directory structure, Makefile, ROM header, and data assembly file so that the project compiles to a valid (but empty) SNES ROM that boots to a black screen on any emulator.

## Prerequisites
- None. This is the first phase.

## Detailed Tasks

1. Create the project directory tree:
```
J:/code/snes/snes-rpg-test/
  src/
    main.c
    game/           (game logic modules)
    battle/         (turn-based battle system)
    ui/             (menu and HUD rendering)
    engine/         (scrolling, sprites, collision)
  include/
    game.h          (master game header)
    types.h         (game-specific typedefs and constants)
    vram_map.h      (centralized VRAM address allocation)
    memory_map.h    (WRAM budget and address assignments)
  assets/
    backgrounds/    (converted .pic/.pal/.map files)
    sprites/        (converted sprite tiles)
    fonts/          (font tile data)
    music/          (IT module files)
    sfx/            (BRR sound effect files)
    raw/            (original PNGs before conversion - gitignored)
  build/            (intermediate object files)
  tools/            (project-local conversion scripts)
  hdr.asm           (ROM header and vectors)
  data.asm          (binary data includes)
```

2. Create the Makefile with all asset conversion rules.

3. Create hdr.asm with LoROM header, 8Mbit (1MB) ROM, 8KB SRAM for save data.

4. Create a minimal main.c that calls consoleInit(), sets Mode 1, and enters an infinite WaitForVBlank() loop.

5. Create data.asm with the .include for hdr.asm and placeholder sections.

6. Create the centralized VRAM map header (vram_map.h) with all address allocations for the entire project.

7. Create the centralized WRAM memory map header (memory_map.h).

8. Verify the ROM compiles and boots in Mesen or snes9x.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/Makefile
```makefile
# ==============================================================================
# VOIDRUNNER - SNES Vertical Shooter RPG
# Master Makefile
# ==============================================================================

ifeq ($(strip $(PVSNESLIB_HOME)),)
$(error "Set PVSNESLIB_HOME. Run: source env setup or set manually")
endif

include ${PVSNESLIB_HOME}/devkitsnes/snes_rules

CFLAGS += -Iinclude

# Fix snes_rules Windows path for bash shell
LIBDIRSOBJSW := $(LIBDIRSOBJS)

.PHONY: all clean rebuild bitmaps audio

export ROMNAME := voidrunner

# Audio files (IT module format) - uncomment when music exists
# AUDIOFILES := assets/music/soundtrack.it
# SOUNDBANK  := soundbank
# SMCONVFLAGS := -s -o $(SOUNDBANK) -v

all: bitmaps $(ROMNAME).sfc

clean: cleanBuildRes cleanRom cleanGfx cleanAudio cleanAssets

cleanAssets:
	@echo clean asset conversions
	@rm -f assets/backgrounds/*.pic assets/backgrounds/*.pal assets/backgrounds/*.map
	@rm -f assets/sprites/*.pic assets/sprites/*.pal assets/sprites/*.map
	@rm -f assets/fonts/*.pic assets/fonts/*.pal assets/fonts/*.map

rebuild: clean all

# ==============================================================================
# Asset Conversion Rules (expanded in Phase 3)
# ==============================================================================

# Placeholder - Phase 3 will add all conversion rules here
bitmaps:
	@echo "No assets to convert yet"
```

### J:/code/snes/snes-rpg-test/hdr.asm
```asm
;==============================================================================
; VOIDRUNNER - SNES ROM Header
; LoROM mapping, 8Mbit (1MB), 8KB SRAM for save data
;==============================================================================

;==LoRom==

.MEMORYMAP
  SLOTSIZE $8000
  DEFAULTSLOT 0
  SLOT 0 $8000                  ; ROM code/data
  SLOT 1 $0 $2000              ; Direct page / stack
  SLOT 2 $2000 $E000           ; Low RAM (WRAM mirror)
  SLOT 3 $0 $10000             ; Full bank for large data
.ENDME

.ROMBANKSIZE $8000
.ROMBANKS 32                    ; 32 banks = 1MB = 8Mbit

;==============================================================================
; SNES Header (LoROM offset $00:FFC0)
;==============================================================================

.SNESHEADER
  ID "SNES"
  NAME "VOIDRUNNER            "  ; 21 bytes padded
  ;    "123456789012345678901"   ; ruler
  SLOWROM
  LOROM
  CARTRIDGETYPE $02             ; ROM + SRAM + Battery
  ROMSIZE $0A                   ; 2^10 = 1024KB = 8Mbit
  SRAMSIZE $03                  ; 2^3 = 8KB SRAM
  COUNTRY $01                   ; USA / NTSC
  LICENSEECODE $00              ; Homebrew
  VERSION $00                   ; v1.0
.ENDSNES

;==============================================================================
; Interrupt Vectors
;==============================================================================

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
  NMI EmptyHandler
  RESET tcc__start
  IRQBRK EmptyHandler
.ENDEMUVECTOR
```

### J:/code/snes/snes-rpg-test/data.asm
```asm
;==============================================================================
; VOIDRUNNER - Data Includes
; Binary asset data embedded into ROM via .incbin
;
; Labels accessed from C:
;   extern char label, label_end;
;   u16 size = &label_end - &label;
;==============================================================================

.include "hdr.asm"

; Asset sections will be added in Phase 3 and later phases.
; Each section uses "superfree" to let the linker place it in any bank.
;
; Naming convention:
;   .section ".rodata_bgN"   - background N tiles/palette/map
;   .section ".rodata_sprN"  - sprite set N tiles/palette
;   .section ".rodata_fontN" - font N tiles/palette
;   .section ".rodata_sndN"  - sound data N
```

### J:/code/snes/snes-rpg-test/include/vram_map.h
```c
/*==============================================================================
 * VRAM_MAP.H - Centralized VRAM Address Allocation
 *
 * SNES VRAM: 64KB total = 32K words (word-addressed)
 * All addresses below are WORD addresses (multiply by 2 for byte offset)
 *
 * Mode 1 Layout:
 *   BG1: 16-color (4bpp), space backgrounds
 *   BG2: 16-color (4bpp), battle UI / HUD
 *   BG3: 4-color  (2bpp), text / dialog overlay
 *   OBJ: 16-color (4bpp), all sprites
 *
 * VRAM MAP (word addresses):
 *   $0000-$1FFF : Sprite tiles (16K words = 32KB)
 *   $2000-$2FFF : BG1 tiles (4K words = 8KB)
 *   $3000-$37FF : BG2 tiles (2K words = 4KB)
 *   $3800-$3BFF : BG3 tiles (1K words = 2KB) - font
 *   $3C00-$3FFF : BG1 tilemap (1K words = 2KB, 32x32)
 *   $4000-$43FF : BG2 tilemap (1K words = 2KB, 32x32)
 *   $4400-$47FF : BG3 tilemap (1K words = 2KB, 32x32)
 *   $4800-$4FFF : BG1 tilemap extended (for 64x32 scrolling)
 *   $5000-$5FFF : Reserved for battle BG tiles
 *   $6000-$6FFF : Reserved for battle UI tiles
 *   $7000-$7FFF : Reserved / overflow
 *============================================================================*/

#ifndef VRAM_MAP_H
#define VRAM_MAP_H

/* --- Sprite (OBJ) VRAM --- */
/* OBJ tiles base: word address $0000 (byte $0000)
 * OBJ size config: OBJ_SIZE16_L32 (small=16x16, large=32x32)
 * Total: 32KB for sprite tiles
 * 16x16 sprite = 128 bytes (4bpp) = 64 words
 * 32x32 sprite = 512 bytes (4bpp) = 256 words
 * Capacity: ~500 unique 16x16 tiles or ~125 unique 32x32 tiles
 */
#define OBJ_TILES_VRAM      0x0000
#define OBJ_TILES_VRAM_END  0x2000
#define OBJ_GFX_SIZE_BYTES  0x4000  /* 16KB per character table */

/* Player ship: 32x32, starts at OBJ tile 0 */
#define OBJ_PLAYER_OFFSET   0x0000
#define OBJ_PLAYER_TILES    16      /* 4x4 tiles for 32x32 */

/* Enemy ships: 16x16 and 32x32, after player */
#define OBJ_ENEMY_OFFSET    0x0100  /* word offset from OBJ base */

/* Bullets: 8x8 and 16x16, packed together */
#define OBJ_BULLET_OFFSET   0x0800

/* UI icons (battle): 16x16 */
#define OBJ_UI_OFFSET       0x0C00

/* --- BG1: Space Background Tiles --- */
/* 4bpp, up to 256 unique tiles (8KB) */
#define BG1_TILES_VRAM      0x2000
#define BG1_TILES_VRAM_END  0x3000

/* --- BG2: Battle UI / HUD Tiles --- */
/* 4bpp, up to 128 unique tiles (4KB) */
#define BG2_TILES_VRAM      0x3000
#define BG2_TILES_VRAM_END  0x3800

/* --- BG3: Text/Dialog Tiles (Font) --- */
/* 2bpp, up to 128 unique tiles (2KB) */
#define BG3_TILES_VRAM      0x3800
#define BG3_TILES_VRAM_END  0x3C00

/* --- Tilemaps --- */
/* BG1 tilemap: 32x32 = 2KB (1K words) */
#define BG1_TILEMAP_VRAM    0x3C00
/* BG1 extended tilemap for 64x32 scrolling (Phase 7) */
#define BG1_TILEMAP_EXT     0x4000

/* BG2 tilemap: 32x32 = 2KB */
#define BG2_TILEMAP_VRAM    0x4400

/* BG3 tilemap: 32x32 = 2KB */
#define BG3_TILEMAP_VRAM    0x4800

/* --- Battle Scene Overrides --- */
/* When entering battle, BG1/BG2 tiles may be replaced */
#define BATTLE_BG_TILES_VRAM    BG1_TILES_VRAM
#define BATTLE_UI_TILES_VRAM    BG2_TILES_VRAM
#define BATTLE_BG_MAP_VRAM      BG1_TILEMAP_VRAM
#define BATTLE_UI_MAP_VRAM      BG2_TILEMAP_VRAM

/* --- VRAM Budget Summary --- */
/* Sprites: $0000-$1FFF = 16K words = 32KB (50% of VRAM) */
/* BG tiles: $2000-$3BFF = 7K words = 14KB (22%) */
/* Tilemaps: $3C00-$4BFF = 4K words = 8KB (12.5%) */
/* Reserved: $4C00-$7FFF = 13K words = 26KB (available) */
/* TOTAL USED: ~54KB / 64KB = 84% */

#endif /* VRAM_MAP_H */
```

### J:/code/snes/snes-rpg-test/include/memory_map.h
```c
/*==============================================================================
 * MEMORY_MAP.H - WRAM Budget and Address Assignments
 *
 * SNES WRAM: 128KB total
 *   $7E:0000-$7E:1FFF = Direct Page + Stack (8KB, managed by CRT0)
 *   $7E:2000-$7E:7FFF = Low RAM (24KB, general purpose)
 *   $7E:8000-$7F:FFFF = High RAM (96KB, large buffers)
 *
 * PVSnesLib uses some Low RAM for its own variables.
 * Our game data starts at $7E:2000 + PVSnesLib overhead.
 *
 * SRAM: 8KB at $70:0000-$70:1FFF (battery-backed save)
 *============================================================================*/

#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

/* --- WRAM Budget (Low RAM, $7E:2000-$7E:7FFF) --- */
/* PVSnesLib uses approximately $7E:2000-$7E:27FF for its internals */
/* Our game variables are allocated by the C compiler in the BSS/DATA segments */
/* Key budget items tracked here for documentation: */

/* Game state machine: ~32 bytes */
/* Player stats: ~64 bytes */
/* Enemy pool (8 enemies): 8 * 48 = 384 bytes */
/* Bullet pool (24 bullets): 24 * 16 = 384 bytes */
/* Battle state: ~256 bytes */
/* Inventory (16 slots): 16 * 8 = 128 bytes */
/* Dialog state: ~128 bytes */
/* Zone data: ~64 bytes */
/* Scroll state: ~32 bytes */
/* HDMA tables: 2 * 673 = 1346 bytes */
/* Subtotal Low RAM game data: ~2.8KB */

/* --- WRAM Budget (High RAM, $7E:8000-$7F:FFFF) --- */
/* Used for large temporary buffers if needed */
/* Battle background decompression buffer: up to 8KB */
/* Map column buffer for streaming: 2KB */

/* --- SRAM Layout ($70:0000) --- */
#define SRAM_MAGIC_OFFSET    0x0000  /* 4 bytes: "VOID" signature */
#define SRAM_MAGIC_VALUE     0x564F  /* "VO" in little-endian */
#define SRAM_MAGIC_VALUE2    0x4944  /* "ID" in little-endian */
#define SRAM_CHECKSUM_OFFSET 0x0004  /* 2 bytes: XOR checksum */
#define SRAM_SAVE_OFFSET     0x0006  /* Save data starts here */

/* Save data structure (see Phase 20 for full definition) */
/* Player level, XP, HP, stats: 32 bytes */
/* Equipment slots: 16 bytes */
/* Inventory: 64 bytes */
/* Story flags: 8 bytes */
/* Current zone/position: 8 bytes */
/* Total save size: ~128 bytes */
#define SRAM_SAVE_SIZE       128

/* Three save slots */
#define SRAM_SLOT_SIZE       (6 + SRAM_SAVE_SIZE)  /* magic + checksum + data */
#define SRAM_SLOT1           0x0000
#define SRAM_SLOT2           (SRAM_SLOT1 + SRAM_SLOT_SIZE)
#define SRAM_SLOT3           (SRAM_SLOT2 + SRAM_SLOT_SIZE)

#endif /* MEMORY_MAP_H */
```

### J:/code/snes/snes-rpg-test/include/types.h
```c
/*==============================================================================
 * TYPES.H - Game-Specific Type Definitions
 *============================================================================*/

#ifndef TYPES_H
#define TYPES_H

#include <snes.h>

/* Fixed-point types for sub-pixel positioning */
/* 8.8 fixed point: integer in high byte, fraction in low byte */
typedef u16 fixed8_8;
typedef s16 sfixed8_8;

/* 12.4 fixed point for velocities */
typedef u16 fixed12_4;
typedef s16 sfixed12_4;

/* Convert integer to 8.8 fixed */
#define TO_FIXED8(x)    ((fixed8_8)((x) << 8))
#define FROM_FIXED8(x)  ((u8)((x) >> 8))
#define FIXED8_FRAC(x)  ((u8)((x) & 0xFF))

/* Convert integer to 12.4 fixed */
#define TO_FIXED4(x)    ((fixed12_4)((x) << 4))
#define FROM_FIXED4(x)  ((u16)((x) >> 4))

/* Direction enum */
#define DIR_NONE    0
#define DIR_UP      1
#define DIR_DOWN    2
#define DIR_LEFT    3
#define DIR_RIGHT   4

/* Entity active/inactive flags */
#define ENTITY_INACTIVE  0
#define ENTITY_ACTIVE    1
#define ENTITY_DYING     2

#endif /* TYPES_H */
```

### J:/code/snes/snes-rpg-test/include/game.h
```c
/*==============================================================================
 * GAME.H - Master Game Include
 *============================================================================*/

#ifndef GAME_H
#define GAME_H

#include <snes.h>
#include "types.h"
#include "vram_map.h"
#include "memory_map.h"

/* Game title */
#define GAME_TITLE "VOIDRUNNER"

/* Screen dimensions */
#define SCREEN_W    256
#define SCREEN_H    224

/* Frame rate */
#define FPS_NTSC    60
#define FPS_PAL     50

#endif /* GAME_H */
```

### J:/code/snes/snes-rpg-test/src/main.c
```c
/*==============================================================================
 * VOIDRUNNER - Main Entry Point
 * Phase 1: Minimal boot to black screen
 *============================================================================*/

#include "game.h"

int main(void)
{
    /* Initialize PPU: force blank ON, clear VRAM/OAM/CGRAM */
    consoleInit();

    /* Set Mode 1 with 8x8 tiles */
    setMode(BG_MODE1, 0);

    /* All BGs disabled, no sprites - just black screen */
    bgSetDisable(0);
    bgSetDisable(1);
    bgSetDisable(2);

    /* Turn on screen (exits force blank) */
    setScreenOn();

    /* Main loop - just wait for VBlank forever */
    while (1) {
        WaitForVBlank();
    }

    return 0;
}
```

## Technical Specifications

### VRAM Layout (Full Project - Locked In Phase 1)
```
Word Addr  Byte Addr  Size     Purpose
---------  ---------  -------  ---------------------------
$0000      $0000      32KB     OBJ (sprite) tile data
$2000      $4000       8KB     BG1 tile data (space bg)
$3000      $6000       4KB     BG2 tile data (UI/HUD)
$3800      $7000       2KB     BG3 tile data (font/text)
$3C00      $7800       2KB     BG1 tilemap (32x32)
$4000      $8000       4KB     BG1 tilemap ext (64x32)
$4400      $8800       2KB     BG2 tilemap (32x32)
$4800      $9000       2KB     BG3 tilemap (32x32)
$4C00      $9800      26KB     Reserved/available
```

### Palette Layout (CGRAM)
```
Index Range   Purpose
-----------   ---------------------------
  0 -  15     BG1 palette 0 (space bg)
 16 -  31     BG1 palette 1 (alternate bg)
 32 -  47     BG2 palette 0 (battle UI)
 48 -  63     BG2 palette 1 (HUD)
 64 -  79     BG3 palette 0 (not used in Mode 1 for BG3)
 80 - 127     Reserved
128 - 143     OBJ palette 0 (player ship)
144 - 159     OBJ palette 1 (enemy type A)
160 - 175     OBJ palette 2 (enemy type B)
176 - 191     OBJ palette 3 (bullets/effects)
192 - 207     OBJ palette 4 (boss)
208 - 223     OBJ palette 5 (UI icons)
224 - 239     OBJ palette 6 (NPC)
240 - 255     OBJ palette 7 (special effects)
```

### OAM Sprite Slot Allocation
```
OAM ID Range   Purpose (x4 for oamSet id parameter)
------------   ---------------------------
  0 -   0      Player ship (32x32, OAM id 0)
  1 -   3      Player ship exhaust/effects
  4 -  11      Enemy ships (up to 8 enemies)
 12 -  35      Bullets (up to 24 active)
 36 -  43      Explosions/particles (up to 8)
 44 -  59      Battle sprites (player, enemy in battle)
 60 -  67      Battle effects (attacks, magic)
 68 -  79      UI cursor, selection indicators
 80 - 127      Reserved / overflow
```

## Asset Requirements
- None for Phase 1 (black screen only).

## Acceptance Criteria
1. Running `make` in J:/code/snes/snes-rpg-test/ produces `voidrunner.sfc` without errors.
2. The ROM file is between 32KB and 1MB in size.
3. Loading `voidrunner.sfc` in Mesen shows a black screen with no crashes or error messages.
4. Loading `voidrunner.sfc` in snes9x shows a black screen with no crashes.
5. The ROM header is correctly read by the emulator (name "VOIDRUNNER", LoROM, 8Mbit).
6. All header files (vram_map.h, memory_map.h, types.h, game.h) exist and contain the documented definitions.

## SNES-Specific Constraints
- LoROM mapping means code must be in banks $00-$3F at $8000-$FFFF (32KB per bank).
- SRAM at $70:0000-$70:1FFF requires CARTRIDGETYPE $02 in header.
- consoleInit() must be the first call - it initializes all hardware to safe defaults.
- setMode() must be called before any BG operations.
- setScreenOn() calls WaitForVBlank() internally before enabling display.

## Estimated Complexity
**Simple** - This is boilerplate setup. The main value is establishing the VRAM/palette/OAM maps that all subsequent phases depend on.
