# Phase 17: Sound Engine & Music Integration

## Objective
Integrate the SNES SPC700 sound processor with background music and sound effects. Convert WAV sound effects to BRR format and IT tracker music to SPC format using smconv. Create a sound manager that plays music per zone and triggers SFX for gameplay events.

## Prerequisites
- Phase 15 (Game State Machine) complete - states defined for music switching
- Sound source files available at `C:/Users/Ryan Rentfro/Downloads/RawSounds/`

## Detailed Tasks

### 1. Convert Sound Effects to BRR Format
Convert WAV files to SNES BRR (Bit Rate Reduction) format using snesbrr.

### 2. Create/Source Background Music
Create or source IT (Impulse Tracker) module files for each game zone.

### 3. Convert Music to SPC Format
Use smconv to convert IT modules to SPC700 soundbank format.

### 4. Create Sound Manager Module
Abstraction layer for playing music and SFX with proper channel management.

### 5. Integrate Sound Triggers Throughout Game

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/sound.h` | CREATE | Sound system header |
| `src/sound.c` | CREATE | Sound system implementation |
| `assets/sfx/` | CREATE | Converted BRR sound effects |
| `assets/music/` | CREATE | IT module files |
| `data/data.asm` | MODIFY | Add sound data includes |
| `Makefile` | MODIFY | Add sound conversion rules, sound.obj |

## Technical Specifications

### Available Sound Files (Source)
```
From C:/Users/Ryan Rentfro/Downloads/RawSounds/:
  Bleep.wav        → SFX_MENU_SELECT (menu cursor move)
  explode.wav      → SFX_EXPLOSION (enemy death)
  Exploder-01.wav  → SFX_BIG_EXPLOSION (boss death)
  ExploderDelay.wav→ SFX_EXPLOSION_DELAY (delayed chain explosion)
  Hit01.wav        → SFX_HIT (bullet hit enemy)
  HitBurn.wav      → SFX_CRITICAL_HIT (critical attack in battle)
  ShipHit.wav      → SFX_PLAYER_HIT (player takes damage)
  Test0001.wav     → SFX_POWERUP (item collect / level up)
  Untitled.wav     → SFX_AMBIENT (background ambience, if suitable)
  Zap.wav          → SFX_PLAYER_SHOOT (player basic shot)
  Zap01.wav        → SFX_SPECIAL_ATTACK (player special attack)
  Zap02.wav        → SFX_ENEMY_SHOOT (enemy fires)
```

### Sound Effect Conversion Pipeline
```batch
REM BRR conversion: WAV (44.1kHz) -> WAV (16kHz mono) -> BRR
REM snesbrr encodes WAV to SNES BRR format

SET SNESBRR=J:\code\snes\snes-build-tools\tools\pvsneslib\devkitsnes\tools\snesbrr.exe

REM Convert each WAV to BRR
%SNESBRR% -e assets/sfx/menu_select.wav assets/sfx/menu_select.brr
%SNESBRR% -e assets/sfx/explosion.wav assets/sfx/explosion.brr
%SNESBRR% -e assets/sfx/hit.wav assets/sfx/hit.brr
%SNESBRR% -e assets/sfx/player_hit.wav assets/sfx/player_hit.brr
%SNESBRR% -e assets/sfx/player_shoot.wav assets/sfx/player_shoot.brr
%SNESBRR% -e assets/sfx/special_attack.wav assets/sfx/special_attack.brr
%SNESBRR% -e assets/sfx/enemy_shoot.wav assets/sfx/enemy_shoot.brr
%SNESBRR% -e assets/sfx/powerup.wav assets/sfx/powerup.brr
%SNESBRR% -e assets/sfx/critical_hit.wav assets/sfx/critical_hit.brr
%SNESBRR% -e assets/sfx/big_explosion.wav assets/sfx/big_explosion.brr
```

### Music Conversion Pipeline
```batch
REM Music conversion: IT module -> SPC soundbank
REM smconv creates soundbank.asm, soundbank.h, soundbank.bnk

SET SMCONV=J:\code\snes\snes-build-tools\tools\pvsneslib\devkitsnes\tools\smconv.exe

REM Convert IT modules to soundbank
%SMCONV% -s -o assets/music/soundbank assets/music/game_music.it
REM Outputs: soundbank.asm, soundbank.h, soundbank.bnk
```

### SFX ID Definitions
```c
/* Sound effect IDs (must match soundbank order) */
#define SFX_MENU_SELECT     0
#define SFX_PLAYER_SHOOT    1
#define SFX_SPECIAL_ATTACK  2
#define SFX_ENEMY_SHOOT     3
#define SFX_HIT             4
#define SFX_CRITICAL_HIT    5
#define SFX_PLAYER_HIT      6
#define SFX_EXPLOSION        7
#define SFX_BIG_EXPLOSION   8
#define SFX_POWERUP          9
#define SFX_COUNT           10

/* Music track IDs */
#define MUSIC_TITLE          0
#define MUSIC_ZONE1          1
#define MUSIC_ZONE2          2
#define MUSIC_ZONE3          3
#define MUSIC_BOSS           4
#define MUSIC_BATTLE         5
#define MUSIC_VICTORY        6
#define MUSIC_GAMEOVER       7
#define MUSIC_NONE          255
```

### sound.h
```c
#ifndef SOUND_H
#define SOUND_H

#include <snes.h>
#include "config.h"

/* SFX and Music IDs as defined above */

typedef struct {
    u8 current_music;      /* Currently playing music track */
    u8 music_volume;       /* Master music volume (0-127) */
    u8 sfx_volume;         /* Master SFX volume (0-127) */
    u8 music_enabled;      /* Music on/off toggle */
    u8 sfx_enabled;        /* SFX on/off toggle */
} SoundState;

extern SoundState g_sound;

/*--- Functions ---*/
void sound_init(void);
void sound_play_music(u8 track_id);
void sound_stop_music(void);
void sound_pause_music(void);
void sound_resume_music(void);
void sound_play_sfx(u8 sfx_id);
void sound_set_music_volume(u8 vol);
void sound_update(void);

#endif /* SOUND_H */
```

### sound.c
```c
#include "sound.h"
#include "soundbank.h"  /* Generated by smconv */

SoundState g_sound;

/* Soundbank data (from ASM includes) */
extern char SOUNDBANK__0, SOUNDBANK__0_END;

void sound_init(void) {
    g_sound.current_music = MUSIC_NONE;
    g_sound.music_volume = 100;
    g_sound.sfx_volume = 127;
    g_sound.music_enabled = 1;
    g_sound.sfx_enabled = 1;

    /* Initialize SPC700 sound driver */
    spcBoot();

    /* Load soundbank */
    spcLoad(0);
}

void sound_play_music(u8 track_id) {
    if (!g_sound.music_enabled) return;
    if (track_id == g_sound.current_music) return; /* Already playing */

    spcStop();
    g_sound.current_music = track_id;

    if (track_id == MUSIC_NONE) return;

    /* spcPlay expects the module index from soundbank */
    spcPlay(track_id);
    spcSetModuleVolume(g_sound.music_volume);
}

void sound_stop_music(void) {
    spcStop();
    g_sound.current_music = MUSIC_NONE;
}

void sound_pause_music(void) {
    spcStop();
}

void sound_resume_music(void) {
    if (g_sound.current_music != MUSIC_NONE) {
        spcPlay(g_sound.current_music);
    }
}

void sound_play_sfx(u8 sfx_id) {
    if (!g_sound.sfx_enabled) return;

    /* Play SFX on effect channel */
    spcPlaySound(sfx_id);
}

void sound_set_music_volume(u8 vol) {
    g_sound.music_volume = vol;
    spcSetModuleVolume(vol);
}

void sound_update(void) {
    /* Call SPC process each frame to keep driver running */
    spcProcess();
}
```

### Sound Integration Points (across all modules)
```c
/* In game.c - state changes trigger music: */
case STATE_TITLE:    sound_play_music(MUSIC_TITLE);    break;
case STATE_FLIGHT:
    switch(g_game.current_zone) {
        case ZONE_DEBRIS:  sound_play_music(MUSIC_ZONE1); break;
        case ZONE_ASTEROID:sound_play_music(MUSIC_ZONE2); break;
        case ZONE_FLAGSHIP:sound_play_music(MUSIC_ZONE3); break;
    }
    break;
case STATE_BATTLE:   sound_play_music(MUSIC_BATTLE);   break;
case STATE_GAMEOVER: sound_play_music(MUSIC_GAMEOVER);  break;
case STATE_VICTORY:  sound_play_music(MUSIC_VICTORY);   break;

/* In bullet.c - shooting sounds: */
/* bullet_spawn_player_basic(): */ sound_play_sfx(SFX_PLAYER_SHOOT);
/* bullet_spawn_player_spread():*/ sound_play_sfx(SFX_SPECIAL_ATTACK);
/* bullet_spawn_enemy_basic():  */ sound_play_sfx(SFX_ENEMY_SHOOT);

/* In collision.c - hit sounds: */
/* Player bullet hits enemy: */    sound_play_sfx(SFX_HIT);
/* Enemy bullet hits player: */    sound_play_sfx(SFX_PLAYER_HIT);

/* In enemy.c - death sounds: */
/* enemy_kill(): */                sound_play_sfx(SFX_EXPLOSION);

/* In items.c - pickup sounds: */
/* Item collected: */              sound_play_sfx(SFX_POWERUP);

/* In ui.c - menu sounds: */
/* Cursor move: */                 sound_play_sfx(SFX_MENU_SELECT);

/* In stats.c - level up: */
/* Level up: */                    sound_play_sfx(SFX_POWERUP);
```

### IT Module Creation Notes
If no IT music modules are available, create a minimal IT file:
- Use OpenMPT (free tracker software) to create simple 4-channel music
- SNES SPC700 supports 8 channels, 16-bit BRR samples
- Keep IT modules under 32KB for SNES memory
- Use simple waveforms (square, triangle, sawtooth) for retro feel
- BPM: 120-140 for action zones, 80-100 for title/story

### Alternative: Music-Free Approach
If IT modules are not available, the game can ship with SFX only:
```c
void sound_play_music(u8 track_id) {
    /* Music disabled - SFX only build */
    g_sound.current_music = track_id;
    /* Do nothing - no music tracks loaded */
}
```

## Acceptance Criteria
1. Sound system initializes without crashing
2. Sound effects play when gameplay events trigger them
3. Different music plays for each game state (title, zones, battle)
4. Music changes when transitioning between zones
5. SFX does not cut off music (plays on separate channels)
6. `sound_stop_music()` silences music immediately
7. Volume control works (music volume adjustable)
8. Game runs fine with music disabled (graceful fallback)
9. No audio pops or clicks during SFX playback
10. SPC700 driver doesn't crash or cause main CPU stalls

## SNES-Specific Constraints
- SPC700 is a separate processor - communication via port registers
- `spcProcess()` MUST be called every frame to keep audio running
- SPC700 has 64KB dedicated RAM for samples and driver code
- BRR samples: 9 bytes encode 16 samples (4:1 compression ratio)
- Max 8 simultaneous sound channels
- Soundbank must fit in SPC700 64KB RAM
- smconv output format: .asm (assembly), .bnk (bank data), .h (C header)

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~80KB | 256KB    | ~176KB    |
| WRAM     | ~1.2KB| 128KB   | ~127KB    |
| VRAM     | ~14KB | 64KB    | ~50KB     |
| CGRAM    | 192B  | 512B    | 320B      |
| SPC RAM  | ~32KB | 64KB    | ~32KB     |

## Estimated Complexity
**Medium-Complex** - Sound conversion toolchain can be finicky. BRR encoding requires specific WAV input format. IT module creation requires music knowledge. However, the API (spcPlay/spcPlaySound) is simple.

## Agent Instructions
1. First, copy WAV files from `C:/Users/Ryan Rentfro/Downloads/RawSounds/` to `assets/sfx/`
2. Rename WAVs to match the SFX mapping (e.g., Zap.wav → player_shoot.wav)
3. Convert WAVs to BRR using snesbrr (may need to resample to 16kHz mono first)
4. If IT modules are available, convert with smconv
5. If no IT modules, use the music-free approach (SFX only)
6. Create `src/sound.h` and `src/sound.c`
7. Update data.asm with sound data includes
8. Add `sound_init()` to game_init() and `sound_update()` to main loop
9. Add sound_play_sfx() calls throughout all modules (see integration points)
10. Add sound_play_music() calls in game_change_state()
11. Test: verify SFX plays on shooting, hitting enemies, taking damage
12. Test: verify music changes between zones
13. **If snesbrr fails**: ensure WAV is 16-bit, mono, 16000Hz sample rate
