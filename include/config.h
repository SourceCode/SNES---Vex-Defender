/*---------------------------------------------------------------------------------
    VEX DEFENDER - Master Configuration
    All VRAM addresses, palette slots, OAM slots, and game constants
---------------------------------------------------------------------------------*/
#ifndef CONFIG_H
#define CONFIG_H

/*=== Screen Dimensions ===*/
#define SCREEN_W        256
#define SCREEN_H        224

/*=== VRAM Address Map (word addresses) ===*/
/*
 * Layout (32K words = 64KB total):
 *   $0000-$03FF  BG1 tilemap / text map (shared - 2KB)
 *   $0800-$0BFF  BG2 tilemap (2KB)
 *   $1000-$4FFF  BG1 char base (32KB - zone backgrounds need ~32KB)
 *                Font tiles at $2000 (tile offset 0x100 from BG1 base)
 *   $5000-$503F  BG2 tiles (128 bytes - star parallax dots)
 *   $6000-$7FFF  OBJ tiles (16KB - sprites)
 *
 * Text uses BG1 (4bpp) following PVSnesLib hello_world pattern:
 *   Font is 4bpp and MUST be on a 4bpp BG layer.
 *   BG3 in Mode 1 is 2bpp and CANNOT display the 4bpp font.
 *   Font at 0x2000 = tile 0x100 in BG1 space (base 0x1000, 4bpp).
 *   Text tilemap shares BG1 map at 0x0000.
 *
 * IMPORTANT: Zone BG tile data is ~32KB and shares space with font tiles.
 * They are mutually exclusive: flight mode uses BG tiles, text modes
 * (dialog, battle, title) reload the font over the same VRAM region.
 */

/* BG1: Game background + text (4bpp, shared tilemap) */
#define VRAM_BG1_GFX    0x1000    /* BG1 char base */
#define VRAM_BG1_MAP    0x0000    /* BG1 tilemap (shared with text) */

/* Text display (on BG1) */
#define VRAM_TEXT_GFX   0x2000    /* Font tiles: tile 0x100 from BG1 base */
#define VRAM_TEXT_MAP   0x0000    /* Text tilemap: same as BG1 map */

/* BG2: Star parallax layer */
#define VRAM_BG2_GFX    0x5000    /* BG2 tiles: 0x5000-0x503F */
#define VRAM_BG2_MAP    0x0800    /* BG2 tilemap: 0x0800-0x0BFF */

/* Sprites: OBJ tiles */
#define VRAM_OBJ_GFX    0x6000    /* OBJ tiles: 0x6000-0x7FFF */

/*=== Palette Allocation (CGRAM: 256 colors) ===*/
/* BG palettes: 0-7 (128 colors, 16 colors each) */
#define PAL_BG1_MAIN    0         /* BG1 palette 0: main background */
#define PAL_BG2_STARS   1         /* BG2 palette 1: star parallax (CGRAM 16-31) */
#define PAL_BG2_HUD     4         /* BG2 palette 4: HUD elements (future) */
#define PAL_BG3_TEXT    16        /* BG3 palette 0 (of 4 available) */

/* Sprite palettes: 8-15 (128 colors) */
#define PAL_OBJ_PLAYER  8         /* Player ship palette */
#define PAL_OBJ_ENEMY   9         /* Enemy sprites palette (type A) */
#define PAL_OBJ_BULLET  10        /* Player bullet sprites palette */
#define PAL_OBJ_EBULLET 11        /* Enemy bullet sprites palette */
#define PAL_OBJ_ITEMS   12        /* Item/pickup sprites palette */
#define PAL_OBJ_ENEMY2  13        /* Enemy sprites palette (type B) */

/*=== OAM Sprite Slot Allocation (128 total) ===*/
#define OAM_PLAYER      0         /* Slots 0-3: player ship */
#define OAM_PLAYER_MAX  4
#define OAM_BULLETS      4        /* Slots 4-19: player bullets */
#define OAM_BULLETS_MAX  16
#define OAM_ENEMIES      20       /* Slots 20-39: enemy sprites */
#define OAM_ENEMIES_MAX  20
#define OAM_EBULLETS     40       /* Slots 40-55: enemy bullets */
#define OAM_EBULLETS_MAX 16
#define OAM_ITEMS        56       /* Slots 56-63: item pickups */
#define OAM_ITEMS_MAX    8
#define OAM_UI           64       /* Slots 64-79: UI elements */
#define OAM_UI_MAX       16

/*=== Game Constants ===*/
#define ZONE_COUNT       3
#define ZONE_DEBRIS      0
#define ZONE_ASTEROID    1
#define ZONE_FLAGSHIP    2

#define MAX_PLAYER_HP    999
#define MAX_PLAYER_MP    99
#define MAX_LEVEL        10
#define MAX_INVENTORY    8

/*=== Fixed Point Math (8.8 format) ===*/
#define FP8(x)          ((u16)((x) * 256))
#define FP8_INT(x)      ((x) >> 8)
#define FP8_FRAC(x)     ((x) & 0xFF)

/*=== Scroll Speeds (8.8 fixed point, pixels per frame) ===*/
#define SCROLL_SPEED_STOP    0x0000
#define SCROLL_SPEED_SLOW    FP8(0.25)
#define SCROLL_SPEED_NORMAL  FP8(0.5)
#define SCROLL_SPEED_FAST    FP8(1.0)
#define SCROLL_SPEED_RUSH    FP8(2.0)

/*=== Max Scroll Triggers Per Zone ===*/
#define MAX_SCROLL_TRIGGERS  24

#endif /* CONFIG_H */
