/*==============================================================================
 * Sound Engine - Phase 17
 *
 * BRR sound effects played via PVSnesLib's SPC700 API.
 *
 * SNES Audio Architecture:
 *   The SNES has a dedicated Sony SPC700 sound processor running at
 *   ~1.024 MHz with 64KB of Audio RAM (ARAM). It is completely separate
 *   from the main 65816 CPU. Communication happens through 4 shared
 *   I/O ports ($2140-$2143) using a protocol-based message passing system.
 *
 *   PVSnesLib provides a driver that runs on the SPC700 side, accepting
 *   commands from the CPU to play samples, set volume, etc. The CPU-side
 *   API (spcBoot, spcPlaySound, spcProcess) manages this protocol.
 *
 * BRR (Bit Rate Reduction):
 *   SNES native audio sample format. Each 9-byte BRR block encodes 16
 *   4-bit ADPCM samples. BRR files are converted from WAV at build time
 *   using tools like wav2brr or BRRtools.
 *
 * IMPORTANT: spcPlaySound index ordering is REVERSE from load order.
 *   Index 0 = LAST loaded sample, Index 1 = penultimate, etc.
 *   To compensate, sounds are loaded in reverse SFX_* ID order so that
 *   spcPlaySound(sfxId - 1) maps correctly after the reversal.
 *
 * Music: Stub functions for future IT module support.
 * When IT modules are available, uncomment the spcSetBank/spcLoad/spcPlay
 * calls and add the soundbank to data.asm.
 *
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 * The 65816 can address up to 256 banks of 64KB each (16MB total). ROM
 * data in different banks is accessed transparently by the linker.
 *============================================================================*/

#include "engine/sound.h"

/*=== BRR Sample Data (from data.asm) ===*/
/* These are external symbols defined in the assembly data file.
 * Each pair (sfx_name, sfx_name_end) marks the start and end of a
 * BRR sample in ROM. The difference gives the sample size in bytes.
 * Declared as 'char' because PVSnesLib's API expects char pointers. */
extern char sfx_player_shoot, sfx_player_shoot_end;
extern char sfx_enemy_shoot, sfx_enemy_shoot_end;
extern char sfx_explosion, sfx_explosion_end;
extern char sfx_hit, sfx_hit_end;
extern char sfx_menu_select, sfx_menu_select_end;
extern char sfx_menu_move, sfx_menu_move_end;
extern char sfx_dialog_blip, sfx_dialog_blip_end;
extern char sfx_level_up, sfx_level_up_end;
extern char sfx_heal, sfx_heal_end;

/*=== BRR Sample Table (one entry per SFX) ===*/
/* PVSnesLib fills these brrsamples structs when spcSetSoundEntry is called.
 * They store the ARAM address and metadata needed by the SPC700 driver
 * to locate and play each sample. */
static brrsamples s_brr_table[SFX_COUNT];

/*=== Internal State ===*/
static u8 s_sfx_count;         /* Number of SFX successfully loaded into ARAM */
static u8 s_current_music;     /* Currently playing MUSIC_* track ID */
static u8 s_music_playing;     /* 1 if music playback is active */

/*
 * SFX definition structure used during initialization.
 * Groups all parameters needed to load one BRR sample into ARAM.
 * Only used locally within soundInit().
 */
typedef struct {
    char *data;       /* Pointer to BRR sample data start in ROM */
    char *data_end;   /* Pointer to BRR sample data end in ROM */
    u8 pitch;         /* BRR playback pitch (1-6). Approximate Hz = pitch * 2000.
                       * 3 = 6kHz (bass/explosions), 4 = 8kHz (standard),
                       * 5 = 10kHz (UI), 6 = 12kHz (crisp blips) */
    u8 volume;        /* Playback volume (0-15). 15 = loudest. */
} SFXLoadDef;

/*===========================================================================*/
/* Initialization                                                            */
/*===========================================================================*/

/*
 * soundInit - Boot the SPC700 and load all BRR sound effects into ARAM.
 *
 * Initialization sequence:
 *   1. spcBoot() - Upload the PVSnesLib SPC700 driver program into ARAM
 *      and start execution. This takes several frames due to the slow
 *      I/O port protocol.
 *
 *   2. spcAllocateSoundRegion(56) - Reserve 56 blocks (56 * 256 = 14336 bytes)
 *      of ARAM for BRR sample storage. The remaining ARAM is available for
 *      the SPC700 driver, echo buffer, and (future) music module data.
 *      Total BRR data is approximately 13.2KB, fitting within 14KB.
 *
 *   3. Load each SFX via spcSetSoundEntry(). This uploads the BRR data
 *      from ROM to ARAM and registers it with the driver. Sounds are
 *      loaded in REVERSE order (SFX_COUNT-1 down to 1) to compensate
 *      for PVSnesLib's reverse playback indexing.
 *
 * The SFX definition table is built on the stack (local array) because
 * it's only needed during init and would waste WRAM if kept globally.
 */
void soundInit(void)
{
    u8 i;
    u16 brr_len;
    SFXLoadDef sfx_defs[SFX_COUNT];

    s_current_music = MUSIC_NONE;
    s_music_playing = 0;
    s_sfx_count = 0;

    /* Boot the SPC700 sound processor.
     * This uploads the PVSnesLib driver code (~2KB) to ARAM via the
     * I/O ports and waits for the SPC700 to acknowledge startup. */
    spcBoot();

    /* Allocate ARAM region for BRR samples.
     * 56 blocks * 256 bytes/block = 14336 bytes.
     * This must be called before any spcSetSoundEntry calls. */
    spcAllocateSoundRegion(56);

    /* Define SFX load parameters.
     * Each entry pairs ROM data pointers with playback settings.
     * SFX_NONE (index 0) has NULL data and will be skipped during loading. */

    /* SFX_NONE (0) - sentinel, not a real sound */
    sfx_defs[SFX_NONE].data = 0;
    sfx_defs[SFX_NONE].data_end = 0;
    sfx_defs[SFX_NONE].pitch = 4;
    sfx_defs[SFX_NONE].volume = 0;

    /* SFX_PLAYER_SHOOT (1) - short high-pitched blip for player firing */
    sfx_defs[SFX_PLAYER_SHOOT].data = &sfx_player_shoot;
    sfx_defs[SFX_PLAYER_SHOOT].data_end = &sfx_player_shoot_end;
    sfx_defs[SFX_PLAYER_SHOOT].pitch = 4;    /* 8kHz - standard pitch */
    sfx_defs[SFX_PLAYER_SHOOT].volume = 15;  /* Full volume (primary gameplay SFX) */

    /* SFX_ENEMY_SHOOT (2) - lower-pitched version of player shoot */
    sfx_defs[SFX_ENEMY_SHOOT].data = &sfx_enemy_shoot;
    sfx_defs[SFX_ENEMY_SHOOT].data_end = &sfx_enemy_shoot_end;
    sfx_defs[SFX_ENEMY_SHOOT].pitch = 3;     /* 6kHz - lower pitch for distinction */
    sfx_defs[SFX_ENEMY_SHOOT].volume = 13;   /* Slightly quieter than player shots */

    /* SFX_EXPLOSION (3) - noise burst for enemy destruction */
    sfx_defs[SFX_EXPLOSION].data = &sfx_explosion;
    sfx_defs[SFX_EXPLOSION].data_end = &sfx_explosion_end;
    sfx_defs[SFX_EXPLOSION].pitch = 3;     /* 6kHz - bass rumble */
    sfx_defs[SFX_EXPLOSION].volume = 15;   /* Full volume (satisfying feedback) */

    /* SFX_HIT (4) - impact sound when bullet hits but doesn't destroy */
    sfx_defs[SFX_HIT].data = &sfx_hit;
    sfx_defs[SFX_HIT].data_end = &sfx_hit_end;
    sfx_defs[SFX_HIT].pitch = 4;
    sfx_defs[SFX_HIT].volume = 14;

    /* SFX_MENU_SELECT (5) - bright chime for confirming menu selections */
    sfx_defs[SFX_MENU_SELECT].data = &sfx_menu_select;
    sfx_defs[SFX_MENU_SELECT].data_end = &sfx_menu_select_end;
    sfx_defs[SFX_MENU_SELECT].pitch = 5;     /* 10kHz - bright and clear */
    sfx_defs[SFX_MENU_SELECT].volume = 12;   /* Moderate (UI sounds are softer) */

    /* SFX_MENU_MOVE (6) - soft tick for cursor movement */
    sfx_defs[SFX_MENU_MOVE].data = &sfx_menu_move;
    sfx_defs[SFX_MENU_MOVE].data_end = &sfx_menu_move_end;
    sfx_defs[SFX_MENU_MOVE].pitch = 5;
    sfx_defs[SFX_MENU_MOVE].volume = 10;    /* Quiet (repeated frequently) */

    /* SFX_DIALOG_BLIP (7) - tiny blip for typewriter text effect */
    sfx_defs[SFX_DIALOG_BLIP].data = &sfx_dialog_blip;
    sfx_defs[SFX_DIALOG_BLIP].data_end = &sfx_dialog_blip_end;
    sfx_defs[SFX_DIALOG_BLIP].pitch = 6;     /* 12kHz - crisp tiny sound */
    sfx_defs[SFX_DIALOG_BLIP].volume = 8;    /* Very quiet (plays per-character) */

    /* SFX_LEVEL_UP (8) - ascending tone jingle for leveling up */
    sfx_defs[SFX_LEVEL_UP].data = &sfx_level_up;
    sfx_defs[SFX_LEVEL_UP].data_end = &sfx_level_up_end;
    sfx_defs[SFX_LEVEL_UP].pitch = 4;
    sfx_defs[SFX_LEVEL_UP].volume = 15;     /* Full volume (rare, celebratory) */

    /* SFX_HEAL (9) - shimmering restoration sound */
    sfx_defs[SFX_HEAL].data = &sfx_heal;
    sfx_defs[SFX_HEAL].data_end = &sfx_heal_end;
    sfx_defs[SFX_HEAL].pitch = 5;
    sfx_defs[SFX_HEAL].volume = 13;

    /* Load sounds in REVERSE order to compensate for PVSnesLib's
     * reverse playback indexing.
     *
     * PVSnesLib's spcPlaySound uses a stack-like index where:
     *   spcPlaySound(0) plays the LAST loaded sound
     *   spcPlaySound(1) plays the second-to-last loaded sound
     *   spcPlaySound(N-1) plays the FIRST loaded sound
     *
     * By loading in reverse ID order (9, 8, 7, ..., 1), the mapping becomes:
     *   SFX ID 1 -> loaded last  -> spcPlaySound(0) -> spc_index = 1-1 = 0
     *   SFX ID 2 -> loaded second-to-last -> spcPlaySound(1) -> spc_index = 2-1 = 1
     *   ...
     *   SFX ID 9 -> loaded first -> spcPlaySound(8) -> spc_index = 9-1 = 8
     *
     * So the simple formula spc_index = sfxId - 1 works for all IDs.
     * SFX_NONE (0) is skipped since it has no sample data. */
    s_sfx_count = 0;
    for (i = SFX_COUNT; i > 0; i--) {
        u8 idx;
        idx = i - 1;
        if (idx == SFX_NONE) continue;    /* Skip sentinel (no data) */
        if (sfx_defs[idx].data == 0) continue;  /* Skip entries without data */

        /* Compute BRR sample length from ROM symbol addresses */
        brr_len = (u16)(sfx_defs[idx].data_end - sfx_defs[idx].data);

        /* Upload BRR sample to ARAM and register with SPC700 driver.
         * Parameters:
         *   volume    - Playback volume (0-15)
         *   8         - Center panning (0=left, 8=center, 15=right)
         *   pitch     - Playback pitch (1-6, Hz ~= pitch * 2000)
         *   brr_len   - BRR data size in bytes
         *   data      - Pointer to BRR data in ROM
         *   brr_table - Output struct (filled with ARAM address info) */
        spcSetSoundEntry(sfx_defs[idx].volume,
                         8,    /* center panning */
                         sfx_defs[idx].pitch,
                         brr_len,
                         (u8 *)sfx_defs[idx].data,
                         &s_brr_table[idx]);
        s_sfx_count++;
    }
}

/*===========================================================================*/
/* Sound Effects                                                             */
/*===========================================================================*/

/*
 * soundPlaySFX - Trigger playback of a sound effect.
 *
 * Maps the game-level SFX ID to the SPC700 driver's playback index
 * and calls spcPlaySound() to start sample playback.
 *
 * The index mapping is simple: spc_index = sfxId - 1.
 * This works because sounds were loaded in reverse order during init
 * (see soundInit comments for the full derivation).
 *
 * spcPlaySound is non-blocking: it sends a play command to the SPC700
 * via the I/O ports and returns immediately. The SPC700 handles mixing
 * and playback asynchronously.
 *
 * Parameters:
 *   sfxId - SFX_* constant (1-9). SFX_NONE (0) and out-of-range values
 *           are silently ignored (no sound played).
 */
void soundPlaySFX(u8 sfxId)
{
    u8 spc_index;

    /* Guard against invalid IDs */
    if (sfxId == SFX_NONE || sfxId >= SFX_COUNT) return;

    /* Convert game SFX ID to SPC700 playback index.
     * See soundInit() for the detailed derivation of this mapping. */
    spc_index = sfxId - 1;

    spcPlaySound(spc_index);
}

/*===========================================================================*/
/* Per-Frame Update                                                          */
/*===========================================================================*/

/*
 * soundUpdate - Per-frame sound system maintenance.
 *
 * Calls spcProcess() which services the SPC700 communication protocol.
 * This function handles:
 *   - Completing pending BRR sample uploads to ARAM
 *   - Processing queued play/stop commands
 *   - Maintaining the driver keepalive handshake
 *
 * CRITICAL: Must be called EVERY frame without exception. The SPC700
 * driver expects regular communication from the CPU side. If spcProcess()
 * is not called for several frames, the driver may:
 *   - Drop pending sound commands
 *   - Fail to acknowledge sample uploads
 *   - In extreme cases, lose sync and require a full reboot (spcBoot)
 *
 * This is typically the last call in the per-frame update sequence,
 * after all game logic and rendering.
 */
void soundUpdate(void)
{
    spcProcess();
}

/*===========================================================================*/
/* Music API (stubs for future IT module support)                            */
/*                                                                           */
/* When IT (Impulse Tracker) modules are available:                          */
/*   1. Convert IT files to SNES format using smconv tool                    */
/*   2. Include the resulting soundbank binary in data.asm                   */
/*   3. In soundInit(): call spcSetBank(&soundbank) to register modules      */
/*   4. In soundPlayMusic(): call spcLoad(module_index) then spcPlay(0)      */
/*   5. spcProcess() handles streaming module data to the SPC700             */
/*                                                                           */
/* Current state: All music functions are stubs that update tracking          */
/* variables but do not produce any audio output.                            */
/*===========================================================================*/

/*
 * soundPlayMusic - Start playing a music track (stub).
 *
 * Currently stores the track ID for query purposes but does not
 * actually play any music. Game code can call this now and music
 * will "just work" once the IT module pipeline is implemented.
 *
 * Parameters:
 *   trackId - MUSIC_* constant to play.
 */
void soundPlayMusic(u8 trackId)
{
    /* TODO: When IT modules are available:
     * 1. Add smconv conversion to Makefile
     * 2. Include soundbank in data.asm
     * 3. Call spcSetBank(&soundbank) in soundInit
     * 4. Here: spcLoad(module_index), spcPlay(0) */
    s_current_music = trackId;
    s_music_playing = 1;

    /* Suppress unused parameter warning (music not yet implemented) */
    (void)trackId;
}

/*
 * soundStopMusic - Stop the currently playing music (stub).
 *
 * When music is active, calls spcStop() to halt SPC700 playback.
 * Resets tracking state to MUSIC_NONE.
 */
void soundStopMusic(void)
{
    if (s_music_playing) {
        spcStop();          /* Tell SPC700 to stop music playback */
        s_music_playing = 0;
    }
    s_current_music = MUSIC_NONE;
}

/*
 * soundPauseMusic - Pause the current music track (e.g., for pause menu).
 *
 * spcPauseMusic() sends a pause command to the SPC700 driver, which
 * halts music playback but remembers the current position for resume.
 * SFX playback continues during music pause.
 */
void soundPauseMusic(void)
{
    if (s_music_playing) {
        spcPauseMusic();
    }
}

/*
 * soundResumeMusic - Resume previously paused music playback.
 *
 * spcResumeMusic() sends a resume command to the SPC700 driver,
 * which continues music playback from where it was paused.
 */
void soundResumeMusic(void)
{
    if (s_music_playing) {
        spcResumeMusic();
    }
}

/*
 * soundGetCurrentMusic - Query the currently playing music track.
 * Returns: MUSIC_* constant, or MUSIC_NONE (0) if no music is playing.
 */
u8 soundGetCurrentMusic(void)
{
    return s_current_music;
}
