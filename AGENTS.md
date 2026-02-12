# VEX DEFENDER - Agent Guide

**Version: 0.1.1**

This document provides comprehensive context for AI agents (Claude Code, Cursor, Copilot, etc.) working on the VEX DEFENDER codebase. It describes the project architecture, build system, conventions, constraints, and development workflow so that an agent can make informed, safe modifications.

---

## Project Overview

**VEX DEFENDER** is a Super Nintendo (SNES) game built with [PVSnesLib](https://github.com/alekmaul/pvsneslib). It is a vertical-scrolling shooter with turn-based RPG battles, an inventory/item system, dialog/story events, boss fights, and battery-backed SRAM saves.

- **Target CPU:** WDC 65816 (16-bit, 3.58 MHz)
- **ROM format:** LoROM, SlowROM, 512KB (16 banks x 32KB)
- **SRAM:** 2KB battery-backed at bank $70:0000
- **PPU mode:** Mode 1 (BG1 4bpp, BG2 4bpp, BG3 2bpp)
- **Resolution:** 256x224
- **Language:** C (compiled by 816-tcc, PVSnesLib's C-to-65816 compiler)

---

## Critical Constraints

### No Hardware Multiply or Divide

The 65816 CPU has no multiply or divide instructions. All math MUST use shift-add patterns:

```c
/* GOOD: shift-add for x * 5 */
result = (x << 2) + x;

/* GOOD: shift for x / 4 */
result = x >> 2;

/* BAD: will fail to compile or produce broken code */
result = x * 5;
result = x / 4;
```

The 816-tcc compiler does not support the `*` or `/` operators for runtime values. Constants known at compile time are sometimes accepted, but runtime multiply/divide WILL fail. Always use shifts, repeated addition, or lookup tables.

### Bank 0 Overflow

Bank 0 ($8000-$FFFF, 32KB) is full. New code is automatically placed in Bank 1+ by the WLA-DX linker. This is transparent for most code but affects:

- Far calls across bank boundaries (handled by the compiler)
- Const data placement (constify tool moves ROM constants out of WRAM)

### 816-tcc Compiler Limitations

- No `switch` with more than ~8 cases (use if/else chains)
- No `float` or `double` types
- No standard library beyond what PVSnesLib provides
- Limited struct support (keep structs small and flat)
- `static` functions are placed in the same bank as their caller
- String literals are placed in ROM and must be accessed carefully

### PVSnesLib API Quirks

- `consoleDrawText()` takes `char *` (non-const) for text strings
- `oamSet()` uses OAM entry IDs that must be multiplied by 4 for byte offset
- SRAM access requires `consoleCopySram()`/`consoleLoadSram()` (handles bank switching)
- VBlank is the only safe time to write to VRAM/OAM/CGRAM

---

## Directory Structure

```
J:\code\snes\snes-rpg-test\
├── assets/                    # Game art and sound assets
│   ├── backgrounds/           # 3 zone backgrounds (.png/.pic/.pal/.map)
│   ├── sfx/                   # 9 sound effects (.wav source, .brr SNES format)
│   └── sprites/               # Player, enemies, bullets (.png/.pic/.pal)
├── build/                     # (empty, reserved for future use)
├── docs/                      # Design documents (phases 1-20)
│   └── v1_docs/               # 40 markdown files covering each dev phase
├── include/                   # All header files
│   ├── engine/                # 10 engine module headers
│   ├── game/                  # 10 game module headers
│   ├── assets.h               # Binary asset declarations (extern labels)
│   ├── config.h               # VRAM layout, palette slots, constants
│   └── game.h                 # GameState struct, state IDs, story flags
├── res/                       # PVSnesLib font resources
├── src/                       # All C source files
│   ├── engine/                # 10 engine modules
│   ├── game/                  # 11 game modules
│   └── main.c                 # Entry point, game loop, state dispatch
├── tests/                     # Host-side unit test suite (clang)
│   ├── mock_snes.h            # Mock PVSnesLib types and stub functions
│   ├── test_framework.h       # Minimal assert-based test macros
│   ├── test_main.c            # SCU test runner (includes all sources + tests)
│   ├── test_*.c               # 10 test modules
│   └── run_tests.bat          # Test build script
├── tools/                     # Python asset conversion scripts
├── build.bat                  # One-click build/test/run script
├── data.asm                   # Asset data (binary includes)
├── hdr.asm                    # SNES ROM header (LoROM, SRAM config)
├── linkfile                   # WLA-DX linker object list
├── Makefile                   # Main build file (uses PVSnesLib snes_rules)
└── vex_defender.sfc           # Compiled ROM output
```

---

## Architecture

### Module Organization

The codebase is split into **engine** (hardware-facing, reusable) and **game** (game-specific logic) layers:

**Engine Modules** (`src/engine/`):
| Module | Purpose |
|--------|---------|
| `background.c` | BG1/BG2 rendering, zone background loading, star parallax |
| `bullets.c` | Bullet pool (24 slots), 3 weapon types (single/spread/laser) |
| `collision.c` | AABB detection, combo scoring, screen shake, wave tracking |
| `fade.c` | Brightness fade with ease-in-out LUT |
| `input.c` | Controller input with action mapping and edge detection |
| `scroll.c` | 8.8 fixed-point vertical scrolling, parallax, distance triggers |
| `sound.c` | SPC700 BRR sound effects (9 SFX), music stubs |
| `sprites.c` | Sprite entity pool, OAM slot management, animation |
| `system.c` | PPU Mode 1 initialization, VRAM clearing |
| `vblank.c` | NMI callback framework, frame counter |

**Game Modules** (`src/game/`):
| Module | Purpose |
|--------|---------|
| `battle.c` | Turn-based combat state machine (ATK^2/(ATK+DEF) formula) |
| `battle_ui.c` | Battle screen rendering: HP bars, menus, messages |
| `boss.c` | Boss AI: 3 phases (NORMAL/ENRAGED/DESPERATE), 5 attacks |
| `dialog.c` | Typewriter text system, story dialog scripts |
| `enemies.c` | Enemy pool (8 slots), 4 types, 5 AI patterns, wave spawning |
| `game_state.c` | Title/flight/gameover/victory screens, zone transitions |
| `inventory.c` | 8-slot inventory, 6 items, loot tables, pity timer |
| `player.c` | Player ship movement, banking animation, invincibility |
| `rpg_stats.c` | XP/leveling (10 levels), stat growth tables |
| `save.c` | SRAM save/load with CRC-8 checksum validation |
| `story.c` | Scroll-triggered story cutscenes |

### Key Global State

| Variable         | Type         | Defined In     | Purpose                                      |
| ---------------- | ------------ | -------------- | -------------------------------------------- |
| `g_game`         | `GameState`  | `game_state.c` | Master game state (zone, progress, flags)    |
| `rpg_stats`      | `RPGStats`   | `rpg_stats.c`  | Player stats (HP/ATK/DEF/SPD/SP/XP/level)    |
| `g_player`       | `PlayerShip` | `player.c`     | Player ship entity (position, sprite, state) |
| `g_inventory`    | `InvSlot[8]` | `inventory.c`  | Inventory slots (item ID + quantity)         |
| `g_score`        | `u16`        | `collision.c`  | Flight score                                 |
| `g_frame_count`  | `u16`        | `vblank.c`     | VBlank frame counter (PRNG seed)             |
| `g_weapon_kills` | `u16[3]`     | `bullets.c`    | Per-weapon kill counts for mastery           |

### State Machine

The main loop in `main.c` dispatches based on `g_game.current_state`:

```
BOOT -> TITLE -> FLIGHT <-> BATTLE
                   |           |
                   v           v
              DIALOG      GAMEOVER
                   |
                   v
              VICTORY (after Zone 3 boss)
```

- **FLIGHT**: Scrolling shooter gameplay. Enemies spawn via scroll-distance triggers.
- **BATTLE**: Turn-based RPG overlay. Triggered on player-enemy contact.
- **DIALOG**: Story text overlay on BG1. Triggered at scroll distances.

### Fixed-Point Math

Sub-pixel movement uses 8.8 fixed-point format:

```c
#define FP8(x)      ((u16)((x) * 256))   /* Float literal -> 8.8 */
#define FP8_INT(x)  ((x) >> 8)            /* Extract integer part */
```

Velocities are stored as 8.8 values. `scroll_y_fp` tracks sub-pixel scroll position.

### Collision System

Three checks per frame (worst case ~144 AABB tests):

1. Player bullets vs enemies (16 x 8 = 128 checks)
2. Enemy bullets vs player (8 checks)
3. Player body vs enemies (8 checks)

Hitboxes are smaller than sprites for fair gameplay:

- Player: 32x32 sprite, 16x16 hitbox (cockpit only)
- Enemy: 32x32 sprite, 24x24 hitbox
- Bullet: 16x16 sprite, 8x8 hitbox

### Save System

SRAM at $70:0000, validated with:

- 4-byte magic signature (`0x5645 0x5844` = "VEXD")
- 1-byte version (currently `4`)
- CRC-8/MAXIM checksum (polynomial 0x31)

SaveData struct packs: RPG stats, inventory, game progress, weapon kills, high score, max combo, zone ranks.

---

## Build System

### SNES ROM Build

```batch
build.bat              # Incremental build
build.bat clean        # Clean rebuild
build.bat run          # Build + launch in bsnes
build.bat all          # Clean + build + test + run
```

Or manually:

```batch
set PVSNESLIB_HOME=J:/code/snes/snes-build-tools/tools/pvsneslib
set PATH=J:/code/snes/snes-build-tools/tools/pvsneslib/devkitsnes/bin;J:/code/snes/snes-build-tools/tools/pvsneslib/tools/wla;%PATH%
make
```

**Toolchain pipeline:**

1. `816-tcc` compiles C to 65816 assembly (`.ps` files)
2. `816-opt` optimizes the assembly (multiple passes)
3. `constify` moves ROM constants from WRAM to ROM sections
4. `wla-65816` assembles to object files (`.obj`)
5. `wlalink` links all objects using `linkfile` into `vex_defender.sfc`

**Asset pipeline:**

1. Python scripts convert high-res PNGs to SNES-sized indexed PNGs
2. `gfx4snes` converts indexed PNGs to `.pic`/`.pal`/`.map` binary data
3. `data.asm` includes binary assets with `.INCBIN` directives

### Unit Test Build

```batch
build.bat test         # Build and run tests
```

Or manually:

```batch
cd tests
clang -I. -I../include -Wall -Wno-unused-function -Wno-unused-variable test_main.c -o run_tests.exe
run_tests.exe
```

Tests use a **Single Compilation Unit (SCU)** pattern: `test_main.c` `#include`s all source files and test files into one translation unit. This allows tests to access `static` functions. `mock_snes.h` provides stub implementations of PVSnesLib hardware functions.

### Running in Emulator

```batch
build.bat run
```

The bsnes v115 emulator is at `J:\code\snes\snes-build-tools\tmp\bsnes_v115-windows\bsnes.exe`. The ROM file is `vex_defender.sfc` in the project root.

---

## Coding Conventions

### Style

- C89-compatible code (no C99 features beyond fixed-width types)
- 4-space indentation
- `camelCase` for functions: `rpgAddXP()`, `enemySpawn()`, `collisionCheckAll()`
- `snake_case` for struct fields and local variables: `max_hp`, `combo_count`
- `g_` prefix for globals: `g_game`, `g_player`, `g_score`
- `UPPER_CASE` for constants and macros: `ENEMY_TYPE_SCOUT`, `FP8()`
- Module prefix for public functions: `rpg*`, `inv*`, `enemy*`, `bullet*`, `scroll*`

### Entity System

- Pool-based allocation with fixed-size arrays
- `active` field: `ENTITY_ACTIVE` (1) or `ENTITY_INACTIVE` (0)
- Spawn returns pointer to allocated slot, or `NULL` if pool full
- Despawn sets `active = ENTITY_INACTIVE`

### Comments

- File headers use `/*===...===*/` block style with phase number
- Function comments document parameters, return values, and 65816 considerations
- `#123:` tags reference improvement numbers for traceability

### Test Pattern

- Each module has a corresponding `test_<module>.c` file
- Test functions are `static void test_<module>_<feature>(void)`
- Tests registered in `run_<module>_tests()` function
- Assertion macros: `TEST_ASSERT_EQ`, `TEST_ASSERT_GT`, `TEST_ASSERT_LE`, `TEST_ASSERT_NOT_NULL`, `TEST_ASSERT_STR`
- Tests verify logic only (hardware calls are stubbed)

---

## Development Workflow

### Adding a New Feature

1. **Header first:** Add struct fields, defines, and function declarations to the appropriate header in `include/`
2. **Implement:** Write the C code in the corresponding `src/` file. Use shift-add math only.
3. **Test:** Add test functions to the corresponding `tests/test_*.c` file. Register them in the runner. Update the assertion count in `test_main.c`.
4. **Verify:** Run `build.bat test` (clang tests) then `build.bat` (SNES ROM build)
5. **If adding new source files:** Add the `.obj` path to `linkfile`

### Adding a New Enemy Wave

Enemy waves are spawned via scroll-distance triggers in `enemies.c`:

```c
static void my_wave_callback(void) {
    enemySpawnFromLeft(ENEMY_TYPE_FIGHTER, -20);
    enemySpawnFromRight(ENEMY_TYPE_FIGHTER, -40);
}
/* In enemySetupZoneTriggers(), for the target zone: */
scrollAddTrigger(desired_distance, my_wave_callback);
```

### Adding a Battle Feature

The battle system is a state machine with states: `BSTATE_INIT`, `BSTATE_PLAYER_TURN`, `BSTATE_PLAYER_ACT`, `BSTATE_ENEMY_TURN`, `BSTATE_ENEMY_ACT`, `BSTATE_RESOLVE`, `BSTATE_VICTORY`, `BSTATE_DEFEAT`, `BSTATE_EXIT`. Add logic to the appropriate state case in `battleUpdate()`.

### Modifying Save Data

1. Add new fields to `SaveData` in `save.h`
2. Bump `SAVE_VERSION` (invalidates old saves, which is intentional)
3. Pack the new field in `saveGame()`
4. Unpack and validate in `loadGame()`
5. Add round-trip test in `test_save.c`

---

## Common Pitfalls

| Pitfall                                      | Solution                                                                         |
| -------------------------------------------- | -------------------------------------------------------------------------------- |
| Using `*` or `/` operators                   | Use `<<` / `>>` shifts and repeated addition                                     |
| Forgetting `#include` for SNES build         | Test build uses SCU (all includes visible); SNES build compiles files separately |
| Calling `static` functions from another file | Only possible in test SCU build; use non-static for cross-module calls           |
| Exceeding 8 enemy pool slots                 | `enemySpawn()` returns NULL; always check return                                 |
| Save format change without version bump      | Old saves pass magic check but have wrong data layout                            |
| Adding `switch` with many cases              | 816-tcc chokes on large switches; use if/else chains                             |
| Writing to VRAM outside VBlank               | Use VBlank callbacks or `WaitForVBlank()` before VRAM writes                     |
| Text rendering with const strings            | `consoleDrawText()` requires `char *`, not `const char *`                        |

---

## File Quick Reference

### Headers (what to include)

- Need SNES types (`u8`, `u16`, etc.)? -> `#include <snes.h>` (or `#include "game.h"` which includes it)
- Need screen size, VRAM addresses? -> `#include "config.h"`
- Need GameState, story flags? -> `#include "game.h"`
- Need RPG stats? -> `#include "game/rpg_stats.h"`
- Need enemy types? -> `#include "game/enemies.h"`
- Need bullet system? -> `#include "engine/bullets.h"`
- Need collision/score? -> `#include "engine/collision.h"`
- Need frame counter? -> `#include "engine/vblank.h"`

### Key Constants

- `MAX_ENEMIES` = 8 (enemy pool size)
- `MAX_BULLETS` = 24 (bullet pool, split: 16 player + 8 enemy)
- `INV_SIZE` = 8 (inventory slots)
- `INV_MAX_STACK` = 9 (max quantity per slot)
- `RPG_MAX_LEVEL` = 10
- `ZONE_COUNT` = 3
- `SAVE_VERSION` = 4
- `ITEM_COUNT` = 7 (including ITEM_NONE)

---

## Improvement Tracking

The project has 199 improvements implemented across 20 development phases. Improvements are tagged with `#NNN` comments in the source code (e.g., `/* #150: Pack weapon mastery kill counts */`). When making changes, add a new improvement number and tag your changes for traceability.

Current improvement ranges:

- Phase 1-20 (base game): #1-#20
- Batch 1 improvements: #101-#120
- Batch 2 improvements: #121-#179
- Batch 3 improvements: #180-#199
