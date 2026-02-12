# VEX DEFENDER - Release History

## v0.2.4 - 2026-02-11

### Repository Cleanup

Cleaned development artifacts from the repository to reduce clutter and repo size.

#### Removed

- **32 development screenshots** from repo root (bsnes captures, test screenshots, flight/title screenshots)
- **12 PowerShell debug scripts**: 7 capture/screenshot automation scripts (`capture*.ps1`) and 5 input/debug scripts (`findwindow.ps1`, `send_input.ps1`, `send_keys*.ps1`, `send_scan.ps1`)
- **3 backup files**: `src/main.c.bak`, `src/main.c.test_bak`, `linkfile_minimal`
- **4 untracked build artifacts** from disk: `data.obj`, `hdr.obj`, `data.asm.test_bak`, `hdr.asm.test_bak`, `nul`

#### Added

- `.gitignore` to prevent build artifacts (`.obj`, `.sfc`, `.sym`, `.srm`) from being tracked

---

## v0.1.1 - 2026-02-11

### Initial Release

This is the first versioned release of VEX DEFENDER, a vertical-scrolling shoot-em-up with turn-based RPG battles for the Super Nintendo.

#### Features

- **Flight engine**: Vertical scrolling with 8.8 fixed-point sub-pixel precision, procedural star parallax on BG2
- **3 zones**: Debris Field, Asteroid Belt, and Flagship Assault with unique backgrounds
- **4 enemy types**: Scout (patrol), Fighter (chase), Heavy (shielded), Elite (dodge-capable) with 5 AI patterns
- **3 boss fights**: Multi-phase AI (Normal/Enraged/Desperate) with 5 attack patterns
- **Turn-based battle system**: ATK^2/(ATK+DEF) damage formula, defend, flee, items, and special attacks
- **RPG progression**: 10 levels, 5 stats (HP/ATK/DEF/SPD/SP), XP from battles
- **6 consumable items**: HP Potion, SP Charge, ATK Boost, DEF Boost, Full Restore, Smoke Bomb
- **8-slot inventory** with loot drops, pity timer, and stacking (max 9)
- **3 weapon types**: Single shot, Spread shot, Laser beam (cycle with L/R)
- **Combo scoring**: 1x-4x multiplier, milestone bonuses at 5/10/15 kills, wave clear bonus
- **Story dialog system**: Typewriter text triggered at scroll checkpoints
- **Battery-backed SRAM saves** with CRC-8 checksum and magic signature validation
- **Per-zone rank system**: D/C/B/A/S based on score, no-damage bonus, and combo performance
- **199 gameplay improvements** across balance, feedback, anti-frustration, and strategic depth

#### Infrastructure

- PVSnesLib C-to-65816 toolchain (816-tcc, 816-opt, constify, wla-65816, wlalink)
- Python asset pipeline (PNG resize/quantize, gfx4snes, brr_encoder)
- Host-side unit test suite: 990 assertions across 10 modules (Clang, SCU pattern)
- Build automation via `build.bat` (build/clean/test/run/all modes)
- 20-phase design documentation in `docs/v1_docs/`
