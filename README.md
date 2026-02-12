# VEX DEFENDER

**Version: 0.1.1**

A vertical-scrolling shoot-em-up with turn-based RPG battles for the Super Nintendo, built from scratch with [PVSnesLib](https://github.com/alekmaul/pvsneslib).

## About

VEX DEFENDER is a homebrew SNES game that blends two classic genres: the fast-paced action of a vertical shmup with the strategic depth of turn-based RPG combat. Fly your ship through three increasingly dangerous zones, dodge enemy fire, build combos for score, and when you collide with an enemy you enter a turn-based battle using RPG mechanics.

### Features

- **3 zones** with unique backgrounds and enemy compositions (Debris Field, Asteroid Belt, Flagship Assault)
- **4 enemy types** with distinct AI patterns: Scout (patrol), Fighter (chase), Heavy (shielded), Elite (dodge-capable)
- **3 boss fights** with multi-phase AI (Normal / Enraged / Desperate) and 5 attack patterns
- **Turn-based battle system** with ATK^2/(ATK+DEF) damage formula, items, defend, flee, and special attacks
- **RPG progression**: 10 levels, 5 stats (HP/ATK/DEF/SPD/SP), XP from battles
- **6 consumable items**: HP potions, SP charge, ATK/DEF boost, full restore
- **8-slot inventory** with loot drops from enemies and a pity timer
- **3 weapon types**: Single shot, Spread shot, Laser beam (cycle with shoulder buttons)
- **Combo scoring system** with multipliers (1x-4x), milestones, and wave clear bonuses
- **Story dialog** with typewriter text triggered at scroll checkpoints
- **Battery-backed SRAM saves** with CRC-8 checksum validation
- **Per-zone rank system** (D/C/B/A/S) based on score, no-damage bonus, and combo performance
- **199 gameplay improvements** across balance, feedback, anti-frustration, and strategic depth

## Screenshots

_(Run the game in bsnes to see: title screen, flight gameplay, battle system, boss fights, victory screen)_

## Building

### Prerequisites

| Tool       | Location                                               | Purpose                            |
| ---------- | ------------------------------------------------------ | ---------------------------------- |
| PVSnesLib  | `J:\code\snes\snes-build-tools\tools\pvsneslib`        | SNES C compiler, assembler, linker |
| GNU Make   | Included in PVSnesLib devkitsnes                       | Build automation                   |
| Python 3   | System PATH                                            | Asset conversion scripts           |
| LLVM/Clang | `C:\Program Files\LLVM\bin\`                           | Unit test compilation (optional)   |
| bsnes v115 | `J:\code\snes\snes-build-tools\tmp\bsnes_v115-windows` | SNES emulator (optional)           |

### Quick Build

Use the included build script:

```batch
build.bat              # Build the SNES ROM (incremental)
build.bat clean        # Clean all artifacts, then rebuild
build.bat test         # Build and run the unit test suite
build.bat run          # Build the ROM and launch in bsnes
build.bat all          # Clean + build + test + launch (full pipeline)
```

### Manual Build

```batch
REM Set environment
set PVSNESLIB_HOME=J:/code/snes/snes-build-tools/tools/pvsneslib
set PATH=J:/code/snes/snes-build-tools/tools/pvsneslib/devkitsnes/bin;J:/code/snes/snes-build-tools/tools/pvsneslib/tools/wla;%PATH%

REM Build ROM
make

REM Output: vex_defender.sfc
```

### Running

Load `vex_defender.sfc` in any SNES emulator. Recommended: bsnes v115 for cycle-accurate emulation.

```batch
REM Launch with bsnes
build.bat run

REM Or directly:
"J:\code\snes\snes-build-tools\tmp\bsnes_v115-windows\bsnes.exe" vex_defender.sfc
```

### Controls

| Button | Flight Mode   | Battle Mode       |
| ------ | ------------- | ----------------- |
| D-Pad  | Move ship     | Navigate menus    |
| B      | Fire weapon   | Confirm selection |
| A      | Fire weapon   | Confirm selection |
| Y      | (unused)      | Cancel / Back     |
| L / R  | Cycle weapons | (unused)          |
| Start  | Pause         | (unused)          |
| Select | (unused)      | (unused)          |

## Project Structure

```
snes-rpg-test/
├── assets/                 # Art and sound assets
│   ├── backgrounds/        # 3 zone backgrounds (PNG -> PIC/PAL/MAP)
│   ├── sfx/                # 9 sound effects (WAV -> BRR)
│   └── sprites/            # Player, enemies, bullets (PNG -> PIC/PAL)
├── include/                # Header files
│   ├── engine/             # Engine module headers (10)
│   ├── game/               # Game module headers (10)
│   ├── assets.h            # Binary asset extern declarations
│   ├── config.h            # VRAM layout, constants, macros
│   └── game.h              # GameState struct, state machine IDs
├── src/                    # C source code
│   ├── engine/             # Hardware-facing engine (10 modules)
│   │   ├── background.c    # BG rendering, star parallax
│   │   ├── bullets.c       # Bullet pool, 3 weapon types
│   │   ├── collision.c     # AABB detection, combo scoring
│   │   ├── fade.c          # Screen brightness transitions
│   │   ├── input.c         # Controller input mapping
│   │   ├── scroll.c        # Vertical scrolling, triggers
│   │   ├── sound.c         # SPC700 sound effects
│   │   ├── sprites.c       # Sprite pool, OAM management
│   │   ├── system.c        # PPU initialization
│   │   └── vblank.c        # VBlank callbacks, frame counter
│   ├── game/               # Game-specific logic (11 modules)
│   │   ├── battle.c        # Turn-based combat engine
│   │   ├── battle_ui.c     # Battle screen UI rendering
│   │   ├── boss.c          # Boss AI (3 phases, 5 attacks)
│   │   ├── dialog.c        # Typewriter text system
│   │   ├── enemies.c       # Enemy pool, AI, wave spawning
│   │   ├── game_state.c    # Title/flight/gameover/victory
│   │   ├── inventory.c     # Items, loot tables
│   │   ├── player.c        # Player ship entity
│   │   ├── rpg_stats.c     # XP, leveling, stat growth
│   │   ├── save.c          # SRAM save/load
│   │   └── story.c         # Story dialog scripts
│   └── main.c              # Entry point, game loop
├── tests/                  # Unit test suite (990 assertions)
│   ├── test_main.c         # SCU test runner
│   ├── test_*.c            # 10 test modules
│   ├── mock_snes.h         # PVSnesLib stubs for host compilation
│   └── test_framework.h    # Minimal assert macros
├── tools/                  # Asset conversion scripts (Python)
├── docs/                   # Design documentation (phases 1-20)
├── build.bat               # Build/test/run script
├── Makefile                # SNES ROM build rules
├── hdr.asm                 # ROM header (LoROM, SRAM)
├── linkfile                # Linker object list
├── data.asm                # Binary asset includes
├── AGENTS.md               # AI agent development guide
└── vex_defender.sfc        # Compiled ROM output
```

## Technical Details

### Architecture

The codebase is organized into two layers:

- **Engine layer** (`src/engine/`): Hardware-facing modules that abstract SNES PPU, DMA, input, and audio. Potentially reusable across projects.
- **Game layer** (`src/game/`): Game-specific logic including combat, enemies, inventory, and story.

All modules communicate through well-defined header APIs. Global state is minimized to a few key structs (`g_game`, `rpg_stats`, `g_player`, `g_inventory`).

### SNES Hardware Utilization

| Resource    | Usage                                           |
| ----------- | ----------------------------------------------- |
| BG1 (4bpp)  | Zone backgrounds + text overlay                 |
| BG2 (4bpp)  | Procedural star parallax layer                  |
| BG3 (2bpp)  | (Reserved)                                      |
| OAM sprites | Player (4), bullets (32), enemies (20), UI (16) |
| VRAM        | 64KB: BG tiles, sprite tiles, tilemaps, font    |
| CGRAM       | 256 colors: 8 BG palettes + 8 sprite palettes   |
| SRAM        | 2KB battery-backed: save data with CRC-8        |
| SPC700      | 9 BRR sound effects via PVSnesLib snesmod       |

### Math Constraints

The 65816 has no hardware multiply or divide. All runtime math uses:

- Bit shifts (`<<`, `>>`) for powers of 2
- Shift-add patterns for arbitrary multiplications (e.g., `x*5 = (x<<2)+x`)
- Repeated subtraction for division
- 8.8 fixed-point for sub-pixel positions and velocities
- Lookup tables for complex functions

### Build Pipeline

```
 C source (.c)
     |
     v
 816-tcc (C -> 65816 asm)
     |
     v
 816-opt (peephole optimization, multiple passes)
     |
     v
 constify (move ROM constants out of WRAM)
     |
     v
 wla-65816 (assemble to .obj)
     |
     v
 wlalink (link all .obj -> .sfc ROM)
```

Asset pipeline runs in parallel:

```
 Source PNG -> Python resize/quantize -> gfx4snes -> .pic/.pal/.map -> data.asm INCBIN
 Source WAV -> sox/brr_encoder -> .brr -> data.asm INCBIN
```

## Testing

The project includes a comprehensive host-side test suite with **990 assertions** across **10 modules**. Tests compile with Clang on Windows and run natively (no emulator needed).

```batch
build.bat test
```

Test architecture uses Single Compilation Unit (SCU): all source files and test files are `#include`d into `test_main.c`. This allows tests to access `static` functions. `mock_snes.h` provides stub implementations of all PVSnesLib hardware functions.

**Test modules:**
| Module | Tests | Assertions |
|--------|-------|------------|
| Configuration | Constants, VRAM layout, math macros | 54 |
| Game State | Initialization, zone transitions | 24 |
| Collision | AABB math, combo system, scoring | 122 |
| RPG Stats | XP, leveling, growth, defeat penalty | 89 |
| Inventory | Add/remove, stacking, loot drops | 85 |
| Scroll | Distance triggers, speed transitions | 31 |
| Sprites | Pool allocation, slot management | 53 |
| Bullets | Firing, weapon cycling, pool management | 95 |
| Enemies | Spawning, AI patterns, damage, waves | 161 |
| Save/Load | Round-trip, checksum, corruption, validation | 57 |

## Game Design

### Zone Progression

| Zone | Name             | Background          | Enemies                   | Boss        |
| ---- | ---------------- | ------------------- | ------------------------- | ----------- |
| 1    | Debris Field     | Space debris        | Scout, Fighter            | Zone 1 Boss |
| 2    | Asteroid Belt    | Asteroids + hazards | Fighter, Heavy, hazards   | Zone 2 Boss |
| 3    | Flagship Assault | Enemy flagship      | Heavy, Elite, elite swarm | Final Boss  |

Each zone is a vertical-scrolling flight section with waves of enemies spawned at scroll-distance checkpoints. The zone ends with a boss battle. Defeating the Zone 3 boss triggers the victory screen.

### Battle System

When the player collides with an enemy during flight, the game transitions to a turn-based battle:

1. **Player turn**: Attack, Defend, Item, Special, or Flee
2. **Enemy turn**: AI selects action based on type and HP
3. **Resolve**: Apply damage using `ATK^2 / (ATK + DEF)` formula
4. **Victory/Defeat**: Award XP and loot, or apply defeat penalty

Boss battles have three AI phases:

- **Normal**: Standard attack patterns
- **Enraged** (HP < 50%): Increased aggression, special attacks
- **Desperate** (HP < 25%): Most dangerous phase, revenge strikes

### Scoring

- Base score per enemy kill (scaled by type)
- Combo multiplier (1x-4x) for consecutive kills within 60 frames
- Milestone bonuses at combo 5/10/15
- Wave clear bonus (500 points for clearing all enemies in a wave)
- Overkill bonus for excess damage
- Speed kill bonus for fast kills
- Per-zone rank (D/C/B/A/S) based on score + no-damage + combo performance

## License

Homebrew project. Game code is original. PVSnesLib is used under its own license.

## Credits

Built with:

- [PVSnesLib](https://github.com/alekmaul/pvsneslib) - SNES C development library
- [WLA-DX](https://github.com/vhelin/wla-dx) - 65816 assembler and linker
- [bsnes](https://github.com/bsnes-emu/bsnes) - Cycle-accurate SNES emulator
