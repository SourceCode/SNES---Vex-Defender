/*==============================================================================
 * Background Rendering System
 *
 * Loads zone backgrounds on BG1, generates a procedural star parallax
 * layer on BG2, and runs a star twinkle effect via palette cycling.
 *============================================================================*/

#include "engine/background.h"
#include "assets.h"

/*--- Star twinkle colors (BGR555 format) ---*/
#define STAR_BRIGHT  0x7FFF   /* Pure white */
#define STAR_MEDIUM  0x56B5   /* Light grey */
#define STAR_DIM     0x318C   /* Dark grey */

#define TWINKLE_SPEED 8       /* Frames between twinkle updates */

/*--- State ---*/
static u8 current_zone;
static u8 palette_dirty;
static u8 twinkle_timer;

/* Working buffer for the 3 cycling star colors (CGRAM entries 17-19) */
static u16 star_cycle[3];

/* Procedural star tilemap for BG2 (32x32 entries, 2 bytes each) */
static u16 star_map[1024];

/*---------------------------------------------------------------------------
 * generateStarMap - Fill star_map[] with scattered star dot tiles
 *
 * Uses a fixed-seed LCG so the pattern is reproducible. ~5% of tiles
 * get a star dot (tile 1, 2, or 3); the rest are empty (tile 0).
 * All entries use BG palette 1 (CGRAM 16-31).
 *---------------------------------------------------------------------------*/
static void generateStarMap(void)
{
    u16 i;
    u16 seed;
    u16 tile;

    seed = 0xBEEF;
    for (i = 0; i < 1024; i++) {
        seed = seed * 31421u + 6927u;
        if ((seed & 0xFF) < 13) {
            tile = 1 + ((seed >> 8) % 3);
        } else {
            tile = 0;
        }
        star_map[i] = tile | (u16)(1 << 10); /* BG_TIL_PAL(1) */
    }
}

/*---------------------------------------------------------------------------*/

void bgSystemInit(void)
{
    current_zone = BG_ZONE_NONE;
    palette_dirty = 0;
    twinkle_timer = 0;
    star_cycle[0] = STAR_BRIGHT;
    star_cycle[1] = STAR_MEDIUM;
    star_cycle[2] = STAR_DIM;
}

void bgLoadZone(u8 zoneId)
{
    current_zone = zoneId;

    setScreenOff();

    /*--- BG1: Load zone background ---*/
    switch (zoneId) {
        case ZONE_DEBRIS:
            bgInitTileSet(0, &zone1_bg_til, &zone1_bg_pal, 0,
                          ASSET_SIZE(zone1_bg_til),
                          ASSET_SIZE(zone1_bg_pal),
                          BG_16COLORS, VRAM_BG1_GFX);
            bgInitMapSet(0, (u8 *)&zone1_bg_map,
                         ASSET_SIZE(zone1_bg_map),
                         SC_32x32, VRAM_BG1_MAP);
            break;
        case ZONE_ASTEROID:
            bgInitTileSet(0, &zone2_bg_til, &zone2_bg_pal, 0,
                          ASSET_SIZE(zone2_bg_til),
                          ASSET_SIZE(zone2_bg_pal),
                          BG_16COLORS, VRAM_BG1_GFX);
            bgInitMapSet(0, (u8 *)&zone2_bg_map,
                         ASSET_SIZE(zone2_bg_map),
                         SC_32x32, VRAM_BG1_MAP);
            break;
        case ZONE_FLAGSHIP:
            bgInitTileSet(0, &zone3_bg_til, &zone3_bg_pal, 0,
                          ASSET_SIZE(zone3_bg_til),
                          ASSET_SIZE(zone3_bg_pal),
                          BG_16COLORS, VRAM_BG1_GFX);
            bgInitMapSet(0, (u8 *)&zone3_bg_map,
                         ASSET_SIZE(zone3_bg_map),
                         SC_32x32, VRAM_BG1_MAP);
            break;
        default:
            return;
    }

    bgSetEnable(0);
    bgSetScroll(0, 0, 0);

    /*--- BG2: Procedural star parallax layer ---*/
    generateStarMap();

    /* Upload star tiles (from data.asm) to BG2 VRAM */
    /* star_tiles = 4 tiles * 32 bytes = 128 bytes */
    dmaCopyVram(&star_tiles, VRAM_BG2_GFX, 128);

    /* Upload star palette to BG palette 1 (CGRAM colors 16-31) */
    /* star_pal = 16 colors * 2 bytes = 32 bytes */
    dmaCopyCGram(&star_pal, 16, 32);

    /* Upload procedural star map */
    dmaCopyVram((u8 *)star_map, VRAM_BG2_MAP, 1024 * 2);

    bgSetEnable(1);
    bgSetScroll(1, 0, 0);

    /* Reset twinkle state */
    star_cycle[0] = STAR_BRIGHT;
    star_cycle[1] = STAR_MEDIUM;
    star_cycle[2] = STAR_DIM;
    twinkle_timer = 0;
    palette_dirty = 0;

    /* Leave in force blank - caller calls setScreenOn() */
}

void bgUpdate(void)
{
    u16 temp;
    if (current_zone == BG_ZONE_NONE) return;

    /* Star twinkle: rotate brightness of BG2 star colors every N frames */
    twinkle_timer++;
    if (twinkle_timer >= TWINKLE_SPEED) {
        twinkle_timer = 0;

        /* Rotate: [0] -> [1] -> [2] -> [0] */
        temp = star_cycle[0];
        star_cycle[0] = star_cycle[1];
        star_cycle[1] = star_cycle[2];
        star_cycle[2] = temp;

        palette_dirty = 1;
    }
}

void bgVBlankUpdate(void)
{
    if (palette_dirty) {
        /* Update CGRAM colors 17, 18, 19 (star dot colors in BG palette 1) */
        dmaCopyCGram((u8 *)star_cycle, 17, 6);
        palette_dirty = 0;
    }
}

void bgSetParallaxVisible(u8 visible)
{
    if (visible) {
        bgSetEnable(1);
    } else {
        bgSetDisable(1);
    }
}

u8 bgGetCurrentZone(void)
{
    return current_zone;
}
