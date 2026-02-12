/*==============================================================================
 * Sprite Engine
 * Manages a pool of sprite entities and maps them to SNES OAM slots.
 * Each pool entry corresponds to one OAM slot (oam_id = index * 4).
 *============================================================================*/

#include "engine/sprites.h"
#include "config.h"

static SpriteEntity sprite_pool[MAX_GAME_SPRITES];

void spriteSystemInit(void)
{
    u8 i;
    for (i = 0; i < MAX_GAME_SPRITES; i++) {
        sprite_pool[i].active = ENTITY_INACTIVE;
        sprite_pool[i].x = 0;
        sprite_pool[i].y = 240;
        sprite_pool[i].tile_offset = 0;
        sprite_pool[i].palette = 0;
        sprite_pool[i].priority = 2;
        sprite_pool[i].size = OBJ_SMALL;
        sprite_pool[i].hflip = 0;
        sprite_pool[i].vflip = 0;
        sprite_pool[i].anim_frame = 0;
        sprite_pool[i].anim_timer = 0;
        sprite_pool[i].anim_speed = 0;
        sprite_pool[i].anim_count = 1;
        sprite_pool[i].oam_id = i * 4;  /* Each OAM entry is 4 bytes */
    }

    /* Hide all 128 OAM entries */
    oamClear(0, 0);
}

SpriteEntity* spriteAlloc(void)
{
    u8 i;
    for (i = 0; i < MAX_GAME_SPRITES; i++) {
        if (sprite_pool[i].active == ENTITY_INACTIVE) {
            sprite_pool[i].active = ENTITY_ACTIVE;
            sprite_pool[i].x = 0;
            sprite_pool[i].y = 240;
            sprite_pool[i].hflip = 0;
            sprite_pool[i].vflip = 0;
            sprite_pool[i].priority = 2;
            sprite_pool[i].anim_frame = 0;
            sprite_pool[i].anim_timer = 0;
            sprite_pool[i].anim_speed = 0;
            sprite_pool[i].anim_count = 1;
            return &sprite_pool[i];
        }
    }
    return (SpriteEntity *)0;
}

void spriteFree(SpriteEntity *spr)
{
    if (spr) {
        spr->active = ENTITY_INACTIVE;
        oamSetVisible(spr->oam_id, OBJ_HIDE);
    }
}

void spriteUpdateAll(void)
{
    u8 i;
    SpriteEntity *spr;

    for (i = 0; i < MAX_GAME_SPRITES; i++) {
        spr = &sprite_pool[i];
        if (spr->active != ENTITY_ACTIVE) continue;

        /* Advance animation */
        if (spr->anim_count > 1 && spr->anim_speed > 0) {
            spr->anim_timer++;
            if (spr->anim_timer >= spr->anim_speed) {
                spr->anim_timer = 0;
                spr->anim_frame++;
                if (spr->anim_frame >= spr->anim_count) {
                    spr->anim_frame = 0;
                }
            }
        }
    }
}

void spriteRenderAll(void)
{
    u8 i;
    SpriteEntity *spr;
    u16 tileNum;

    for (i = 0; i < MAX_GAME_SPRITES; i++) {
        spr = &sprite_pool[i];

        if (spr->active != ENTITY_ACTIVE) {
            oamSetVisible(spr->oam_id, OBJ_HIDE);
            continue;
        }

        /* Offscreen culling */
        if (spr->x < -32 || spr->x > 256 || spr->y < -32 || spr->y > 224) {
            oamSetVisible(spr->oam_id, OBJ_HIDE);
            continue;
        }

        /* Calculate tile number with animation frame offset.
         * 32x32 sprite = 16 tiles per frame (4x4 of 8x8).
         * 16x16 sprite = 4 tiles per frame (2x2 of 8x8). */
        if (spr->size == OBJ_LARGE) {
            tileNum = spr->tile_offset + (spr->anim_frame * 16);
        } else {
            tileNum = spr->tile_offset + (spr->anim_frame * 4);
        }

        oamSet(spr->oam_id,
               (u16)spr->x, (u16)spr->y,
               spr->priority,
               spr->hflip, spr->vflip,
               tileNum,
               spr->palette);

        oamSetEx(spr->oam_id, spr->size, OBJ_SHOW);
    }
}

void spriteHideAll(void)
{
    oamClear(0, 0);
}

void spriteLoadTiles(u8 *tileData, u16 tileSize, u16 vramWordOffset)
{
    dmaCopyVram(tileData, VRAM_OBJ_GFX + vramWordOffset, tileSize);
}

void spriteLoadPalette(u8 *palData, u16 palSize, u8 palSlot)
{
    /* OBJ palettes occupy CGRAM 128-255.
     * Each palette = 16 colors = 32 bytes.
     * Slot 0 = CGRAM 128, Slot 1 = CGRAM 144, etc. */
    dmaCopyCGram(palData, 128 + (palSlot * 16), palSize);
}
