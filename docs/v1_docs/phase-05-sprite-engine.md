# Phase 5: Sprite Engine & Player Ship

## Objective
Implement a sprite management layer on top of PVSnesLib's OAM system. Create the player ship entity that renders as a 32x32 sprite, supports animation frames (idle, banking left, banking right), and loads its graphics into VRAM. The player ship appears on screen at a fixed position for now (input handled in Phase 6).

## Prerequisites
- Phase 1, Phase 2, Phase 3 (asset pipeline produces player_ship.pic and .pal).

## Detailed Tasks

1. Create `src/engine/sprites.c` - Sprite pool manager that tracks active sprites, assigns OAM slots, and handles VRAM tile management for dynamic sprite loading.

2. Create `src/game/player.c` - Player ship entity with position, animation state, and rendering logic.

3. Convert the player ship asset to include 3 animation frames: center (idle), bank-left, bank-right. Since the source is a single image, the 3 frames will be the same tile data with horizontal flip for banking. Alternatively, we can use a single 32x32 frame with flip flags.

4. Load the player sprite tiles and palette into OBJ VRAM during scene setup.

5. Render the player ship at screen center-bottom (x=112, y=176) using oamSet().

6. Implement sprite animation framework (frame counter, frame table, auto-advance).

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/engine/sprites.h
```c
#ifndef SPRITES_H
#define SPRITES_H

#include "game.h"

/* Maximum active game sprites (not counting HUD/UI sprites) */
#define MAX_GAME_SPRITES 48

/* Sprite entity structure */
typedef struct {
    s16 x;              /* Screen X position (pixels) */
    s16 y;              /* Screen Y position (pixels) */
    u16 tile_offset;    /* Tile number offset in OBJ VRAM */
    u8  palette;        /* OBJ palette number (0-7) */
    u8  priority;       /* Sprite priority (0-3, 3=highest) */
    u8  size;           /* OBJ_SMALL or OBJ_LARGE */
    u8  hflip;          /* Horizontal flip */
    u8  vflip;          /* Vertical flip */
    u8  active;         /* ENTITY_ACTIVE, ENTITY_INACTIVE, ENTITY_DYING */
    u8  anim_frame;     /* Current animation frame index */
    u8  anim_timer;     /* Frames until next animation step */
    u8  anim_speed;     /* Frames per animation step */
    u8  anim_count;     /* Total frames in animation */
    u16 oam_id;         /* Assigned OAM slot (multiplied by 4) */
} SpriteEntity;

/* Initialize sprite system, clear all entities */
void spriteSystemInit(void);

/* Allocate a sprite entity from the pool
 * Returns pointer to entity, or NULL if pool is full */
SpriteEntity* spriteAlloc(void);

/* Free a sprite entity back to the pool */
void spriteFree(SpriteEntity *spr);

/* Update all active sprites (animation, etc.) */
void spriteUpdateAll(void);

/* Render all active sprites to OAM
 * Must be called before WaitForVBlank so OAM buffer is ready */
void spriteRenderAll(void);

/* Hide all sprites (set offscreen) */
void spriteHideAll(void);

/* Load sprite tile data to OBJ VRAM at the specified word offset */
void spriteLoadTiles(u8 *tileData, u16 tileSize, u16 vramWordOffset);

/* Load sprite palette to OBJ CGRAM slot (0-7) */
void spriteLoadPalette(u8 *palData, u16 palSize, u8 palSlot);

#endif
```

### J:/code/snes/snes-rpg-test/src/engine/sprites.c
```c
/*==============================================================================
 * Sprite Engine
 * Manages a pool of sprite entities and maps them to OAM slots.
 *============================================================================*/

#include "engine/sprites.h"

static SpriteEntity sprite_pool[MAX_GAME_SPRITES];

/* OAM slot assignment:
 * Slots 0-3 (OAM ids 0-12): Reserved for player + effects
 * Slots 4-11 (OAM ids 16-44): Enemies
 * Slots 12-35 (OAM ids 48-140): Bullets
 * Slots 36-47 (OAM ids 144-188): Particles/misc
 */
#define OAM_PLAYER_START    0
#define OAM_ENEMY_START     4
#define OAM_BULLET_START    12
#define OAM_PARTICLE_START  36

void spriteSystemInit(void)
{
    u8 i;
    for (i = 0; i < MAX_GAME_SPRITES; i++) {
        sprite_pool[i].active = ENTITY_INACTIVE;
        sprite_pool[i].oam_id = i * 4; /* Each OAM entry is 4 bytes */
    }

    /* Initialize OAM hardware */
    oamInit();

    /* Hide all OAM entries */
    oamClear(0, 0);
}

SpriteEntity* spriteAlloc(void)
{
    u8 i;
    for (i = 0; i < MAX_GAME_SPRITES; i++) {
        if (sprite_pool[i].active == ENTITY_INACTIVE) {
            sprite_pool[i].active = ENTITY_ACTIVE;
            sprite_pool[i].x = 0;
            sprite_pool[i].y = 240;  /* Offscreen by default */
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
    return (SpriteEntity *)0;  /* Pool exhausted */
}

void spriteFree(SpriteEntity *spr)
{
    if (spr) {
        spr->active = ENTITY_INACTIVE;
        /* Hide this OAM entry */
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
    u8 attr;

    for (i = 0; i < MAX_GAME_SPRITES; i++) {
        spr = &sprite_pool[i];

        if (spr->active != ENTITY_ACTIVE) {
            /* Hide inactive sprites */
            oamSetVisible(spr->oam_id, OBJ_HIDE);
            continue;
        }

        /* Check if on screen */
        if (spr->x < -32 || spr->x > 256 || spr->y < -32 || spr->y > 224) {
            oamSetVisible(spr->oam_id, OBJ_HIDE);
            continue;
        }

        /* Calculate tile number with animation offset */
        /* For 32x32 sprite: each frame = 16 tiles (4x4 of 8x8) */
        /* For 16x16 sprite: each frame = 4 tiles (2x2 of 8x8) */
        if (spr->size == OBJ_LARGE) {
            tileNum = spr->tile_offset + (spr->anim_frame * 16);
        } else {
            tileNum = spr->tile_offset + (spr->anim_frame * 4);
        }

        /* Set OAM entry */
        oamSet(spr->oam_id,
               (u16)spr->x, (u16)spr->y,
               spr->priority,
               spr->hflip, spr->vflip,
               tileNum,
               spr->palette);

        /* Set size (small or large) and make visible */
        oamSetEx(spr->oam_id, spr->size, OBJ_SHOW);
    }
}

void spriteHideAll(void)
{
    oamClear(0, 0);
}

void spriteLoadTiles(u8 *tileData, u16 tileSize, u16 vramWordOffset)
{
    dmaCopyVram(tileData, OBJ_TILES_VRAM + vramWordOffset, tileSize);
}

void spriteLoadPalette(u8 *palData, u16 palSize, u8 palSlot)
{
    /* OBJ palettes start at CGRAM index 128
     * Each palette = 16 colors = 32 bytes
     * Slot 0 = CGRAM 128, Slot 1 = CGRAM 144, etc. */
    dmaCopyCGram(palData, 128 + (palSlot * 16), palSize);
}
```

### J:/code/snes/snes-rpg-test/include/game/player.h
```c
#ifndef PLAYER_H
#define PLAYER_H

#include "game.h"
#include "engine/sprites.h"

/* Player ship start position */
#define PLAYER_START_X  112     /* Centered: (256 - 32) / 2 */
#define PLAYER_START_Y  176     /* Near bottom of screen */

/* Player ship visual state */
#define PLAYER_ANIM_IDLE    0
#define PLAYER_ANIM_LEFT    1   /* Banking left (using hflip) */
#define PLAYER_ANIM_RIGHT   2   /* Banking right */

/* Player data structure */
typedef struct {
    SpriteEntity *sprite;   /* OAM sprite entity */
    s16 x;                  /* World X position (pixels) */
    s16 y;                  /* World Y position (pixels) */
    u8 anim_state;          /* PLAYER_ANIM_IDLE/LEFT/RIGHT */
    u8 invincible_timer;    /* Frames of invincibility remaining */
    u8 visible;             /* 1=shown, 0=hidden (for blink effect) */
} PlayerShip;

/* Global player instance */
extern PlayerShip player;

/* Initialize player: load graphics, create sprite, set position */
void playerInit(void);

/* Update player each frame (animation, invincibility blink) */
void playerUpdate(void);

/* Set player banking animation state */
void playerSetBanking(u8 state);

/* Set player position directly */
void playerSetPosition(s16 x, s16 y);

/* Show/hide the player ship */
void playerShow(void);
void playerHide(void);

#endif
```

### J:/code/snes/snes-rpg-test/src/game/player.c
```c
/*==============================================================================
 * Player Ship
 *============================================================================*/

#include "game/player.h"

/* Extern asset labels */
extern char player_ship_tiles, player_ship_tiles_end;
extern char player_ship_pal, player_ship_pal_end;

PlayerShip player;

void playerInit(void)
{
    /* Load player ship tiles into OBJ VRAM */
    spriteLoadTiles(&player_ship_tiles,
                    &player_ship_tiles_end - &player_ship_tiles,
                    OBJ_PLAYER_OFFSET);

    /* Load player palette into OBJ palette slot 0 */
    spriteLoadPalette(&player_ship_pal,
                      &player_ship_pal_end - &player_ship_pal,
                      0);

    /* Allocate sprite entity */
    player.sprite = spriteAlloc();
    if (player.sprite) {
        player.sprite->x = PLAYER_START_X;
        player.sprite->y = PLAYER_START_Y;
        player.sprite->tile_offset = 0; /* First tile in OBJ VRAM */
        player.sprite->palette = 0;
        player.sprite->priority = 3;    /* Above backgrounds */
        player.sprite->size = OBJ_LARGE; /* 32x32 */
        player.sprite->hflip = 0;
        player.sprite->vflip = 0;
        player.sprite->anim_count = 1;  /* Single frame for now */
        player.sprite->anim_speed = 0;
    }

    player.x = PLAYER_START_X;
    player.y = PLAYER_START_Y;
    player.anim_state = PLAYER_ANIM_IDLE;
    player.invincible_timer = 0;
    player.visible = 1;
}

void playerUpdate(void)
{
    if (!player.sprite) return;

    /* Update sprite position from player position */
    player.sprite->x = player.x;
    player.sprite->y = player.y;

    /* Invincibility blink effect */
    if (player.invincible_timer > 0) {
        player.invincible_timer--;
        /* Blink: visible every other 4-frame period */
        player.visible = (player.invincible_timer >> 2) & 1;
        if (player.visible) {
            oamSetEx(player.sprite->oam_id, OBJ_LARGE, OBJ_SHOW);
        } else {
            oamSetVisible(player.sprite->oam_id, OBJ_HIDE);
        }
    } else {
        player.visible = 1;
    }
}

void playerSetBanking(u8 state)
{
    player.anim_state = state;
    if (!player.sprite) return;

    switch (state) {
        case PLAYER_ANIM_LEFT:
            player.sprite->hflip = 1;  /* Mirror sprite to show left bank */
            break;
        case PLAYER_ANIM_RIGHT:
            player.sprite->hflip = 0;  /* Normal orientation = right bank */
            break;
        case PLAYER_ANIM_IDLE:
        default:
            player.sprite->hflip = 0;
            break;
    }
}

void playerSetPosition(s16 x, s16 y)
{
    player.x = x;
    player.y = y;
}

void playerShow(void)
{
    player.visible = 1;
    if (player.sprite) {
        oamSetEx(player.sprite->oam_id, OBJ_LARGE, OBJ_SHOW);
    }
}

void playerHide(void)
{
    player.visible = 0;
    if (player.sprite) {
        oamSetVisible(player.sprite->oam_id, OBJ_HIDE);
    }
}
```

## Technical Specifications

### OBJ VRAM Tile Layout for 32x32 Sprites
```
With OBJ_SIZE16_L32: small=16x16, large=32x32
OBJ base address = $0000

For a 32x32 large sprite at tile offset T:
  The SNES reads a 4x4 grid of 8x8 tiles:
  Row 0: T+0, T+1, T+2, T+3     (but OBJ uses column-first for 16x16 blocks)

Actually, with OBJ_SIZE16_L32, a "large" sprite is 32x32 composed of:
  4 blocks of 16x16, each block is 2x2 tiles of 8x8

  Top-Left  16x16: tiles T+0, T+1 (horiz), T+16, T+17 (next row in char table)
  Top-Right 16x16: tiles T+2, T+3, T+18, T+19
  Bot-Left  16x16: tiles T+32, T+33, T+48, T+49
  Bot-Right 16x16: tiles T+34, T+35, T+50, T+51

This means the tile data must be arranged as a 16-tile-wide character table.
gfx4snes -R outputs tiles in image reading order (row by row), which maps
correctly when the source image is 32 pixels wide (4 tiles per row).

For a 32x32 source image output by gfx4snes -R:
  Tile 0 = top-left 8x8
  Tile 1 = top-second-from-left 8x8
  Tile 2 = top-third 8x8
  Tile 3 = top-right 8x8
  Tile 4-7 = second row of 8x8 tiles
  ... etc, 16 tiles total

The OAM tile number for a 32x32 sprite should point to tile 0 of this
block, and the SNES hardware figures out the 4x4 arrangement based on
the character width (which is 16 tiles wide in a 128-pixel char row).
```

### OAM Slot to Entity Mapping
```
OAM Slot 0  (id=0):   Player ship
OAM Slot 1  (id=4):   Player exhaust effect
OAM Slot 2  (id=8):   Player shield effect
OAM Slot 3  (id=12):  Reserved
OAM Slots 4-11  (id=16-44):  Enemies (8 max)
OAM Slots 12-35 (id=48-140): Bullets (24 max)
OAM Slots 36-47 (id=144-188): Particles
```

## Acceptance Criteria
1. Player ship sprite appears on screen at position (112, 176).
2. The ship is 32x32 pixels with correct colors from the converted palette.
3. No other sprites are visible (all hidden).
4. playerSetBanking(PLAYER_ANIM_LEFT) mirrors the sprite horizontally.
5. playerHide() makes the ship invisible; playerShow() brings it back.
6. In Mesen OAM viewer, OAM slot 0 shows the player ship correctly.
7. spriteAlloc/spriteFree correctly manage the pool (verified via debugger).

## SNES-Specific Constraints
- OAM ids must be multiplied by 4 for oamSet() (each entry is 4 bytes).
- oamSetEx() must be called to set the large/small flag, otherwise sprite uses default small size.
- Sprites beyond x=255 require the 9th bit set via oamSetEx() or oamSet() handles it.
- OBJ palette starts at CGRAM 128 (not 0). Palette slot 0 = CGRAM 128-143.
- Maximum 32 sprites per scanline. Player at bottom means less density there.

## Estimated Complexity
**Medium** - The sprite pool is conceptually simple, but getting 32x32 OBJ tile layout correct requires understanding SNES OAM character table geometry.
