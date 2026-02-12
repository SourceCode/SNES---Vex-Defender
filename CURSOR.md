# VEX DEFENDER - Progress Tracker

## Project Overview
SNES vertical shooter / turn-based RPG hybrid built with PVSnesLib (LoROM, 256KB).
20-phase development plan documented in `docs/v1_docs/`.

## Build Instructions
```bash
# Set environment (Windows)
env.bat
# Or manually:
set PVSNESLIB_HOME=J:/code/snes/snes-build-tools/tools/pvsneslib

# Build
make clean && make

# Output: vex_defender.sfc (262KB ROM)
```

---

## Phase Status

| Phase | Name | Status | Notes |
|-------|------|--------|-------|
| 1 | Project Scaffolding | COMPLETE | Makefile, ROM header, directory structure |
| 2 | Hardware Init | COMPLETE | Mode 1 PPU, OAM sizing (16x16/32x32), VRAM layout |
| 3 | Asset Pipeline | COMPLETE | Python preprocessing + gfx4snes conversion |
| 4 | Background Rendering | COMPLETE | BG1 zone backgrounds, BG2 star parallax with twinkle |
| 5 | Sprite Engine & Player Ship | COMPLETE | Sprite pool, player entity, OAM management |
| 6 | Input System | COMPLETE | Action-mapped input, D-pad movement, banking |
| 7 | Vertical Scrolling | COMPLETE | 8.8 FP scrolling, parallax, speed transitions, triggers |
| 8 | Bullet System | COMPLETE | Bullet pool, 3 weapon types, auto-fire, weapon cycling |
| 9 | Enemy System | COMPLETE | 4 enemy types, 5 AI patterns, scroll-triggered waves |
| 10 | Collision Detection | COMPLETE | AABB hitboxes, bullet/enemy/contact checks, score |
| **11** | **Battle System Core** | **COMPLETE** | **Turn-based combat, BG3 text UI, state machine, auto Bank 1 overflow** |
| **12** | **Battle UI** | **COMPLETE** | **HP bars, battle sprites, shake animation, UI module refactor** |
| **13** | **RPG Stats** | **COMPLETE** | **XP/leveling, persistent stats, ATK^2/(ATK+DEF) damage, level-up UI** |
| **14** | **Items & Inventory** | **COMPLETE** | **6 consumable items, battle item sub-menu, loot drops on victory** |
| **15** | **Game State Machine** | **COMPLETE** | **Title screen, game over, pause, state dispatch in main loop** |
| **16** | **Dialog System** | **COMPLETE** | **Scroll-triggered story dialog, typewriter text, BG3 text overlay** |
| **17** | **Sound Engine** | **COMPLETE** | **9 BRR SFX from source WAVs, SPC700 sound manager, music stubs for future IT modules** |
| **18** | **Zone Progression** | **COMPLETE** | **3-zone campaign, unique BGs/enemy sprites per zone, zone transitions, victory screen** |
| **19** | **Boss Battles** | **COMPLETE** | **3 zone bosses, multi-phase AI, 5 boss attacks, zone-end boss triggers** |
| **20** | **Polish & Final** | **COMPLETE** | **SRAM save system, enhanced title/game over/victory, play time tracking** |

---

## Phase 20 Implementation Details

### Files Created
- `include/game/save.h` - Save system header: SaveData struct (magic, checksum, RPG stats, inventory, game progress), API (saveInit/saveGame/loadGame/saveExists/saveErase)
- `src/game/save.c` - SRAM save/load implementation using PVSnesLib consoleCopySram/consoleLoadSram, XOR checksum validation

### Files Modified
- `src/game/game_state.c` - Major overhaul: enhanced title screen (NEW GAME/CONTINUE menu with cursor, save detection), enhanced game over (RETRY ZONE/TITLE menu), enhanced victory (mission stats: level, kills, play time), auto-save on zone entry, gsNumToStr helper
- `src/main.c` - Updated header to Phase 20, removed debug SELECT battle trigger, added play time tracking (60-frame counter → play_time_seconds)
- `src/engine/background.c` - Replaced ASSET_SIZE(star_tiles) and ASSET_SIZE(star_pal) with hardcoded sizes (128/32 bytes) to fix WLA-DX linker bank boundary overflow
- `linkfile` - Added `src/game/save.obj`

### Save System Architecture
```
SRAM Layout ($70:0000):
  SaveData struct (~50 bytes):
    magic1: 0x5645 ("VE")
    magic2: 0x5844 ("XD")
    checksum: XOR over all bytes after 6-byte header
    RPG stats: level, xp, max_hp, hp, atk, def, spd, max_sp, sp, credits, total_kills
    Inventory: 8 item IDs + 8 quantities (flattened from InvSlot[8])
    Progress: current_zone, zones_cleared, story_flags, play_time_seconds

API:
  saveInit()    - Called once at boot, no-op (reserves static buffer)
  saveGame()    - Pack rpg_stats + g_inventory + g_game → SRAM
  loadGame()    - Validate magic + checksum, unpack SRAM → game state
  saveExists()  - Read SRAM, check magic + checksum without restoring
  saveErase()   - Write zeroed buffer to SRAM (on victory/game complete)

Auto-save: saveGame() called at start of gsFlightEnter() (every zone entry)
Save erased on victory (game complete, fresh start next time)

PVSnesLib SRAM API:
  consoleCopySram(u8 *source, u16 size) - Write to SRAM
  consoleLoadSram(u8 *dest, u16 size)   - Read from SRAM
  hdr.asm: CARTRIDGETYPE $02 (ROM+SRAM), SRAMSIZE $01 (16 kilobits)
```

### Enhanced Title Screen
```
Layout (BG3 text, Mode 1):
  Row 5:  "VEX DEFENDER"
  Row 7:  "VERTICAL ASSAULT RPG"
  Row 10: "> NEW GAME"         (cursor menu)
  Row 11: "  CONTINUE"         (grayed out if no save)
  Row 14: "(C) 2026 HOMEBREW"

Navigation:
  D-pad Up/Down: move cursor (CONTINUE only selectable if saveExists())
  A / Start: confirm selection
    NEW GAME:  rpgStatsInit + invInit + reset state → gsFlightEnter
    CONTINUE:  loadGame() → gsFlightEnter at saved zone
```

### Enhanced Game Over Screen
```
Layout:
  Row 8:  "GAME OVER"
  Row 11: "> RETRY ZONE"      (cursor menu)
  Row 12: "  TITLE"

Navigation:
  D-pad Up/Down: move cursor
  A / Start: confirm
    RETRY ZONE: hp=max_hp, sp=max_sp → gsFlightEnter (same zone, auto-saves)
    TITLE:      rpgStatsInit + invInit → gsTitleEnter (full reset)
```

### Enhanced Victory Screen
```
Layout:
  Row 5:  "VICTORY!"
  Row 7:  "THE ARK IS SAVED!"
  Row 9:  "= MISSION STATS ="
  Row 11: "LEVEL: N"
  Row 12: "KILLS: NNN"
  Row 13: "TIME:  MM:SS"
  Row 18: "PRESS START"        (blinking)

On enter: saveErase() (clear save for fresh start)
Number display: gsNumToStr() uses subtraction-based conversion (no division on 65816)
Play time: play_time_seconds / 60 for minutes, % 60 for seconds (via subtraction loops)
```

### Play Time Tracking
```
In main loop, after state machine dispatch:
  If state is FLIGHT, BATTLE, or DIALOG:
    g_game.frame_counter++
    if frame_counter >= 60:
      frame_counter = 0
      play_time_seconds++ (capped at 0xFFFF = 18.2 hours)

Tracked per-session, saved to SRAM on zone entry.
Displayed on victory screen as MM:SS format.
```

### Linker Fix: ASSET_SIZE Bank Boundary Overflow
```
Problem:
  ASSET_SIZE(star_tiles) = &star_tiles_end - &star_tiles
  With WLA-DX assembler -d flag, label arithmetic is deferred to linker.
  If .rodata_bg2_stars section placed at end of a ROM bank, star_tiles_end
  label could resolve to $10000 (65536), overflowing 16-bit range.
  Error: "COMPUTE_PENDING_CALCULATIONS: Result (65536/$10000) out of 16-bit range"

Fix:
  Replaced ASSET_SIZE(star_tiles) with hardcoded 128 (4 tiles × 32 bytes)
  Replaced ASSET_SIZE(star_pal) with hardcoded 32 (16 colors × 2 bytes)
  Star tile/palette data is static and won't change, so hardcoded sizes are safe.
  Zone background ASSET_SIZE calls unaffected (large sections placed at bank start).
```

### Pragmatic Scope Decisions
```
IMPLEMENTED (high-value, practical):
  ✓ SRAM save system with checksum validation
  ✓ Enhanced title screen (NEW GAME / CONTINUE menu)
  ✓ Enhanced game over (RETRY ZONE / TITLE menu)
  ✓ Enhanced victory (mission stats display)
  ✓ Auto-save on zone entry
  ✓ Play time tracking
  ✓ Removed debug battle trigger

SKIPPED (impractical or high-risk):
  ✗ Flight HUD - BG3 font at VRAM 0x3000 conflicts with BG1 tile space
  ✗ HDMA gradient - Complex hardware programming, diminishing returns
  ✗ Particle explosions - OAM allocation conflict, no tile space
  ✗ Screen shake in flight - Already have battle shake, complex in flight
  ✗ Credits sequence - No additional content to credit
```

### ROM Stats (Post Phase 20)
```
Bank 0:     8 bytes free (0.02%)
Bank 1:     2 bytes free (0.01%) - FULL (code overflow)
Bank 2:     1 byte free (0.00%) - FULL (assets)
Bank 3:     1 byte free (0.00%) - FULL (assets)
Bank 4:     0 bytes free (0.00%) - FULL
Bank 5: 3,990 bytes free (12.18%) - save.c + game_state.c growth
Bank 6: 32,768 bytes free (100%)
Bank 7: 32,768 bytes free (100%)
Total ROM: 69,538 bytes (26.53%) free of 262,144
Phase 20 code cost: ~3,685 bytes (save system + enhanced menus + play time)
```

---

## Phase 19 Implementation Details

### Files Created
- `include/game/boss.h` - Boss system header: BossTypeDef/BossState structs, BOSS_ZONE* type IDs, BOSS_TRIGGER_BASE (0x80+) trigger range, BOSS_AI_* phase defines, BOSS_ACT_* attack IDs, IS_BOSS_TRIGGER/BOSS_TYPE_FROM_TRIGGER macros, bossInit/bossSetup/bossUpdatePhase/bossChooseAction/bossResolveAction API
- `src/game/boss.c` - Boss battle implementation: 3 boss stat tables, multi-phase AI with 3 phases, 5 boss-specific attack types (HEAVY/MULTI/DRAIN/CHARGE/REPAIR), boss damage formula, phase transition messages, guaranteed item drops

### Files Modified
- `include/game/battle.h` - Added `is_boss` (u8) and `boss_zone` (u8) fields to BattleContext struct
- `src/game/battle.c` - Added boss.h include, battleInit() calls bossInit(), battleStart() detects boss triggers (0x80+) and loads boss stats from BossTypeDef, BSTATE_ENEMY_TURN delegates to bossUpdatePhase()+bossChooseAction() for boss battles, resolveAction() default case routes BOSS_ACT_* actions to bossResolveAction(), BSTATE_VICTORY uses guaranteed boss drop item instead of RNG, BSTATE_EXIT boss victory path fades out without restoring flight (zone advance handles reload)
- `src/game/battle_ui.c` - Added boss.h include, battleUIDrawEnemyStats() shows g_boss.name for boss battles, battleUIDrawScreen() shows "BOSS BATTLE!" instead of "ENCOUNTER!"
- `src/game/game_state.c` - Replaced zone-end trigger (5000px gsOnZoneEnd → g_zone_advance) with boss battle trigger (4800px gsOnBossTrigger → g_battle_trigger = BOSS_TRIGGER_BASE + current_zone)
- `src/main.c` - Added boss.h include, STATE_BATTLE victory path detects battle.is_boss and calls gsZoneAdvance() directly instead of returning to STATE_FLIGHT

### Boss Battle Architecture
```
Zone-End Flow (Phase 18 → Phase 19 change):
  OLD: 5000px scroll → g_zone_advance = 1 → gsZoneAdvance()
  NEW: 4800px scroll → g_battle_trigger = 0x80+zone → boss battle
       Boss victory → gsZoneAdvance() (screen already dark)
       Boss defeat → gsGameOverEnter()

Boss Trigger Range:
  0x80 = Zone 1 boss (COMMANDER)
  0x81 = Zone 2 boss (CRUISER)
  0x82 = Zone 3 boss (FLAGSHIP)
  IS_BOSS_TRIGGER(t): t >= 0x80 && t < 0x83

battleStart() Detection:
  if IS_BOSS_TRIGGER(enemyType):
    bdef = bossSetup(type) → loads boss stats into g_boss
    battle.is_boss = 1
    enemy stats from BossTypeDef (not enemy_battle_stats table)
  else:
    standard enemy battle (unchanged)
```

### Boss Stats
```
Boss        Zone  HP   ATK  DEF  SPD  SP  XP   Drop
---------   ----  ---  ---  ---  ---  --  ---  ---------
COMMANDER   1     120  18   10   8    3   100  HP POT L
CRUISER     2     200  22   18   6    4   200  SP CHARGE
FLAGSHIP    3     350  30   22   12   6   400  FULL REST

Balance vs expected player stats:
  Zone 1 (Lv3): Player HP=110, ATK=16, DEF=9
    Dmg to boss: 16*16/(16+10) ≈ 10/turn, 12 turns to kill
    Boss dmg: 18*18/(18+9) ≈ 12/turn, 9 turns to kill player
    → Tight fight, player needs defend/heal to win

  Zone 3 (Lv8): Player HP=230, ATK=33, DEF=22
    Dmg to boss: 33*33/(33+22) ≈ 20/turn, 17.5 turns to kill
    Boss dmg: 30*30/(30+22) ≈ 17/turn, 13.5 turns to kill player
    → Extended battle, boss specials add danger
```

### Multi-Phase Boss AI
```
AI Phase Thresholds (based on enemy HP):
  BOSS_AI_NORMAL    (>50% HP):  Balanced attacks
  BOSS_AI_ENRAGED   (25-50% HP): Aggressive, more specials
  BOSS_AI_DESPERATE (<25% HP):  All-out, charge/drain

Phase Change Messages:
  ENRAGED: "ENEMY POWERS UP!" + SFX_EXPLOSION
  DESPERATE: "GOING ALL OUT!" + SFX_EXPLOSION

Action Selection by Phase (pseudo-random via g_frame_count):
  NORMAL:    37% ATK, 19% SPECIAL, 13% HEAVY, 13% DEFEND, 18% ATK
  ENRAGED:   25% ATK, 19% MULTI, 19% HEAVY, 13% SPECIAL, 12% REPAIR, 12% ATK
  DESPERATE: 19% DRAIN, 13% CHARGE, 19% MULTI, 19% HEAVY, 13% REPAIR, 17% ATK
```

### Boss-Specific Attacks
```
BOSS_ACT_HEAVY (10):
  2x base damage. If preceded by CHARGE, adds charge_bonus.
  Message: "HEAVY STRIKE!"

BOSS_ACT_MULTI (11):
  2-3 hits at 75% base damage each. Costs 1 SP.
  Message: "RAPID FIRE x2!" or "RAPID FIRE x3!"

BOSS_ACT_DRAIN (12):
  Base damage to player, heals boss for half. Costs 1 SP.
  Message: "ENERGY DRAIN!"

BOSS_ACT_CHARGE (13):
  No damage this turn. Stores base_dmg as charge_bonus.
  Next turn forces BOSS_ACT_HEAVY with bonus added.
  Message: "CHARGING..."

BOSS_ACT_REPAIR (14):
  Heals boss for 20% max HP (max_hp / 5).
  Limited: turns_since_heal must be >= 3.
  Message: "SELF-REPAIR!"

All boss attacks respect player DEFEND (doubled DEF).
Standard BACT_ATTACK/DEFEND/SPECIAL also available to bosses.
```

### Boss Victory → Zone Advance Flow
```
1. BSTATE_VICTORY: Display XP + guaranteed item drop (90 frames)
2. BSTATE_LEVELUP: If XP triggers level-up, show notification (90 frames)
3. BSTATE_EXIT (boss path):
   - fadeOutBlocking(15)
   - battleUIHideSprites()
   - bgSetDisable(2)
   - g_boss.active = 0
   - battle.state = BSTATE_NONE, return 0
4. main.c STATE_BATTLE: battle ended, is_boss → call gsZoneAdvance()
5. gsZoneAdvance():
   - zones_cleared++, set story flags
   - If Zone 3: gsVictoryEnter() (game complete!)
   - Else: current_zone++, gsFlightEnter() (next zone)

Screen is already dark from boss exit path.
gsZoneAdvance fadeOutBlocking is instant (already dark).
Single clean fade-in when new zone loads.
```

### ROM Stats (Post Phase 19)
```
Bank 0:     9 bytes free (0.03%)
Bank 1:     0 bytes free (0.00%) - FULL (code overflow)
Bank 2:     1 byte free (0.00%) - FULL (assets)
Bank 3:     0 bytes free (0.00%) - FULL (assets)
Bank 4:     0 bytes free (0.00%) - FULL
Bank 5: 7,677 bytes free (23.43%) - boss.c code added here
Bank 6: 32,768 bytes free (100%)
Bank 7: 32,768 bytes free (100%)
Total ROM: 73,223 bytes (27.93%) free of 262,144
Phase 19 code cost: ~4,001 bytes (boss system)
```

---

## Phase 18 Implementation Details

### Files Created
- `assets/sprites/enemies/enemy_fighter.png/.pic/.pal` - Fighter ship 32x32 sprite (from ship020.png)
- `assets/sprites/enemies/enemy_heavy.png/.pic/.pal` - Heavy ship 32x32 sprite (from ship030.png)
- `assets/sprites/enemies/enemy_elite.png/.pic/.pal` - Elite ship 32x32 sprite (from ship050.png)
- `assets/backgrounds/zone2_bg.png/.pic/.pal/.map` - Zone 2 (Asteroid Belt) background (from background-05.png)
- `assets/backgrounds/zone3_bg.png/.pic/.pal/.map` - Zone 3 (Flagship Approach) background (from background-09.png)

### Files Modified
- `include/config.h` - MAX_SCROLL_TRIGGERS 16→24, added PAL_OBJ_ENEMY2 (13) for second enemy palette
- `include/assets.h` - Added externs for enemy_fighter/heavy/elite sprites and zone2/zone3 backgrounds
- `data.asm` - Added 5 new asset sections: 3 enemy sprites + 2 backgrounds (each in superfree sections for auto bank placement)
- `Makefile` - Added build rules for 3 new enemy sprites and 2 new backgrounds in both Python conversion and gfx4snes stages
- `src/engine/background.c` - Added ZONE_ASTEROID and ZONE_FLAGSHIP cases to bgLoadZone()
- `include/game/enemies.h` - Added enemySpawnFromLeft/Right declarations, updated enemySpawnWave signature (added spacingY parameter)
- `src/game/enemies.c` - Major overhaul: 2 enemy VRAM slots per zone, zone_type_a/b mapping, enemySpawnFromLeft/Right helpers, 37 wave trigger callbacks across 3 zones, zone-aware tile/palette rendering
- `src/game/story.c` - Adjusted trigger distances for longer zones: Z1(150,1550,3300), Z2(1400,3000), Z3(2050)
- `include/game/game_state.h` - Added g_zone_advance extern, gsZoneAdvance(), gsVictoryEnter(), gsVictoryUpdate()
- `src/game/game_state.c` - Added zone-end scroll trigger, zone advancement logic, victory screen, reset zone on new game
- `src/main.c` - Added g_zone_advance check in STATE_FLIGHT (priority over dialog/battle), added STATE_VICTORY case
- `src/game/battle.c` - Fixed hardcoded ZONE_DEBRIS in battleTransitionOut to use g_game.current_zone

### Zone System Architecture
```
3-Zone Campaign:
  Zone 1: Debris Field   (NORMAL speed, Scout+Fighter, 15 waves)
  Zone 2: Asteroid Belt   (NORMAL speed, Fighter+Heavy, 12 waves)
  Zone 3: Flagship Approach (FAST speed, Heavy+Elite, 10 waves)

Each zone ~4800 scroll pixels to boss:
  Enemy waves: 300-4700px (10-15 per zone)
  Story triggers: between waves (2-3 per zone)
  Pre-boss slow: 4700px (scrollTransitionSpeed to SLOW)
  Boss trigger: 4800px (Phase 19: triggers boss battle)

Total triggers per zone: 14-19 (within MAX_SCROLL_TRIGGERS=24)
```

### Two Enemy Sprites Per Zone (VRAM)
```
OBJ VRAM Layout:
  0x0000: Player ship     (tile 0)
  0x0400: Player bullets   (tile 64)
  0x0600: Enemy bullets    (tile 96)
  0x0800: Enemy type A     (tile 128) - PAL_OBJ_ENEMY (9)
  0x0900: Enemy type B     (tile 144) - PAL_OBJ_ENEMY2 (13)

Zone → VRAM Slot Mapping:
  Zone 1: A=Scout,   B=Fighter
  Zone 2: A=Fighter, B=Heavy
  Zone 3: A=Heavy,   B=Elite

Rendering picks tile/palette based on enemy type vs zone_type_a/b:
  if (e->type == zone_type_a) → TILE_ENEMY_A + PAL_ENEMY_A
  else                        → TILE_ENEMY_B + PAL_ENEMY_B
```

### Zone Transition Sequence (Updated Phase 19)
```
1. Boss trigger at 4800px → g_battle_trigger = BOSS_TRIGGER_BASE + zone
2. main.c STATE_FLIGHT: battleStart(trigger) → boss battle begins
3. Boss defeated → BSTATE_EXIT (screen dark) → main.c detects is_boss
4. main.c calls gsZoneAdvance():
   a. Set story flags (STORY_ZONE1_CLEAR, STORY_ZONE2_CLEAR)
   b. Increment g_game.zones_cleared
   c. If last zone → gsVictoryEnter()
   d. Else: g_game.current_zone++
   e. fadeOutBlocking(20) [instant - already dark from boss exit]
   f. gsFlightEnter() → full subsystem re-init for new zone:
      - bgSystemInit, spriteSystemInit, scrollInit, bulletInit, enemyInit
      - bgLoadZone (new background), playerInit, bulletLoadGraphics
      - enemyLoadGraphics (new 2 sprites for zone)
      - enemySetupZoneTriggers (new wave pattern)
      - storyRegisterTriggers (new story triggers)
      - scrollAddTrigger(4800, gsOnBossTrigger)
      - scrollSetSpeed (NORMAL or FAST for zone 3)
      - setScreenOn, fadeInBlocking(30)

Player RPG stats and inventory persist across zones.
```

### New Spawn Helpers
```
enemySpawnWave(type, count, startX, startY, spacingX, spacingY):
  Added spacingY for diagonal formations (e.g., spacingY=-10).
  Existing callers updated to pass spacingY=0.

enemySpawnFromLeft(type, y):
  LINEAR types: spawn at x=-24 with vx=+1.5 px/f (diagonal entry)
  Other types: spawn at x=24 (screen left edge, AI handles movement)

enemySpawnFromRight(type, y):
  LINEAR types: spawn at x=264 with vx=-1.5 px/f (diagonal entry)
  Other types: spawn at x=200 (screen right edge, AI handles movement)

AI_LINEAR updated to apply horizontal velocity (e->vx >> 8).
```

### Wave Trigger Summary
```
Zone 1 (Debris Field) - 15 waves:
  300:  2 scouts           1700: 1st fighter
  600:  3 scouts           2000: 3 scouts diagonal
  900:  scout from left    2300: pincer scouts
  1100: scout from right   2700: 1 fighter
  1400: 4 scouts           3100: 3 scouts
                           3500: 2 fighters
                           3900: 5 scouts
                           4200: scout+scouts
                           4500: 1 fighter
                           4700: pre-boss slow

Zone 2 (Asteroid Belt) - 12 waves:
  300:  2 fighters          2000: 4 fighters
  600:  pincer fighters     2400: 2 heavies
  900:  3 fighters          2800: 3 fighters diagonal
  1200: 1st heavy           3200: fighters+scouts
  1600: 2 fighters+1 left   3600: 5 fighters
                            4200: heavy+fighter
                            4700: pre-boss slow

Zone 3 (Flagship) - 10 waves:
  300:  2 heavies            2300: 3 elites diagonal
  700:  2 elites             2800: pincer heavy+elite
  1100: pincer elites        3300: 2 elites+2 heavies
  1500: 3 heavies            3800: 4 elites
  1900: elite+2 heavies      4700: pre-boss slow
```

### Victory Screen
```
gsVictoryEnter():
  BG1 disabled, BG3 text enabled.
  "VICTORY!" at (11,8)
  "THE ARK IS SAVED!" at (5,10)
  "PRESS START" blinking at (6,14)
  Start → title screen with full reset.

STATE_VICTORY (8) added to game.h state defines (already existed).
```

### Battle Transition Fix
```
battle.c battleTransitionOut() was hardcoded to bgLoadZone(ZONE_DEBRIS).
Fixed to use bgLoadZone(g_game.current_zone) so the correct zone's
background reloads after battle in any zone.
```

### ROM Stats
```
Bank 0:  9 bytes free (0.03%)
Bank 1:  0 bytes free (0.00%) - FULL (code overflow)
Bank 2:  1 byte free (0.00%) - FULL (assets)
Bank 3:  2 bytes free (0.01%) - FULL (assets)
Bank 4:  3 bytes free (0.01%) - nearly full (new zone assets)
Bank 5: 11,678 bytes free (35.64%) - new zone assets overflow
Bank 6: 32,768 bytes free (100%)
Bank 7: 32,768 bytes free (100%)
Total ROM: 77,229 bytes (29.46%) free of 262,144
Zero warnings, zero errors
```

### Asset Inventory (Phase 18 additions)
```
Source                                    Output
------                                    ------
ship020.png → enemy_fighter.pic/pal       32x32 4bpp fighter sprite
ship030.png → enemy_heavy.pic/pal         32x32 4bpp heavy sprite
ship050.png → enemy_elite.pic/pal         32x32 4bpp elite sprite
background-05.png → zone2_bg.pic/pal/map  256x256 4bpp zone 2 BG (1006 tiles)
background-09.png → zone3_bg.pic/pal/map  256x256 4bpp zone 3 BG (1018 tiles)
```

---

## Phase 17 Implementation Details

### Files Created
- `include/engine/sound.h` - Sound system header: SFX IDs (1-9), MUSIC IDs (0-8), soundInit/soundPlaySFX/soundUpdate/soundPlayMusic/soundStopMusic/soundPauseMusic/soundResumeMusic API
- `src/engine/sound.c` - SPC700 sound engine: spcBoot + spcAllocateSoundRegion + spcSetSoundEntry for 9 BRR samples, spcPlaySound for playback, spcProcess per frame, music stub functions for future IT module support
- `tools/convert_sfx.py` - WAV→BRR conversion pipeline: stereo 44.1kHz → mono 16kHz, silence trimming, duration truncation, amplitude normalization, BRR block alignment, snesbrr -e conversion
- `assets/sfx/*.brr` - 9 BRR sound effect files converted from source WAVs

### Files Modified
- `data.asm` - Added `.rodata_sfx` section with 9 BRR .incbin entries (sfx_player_shoot through sfx_heal with _end markers)
- `src/main.c` - Added `#include "engine/sound.h"`, soundInit() after bootSequence, soundUpdate() before vblankProcessCallbacks in main loop
- `src/engine/bullets.c` - Added sound include, soundPlaySFX(SFX_PLAYER_SHOOT) in bulletPlayerFire()
- `src/engine/collision.c` - Added sound include, SFX_EXPLOSION on enemy kill, SFX_HIT on bullet hits and player damage
- `src/game/enemies.c` - Added sound include, soundPlaySFX(SFX_ENEMY_SHOOT) when enemy fires
- `src/game/battle.c` - Added sound include, SFX_HIT on attacks, SFX_HEAL on items, SFX_MENU_MOVE on cursor navigation, SFX_MENU_SELECT on action confirm, SFX_LEVEL_UP on level up
- `src/game/dialog.c` - Added sound include, SFX_DIALOG_BLIP on typewriter reveal, SFX_MENU_SELECT on page advance
- `src/game/game_state.c` - Added sound include, SFX_MENU_SELECT on title/game-over Start press
- `linkfile` - Added `src/engine/sound.obj` (20 game objects + 4 PVSnesLib libs)

### Sound Effect Inventory
```
ID  Name            Source WAV      Duration  BRR Size  Pitch
--  ----            ----------      --------  --------  -----
1   PLAYER_SHOOT    Zap.wav         0.12s     1089B     4
2   ENEMY_SHOOT     Zap02.wav       0.12s     1089B     3
3   EXPLOSION       explode.wav     0.25s     2259B     3
4   HIT             Hit01.wav       0.12s     1089B     4
5   MENU_SELECT     Bleep.wav       0.10s     909B      5
6   MENU_MOVE       Bleep.wav       0.06s     549B      5
7   DIALOG_BLIP     Bleep.wav       0.04s     369B      6
8   LEVEL_UP        Test0001.wav    0.40s     3609B     4
9   HEAL            ShipHit.wav     0.25s     2259B     5

Total BRR: 13,221 bytes (12.9 KB)
ARAM allocation: 56 blocks (14,336 bytes)
```

### SPC700 Initialization Flow
```
soundInit():
  spcBoot()                        # Boot SPC700 co-processor
  spcAllocateSoundRegion(56)       # Allocate 14KB ARAM for BRR samples
  for each SFX (reverse order):    # Load in reverse so index matches
    spcSetSoundEntry(vol, pan=8, pitch, len, data, &brrTable[id])

soundPlaySFX(sfxId):
  spc_index = sfxId - 1            # Reverse index mapping
  spcPlaySound(spc_index)          # Triggers BRR playback

soundUpdate():
  spcProcess()                     # MUST call every frame
```

### WAV Conversion Pipeline
```
Source: C:/Users/Ryan Rentfro/Downloads/RawSounds/
  (Stereo, 44.1kHz, 16-bit, 14-32 seconds each)

Processing (tools/convert_sfx.py):
  1. Read WAV, convert stereo to mono (average channels)
  2. Downsample 44100→16000 Hz (nearest-neighbor)
  3. Trim leading silence (threshold 800 amplitude)
  4. Truncate to max duration (0.04-0.40s per SFX)
  5. Normalize amplitude to peak 28000
  6. Pad to multiple of 16 samples (BRR block alignment)
  7. Write intermediate 16-bit mono WAV
  8. Run snesbrr -e to produce .brr file
```

### Music System (Stub for Phase 18+)
```
soundPlayMusic(trackId): Sets s_current_music only (no-op)
soundStopMusic():         Calls spcStop if playing
soundPauseMusic():        Calls spcPauseMusic (native)
soundResumeMusic():       Calls spcResumeMusic (native)

To enable music in the future:
  1. Create IT module files (OpenMPT or similar)
  2. Convert via smconv: smconv -s -o soundbank *.it
  3. Add soundbank.asm to data.asm
  4. In soundInit: spcSetBank(&soundbank)
  5. In soundPlayMusic: spcLoad(moduleIndex), spcPlay(0)
```

### SFX Integration Points
```
Gameplay:
  bulletPlayerFire()    → SFX_PLAYER_SHOOT
  enemyUpdateAll()      → SFX_ENEMY_SHOOT (on fire)
  collision hit         → SFX_HIT
  collision kill        → SFX_EXPLOSION
  player takes damage   → SFX_HIT

Battle:
  resolveAction attack  → SFX_HIT
  resolveAction item    → SFX_HEAL
  applyBattleItem       → SFX_HEAL
  menu cursor move      → SFX_MENU_MOVE
  action confirm        → SFX_MENU_SELECT
  level up              → SFX_LEVEL_UP

Dialog:
  typewriter character  → SFX_DIALOG_BLIP
  page advance          → SFX_MENU_SELECT

Game State:
  title Start press     → SFX_MENU_SELECT
  game over Start press → SFX_MENU_SELECT
```

### ROM Stats
```
Bank 0: 9 bytes free (0.03%)
Bank 1: 2 bytes free (0.01%) - nearly full
Bank 2: 0 bytes free (0.00%) - BRR data section filled this bank
Bank 3: 20,776 bytes free (63.40%) - overflow started here
Total ROM: 151,859 bytes (57.93%) free of 262,144
Zero warnings, zero errors
```

---

## Phase 16 Implementation Details

### Files Created
- `include/game/dialog.h` - Dialog system header: DialogLine/DialogScript structs, speaker IDs, dialog engine API (dlgInit/dlgOpen/dlgUpdate/dlgIsActive), story API (storyInit/storyRegisterTriggers/storyHasFlag/storySetFlag), g_dialog_pending extern
- `src/game/dialog.c` - Dialog engine: typewriter text reveal, battle-like BG3 transitions, box drawing, speaker names, blinking ">" prompt, A-button skip/advance
- `src/game/story.c` - Story scripts for all 3 zones (7 dialog sequences, 31 total dialog pages), scroll trigger callbacks, story flag management

### Files Modified
- `src/main.c` - Added `#include "game/dialog.h"`, STATE_DIALOG case in switch, g_dialog_pending check in flight loop (priority over battle trigger)
- `src/game/game_state.c` - Added `#include "game/dialog.h"`, dlgInit()/storyInit() calls in gsInit(), storyRegisterTriggers() call in gsFlightEnter() after enemy triggers
- `linkfile` - Added `src/game/dialog.obj` and `src/game/story.obj` (19 game objects + 4 PVSnesLib libs)

### Dialog System Architecture
```
Dialog uses same transition pattern as battle (VRAM conflict):
  BG3 font at 0x3000 conflicts with BG1 tile space (0x0000-0x3FFF).
  During dialog: BG1 DISABLED, BG3 ENABLED with consoleInitText font.
  On close: BG3 disabled, bgLoadZone reloads BG1 tiles.

Transition In:
  fadeOut -> scrollStop -> bulletClearAll -> enemyKillAll ->
  playerHide -> spriteHideAll -> screenOff -> bgSetDisable(BG1) ->
  consoleInitText -> bgSetEnable(BG3) -> dlgDrawBox -> screenOn -> fadeIn

Transition Out:
  fadeOut -> bgSetDisable(BG3) -> bgLoadZone(zone) -> playerShow ->
  screenOn -> fadeIn -> scrollResume -> invincible_timer=120
```

### Dialog Engine States
```
DSTATE_INACTIVE (0) - No dialog active
DSTATE_TYPING   (2) - Typewriter revealing text (2 frames/char)
DSTATE_WAIT     (3) - All text shown, blinking ">" prompt, waiting A
DSTATE_CLOSE    (4) - Transition out in progress

A-button during TYPING: fast-fill all remaining text
A-button during WAIT: advance to next page or close if last page
```

### Dialog Data Structures
```
DialogLine:
  u8 speaker       - SPEAKER_NONE/VEX/COMMANDER/ENGINEER/ENEMY/SYSTEM
  char *line1       - Top text line (max 26 chars)
  char *line2       - Bottom text line (max 26 chars)

DialogScript:
  DialogLine *lines - Array of pages
  u8 line_count     - Number of pages in script

Dialog Box Layout (BG3 tile rows):
  Row 19: "-----..." top border
  Row 20: Speaker name
  Row 21: Text line 1
  Row 22: Text line 2 + ">" prompt at col 28
  Row 24: "-----..." bottom border
```

### Story Scripts
```
7 dialog sequences, 31 total pages:

Zone 1 (Debris Field):
  Intro (50px):  Commander briefs Vex, 3 pages
  Mid (450px):   Engineer warns of strange readings, 4 pages
  End (850px):   Enemy contact + first taunt, 4 pages

Zone 2 (Asteroid Belt):
  Mid (450px):   Commander reports active structure, 4 pages
  End (850px):   Engineer decodes alien signal, 5 pages

Zone 3 (Flagship):
  Mid (450px):   THE TWIST - Ark stolen from aliens, 7 pages
                 Sets STORY_TWIST_SEEN flag

Story triggers at distances between enemy wave triggers:
  Enemy waves: 100, 300, 500, 700, 900 px
  Story:       50, 450, 850 px (no overlap)
```

### Trigger Pattern
```
g_dialog_pending (same pattern as g_battle_trigger):
  1. Scroll callback fires when distance reached
  2. Callback checks story flags, sets g_dialog_pending = &script
  3. Main loop checks g_dialog_pending in STATE_FLIGHT
  4. Dialog trigger has PRIORITY over battle trigger
  5. Opens dialog, sets STATE_DIALOG
  6. On close, returns to STATE_FLIGHT

Story flags use two regions in g_game.story_flags (u16):
  Lower byte (0x01-0x20): Game flags (STORY_ZONE1_CLEAR, etc.)
  Upper byte (0x0100+):   Dialog-seen flags (SFLAG_INTRO_SEEN, etc.)
```

### ROM Stats
```
Bank 0: 14 bytes free (0.04%)
Bank 1: 10,106 bytes free (30.84%)
Total ROM: 174,664 bytes (66.63%) free of 262,144
Zero warnings, zero errors
```

---

## Phase 15 Implementation Details

### Files Created
- `include/game/game_state.h` - State machine header (gsInit, gsTitleEnter/Update, gsFlightEnter, gsGameOverEnter/Update, gsPauseToggle)
- `src/game/game_state.c` - State machine implementation, defines `GameState g_game` (was previously extern-only)

### Files Modified
- `include/game.h` - Added `u8 paused` field to GameState struct
- `src/game/battle.c` - BSTATE_DEFEAT: removed defeat penalty (now game over). BSTATE_EXIT: split into defeat path (minimal cleanup, screen dark) and victory path (full flight restore)
- `src/main.c` - Restructured to state machine dispatch (TITLE/FLIGHT/BATTLE/GAMEOVER). Simplified bootSequence (hardware init only). Flight mode wrapped in STATE_FLIGHT case with pause toggle. Battle end checks for game over.
- `linkfile` - Added `src/game/game_state.obj` (17 game objects + 4 PVSnesLib libs)

### State Machine
```
Game States (from game.h STATE_* defines):
  STATE_BOOT (0)     - Hardware init only (bootSequence)
  STATE_TITLE (1)    - Title screen with blinking "PRESS START"
  STATE_FLIGHT (2)   - Main gameplay (scrolling, enemies, bullets, collision)
  STATE_BATTLE (3)   - Turn-based combat (managed by battle.c)
  STATE_GAMEOVER (7) - "GAME OVER" screen with restart

Flow:
  Boot -> Title -> [Start] -> Flight -> [collision] -> Battle
    Battle Victory -> Flight (auto-restore by battleTransitionOut)
    Battle Defeat -> Game Over -> [Start] -> Title -> [Start] -> Flight

Title Screen:
  BG1 off, BG2 off, BG3 text enabled
  "VEX DEFENDER" at (10,9), "PRESS START" blinking at (6,15)
  Start button -> fade out -> rpgStatsInit + invInit -> gsFlightEnter

Game Over Screen:
  BG1 off (corrupted by font), BG2 stars visible as backdrop, BG3 text
  "GAME OVER" at (11,10), "PRESS START" blinking at (6,14)
  Start button -> fade out -> rpgStatsInit + invInit -> gsTitleEnter
```

### Pause System
```
During flight mode, Start button toggles pause:
  Pause ON:  g_game.paused = 1, setBrightness(8) (dim screen)
  Pause OFF: g_game.paused = 0, setBrightness(15) (full brightness)

When paused:
  - All game updates skipped (scroll, enemies, bullets, player, collision)
  - Only Start button input processed (to unpause)
  - No BG3 text overlay (would corrupt BG1 tiles at VRAM 0x3000)
  - Screen dimming is the only visual pause indicator
```

### Battle Defeat -> Game Over
```
Phase 15 changes to battle.c:
  BSTATE_DEFEAT:
    - Timer counts down (90 frames showing "DEFEATED...")
    - NO stat sync or defeat penalty (player is dead)
    - Transitions to BSTATE_EXIT

  BSTATE_EXIT (two paths):
    If battle.player.hp <= 0 (defeat):
      - fadeOutBlocking(15)
      - battleUIHideSprites()
      - bgSetDisable(2)
      - Screen left dark for game over transition
    If victory:
      - battleTransitionOut() (full flight restore, unchanged)

  main.c checks battle.player.hp after battle.state == BSTATE_NONE:
    hp <= 0 -> gsGameOverEnter() (screen already dark)
    hp > 0  -> resume STATE_FLIGHT (already restored)
```

### g_game Global (Previously Undefined)
```
GameState g_game was declared extern in game.h but never defined.
Phase 15 defines it in game_state.c:
  GameState g_game;

gsInit() zeroes all fields:
  current_state = STATE_BOOT
  current_zone = ZONE_DEBRIS
  paused = 0, story_flags = 0, etc.
```

### Main Loop Structure (Post Phase 17)
```
bootSequence()       # Hardware init + 30-frame settle
soundInit()          # SPC700 boot + BRR sample loading (Phase 17)
inputInit()          # Controller init (needed before title)
gsInit()             # Zero g_game + dlgInit + storyInit
gsTitleEnter()       # Show title screen with fade

Main Loop:
  WaitForVBlank()
  bgVBlankUpdate()      # CGRAM cycling (no-op when not in flight)
  scrollVBlankUpdate()  # Scroll registers (no-op when not in flight)
  inputUpdate()

  switch (g_game.current_state):
    STATE_TITLE:
      gsTitleUpdate()         # Blink text, check Start button

    STATE_FLIGHT:
      if Start pressed: gsPauseToggle()
      if paused: skip
      bgUpdate, scrollUpdate, player, bullets, enemies, collision
      if dialog pending: dlgOpen, state = DIALOG  (priority over battle)
      elif battle trigger: battleStart, state = BATTLE
      else: spriteUpdateAll, spriteRenderAll, bulletRenderAll, enemyRenderAll

    STATE_DIALOG:
      dlgUpdate()
      if !dlgIsActive(): state = FLIGHT

    STATE_BATTLE:
      battleUpdate()
      if battle ended:
        if defeat (hp<=0): gsGameOverEnter()
        if victory: state = FLIGHT

    STATE_GAMEOVER:
      gsGameOverUpdate()      # Blink text, check Start button

  soundUpdate()              # spcProcess() - keep SPC700 alive (Phase 17)
  vblankProcessCallbacks()
```

### gsFlightEnter() - Full Flight Initialization
```
Called from title screen (new game) and could be called for game restart.
Initializes ALL flight subsystems:
  bgSystemInit, spriteSystemInit, scrollInit, bulletInit, enemyInit,
  collisionInit, battleInit
Then loads zone graphics:
  bgLoadZone, playerInit, bulletLoadGraphics, enemyLoadGraphics,
  enemySetupZoneTriggers, storyRegisterTriggers (Phase 16)
Then starts gameplay:
  scrollSetSpeed(NORMAL), setScreenOn, fadeInBlocking
  g_game.current_state = STATE_FLIGHT
```

---

## Phase 14 Implementation Details

### Files Created
- `include/game/inventory.h` - Inventory header (InvSlot struct, item IDs, 8 slots, API)
- `src/game/inventory.c` - Inventory management, item defs, loot table, add/remove/count/roll

### Files Modified
- `include/game/battle.h` - Added `BSTATE_ITEM_SELECT` (11) for item sub-menu state
- `include/game/battle_ui.h` - Added `battleUIDrawItemMenu()`, `battleUIDrawItemDrop()` declarations
- `src/game/battle_ui.c` - Added inventory include, item menu drawing (names + quantities + cursor), item drop notification
- `src/game/battle.c` - Added inventory include, item selection state vars, `buildItemList()`, `applyBattleItem()`, BSTATE_ITEM_SELECT handler, loot drop roll on victory
- `src/main.c` - Added `#include "game/inventory.h"` and `invInit()` call
- `linkfile` - Added `src/game/inventory.obj` (16 game objects + 4 PVSnesLib libs)

### Item System
```
6 Consumable Items:
  ID  Name        Effect
  --  ----        ------
  1   HP POT S    Restore 30 HP
  2   HP POT L    Restore 80 HP
  3   SP CHARGE   Restore 1 SP
  4   ATK BOOST   +5 ATK for current battle (temporary)
  5   DEF BOOST   +5 DEF for current battle (temporary)
  6   FULL REST   Full HP + SP restore

Inventory: 8 slots, max stack 9 per item
Starting items: 2x HP POT S
No equipment/weapons (bullet system already handles weapon cycling)
```

### Battle Item Sub-Menu (BSTATE_ITEM_SELECT)
```
Flow:
  PLAYER_TURN -> select ITEM -> check inventory
    If empty: "NO ITEMS!" stays in PLAYER_TURN
    If has items: -> BSTATE_ITEM_SELECT

BSTATE_ITEM_SELECT:
  Shows up to 4 items from inventory on rows 9-12 (same as action menu):
    Row 9:  > HP POT S  x2
    Row 10:   SP CHARGE x1
    Row 11:   ATK BOOST x3
    Row 12:   (empty)

  D-pad Up/Down: navigate cursor
  A button: use selected item
    -> Apply effect to battle.player
    -> Consume 1 from inventory
    -> Show message (VEX HEALS! / SP RESTORED! / ATK UP! etc.)
    -> BSTATE_RESOLVE (30 frames)
  Select button: cancel back to PLAYER_TURN main menu

Item effects applied directly to BattleCombatant:
  HP potions: battle.player.hp += effect (capped at max_hp)
  SP Charge: battle.player.sp += 1 (capped at max_sp)
  ATK/DEF Boost: battle.player.atk/def += 5 (lost when battle ends)
  Full Restore: hp = max_hp, sp = max_sp
```

### Loot Drop System
```
On victory (in BSTATE_RESOLVE when enemy HP <= 0):
  1. Roll invRollDrop(enemy_type) using g_frame_count as PRNG
  2. If drop != ITEM_NONE: invAdd(drop, 1)
  3. Show "GOT: [ITEM NAME]" on victory screen (row 7)

Drop rates per enemy type:
  SCOUT:   ~30% HP POT S
  FIGHTER: ~25% HP POT S, ~25% SP CHARGE
  HEAVY:   ~20% HP POT L, ~20% ATK BOOST, ~31% SP CHARGE
  ELITE:   ~31% HP POT L, ~20% FULL REST, ~27% DEF BOOST

PRNG: (g_frame_count * 31 + enemy_type * 17) & 0xFF
  Deterministic per frame+type. Good enough for gameplay.
```

### What Was NOT Implemented (deferred)
```
- Equipment/weapons: Bullet system already has weapon cycling (L/R)
- Flight item pickups: No item sprites exist, would need new tiles
- Shield/Revive items: Need buff tracking system not yet built
- Pause menu item use: No pause menu exists yet
- Item shop: Deferred to later phase (credits field exists in rpg_stats)
```

---

## Phase 13 Implementation Details

### Files Created
- `include/game/rpg_stats.h` - RPG stats header (PlayerRPGStats struct, level cap, base stats, API)
- `src/game/rpg_stats.c` - XP table, growth table, init, addXP with level-up loop, defeat penalty

### Files Modified
- `include/game/battle.h` - Added `BSTATE_LEVELUP` (10) for level-up display state
- `src/game/battle.c` - Integrated rpg_stats: player stats from rpg_stats, new damage formula, post-battle XP/sync/penalty, BSTATE_LEVELUP handler
- `include/game/battle_ui.h` - Added `battleUIDrawLevelUp(u8 new_level)` declaration
- `src/game/battle_ui.c` - Added `battleUIDrawLevelUp()` implementation (level-up text + stat refresh)
- `src/main.c` - Added `#include "game/rpg_stats.h"` and `rpgStatsInit()` call
- `linkfile` - Added `src/game/rpg_stats.obj` (15 game objects + 4 PVSnesLib libs)

### RPG Stats System
```
PlayerRPGStats struct (persistent between battles):
  level (1-10), xp, xp_to_next, max_hp, hp, atk, def, spd, max_sp, sp, credits, total_kills

Base Stats (Level 1):
  HP:80, ATK:12, DEF:6, SPD:10, SP:2

Level Cap: 10
XP Table (cumulative): 0, 30, 80, 160, 280, 450, 680, 1000, 1400, 2000

Growth Table (per level-up):
  L1->L2:  HP+15, ATK+2, DEF+1, SPD+1, SP+0
  L2->L3:  HP+15, ATK+2, DEF+2, SPD+1, SP+1
  L3->L4:  HP+20, ATK+3, DEF+2, SPD+1, SP+0
  L4->L5:  HP+20, ATK+3, DEF+2, SPD+2, SP+1
  L5->L6:  HP+25, ATK+3, DEF+3, SPD+1, SP+0
  L6->L7:  HP+25, ATK+4, DEF+3, SPD+2, SP+1
  L7->L8:  HP+30, ATK+4, DEF+3, SPD+1, SP+0
  L8->L9:  HP+30, ATK+5, DEF+4, SPD+2, SP+1
  L9->L10: HP+35, ATK+5, DEF+4, SPD+2, SP+1

Expected Stats at Max Level (10):
  HP:295, ATK:43, DEF:30, SPD:23, SP:7
```

### Damage Formula (Phase 13 - replaces Phase 11)
```
Old:  ATK - (DEF/2) + variance, min 1
New:  ATK^2 / (ATK + DEF) + variance(-1..+2), min 1

Max ATK=43, ATK^2=1849, fits s16 (max 32767)
Division is one-time per action, acceptable on 65816

SPECIAL: 1.5x damage (was 2x)
  damage = base + (base >> 1)

DEFEND: still doubles DEF for incoming attack
ITEM: still heals 25% max HP (max_hp >> 2)
```

### Battle Integration Changes
```
battleStart():
  Player stats now loaded from rpg_stats (hp, max_hp, atk, def, spd, sp, max_sp)
  Previously: hardcoded HP:100, ATK:15, DEF:8, SPD:12, SP:3

BSTATE_VICTORY flow:
  1. Sync surviving HP/SP back to rpg_stats
  2. Increment rpg_stats.total_kills
  3. Add XP to g_score (display)
  4. Call rpgAddXP() - processes level-ups internally
  5. If leveled up:
     - Update battle.player from rpg_stats (for UI display)
     - Transition to BSTATE_LEVELUP
     - Show "LEVEL UP! NOW LV:N" for 90 frames
  6. If no level-up: go to BSTATE_EXIT

BSTATE_DEFEAT flow:
  1. Sync SP back to rpg_stats
  2. Call rpgApplyDefeatPenalty() (lose ~25% HP, min 1)
  3. Go to BSTATE_EXIT

BSTATE_LEVELUP (new state 10):
  Display level-up text for 90 frames, then BSTATE_EXIT
```

### Defeat Penalty
```
rpgApplyDefeatPenalty():
  penalty = rpg_stats.hp >> 2   (~25% of current HP)
  if penalty < 1: penalty = 1
  rpg_stats.hp -= penalty
  if rpg_stats.hp < 1: rpg_stats.hp = 1
  Uses bit shift (no division) for 65816 efficiency
```

### Level-Up UI
```
battleUIDrawLevelUp(new_level):
  Row 5: "LEVEL UP!"
  Row 6: "NOW LV:" + level number (handles 10)
  Also refreshes player stats row to show new max HP/SP
```

---

## Phase 12 Implementation Details

### Files Created
- `include/game/battle_ui.h` - Battle UI header (sprite positions, OAM IDs, HP bar width, shake frames, function declarations)
- `src/game/battle_ui.c` - Battle UI implementation (HP bars, battle sprites, shake animation, all BG3 text drawing)

### Files Modified
- `src/game/battle.c` - Removed all inline drawing functions, added `#include "game/battle_ui.h"`, replaced draw calls with `battleUI*()` functions, added sprite show/hide in transitions, added shake update in battle loop
- `linkfile` - Added `src/game/battle_ui.obj`

### Architecture: Drawing Separated from Logic
```
battle.c (logic):
  battleCalcDamage()      - ATK-(DEF/2)+variance, min 1
  resolveAction()         - Calls battleUI* for display
  enemyChooseAction()     - Weighted random AI
  battleTransitionIn()    - Calls battleUIShowSprites(), battleUIDrawScreen()
  battleTransitionOut()   - Calls battleUIHideSprites()
  battleUpdate()          - State machine, calls battleUIUpdateShake() each frame

battle_ui.c (drawing):
  battleUIInit()           - Zero shake state
  battleUIShowSprites()    - Set up OAM slots 64-65 for battle sprites
  battleUIHideSprites()    - Hide OAM slots 64-65
  battleUIDrawScreen()     - Draw initial battle screen (stats + "ENCOUNTER!")
  battleUIDrawEnemyStats() - Enemy name + HP bar + number
  battleUIDrawPlayerStats()- Player HP bar + number + SP
  battleUIDrawMenu()       - 4-option menu with cursor
  battleUIClearMenu()      - Clear menu rows
  battleUIDrawMessage()    - Message text on row 5
  battleUIDrawDamage()     - Damage/heal number on row 6
  battleUIStartShake()     - Begin 8-frame shake on target sprite
  battleUIUpdateShake()    - Per-frame shake position update
  battleUIDrawVictory()    - Victory message + XP display
  battleUIDrawDefeat()     - Defeat message
```

### HP Bar System (No Division)
```
Text-based bar: [==========] (10 segments in brackets)
  Full HP:  [==========]
  Half HP:  [=====----- ]
  Low HP:   [=--------- ]
  Dead:     [----------]

Fill calculation (integer math only):
  prod = (current << 3) + (current << 1)   // current * 10
  fill = 0
  while (prod >= max_hp) { prod -= max_hp; fill++; }
  Guarantees fill >= 1 when current > 0 (always show life)

Enemy row 2:  HP:[==========] 060
Player row 16: VEX HP:[==========]100 SP:3
```

### Battle Sprites (OAM_UI Slots)
```
Enemy sprite:
  OAM slot 64 (oam_id 256)
  Position: (28, 28) - top-left area
  Tile: 128 (enemy scout at VRAM 0x0800)
  Palette: 1 (PAL_OBJ_ENEMY - 8)
  Size: OBJ_LARGE (32x32)
  Priority: 3 (above all BGs)

Player sprite:
  OAM slot 65 (oam_id 260)
  Position: (184, 96) - right-center area
  Tile: 0 (player ship at VRAM 0x0000)
  Palette: 0 (PAL_OBJ_PLAYER - 8)
  Size: OBJ_LARGE (32x32)
  Priority: 3

Lifecycle:
  spriteHideAll() clears ALL 128 OAM entries
  battleUIShowSprites() sets up slots 64-65 (after hide)
  During battle: slots untouched (spriteRenderAll not called)
  battleUIHideSprites() hides slots 64-65 on exit
  Back to flight: spriteRenderAll manages pool 0-47 only
```

### Hit Shake Animation
```
Triggered by battleUIStartShake(target):
  target 0 = shake enemy sprite
  target 1 = shake player sprite

Duration: 8 frames (BUI_SHAKE_FRAMES)
Pattern: alternating ±2px X offset
  frame 7: +2px  (timer & 2 = 2)
  frame 6: -2px  (timer & 2 = 0)
  frame 5: +2px
  frame 4: -2px
  frame 3: +2px
  frame 2: -2px
  frame 1: +2px
  frame 0: restore to base position

Triggers:
  ATTACK -> shake target
  SPECIAL (with SP) -> shake target
  SPECIAL (no SP, fallback) -> shake target
  DEFEND -> no shake
  ITEM -> no shake
```

### BG3 Text Layout (Enhanced from Phase 11)
```
Row 1:  SCOUT                         <- enemy name
Row 2:  HP:[==========] 060           <- HP bar (NEW)
Row 3:  (empty - enemy sprite area)
Row 4:  (empty - enemy sprite area)
Row 5:  VEX ATTACKS!                  <- battle message (unchanged)
Row 6:  045 DAMAGE!                   <- damage display (unchanged)
Row 7-8: (empty)
Row 9:  > ATTACK                      <- menu (unchanged)
Row 10:   DEFEND
Row 11:   SPECIAL
Row 12:   ITEM
Row 13-15: (empty - player sprite area)
Row 16: VEX HP:[==========]100 SP:3   <- HP bar (NEW)
```

---

## Phase 11 Implementation Details

### Files Created
- `include/game/battle.h` - Battle system header (BattleCombatant, BattleContext, states, actions, API)
- `src/game/battle.c` - Full battle engine: state machine, damage calc, enemy AI, BG3 text UI, transitions

### Files Modified
- `src/engine/collision.c` - Added battle trigger for FIGHTER+ enemy contacts (scouts still instant kill)
- `src/main.c` - Added `battleInit()`, split main loop into battle/flight modes, SELECT debug trigger
- `linkfile` - Added `src/game/battle.obj`

### Battle System Architecture
```
battleInit()              - Zero battle state, set trigger to NONE
battleStart(enemyType)    - Init combatants, blocking transition IN, start INIT state
battleUpdate(pad_pressed) - Per-frame state machine update, returns 1 while active
g_battle_trigger          - u8, set by collision.c; BATTLE_TRIGGER_NONE (0xFF) = no battle
battle                    - BattleContext global, check battle.state != BSTATE_NONE
```

### Battle State Machine Flow
```
BSTATE_INIT (60 frames):
  "ENCOUNTER!" text displayed
  After timer: PLAYER_TURN or ENEMY_TURN based on SPD

BSTATE_PLAYER_TURN:
  D-pad Up/Down navigates cursor, A button confirms action
  SPECIAL blocked if SP == 0
  After confirm: PLAYER_ACT (15 frame pause)

BSTATE_PLAYER_ACT:
  15 frame pause, then resolve player action
  Message + damage drawn to BG3
  -> RESOLVE (30 frame display)

BSTATE_ENEMY_TURN:
  AI picks action instantly (weighted random via g_frame_count)
  -> ENEMY_ACT (15 frame pause)

BSTATE_ENEMY_ACT:
  15 frame pause, then resolve enemy action
  -> RESOLVE (30 frame display)

BSTATE_RESOLVE:
  Check HP: enemy <= 0 -> VICTORY, player <= 0 -> DEFEAT
  Otherwise: alternate turns (player -> enemy -> player -> ...)
  Round number increments after enemy acts

BSTATE_VICTORY (90 frames):
  "VICTORY! +NNN XP" displayed
  XP added to g_score (placeholder until Phase 13)

BSTATE_DEFEAT (90 frames):
  "DEFEATED..." displayed
  Returns to flight with invincibility (Phase 15 adds game over)

BSTATE_EXIT:
  Blocking transition OUT -> BSTATE_NONE
```

### Battle Actions (4 types)
```
BACT_ATTACK (0):  ATK - DEF/2 + variance(-1..+2), min 1 damage
BACT_DEFEND (1):  Doubles DEF for one incoming attack
BACT_SPECIAL (2): Costs 1 SP, deals 2x damage (falls back to ATTACK if no SP)
BACT_ITEM (3):    Heals 25% max HP (max_hp >> 2), no limit (placeholder)
```

### Damage Formula (integer math, no floating point)
```
damage = ATK - (DEF >> 1) + (g_frame_count & 3) - 1
if (defender.defending) DEF is doubled before calculation
SPECIAL: damage <<= 1 (2x)
minimum damage: 1
```

### Player Combatant (replaced by Phase 13 RPG stats)
```
Phase 11 placeholder: HP:100, ATK:15, DEF:8, SPD:12, SP:3
Phase 13: Now loaded from rpg_stats (Level 1: HP:80, ATK:12, DEF:6, SPD:10, SP:2)
```

### Enemy Battle Stats (per type)
```
SCOUT:   HP:30  ATK:8  DEF:3  SPD:5   SP:0  XP:15
FIGHTER: HP:60  ATK:14 DEF:8  SPD:10  SP:2  XP:30
HEAVY:   HP:100 ATK:20 DEF:15 SPD:6   SP:3  XP:50
ELITE:   HP:80  ATK:18 DEF:10 SPD:14  SP:4  XP:75
```

### Enemy AI Decision (g_frame_count pseudo-random)
```
Normal HP: 62.5% attack, 18.75% special (if SP>0), 18.75% defend
Low HP (<25%): 25% special (if SP>0), 25% defend, 50% attack
Uses g_frame_count + turn_number for pseudo-random seed
```

### Battle Trigger System
```
Collision contact with FIGHTER/HEAVY/ELITE enemies:
  -> g_battle_trigger = enemy_type (set in collision.c)
  -> main.c checks trigger after collisionCheckAll()
  -> battleStart() does blocking transition

Scout contacts: instant destroy + score (unchanged from Phase 10)

Debug: SELECT button forces test battle against FIGHTER
```

### BG3 Text UI Layout (consoleDrawText coordinates)
```
Row 1:  Enemy name (SCOUT/FIGHTER/CRUISER/ELITE)
Row 2:  Enemy HP: NNN/NNN
Row 5:  Battle message ("VEX ATTACKS!", "ENEMY DEFENDS!", etc.)
Row 6:  Damage/heal amount ("NNN DAMAGE!" or "NNN HEALED!")
Row 9:  > ATTACK    (cursor menu, visible during PLAYER_TURN)
Row 10:   DEFEND
Row 11:   SPECIAL
Row 12:   ITEM
Row 16: VEX  HP:NNN  SP:N
```

### Battle Transitions
```
Transition IN (battleStart -> battleTransitionIn):
  1. fadeOutBlocking(15) - fade to black
  2. scrollSetSpeed(STOP), bulletClearAll(), enemyKillAll()
  3. playerHide(), spriteHideAll()
  4. setScreenOff() - force blank
  5. bgSetDisable(0) - hide BG1 (font will corrupt tiles at 0x3000)
  6. consoleInitText(0, BG_4COLORS, 0, 0) - init BG3 text system
  7. bgSetEnable(2) - show BG3
  8. Draw initial UI (enemy stats, player stats, "ENCOUNTER!")
  9. setScreenOn(), fadeInBlocking(15) - fade in

Transition OUT (BSTATE_EXIT -> battleTransitionOut):
  1. fadeOutBlocking(15) - fade to black
  2. bgSetDisable(2) - hide BG3
  3. bgLoadZone(ZONE_DEBRIS) - reload BG1 tiles (fixes font corruption)
  4. playerShow()
  5. setScreenOn(), fadeInBlocking(15) - fade in
  6. scrollSetSpeed(NORMAL), invincible_timer=120
```

### VRAM During Battle
```
BG1 tiles (0x0000-0x3FFF): CORRUPTED at 0x3000+ by font - BG1 DISABLED
BG2 tiles (0x5000-0x5FFF): Star parallax - still visible as backdrop
BG3 tiles (0x3000):        Font loaded by consoleInitText - BG3 ENABLED
OBJ tiles (0x4000-0x4FFF): Unchanged - sprites hidden, tiles persist
```

### Bank Overflow (RESOLVED)
```
WLA-DX linker auto-overflows to Bank 1 via -A -c flags in snes_rules.
No #pragma code(bank1) needed. New .c files just get linked and the
linker places code in the next bank with space. battle.c went to Bank 1.
Cross-bank calls use JSL (jump subroutine long), handled by 816-tcc.
```

### Main Loop Structure (Post Phase 11)
```
Main Loop:
  WaitForVBlank()
  bgVBlankUpdate()
  scrollVBlankUpdate()
  inputUpdate()

  if battle.state != BSTATE_NONE:
    battleUpdate(pad_pressed)      # Battle mode
  else:
    bgUpdate()                     # Flight mode
    scrollUpdate()
    playerHandleInput()
    playerUpdate()
    [bullet firing, weapon cycling]
    bulletUpdateAll()
    enemyUpdateAll()
    collisionCheckAll()            # May set g_battle_trigger
    [SELECT debug: force FIGHTER battle]
    if g_battle_trigger != NONE:
      battleStart(trigger)         # Blocking transition -> battle mode
    else:
      spriteUpdateAll()            # Render flight entities
      spriteRenderAll()
      bulletRenderAll()
      enemyRenderAll()

  vblankProcessCallbacks()
```

### Collision Contact Changes (Phase 11)
```
Player contacts enemy:
  SCOUT (type 0):    Instant kill + score + invincible (unchanged)
  FIGHTER (type 1):  g_battle_trigger = 1 (trigger battle)
  HEAVY (type 2):    g_battle_trigger = 2 (trigger battle)
  ELITE (type 3):    g_battle_trigger = 3 (trigger battle)

Player bullet hits: Unchanged (still instant damage/destroy)
Enemy bullet hits:  Unchanged (still invincibility frames)
```

---

## Phase 10 Implementation Details

### Files Created
- `include/engine/collision.h` - Collision system header (Hitbox struct, AABB check, score extern)
- `src/engine/collision.c` - AABB collision detection, 3 check loops, score tracking

### Files Modified
- `src/main.c` - Integrated `collisionInit()`, `collisionCheckAll()` in main loop
- `linkfile` - Added `src/engine/collision.obj`

### Collision System Architecture
```
collisionInit()         - Zero score
collisionCheckAll()     - Run all 3 collision check loops per frame
collisionCheckAABB()    - AABB overlap test (4 additions + 4 comparisons)
g_score                 - u16 player score (extern)
```

### Three Collision Checks Per Frame
```
1. Player bullets vs enemies (checkPlayerBulletsVsEnemies):
   - Iterate player bullets (pool 0-15) × enemies (pool 0-7)
   - On hit: deactivate bullet, call enemyDamage(), add score if destroyed
   - Laser bullets get larger hitbox (12x12 vs 8x8)
   - Worst case: 16 × 8 = 128 AABB checks

2. Enemy bullets vs player (checkEnemyBulletsVsPlayer):
   - Skip if player invincible or hidden
   - Iterate enemy bullets (pool 16-23) vs player
   - On hit: deactivate bullet, set invincible_timer = 120 (2 sec)
   - Worst case: 8 AABB checks

3. Player body vs enemies (checkPlayerVsEnemies):
   - Skip if player invincible or hidden
   - Iterate enemies (pool 0-7) vs player
   - On hit: destroy enemy + score, set invincible_timer = 120
   - Worst case: 8 AABB checks

Total worst case: 144 AABB checks × ~8 ops each = ~1152 CPU cycles (<1% frame)
```

### Hitbox Definitions (smaller than sprites for fair gameplay)
```
Player ship (32x32 sprite): offset(8,8), size 16x16  (cockpit area)
Enemy ship  (32x32 sprite): offset(4,4), size 24x24
Bullet      (16x16 sprite): offset(4,4), size  8x8   (core projectile)
Laser       (16x16 sprite): offset(2,2), size 12x12  (larger impact)
```

### Collision Responses (updated in Phase 11)
```
Player bullet hits enemy:  bullet destroyed, enemyDamage() called
  -> If enemy HP reaches 0: enemy destroyed, score += enemy.score_value
  -> If enemy survives: flash_timer set (blink effect)

Enemy bullet hits player:  bullet destroyed, player invincible for 120 frames
  -> Player blinks every 4 frames during invincibility (already in playerUpdate)
  -> No HP loss yet (HP system added in Phase 13)

Player contacts SCOUT:     scout destroyed, score added, player invincible
Player contacts FIGHTER+:  g_battle_trigger set, enemy deactivated -> battle starts
```

### Score System
- `g_score` is u16, max 65535 points
- Score values: Scout=100, Fighter=200, Heavy=350, Elite=500
- Score display not yet implemented (Phase 12 or 20)

### Main Loop Order (Post Phase 10)
```
  ...
  bulletUpdateAll()       # Move bullets, despawn, tick cooldown
  enemyUpdateAll()        # AI movement, firing, despawn, blink
  collisionCheckAll()     # AABB: bullets vs enemies, bullets vs player, contact
  spriteUpdateAll()       # Advance all sprite animations
  spriteRenderAll()       # Write all active sprites to OAM
  bulletRenderAll()       # Write bullet OAM (after spriteRenderAll)
  enemyRenderAll()        # Write enemy OAM (after spriteRenderAll)
  ...
```

---

## Phase 9 Implementation Details

### Files Created
- `include/game/enemies.h` - Enemy system header (Enemy struct, EnemyTypeDef, AI defines, full API)
- `src/game/enemies.c` - Enemy pool, 5 AI patterns, spawning, firing, rendering, scroll trigger waves

### Files Modified
- `src/main.c` - Integrated `enemyInit()`, `enemyLoadGraphics()`, `enemySetupZoneTriggers()`, `enemyUpdateAll()`, `enemyRenderAll()`
- `linkfile` - Added `src/game/enemies.obj`

### Enemy System Architecture
```
enemyInit()                 - Clear pool, assign OAM slots (20-27)
enemyLoadGraphics(zone)     - DMA enemy tiles + palette to OBJ VRAM (force blank)
enemySetupZoneTriggers(zone)- Register scroll distance callbacks for wave spawning
enemySpawn(type, x, y)     - Spawn single enemy from pool
enemySpawnWave(type,n,x,y,dx) - Spawn horizontal formation
enemyUpdateAll()            - AI movement, firing, off-screen despawn, blink countdown
enemyRenderAll()            - Write all enemy slots to OAM (after spriteRenderAll + bulletRenderAll)
enemyDamage(e, dmg)         - Reduce HP, flash effect; returns 1 if destroyed
enemyKillAll()              - Deactivate all enemies (zone transition)
enemyGetPool()              - Return pool pointer for collision checks (Phase 10)
enemyGetTypeDef(type)       - Return ROM type definition pointer
```

### Enemy Types
```
ENEMY_TYPE_SCOUT (0):
  - HP: 10, Speed: 2 px/f, Fire Rate: 90 frames
  - AI: LINEAR (straight down)
  - Score: 100, Contact Damage: 10

ENEMY_TYPE_FIGHTER (1):
  - HP: 20, Speed: 1 px/f, Fire Rate: 60 frames
  - AI: SINE_WAVE (oscillate ±7px from center, 64-frame period)
  - Score: 200, Contact Damage: 15

ENEMY_TYPE_HEAVY (2):
  - HP: 40, Speed: 1 px/f, Fire Rate: 45 frames
  - AI: HOVER (descend to y=60, then strafe left/right between x=16..224)
  - Score: 350, Contact Damage: 20

ENEMY_TYPE_ELITE (3):
  - HP: 30, Speed: 2 px/f, Fire Rate: 50 frames
  - AI: CHASE (track player X at ~0.5 px/f while descending)
  - Score: 500, Contact Damage: 20
```

### AI Patterns
```
AI_LINEAR (0):    Straight down at constant vy. Simplest pattern.
AI_SINE_WAVE (1): Descend + horizontal sine oscillation (16-entry lookup table).
                  Position-based: x = center + sine[timer>>2 & 0xF].
AI_SWOOP (2):     Enter from side, curve, decelerate laterally. vx decays every
                  8 frames after frame 30. (Available for future enemy types.)
AI_HOVER (3):     Phase 0: descend to y=60. Phase 1: strafe at 1.0 px/f,
                  bounce off edges. Fires aimed bullets at player.
AI_CHASE (4):     Descend at constant vy. Every other frame, move 1px toward
                  player X (with ±4px dead zone). Fires aimed bullets.
```

### Zone 1 Trigger Waves (Scroll Distance -> Spawn)
```
100 px: Wave 1 - 3 scouts horizontal line (x=48, spacing=64)
300 px: Wave 2 - 2 scouts staggered (x=32,y=-32 + x=192,y=-48)
500 px: Wave 3 - 3 fighters tight formation (x=64, spacing=48)
700 px: Wave 4 - 2 scouts flanking + 1 heavy center
900 px: Wave 5 - 4 scouts wide line (x=32, spacing=56)
```
At SCROLL_SPEED_NORMAL (0.5 px/f), waves arrive at ~3.3s, 10s, 16.7s, 23.3s, 30s.

### Enemy Firing
```
LINEAR/SINE_WAVE/SWOOP enemies: bulletEnemyFireDown() - straight down
HOVER/CHASE enemies:            bulletEnemyFire() - aimed at player position
Fire origin: (enemy.x + 8, enemy.y + 24/32) - bottom-center of 32x32 sprite
```

### Damage Flash Effect
When damaged, `flash_timer` set to 6 frames. During flash, enemy blinks
(hidden on odd flash_timer frames, shown on even). No palette swap needed.

### OBJ VRAM Layout (Post Phase 9)
```
VRAM_OBJ_GFX + 0x0000: Player ship tiles (32x32, 4bpp), tile# = 0
VRAM_OBJ_GFX + 0x0400: Player bullet tiles (16x16, 4bpp), tile# = 64
VRAM_OBJ_GFX + 0x0600: Enemy bullet tiles (16x16, 4bpp), tile# = 96
VRAM_OBJ_GFX + 0x0800: Enemy scout tiles (32x32, 4bpp), tile# = 128
                        3072 of 4096 words used (75%)
```

### OAM Rendering Note
Both bullets and enemies manage their own OAM slots independently from the
sprite engine pool. Render order must be:
1. `spriteRenderAll()` - hides all inactive pool entries (including bullet/enemy OAM slots)
2. `bulletRenderAll()` - overwrites bullet OAM slots with correct state
3. `enemyRenderAll()` - overwrites enemy OAM slots with correct state

### Main Loop Order (Post Phase 9)
```
main.c
  -> bgSystemInit()         # Background system
  -> spriteSystemInit()      # Sprite pool + OAM clear
  -> inputInit()             # Zero input state
  -> scrollInit()            # Zero scroll state
  -> bulletInit()            # Zero bullet pool, init weapon
  -> enemyInit()             # Zero enemy pool, assign OAM slots
  -> bgLoadZone()            # Load backgrounds (force blank)
  -> playerInit()            # Load player graphics, allocate sprite
  -> bulletLoadGraphics()    # Load bullet tiles + palettes (force blank)
  -> enemyLoadGraphics()     # Load enemy tiles + palette (force blank)
  -> enemySetupZoneTriggers()# Register scroll-based wave spawns
  -> scrollSetSpeed(NORMAL)  # Start flight scrolling
  -> Main Loop:
       WaitForVBlank()
       bgVBlankUpdate()      # CGRAM palette cycling
       scrollVBlankUpdate()  # Write scroll registers to hardware
       inputUpdate()         # Read controller, compute edges
       bgUpdate()            # Star twinkle timer
       scrollUpdate()        # Advance scroll, check triggers -> enemy spawns
       playerHandleInput()   # D-pad movement, banking, clamping
       playerUpdate()        # Invincibility blink, sprite sync
       bulletPlayerFire()    # Fire if ACTION_FIRE held (Y button)
       bulletNextWeapon()    # If ACTION_NEXT_WPN pressed (R shoulder)
       bulletPrevWeapon()    # If ACTION_PREV_WPN pressed (L shoulder)
       bulletUpdateAll()     # Move bullets, despawn, tick cooldown
       enemyUpdateAll()      # AI movement, firing, despawn, blink
       spriteUpdateAll()     # Advance all sprite animations
       spriteRenderAll()     # Write all active sprites to OAM
       bulletRenderAll()     # Write bullet OAM (after spriteRenderAll)
       enemyRenderAll()      # Write enemy OAM (after spriteRenderAll)
       vblankProcessCallbacks()
```

---

## Phase 8 Implementation Details

### Files Created
- `include/engine/bullets.h` - Bullet system header (Bullet struct, WeaponState, pool constants, full API)
- `src/engine/bullets.c` - Bullet pool, weapon types, spawning, movement, OAM rendering

### Files Modified
- `include/config.h` - Added `PAL_OBJ_EBULLET` (11), shifted `PAL_OBJ_ITEMS` to 12
- `src/main.c` - Integrated `bulletInit()`, `bulletLoadGraphics()`, firing/cycling/update/render
- `linkfile` - Added `src/engine/bullets.obj`

### Bullet System Architecture
```
bulletInit()            - Clear pool, assign OAM slots, init weapon to SINGLE
bulletLoadGraphics()    - DMA bullet tiles + palettes to OBJ VRAM (force blank)
bulletPlayerFire(x,y)   - Spawn bullets based on weapon type, respects cooldown
bulletEnemyFireDown(x,y)     - Spawn straight-down enemy bullet
bulletEnemyFire(x,y,tx,ty,t) - Spawn aimed enemy bullet toward target
bulletUpdateAll()       - Move all bullets, despawn off-screen, tick cooldown
bulletRenderAll()       - Write all bullet slots to OAM (after spriteRenderAll)
bulletClearAll()        - Deactivate all bullets (scene transitions)
bulletNextWeapon()      - Cycle weapon forward (R shoulder)
bulletPrevWeapon()      - Cycle weapon backward (L shoulder)
bulletGetPool()         - Return pool pointer for collision checks (Phase 10)
```

### Player Weapon Types
```
WEAPON_SINGLE (Y button):
  - 1 bullet straight up, vy = -4.0 px/f (0xFC00)
  - Fire rate: 8 frames (~7.5 shots/sec)
  - Damage: 10

WEAPON_SPREAD (Y button):
  - 3 bullets in fan: center + left-angled + right-angled
  - vy = -3.0 px/f (0xFD00), vx = ±1.0 px/f (0x0100)
  - Fire rate: 12 frames (~5 shots/sec)
  - Damage: 6 per bullet

WEAPON_LASER (Y button):
  - 1 bullet straight up, vy = -2.0 px/f (0xFE00)
  - Fire rate: 16 frames (~3.75 shots/sec)
  - Damage: 25
```

### Enemy Bullet Types (used by Phase 9)
```
BULLET_TYPE_ENEMY_BASIC: vy = +2.0 px/f (0x0200) straight down
BULLET_TYPE_ENEMY_AIMED: speed = 1.5 px/f (0x0180) toward player
```

### Bullet Pool Layout
```
Pool indices 0-15:  Player bullets (16 max)
  OAM slots 4-19   (oam_id = (OAM_BULLETS + i) * 4)

Pool indices 16-23: Enemy bullets (8 max)
  OAM slots 40-47  (oam_id = (OAM_EBULLETS + (i-16)) * 4)

Total: 24 bullets max on screen
```

---

## Phase 7 Implementation Details

### Files Created
- `include/engine/scroll.h` - Scroll engine header (speed presets, ScrollTrigger struct, full API)
- `src/engine/scroll.c` - 8.8 fixed-point scrolling, parallax, speed transitions, trigger system

### Files Modified
- `include/config.h` - Added `SCROLL_SPEED_STOP` (0x0000) and `SCROLL_SPEED_RUSH` FP8(2.0)
- `src/main.c` - Integrated `scrollInit()`, `scrollSetSpeed()`, `scrollUpdate()`, `scrollVBlankUpdate()`
- `linkfile` - Added `src/engine/scroll.obj`

### Scroll Engine Architecture
```
scrollInit()           - Zero all state, position, speed, triggers
scrollSetSpeed(spd)    - Set speed immediately (8.8 fixed-point)
scrollTransitionSpeed(target, frames) - Smooth accel/decel over N frames
scrollUpdate()         - Per-frame: advance position, check triggers
scrollVBlankUpdate()   - VBlank: write bgSetScroll() to hardware registers
scrollGetY()           - Current BG1 scroll Y (integer pixels, wraps at 256)
scrollGetDistance()     - Total distance scrolled (integer pixels, no wrap)
scrollAddTrigger()     - Register callback at distance threshold
scrollClearTriggers()  - Remove all triggers
scrollResetTriggers()  - Reset fired flags for zone restart
```

### Scroll Speed Presets (8.8 fixed-point)
```
SCROLL_SPEED_STOP    = 0x0000  (0.0 px/frame)
SCROLL_SPEED_SLOW    = 0x0040  (0.25 px/frame = 15 px/sec)
SCROLL_SPEED_NORMAL  = 0x0080  (0.5 px/frame = 30 px/sec)
SCROLL_SPEED_FAST    = 0x0100  (1.0 px/frame = 60 px/sec)
SCROLL_SPEED_RUSH    = 0x0200  (2.0 px/frame = 120 px/sec)
```

### Parallax Effect
- BG1 (main background): Scrolls at full speed
- BG2 (star dots): Scrolls at half speed (parallax_y += speed >> 1)
- BG3 (text/HUD): Fixed, no scroll
- 32x32 tilemap wraps seamlessly at 256 pixels vertically

### Trigger System
- Up to 16 triggers per zone (`MAX_SCROLL_TRIGGERS` in config.h)
- Each trigger: distance threshold (pixels) + callback function + fired flag
- Triggers fire exactly once when `scrollGetDistance() >= trigger.distance`
- Used by Phase 9 (enemy spawning) and Phase 18 (zone progression)

---

## Phase 6 Implementation Details

### Files Created
- `include/engine/input.h` - Action-mapped input system header (12 ACTION_ flags)
- `src/engine/input.c` - Controller reading, button-to-action mapping, edge detection

### Files Modified
- `include/game/player.h` - Added movement constants, screen bounds, `playerHandleInput()`, `bank_timer`
- `src/game/player.c` - Added `playerHandleInput(u16 held)` with D-pad movement, banking, slow mode
- `src/main.c` - Integrated `inputInit()`, `inputUpdate()`, `playerHandleInput(inputHeld())`
- `linkfile` - Added `src/engine/input.obj`

### Button-to-Action Mapping
```
D-Pad       -> ACTION_UP/DOWN/LEFT/RIGHT  (movement)
Y           -> ACTION_FIRE                (shoot)
B           -> ACTION_SLOW                (focus/slow movement)
A           -> ACTION_CONFIRM             (confirm - battles)
X           -> ACTION_MENU                (open menu)
Start       -> ACTION_PAUSE               (pause game)
Select      -> ACTION_CANCEL              (cancel/back)
L/R         -> ACTION_PREV_WPN/NEXT_WPN   (weapon cycle)
```

---

## Phase 5 Implementation Details

### Files Created
- `include/engine/sprites.h` - Sprite pool manager (SpriteEntity struct, 48-entry pool)
- `src/engine/sprites.c` - Pool init/alloc/free, animation update, OAM rendering, DMA helpers
- `include/game/player.h` - PlayerShip struct with sprite reference, position, animation state
- `src/game/player.c` - Player init (loads graphics), update (invincibility blink), banking (hflip)

### Key Constants (from config.h)
```
VRAM_OBJ_GFX = 0x4000          # OBJ tile base
OAM_PLAYER = 0 (slots 0-3)     # Player OAM reservation
PAL_OBJ_PLAYER = 8             # Player palette (CGRAM 128-143)
OBJ_SIZE16_L32                  # Small=16x16, Large=32x32
MAX_GAME_SPRITES = 48           # Sprite pool size
```

---

## Current ROM Stats (Post Phase 20 - FINAL)
```
ROM: 69,538 bytes (26.53%) free of 262,144
  Bank 0:      8 bytes (0.02%) free  # FULL - no more code here
  Bank 1:      2 bytes (0.01%) free  # FULL - code overflow
  Bank 2:      1 byte  (0.00%) free  # FULL - assets
  Bank 3:      1 byte  (0.00%) free  # FULL - assets
  Bank 4:      0 bytes (0.00%) free  # FULL
  Bank 5:  3,990 bytes (12.18%) free # Phase 20 save/menu code here
  Banks 6-7: 100% free (reserved for future content)

RAM: 150,879 bytes (92.09%) free of 163,840
  Slot 0 (Bank 126): 25,129 bytes (76.69%) free  # Main WRAM
  Slot 1 (Bank 0):    7,702 bytes (94.02%) free  # Direct page
  Slot 2 (Bank 126): 53,143 bytes (92.67%) free  # Extended WRAM
  Slot 3 (Bank 127): 64,905 bytes (99.04%) free  # Upper WRAM
```

### Bank 0 Full (RESOLVED)
Bank 0 is effectively full (13 bytes free). WLA-DX linker auto-overflows new
code to Bank 1 via `-A -c` flags configured in snes_rules. No manual bank
switching needed. battle.c was automatically placed in Bank 1.
Cross-bank function calls use JSL (65816 long jump), handled transparently
by 816-tcc compiler. Future phases will continue to overflow to Bank 1.

---

## OBJ Palette Allocation (Post Phase 9)
```
Slot 0 (PAL_OBJ_PLAYER=8):  Player ship   (CGRAM 128-143)
Slot 1 (PAL_OBJ_ENEMY=9):   Enemy sprites (CGRAM 144-159)
Slot 2 (PAL_OBJ_BULLET=10): Player bullets (CGRAM 160-175)
Slot 3 (PAL_OBJ_EBULLET=11): Enemy bullets (CGRAM 176-191)
Slot 4 (PAL_OBJ_ITEMS=12):  Items/pickups (CGRAM 192-207)
```

---

## Asset Pipeline
Two-stage conversion in Makefile:
1. **Python** (`tools/convert_sprite.py`, `tools/convert_background.py`): Resize high-res PNGs to SNES dimensions
2. **gfx4snes**: Convert indexed PNG to `.pic` (tiles), `.pal` (palette), `.map` (tilemap)

Source assets referenced from `G:/2024-unity/0-GameAssets/shooter/`.

### gfx4snes Sprite Flags
```
-s 8 -o 16 -u 16 -p -R -i <file>
  -s 8:  8x8 tile size
  -o 16: 4bpp (16 colors)
  -u 16: 16-tile wide character map (SNES OBJ format)
  -p:    generate palette file
  -R:    no tile reduction (keep all tiles)
```

### Available Assets (in data.asm)
- `player_ship_til/pal` - Player ship 32x32
- `enemy_scout_til/pal` - Enemy scout 32x32
- `enemy_fighter_til/pal` - Enemy fighter 32x32 (Phase 18)
- `enemy_heavy_til/pal` - Enemy heavy 32x32 (Phase 18)
- `enemy_elite_til/pal` - Enemy elite 32x32 (Phase 18)
- `bullet_player_til/pal` - Player bullet 16x16
- `bullet_enemy_til/pal` - Enemy bullet 16x16
- `zone1_bg_til/pal/map` - Zone 1 background 256x256 (Debris Field)
- `zone2_bg_til/pal/map` - Zone 2 background 256x256 (Asteroid Belt, Phase 18)
- `zone3_bg_til/pal/map` - Zone 3 background 256x256 (Flagship, Phase 18)
- `star_tiles/star_pal` - Procedural star tiles (4 tiles, hand-coded in ASM)
- `sfx_*` - 9 BRR sound effects (Phase 17)

---

## Complete File Manifest (Phase 20 - FINAL)
```
src/
  main.c                    # Entry point, boot sequence, state machine main loop, play time tracking
  engine/
    system.c                # Hardware init (Mode 1 PPU, OAM, VRAM)
    background.c            # BG1 zone loading, BG2 star parallax + twinkle
    fade.c                  # 8.8 fixed-point brightness fading
    vblank.c                # Post-VBlank callback framework
    sprites.c               # Sprite entity pool, OAM rendering, DMA helpers
    input.c                 # Controller reading, action mapping, edge detect
    scroll.c                # Vertical scrolling, parallax, speed transitions, triggers
    bullets.c               # Bullet pool, weapon types, spawning, movement, OAM render
    collision.c             # AABB collision, battle triggers for FIGHTER+ contacts
    sound.c                 # SPC700 sound engine, BRR sample playback
  game/
    player.c                # Player ship: graphics, movement, banking, blink
    enemies.c               # Enemy pool, AI patterns, spawning, firing, OAM render
    battle.c                # Turn-based battle engine (Bank 1 overflow)
    battle_ui.c             # Battle UI: HP bars, sprites, shake, text drawing (Bank 1)
    boss.c                  # Boss battle system: 3 bosses, multi-phase AI, special attacks (Bank 5)
    rpg_stats.c             # RPG stats: XP, leveling, growth table, defeat penalty (Bank 1)
    inventory.c             # Inventory: item defs, add/remove, loot table (Bank 1)
    game_state.c            # Game state machine: title, flight, game over, victory, zone transitions (Bank 1)
    story.c                 # Story scripts and dialog triggers for all 3 zones (Bank 1)
    save.c                  # SRAM save/load with XOR checksum validation (Phase 20)
    dialog.c                # Dialog engine: typewriter text, BG3 transitions

include/
  config.h                  # Master config (VRAM, palettes, OAM slots, FP8, scroll speeds)
  game.h                    # GameState struct (with paused/frame_counter/play_time), state defines, g_game extern
  assets.h                  # Extern labels for data.asm assets
  engine/
    system.h                # systemInit, systemResetVideo, systemWaitFrames
    background.h            # bgSystemInit, bgLoadZone, bgUpdate, bgVBlankUpdate
    fade.h                  # fadeIn/Out (blocking + non-blocking)
    vblank.h                # VBlankCallback registration
    sprites.h               # SpriteEntity struct, pool API, load helpers
    input.h                 # ACTION_ flags, inputInit/Update/Held/Pressed
    scroll.h                # ScrollTrigger, scroll speed/position/trigger API
    bullets.h               # Bullet struct, WeaponState, pool API, weapon cycling
    collision.h             # Hitbox struct, AABB check, collisionCheckAll, g_score
    sound.h                 # SFX/music IDs, sound system API
  game/
    player.h                # PlayerShip struct, movement constants, API
    enemies.h               # Enemy struct, EnemyTypeDef, AI defines, enemy API
    battle.h                # BattleContext (with is_boss/boss_zone), BattleCombatant, states/actions, battle API
    battle_ui.h             # Battle UI defines, sprite positions, HP bar, shake API
    boss.h                  # BossTypeDef, BossState, boss trigger macros, boss AI/attack API
    rpg_stats.h             # PlayerRPGStats struct, level cap, base stats, RPG API
    inventory.h             # InvSlot struct, item IDs, inventory limits, inventory API
    game_state.h            # State machine API: gsInit, gsTitle/Flight/GameOver/Victory, gsZoneAdvance, gsPause
    dialog.h                # Dialog system + story API
    save.h                  # SaveData struct, SRAM save/load/erase API (Phase 20)

data.asm                    # Asset includes (.incbin for all sprites/BGs/SFX)
hdr.asm                     # ROM header (LoROM, 256KB, ROM+SRAM)
linkfile                    # Linker object list (22 game objects + 4 PVSnesLib libs)
Makefile                    # Build system (Python + gfx4snes + PVSnesLib)
```

---

## ALL 20 PHASES COMPLETE
VEX DEFENDER v1.0 is feature-complete. The game includes:
- 3-zone vertical shooter campaign with unique backgrounds and enemy types
- Turn-based RPG battle system with XP, leveling, items, and boss fights
- Story dialog system with typewriter text and narrative arc
- SRAM save system with auto-save and checksum validation
- 9 BRR sound effects via SPC700
- 192,606 bytes of ROM used (73.47% of 256KB)

---

## Documentation Reference
Full specs for all 20 phases in `docs/v1_docs/`. Two naming conventions exist:
- `phase_NN_name.md` (underscore style)
- `phase-NN-name.md` (hyphen style)
Both contain valid specs; the hyphen-style files tend to be more detailed with full code listings.
