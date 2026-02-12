# Phase 17: Sound Engine & Music Integration

## Objective
Implement the sound system using the SNES SPC700 audio processor via PVSnesLib's spcPlay/spcEffect APIs. Load and play background music tracks for each game scene (title, flight zones, battle, victory, game over) and trigger sound effects for gameplay events (shooting, explosions, hits, menu navigation, dialog beeps). Music is authored as IT (Impulse Tracker) module files and converted with smconv. Sound effects are BRR-encoded samples triggered on dedicated SPC channels.

## Prerequisites
- Phase 2 (System init, VBlank framework), Phase 15 (State machine handles scene transitions where music changes).

## Detailed Tasks

1. Create `src/engine/sound.c` - Sound manager that wraps PVSnesLib's SPC700 API with game-level concepts (play music by scene ID, trigger SFX by event ID).
2. Create placeholder IT module files for 5 music tracks: title theme, zone 1 flight, zone 2 flight, battle theme, victory fanfare. These can be short loops (8-16 bars).
3. Create placeholder BRR samples for 8 sound effects: player shoot, enemy shoot, explosion, hit damage, menu select, menu move, dialog blip, level up.
4. Add smconv conversion rules to the Makefile to produce soundbank data from IT files.
5. Add snesbrr conversion rules for BRR sound effect samples.
6. Implement music state management: track which music is playing, handle cross-fade or instant swap on scene transitions.
7. Implement SFX priority system: up to 4 simultaneous SFX, newer sounds preempt older ones when channels are full.
8. Wire sound triggers into existing game systems: bullet fire, enemy death, battle actions, menu navigation, dialog typewriter.
9. Call spcProcess() in the main loop to keep the SPC700 driver running.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/engine/sound.h
```c
#ifndef SOUND_H
#define SOUND_H

#include "game.h"

/* Music track IDs */
#define MUSIC_NONE          0
#define MUSIC_TITLE         1   /* Title screen theme */
#define MUSIC_FLIGHT_ZONE1  2   /* Zone 1: Debris Field */
#define MUSIC_FLIGHT_ZONE2  3   /* Zone 2: Asteroid Belt */
#define MUSIC_FLIGHT_ZONE3  4   /* Zone 3: Flagship Approach */
#define MUSIC_BATTLE        5   /* Turn-based battle */
#define MUSIC_BOSS          6   /* Boss encounter */
#define MUSIC_VICTORY       7   /* Victory fanfare */
#define MUSIC_GAME_OVER     8   /* Game over */
#define MUSIC_COUNT         9

/* Sound effect IDs */
#define SFX_NONE            0
#define SFX_PLAYER_SHOOT    1   /* Player bullet fired */
#define SFX_ENEMY_SHOOT     2   /* Enemy bullet fired */
#define SFX_EXPLOSION       3   /* Enemy destroyed */
#define SFX_HIT_DAMAGE      4   /* Player/enemy takes damage */
#define SFX_MENU_SELECT     5   /* Menu confirm (A button) */
#define SFX_MENU_MOVE       6   /* Menu cursor move */
#define SFX_DIALOG_BLIP     7   /* Typewriter text character */
#define SFX_LEVEL_UP        8   /* Level up jingle */
#define SFX_ITEM_GET        9   /* Item pickup */
#define SFX_BATTLE_START    10  /* Battle transition */
#define SFX_HEAL            11  /* HP/SP restore */
#define SFX_COUNT           12

/* Initialize the sound system (must be called after consoleInit) */
void soundInit(void);

/* Play a music track. Stops any currently playing music.
 * If trackId is the same as current, does nothing (no restart). */
void soundPlayMusic(u8 trackId);

/* Stop the current music */
void soundStopMusic(void);

/* Pause/resume the current music (for pause menu) */
void soundPauseMusic(void);
void soundResumeMusic(void);

/* Fade out current music over N frames, then stop */
void soundFadeMusicOut(u8 frames);

/* Play a sound effect. Automatically picks an available channel. */
void soundPlaySFX(u8 sfxId);

/* Update sound system - call once per frame in main loop.
 * Handles fade-outs and calls spcProcess(). */
void soundUpdate(void);

/* Set music volume (0-15) */
void soundSetMusicVolume(u8 vol);

/* Set SFX volume (0-15) */
void soundSetSFXVolume(u8 vol);

/* Get currently playing music track ID */
u8 soundGetCurrentMusic(void);

/* Is music currently playing? */
u8 soundIsMusicPlaying(void);

#endif
```

### J:/code/snes/snes-rpg-test/src/engine/sound.c
```c
/*==============================================================================
 * Sound Engine
 *
 * Wraps PVSnesLib's SPC700 API for game-level music and SFX management.
 *
 * SPC700 has 8 channels (0-7):
 *   Channels 0-3: Music (IT module uses these)
 *   Channels 4-7: Sound effects (SFX round-robin on these)
 *
 * Music is loaded via spcLoad() from soundbank data produced by smconv.
 * SFX are triggered via spcEffect() on the next available SFX channel.
 *============================================================================*/

#include "engine/sound.h"

/* Soundbank data (produced by smconv, included in data.asm) */
extern char soundbank, soundbank_end;

/* BRR sample data for SFX */
extern char sfx_player_shoot, sfx_player_shoot_end;
extern char sfx_explosion, sfx_explosion_end;
extern char sfx_hit, sfx_hit_end;
extern char sfx_menu_select, sfx_menu_select_end;
extern char sfx_menu_move, sfx_menu_move_end;
extern char sfx_dialog_blip, sfx_dialog_blip_end;
extern char sfx_level_up, sfx_level_up_end;
extern char sfx_heal, sfx_heal_end;

/* State */
static u8 current_music;       /* Currently playing MUSIC_* ID */
static u8 music_playing;       /* 1 if music is active */
static u8 music_volume;        /* 0-15 */
static u8 sfx_volume;          /* 0-15 */
static u8 sfx_channel;         /* Round-robin channel (4-7) */

/* Fade state */
static u8 fade_active;
static u8 fade_timer;
static u8 fade_frames;
static u8 fade_start_vol;

/* Music module indices in the soundbank (order must match smconv input) */
/* These correspond to the order of IT files passed to smconv */
#define MOD_TITLE       0
#define MOD_ZONE1       1
#define MOD_ZONE2       2
#define MOD_ZONE3       3
#define MOD_BATTLE      4
#define MOD_BOSS        5
#define MOD_VICTORY     6
#define MOD_GAMEOVER    7

/* Map MUSIC_* IDs to soundbank module indices */
static const u8 music_to_module[MUSIC_COUNT] = {
    0xFF,           /* MUSIC_NONE */
    MOD_TITLE,
    MOD_ZONE1,
    MOD_ZONE2,
    MOD_ZONE3,
    MOD_BATTLE,
    MOD_BOSS,
    MOD_VICTORY,
    MOD_GAMEOVER,
};

/* SFX sample table: maps SFX_* IDs to BRR sample addresses and sizes */
/* This is populated during soundInit */
typedef struct {
    u8 *data;
    u16 size;
    u16 pitch;      /* Playback pitch (4096 = 1.0x) */
} SFXDef;

static SFXDef sfx_table[SFX_COUNT];

void soundInit(void)
{
    current_music = MUSIC_NONE;
    music_playing = 0;
    music_volume = 15;
    sfx_volume = 15;
    sfx_channel = 4;
    fade_active = 0;

    /* Initialize SPC700 */
    spcBoot();

    /* Set the soundbank ROM bank */
    spcSetBank(&soundbank);

    /* Populate SFX table */
    sfx_table[SFX_NONE].data = (u8 *)0;
    sfx_table[SFX_NONE].size = 0;
    sfx_table[SFX_NONE].pitch = 4096;

    sfx_table[SFX_PLAYER_SHOOT].data = (u8 *)&sfx_player_shoot;
    sfx_table[SFX_PLAYER_SHOOT].size =
        (u16)(&sfx_player_shoot_end - &sfx_player_shoot);
    sfx_table[SFX_PLAYER_SHOOT].pitch = 4096;

    sfx_table[SFX_ENEMY_SHOOT].data = (u8 *)&sfx_player_shoot;
    sfx_table[SFX_ENEMY_SHOOT].size =
        (u16)(&sfx_player_shoot_end - &sfx_player_shoot);
    sfx_table[SFX_ENEMY_SHOOT].pitch = 3400; /* Lower pitch for enemy */

    sfx_table[SFX_EXPLOSION].data = (u8 *)&sfx_explosion;
    sfx_table[SFX_EXPLOSION].size =
        (u16)(&sfx_explosion_end - &sfx_explosion);
    sfx_table[SFX_EXPLOSION].pitch = 4096;

    sfx_table[SFX_HIT_DAMAGE].data = (u8 *)&sfx_hit;
    sfx_table[SFX_HIT_DAMAGE].size =
        (u16)(&sfx_hit_end - &sfx_hit);
    sfx_table[SFX_HIT_DAMAGE].pitch = 4096;

    sfx_table[SFX_MENU_SELECT].data = (u8 *)&sfx_menu_select;
    sfx_table[SFX_MENU_SELECT].size =
        (u16)(&sfx_menu_select_end - &sfx_menu_select);
    sfx_table[SFX_MENU_SELECT].pitch = 4096;

    sfx_table[SFX_MENU_MOVE].data = (u8 *)&sfx_menu_move;
    sfx_table[SFX_MENU_MOVE].size =
        (u16)(&sfx_menu_move_end - &sfx_menu_move);
    sfx_table[SFX_MENU_MOVE].pitch = 4096;

    sfx_table[SFX_DIALOG_BLIP].data = (u8 *)&sfx_dialog_blip;
    sfx_table[SFX_DIALOG_BLIP].size =
        (u16)(&sfx_dialog_blip_end - &sfx_dialog_blip);
    sfx_table[SFX_DIALOG_BLIP].pitch = 5000; /* Higher pitch for blip */

    sfx_table[SFX_LEVEL_UP].data = (u8 *)&sfx_level_up;
    sfx_table[SFX_LEVEL_UP].size =
        (u16)(&sfx_level_up_end - &sfx_level_up);
    sfx_table[SFX_LEVEL_UP].pitch = 4096;

    sfx_table[SFX_ITEM_GET].data = (u8 *)&sfx_level_up;
    sfx_table[SFX_ITEM_GET].size =
        (u16)(&sfx_level_up_end - &sfx_level_up);
    sfx_table[SFX_ITEM_GET].pitch = 4500;

    sfx_table[SFX_BATTLE_START].data = (u8 *)&sfx_explosion;
    sfx_table[SFX_BATTLE_START].size =
        (u16)(&sfx_explosion_end - &sfx_explosion);
    sfx_table[SFX_BATTLE_START].pitch = 3000; /* Low rumble */

    sfx_table[SFX_HEAL].data = (u8 *)&sfx_heal;
    sfx_table[SFX_HEAL].size =
        (u16)(&sfx_heal_end - &sfx_heal);
    sfx_table[SFX_HEAL].pitch = 4096;
}

void soundPlayMusic(u8 trackId)
{
    u8 modIndex;

    if (trackId == current_music && music_playing) return;
    if (trackId >= MUSIC_COUNT) return;

    /* Stop current music first */
    if (music_playing) {
        spcStop();
        music_playing = 0;
    }

    if (trackId == MUSIC_NONE) {
        current_music = MUSIC_NONE;
        return;
    }

    modIndex = music_to_module[trackId];
    if (modIndex == 0xFF) return;

    /* Load and play the module */
    spcLoad(modIndex);
    spcPlay(0);  /* Start from beginning */

    current_music = trackId;
    music_playing = 1;
    fade_active = 0;
}

void soundStopMusic(void)
{
    if (music_playing) {
        spcStop();
        music_playing = 0;
        current_music = MUSIC_NONE;
    }
    fade_active = 0;
}

void soundPauseMusic(void)
{
    /* SPC700 does not have a native pause.
     * Workaround: set volume to 0 */
    if (music_playing) {
        spcSetModuleVolume(0);
    }
}

void soundResumeMusic(void)
{
    if (music_playing) {
        spcSetModuleVolume(music_volume * 16); /* Scale 0-15 to 0-240 */
    }
}

void soundFadeMusicOut(u8 frames)
{
    if (!music_playing || frames == 0) {
        soundStopMusic();
        return;
    }
    fade_active = 1;
    fade_timer = 0;
    fade_frames = frames;
    fade_start_vol = music_volume;
}

void soundPlaySFX(u8 sfxId)
{
    SFXDef *sfx;

    if (sfxId == SFX_NONE || sfxId >= SFX_COUNT) return;

    sfx = &sfx_table[sfxId];
    if (!sfx->data || sfx->size == 0) return;

    /* Play on next available SFX channel (round-robin 4-7) */
    spcPlaySound(sfx_channel, sfx->pitch,
                 sfx_volume * 16,  /* Volume 0-240 */
                 sfx->data, sfx->size);

    /* Advance channel */
    sfx_channel++;
    if (sfx_channel > 7) sfx_channel = 4;
}

void soundUpdate(void)
{
    /* Handle music fade-out */
    if (fade_active) {
        fade_timer++;
        if (fade_timer >= fade_frames) {
            soundStopMusic();
        } else {
            /* Linearly interpolate volume */
            u8 vol = fade_start_vol -
                     (u8)((u16)fade_start_vol * fade_timer / fade_frames);
            spcSetModuleVolume(vol * 16);
        }
    }

    /* Keep SPC700 driver running */
    spcProcess();
}

void soundSetMusicVolume(u8 vol)
{
    if (vol > 15) vol = 15;
    music_volume = vol;
    if (music_playing && !fade_active) {
        spcSetModuleVolume(vol * 16);
    }
}

void soundSetSFXVolume(u8 vol)
{
    if (vol > 15) vol = 15;
    sfx_volume = vol;
}

u8 soundGetCurrentMusic(void)
{
    return current_music;
}

u8 soundIsMusicPlaying(void)
{
    return music_playing;
}
```

### Makefile Additions for Audio
```makefile
# ==============================================================================
# Phase 17: Audio Conversion Rules
# ==============================================================================

# IT module files for music
# Place source .it files in assets/music/
# smconv converts IT -> soundbank.asm + soundbank.h

MUSIC_FILES := assets/music/title.it \
               assets/music/zone1.it \
               assets/music/zone2.it \
               assets/music/zone3.it \
               assets/music/battle.it \
               assets/music/boss.it \
               assets/music/victory.it \
               assets/music/gameover.it

# BRR sound effect samples
# Place source .wav files in assets/sfx/
# snesbrr converts WAV -> BRR

SFX_WAV_FILES := assets/sfx/player_shoot.wav \
                 assets/sfx/explosion.wav \
                 assets/sfx/hit.wav \
                 assets/sfx/menu_select.wav \
                 assets/sfx/menu_move.wav \
                 assets/sfx/dialog_blip.wav \
                 assets/sfx/level_up.wav \
                 assets/sfx/heal.wav

# Soundbank conversion (IT -> soundbank assembly)
soundbank.asm: $(MUSIC_FILES)
	$(SMCONV) -s -o soundbank -v $(MUSIC_FILES)

# BRR conversion rules
assets/sfx/%.brr: assets/sfx/%.wav
	$(BRCONV) -e $< $@

# Collect all SFX BRR files
SFX_BRR_FILES := $(SFX_WAV_FILES:.wav=.brr)

audio: soundbank.asm $(SFX_BRR_FILES)
```

### data.asm Additions
```asm
;----------------------------------------------------------------------
; Soundbank (music module data, produced by smconv)
;----------------------------------------------------------------------
.section ".rodata_soundbank" superfree

soundbank:
.incbin "soundbank.brr"
soundbank_end:

.ends

;----------------------------------------------------------------------
; Sound Effects (BRR samples)
;----------------------------------------------------------------------
.section ".rodata_sfx" superfree

sfx_player_shoot:
.incbin "assets/sfx/player_shoot.brr"
sfx_player_shoot_end:

sfx_explosion:
.incbin "assets/sfx/explosion.brr"
sfx_explosion_end:

sfx_hit:
.incbin "assets/sfx/hit.brr"
sfx_hit_end:

sfx_menu_select:
.incbin "assets/sfx/menu_select.brr"
sfx_menu_select_end:

sfx_menu_move:
.incbin "assets/sfx/menu_move.brr"
sfx_menu_move_end:

sfx_dialog_blip:
.incbin "assets/sfx/dialog_blip.brr"
sfx_dialog_blip_end:

sfx_level_up:
.incbin "assets/sfx/level_up.brr"
sfx_level_up_end:

sfx_heal:
.incbin "assets/sfx/heal.brr"
sfx_heal_end:

.ends
```

### Integration Points (modifications to existing files)

```c
/* In game_state.c - enterTitle(): */
soundPlayMusic(MUSIC_TITLE);

/* In game_state.c - enterFlight(): */
switch (current_zone) {
    case 0: soundPlayMusic(MUSIC_FLIGHT_ZONE1); break;
    case 1: soundPlayMusic(MUSIC_FLIGHT_ZONE2); break;
    case 2: soundPlayMusic(MUSIC_FLIGHT_ZONE3); break;
}

/* In game_state.c - enterBattle(): */
soundPlayMusic(MUSIC_BATTLE);

/* In game_state.c - enterPause(): */
soundPauseMusic();

/* In game_state.c - exitPause(): */
soundResumeMusic();

/* In game_state.c - enterGameOver(): */
soundPlayMusic(MUSIC_GAME_OVER);

/* In game_state.c - enterVictory(): */
soundPlayMusic(MUSIC_VICTORY);

/* In bullets.c - bulletPlayerFire(): */
soundPlaySFX(SFX_PLAYER_SHOOT);

/* In enemies.c - enemy fires: */
soundPlaySFX(SFX_ENEMY_SHOOT);

/* In collision.c - enemy destroyed: */
soundPlaySFX(SFX_EXPLOSION);

/* In battle_engine.c - damage dealt: */
soundPlaySFX(SFX_HIT_DAMAGE);

/* In battle_ui.c - menu cursor move: */
soundPlaySFX(SFX_MENU_MOVE);

/* In battle_ui.c - menu confirm: */
soundPlaySFX(SFX_MENU_SELECT);

/* In dialog.c - typewriter character reveal: */
soundPlaySFX(SFX_DIALOG_BLIP);

/* In rpg_stats.c - level up: */
soundPlaySFX(SFX_LEVEL_UP);

/* In inventory.c - item pickup: */
soundPlaySFX(SFX_ITEM_GET);

/* In main.c - main loop: */
while (1) {
    inputUpdate();
    gameStateUpdate();
    soundUpdate();       /* <-- ADD THIS */
    WaitForVBlank();
}
```

## Technical Specifications

### SPC700 Architecture
```
The SNES SPC700 is an independent audio co-processor:
  - 64KB of dedicated audio RAM (ARAM)
  - 8 voice channels
  - Supports BRR-encoded samples (4-bit ADPCM, ~4:1 compression)
  - Independent timing from main CPU

PVSnesLib uses a driver (snesmod) that:
  - Occupies ~8KB of ARAM for the driver code
  - Leaves ~56KB for music modules and samples
  - Supports IT (Impulse Tracker) module format
  - Module data is streamed from ROM to ARAM via spcLoad()
  - spcProcess() must be called every frame to maintain playback
```

### Audio Memory Budget (ARAM)
```
Component             Size (approx)
---                   ---
snesmod driver        ~8KB
Title theme           ~4KB (short loop, few instruments)
Zone 1 music          ~6KB (more complex)
Zone 2 music          ~6KB
Zone 3 music          ~6KB
Battle music          ~5KB
Boss music            ~5KB
Victory fanfare       ~2KB
Game over             ~2KB
SFX samples (BRR)     ~6KB (8 effects at ~750 bytes each)
---                   ---
Total                 ~50KB / 56KB available

Note: Only ONE music module is loaded at a time.
The actual peak ARAM usage is:
  driver(8KB) + largest_module(6KB) + sfx(6KB) = 20KB
This leaves plenty of headroom.
```

### BRR Sample Specifications
```
BRR (Bit Rate Reduced) format:
  - 9 bytes per block (1 header + 8 data bytes)
  - Each block encodes 16 PCM samples
  - ~4:1 compression ratio from 16-bit PCM

Recommended source WAV specs:
  - 16-bit mono
  - 16000 Hz sample rate (will be pitch-shifted by SPC700)
  - Duration: 0.1-0.5 seconds for short SFX
  - Duration: 0.5-2.0 seconds for longer effects (explosion, level up)

BRR conversion: snesbrr -e input.wav output.brr
  -e flag: encode (WAV to BRR)
```

### IT Module Guidelines
```
Impulse Tracker module constraints for snesmod:
  - Maximum 8 channels (matches SPC700 hardware)
  - Use channels 1-4 for music (leaves 5-8 for SFX)
  - Sample count: keep under 16 per module
  - Sample size: keep each sample under 4KB
  - Module effects supported: volume, panning, portamento, vibrato
  - Module effects NOT supported: some advanced IT effects

Loop points: set loop start/end in IT for seamless BGM looping.
Tempo: 125 BPM default. Adjust for mood:
  - Title: 90-100 BPM (atmospheric)
  - Flight: 130-150 BPM (energetic)
  - Battle: 140-160 BPM (intense)
  - Victory: 120 BPM (triumphant)
```

### Music Transition Strategy
```
Scene Transition        Music Action
---                     ---
Title -> Flight         Fade out title (15f), play zone music
Flight -> Battle        Instant swap to battle music
Battle -> Flight        Fade out battle (15f), resume zone music
Flight -> Pause         Pause music (volume to 0)
Pause -> Flight         Resume music (restore volume)
Any -> Game Over        Instant swap to game over music
Any -> Victory          Instant swap to victory fanfare
```

### SFX Channel Allocation
```
Channels 4-7 are used for SFX in round-robin order.
Each new SFX call advances to the next channel.

Priority consideration: if a SFX is still playing and
a new SFX is triggered, the new SFX overwrites the oldest
playing channel. This is acceptable because:
  - Most SFX are very short (< 0.3 seconds)
  - 4 channels allow 4 simultaneous SFX
  - In the worst case (rapid fire), older shots are cut off
    by newer ones, which sounds natural
```

## Asset Requirements

### Music (IT Module Files)
Create or source the following IT module files. For initial development, use simple sine wave or square wave instruments with basic drum patterns.

| File | Scene | BPM | Duration | Mood |
|------|-------|-----|----------|------|
| title.it | Title screen | 90 | 16 bars loop | Atmospheric, mysterious |
| zone1.it | Debris Field | 130 | 16 bars loop | Tense, military |
| zone2.it | Asteroid Belt | 140 | 16 bars loop | Urgent, driving |
| zone3.it | Flagship | 150 | 16 bars loop | Intense, climactic |
| battle.it | Battle scene | 145 | 8 bars loop | Combat, rhythmic |
| boss.it | Boss fight | 160 | 8 bars loop | Aggressive, heavy |
| victory.it | Victory | 120 | 4 bars, no loop | Triumphant fanfare |
| gameover.it | Game Over | 80 | 4 bars, no loop | Somber, descending |

### Sound Effects (WAV Source Files)
| File | Event | Duration | Character |
|------|-------|----------|-----------|
| player_shoot.wav | Player fires | 0.1s | Short pew/laser |
| explosion.wav | Enemy destroyed | 0.3s | Boom/crash |
| hit.wav | Damage taken | 0.15s | Impact thud |
| menu_select.wav | A button confirm | 0.1s | Bright blip |
| menu_move.wav | D-pad in menu | 0.05s | Soft tick |
| dialog_blip.wav | Text character | 0.03s | Tiny beep |
| level_up.wav | Level up | 0.5s | Ascending arpeggio |
| heal.wav | HP/SP restore | 0.3s | Chime/sparkle |

## Acceptance Criteria
1. soundInit() completes without hanging or crashing.
2. soundPlayMusic(MUSIC_TITLE) plays the title theme and it loops.
3. Switching from title to flight correctly changes the music track.
4. soundPlaySFX(SFX_PLAYER_SHOOT) produces an audible sound.
5. Multiple SFX can play simultaneously (up to 4).
6. Music does not audibly glitch when SFX are triggered.
7. soundFadeMusicOut() smoothly reduces music volume to silence.
8. soundPauseMusic()/soundResumeMusic() works for the pause menu.
9. spcProcess() is called every frame without missing frames.
10. Scene transitions play the correct music for each state.
11. Total audio data fits in ARAM (under 56KB at peak).
12. No audio pops or clicks during music changes.

## SNES-Specific Constraints
- spcBoot() must be called once, after consoleInit(). It initializes the SPC700 and loads the snesmod driver.
- spcSetBank() must be called before spcLoad(). It tells the driver which ROM bank contains the soundbank data.
- spcProcess() MUST be called every frame. Missing calls causes music to stutter or stop. It handles the streaming of module data from ROM to ARAM.
- spcLoad() blocks for several frames while loading module data. Call it during a fade-out or force blank to hide the pause.
- BRR samples for SFX must be pre-loaded into ARAM. spcPlaySound() triggers playback from ARAM, not ROM.
- The SPC700 runs at ~1.024 MHz, independent of the 3.58 MHz main CPU. Communication is via 4 I/O registers ($2140-$2143).
- Volume control is 0-255 per channel. spcSetModuleVolume() sets the master module volume (0-255).
- spcPlaySound() parameters: channel (0-7), pitch (4096 = normal), volume (0-255), BRR data pointer, BRR data size.

## Estimated Complexity
**Medium** - The PVSnesLib API handles most of the complexity. The main challenge is creating the audio assets (IT modules and WAV samples) and ensuring the module-to-soundbank conversion produces valid data. Integration with existing systems is straightforward but touches many files.
