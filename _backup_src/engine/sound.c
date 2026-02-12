/*==============================================================================
 * Sound Engine - Phase 17
 *
 * BRR sound effects played via PVSnesLib's SPC700 API.
 * Uses spcSetSoundEntry to register BRR samples in ARAM, then
 * spcPlaySound to trigger playback.
 *
 * IMPORTANT: spcPlaySound index is REVERSE order from load order.
 * Index 0 = LAST loaded, index 1 = penultimate, etc.
 * Sounds are loaded in reverse SFX_* ID order so that
 * spcPlaySound(SFX_COUNT - 1 - sfxId) maps correctly.
 *
 * Music: Stub functions for future IT module support.
 * When IT modules are available, uncomment the spcSetBank/spcLoad/spcPlay
 * calls and add the soundbank to data.asm.
 *
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#include "engine/sound.h"

/*=== BRR Sample Data (from data.asm) ===*/
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
/* PVSnesLib fills these structs when spcSetSoundEntry is called */
static brrsamples s_brr_table[SFX_COUNT];

/*=== Internal State ===*/
static u8 s_sfx_count;         /* Number of SFX loaded */
static u8 s_current_music;     /* Currently playing MUSIC_* ID */
static u8 s_music_playing;     /* 1 if music is active */

/*=== SFX Definition for Loading ===*/
typedef struct {
    char *data;
    char *data_end;
    u8 pitch;       /* BRR pitch (1-6, Hz = pitch*2000). 4 = 8kHz playback */
    u8 volume;      /* 0-15 */
} SFXLoadDef;

/*===========================================================================*/
/* Initialization                                                            */
/*===========================================================================*/

void soundInit(void)
{
    u8 i;
    u16 brr_len;
    SFXLoadDef sfx_defs[SFX_COUNT];

    s_current_music = MUSIC_NONE;
    s_music_playing = 0;
    s_sfx_count = 0;

    /* Boot SPC700 sound processor */
    spcBoot();

    /* Allocate ARAM region for BRR samples.
     * Total BRR size ~13.2KB. Allocate 56 blocks (56*256 = 14336 bytes). */
    spcAllocateSoundRegion(56);

    /* Define SFX load entries (index matches SFX_* IDs) */
    /* SFX_NONE (0) - no sound */
    sfx_defs[SFX_NONE].data = 0;
    sfx_defs[SFX_NONE].data_end = 0;
    sfx_defs[SFX_NONE].pitch = 4;
    sfx_defs[SFX_NONE].volume = 0;

    /* SFX_PLAYER_SHOOT (1) */
    sfx_defs[SFX_PLAYER_SHOOT].data = &sfx_player_shoot;
    sfx_defs[SFX_PLAYER_SHOOT].data_end = &sfx_player_shoot_end;
    sfx_defs[SFX_PLAYER_SHOOT].pitch = 4;
    sfx_defs[SFX_PLAYER_SHOOT].volume = 15;

    /* SFX_ENEMY_SHOOT (2) */
    sfx_defs[SFX_ENEMY_SHOOT].data = &sfx_enemy_shoot;
    sfx_defs[SFX_ENEMY_SHOOT].data_end = &sfx_enemy_shoot_end;
    sfx_defs[SFX_ENEMY_SHOOT].pitch = 3;
    sfx_defs[SFX_ENEMY_SHOOT].volume = 13;

    /* SFX_EXPLOSION (3) */
    sfx_defs[SFX_EXPLOSION].data = &sfx_explosion;
    sfx_defs[SFX_EXPLOSION].data_end = &sfx_explosion_end;
    sfx_defs[SFX_EXPLOSION].pitch = 3;
    sfx_defs[SFX_EXPLOSION].volume = 15;

    /* SFX_HIT (4) */
    sfx_defs[SFX_HIT].data = &sfx_hit;
    sfx_defs[SFX_HIT].data_end = &sfx_hit_end;
    sfx_defs[SFX_HIT].pitch = 4;
    sfx_defs[SFX_HIT].volume = 14;

    /* SFX_MENU_SELECT (5) */
    sfx_defs[SFX_MENU_SELECT].data = &sfx_menu_select;
    sfx_defs[SFX_MENU_SELECT].data_end = &sfx_menu_select_end;
    sfx_defs[SFX_MENU_SELECT].pitch = 5;
    sfx_defs[SFX_MENU_SELECT].volume = 12;

    /* SFX_MENU_MOVE (6) */
    sfx_defs[SFX_MENU_MOVE].data = &sfx_menu_move;
    sfx_defs[SFX_MENU_MOVE].data_end = &sfx_menu_move_end;
    sfx_defs[SFX_MENU_MOVE].pitch = 5;
    sfx_defs[SFX_MENU_MOVE].volume = 10;

    /* SFX_DIALOG_BLIP (7) */
    sfx_defs[SFX_DIALOG_BLIP].data = &sfx_dialog_blip;
    sfx_defs[SFX_DIALOG_BLIP].data_end = &sfx_dialog_blip_end;
    sfx_defs[SFX_DIALOG_BLIP].pitch = 6;
    sfx_defs[SFX_DIALOG_BLIP].volume = 8;

    /* SFX_LEVEL_UP (8) */
    sfx_defs[SFX_LEVEL_UP].data = &sfx_level_up;
    sfx_defs[SFX_LEVEL_UP].data_end = &sfx_level_up_end;
    sfx_defs[SFX_LEVEL_UP].pitch = 4;
    sfx_defs[SFX_LEVEL_UP].volume = 15;

    /* SFX_HEAL (9) */
    sfx_defs[SFX_HEAL].data = &sfx_heal;
    sfx_defs[SFX_HEAL].data_end = &sfx_heal_end;
    sfx_defs[SFX_HEAL].pitch = 5;
    sfx_defs[SFX_HEAL].volume = 13;

    /* Load sounds in REVERSE order so spcPlaySound index matches.
     * spcPlaySound(0) = last loaded, spcPlaySound(N-1) = first loaded.
     * We load SFX_COUNT-1 down to 1 (skip SFX_NONE). */
    s_sfx_count = 0;
    for (i = SFX_COUNT; i > 0; i--) {
        u8 idx;
        idx = i - 1;
        if (idx == SFX_NONE) continue;
        if (sfx_defs[idx].data == 0) continue;

        brr_len = (u16)(sfx_defs[idx].data_end - sfx_defs[idx].data);

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

void soundPlaySFX(u8 sfxId)
{
    u8 spc_index;

    if (sfxId == SFX_NONE || sfxId >= SFX_COUNT) return;

    /* spcPlaySound index is reverse of load order.
     * We loaded from SFX_COUNT-1 down to 1, so:
     *   Last loaded  = SFX ID 1 -> spcPlaySound(0)
     *   First loaded = SFX ID SFX_COUNT-1 -> spcPlaySound(SFX_COUNT-2)
     * So: spc_index = sfxId - 1 */
    spc_index = sfxId - 1;

    spcPlaySound(spc_index);
}

/*===========================================================================*/
/* Per-Frame Update                                                          */
/*===========================================================================*/

void soundUpdate(void)
{
    /* Keep SPC700 driver running. MUST be called every frame. */
    spcProcess();
}

/*===========================================================================*/
/* Music API (stubs for future IT module support)                            */
/*===========================================================================*/

void soundPlayMusic(u8 trackId)
{
    /* TODO: When IT modules are available:
     * 1. Add smconv conversion to Makefile
     * 2. Include soundbank in data.asm
     * 3. Call spcSetBank(&soundbank) in soundInit
     * 4. Here: spcLoad(module_index), spcPlay(0) */
    s_current_music = trackId;

    /* Music not available yet - SFX only build */
    (void)trackId;
}

void soundStopMusic(void)
{
    if (s_music_playing) {
        spcStop();
        s_music_playing = 0;
    }
    s_current_music = MUSIC_NONE;
}

void soundPauseMusic(void)
{
    if (s_music_playing) {
        spcPauseMusic();
    }
}

void soundResumeMusic(void)
{
    if (s_music_playing) {
        spcResumeMusic();
    }
}

u8 soundGetCurrentMusic(void)
{
    return s_current_music;
}
