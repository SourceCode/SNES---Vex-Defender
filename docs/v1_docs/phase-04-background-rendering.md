# Phase 4: Background Rendering System (Starfield/Space Backgrounds)

## Objective
Implement the multi-layer space background system using SNES Mode 1 BG layers. BG1 displays the primary scrolling star/nebula background. BG2 is reserved for UI overlays but in the flight mode shows a parallax star layer or is disabled. Create a procedural star twinkle effect using palette cycling.

## Prerequisites
- Phase 1 (Project Scaffolding), Phase 2 (Hardware Init), Phase 3 (Asset Pipeline).

## Detailed Tasks

1. Create `src/engine/background.c` with functions to load and display backgrounds.
2. Implement per-zone background loading that swaps tile/palette/map data into VRAM.
3. Implement palette cycling on BG1 for a star twinkle effect (cycle 2-3 bright star colors every few frames).
4. Implement a simple HDMA color gradient on the backdrop color (color 0) to create a subtle space atmosphere effect (dark blue at top, pure black at bottom).
5. Create a procedural "distant star" layer on BG2 using a minimal tileset (just a few star dot tiles on a black background) for parallax depth.
6. Set up BG3 as the text/HUD layer with transparent background.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/engine/background.h
```c
#ifndef BACKGROUND_H
#define BACKGROUND_H

#include "game.h"

/* Zone IDs for background selection */
#define ZONE_NONE       0
#define ZONE_TITLE      1
#define ZONE_DEBRIS     2   /* Zone 1: Debris Field */
#define ZONE_ASTEROID   3   /* Zone 2: Asteroid Belt */
#define ZONE_FLAGSHIP   4   /* Zone 3: Flagship Assault */
#define ZONE_BATTLE     5   /* Battle scene */

/* Initialize the background system */
void bgSystemInit(void);

/* Load a zone's background (tiles, palette, map) into VRAM
 * This enters force blank, loads data, then returns.
 * Caller must call setScreenOn() after. */
void bgLoadZone(u8 zoneId);

/* Update background effects each frame (palette cycling, etc.)
 * Call from main loop BEFORE WaitForVBlank. */
void bgUpdate(void);

/* VBlank callback: upload modified palette to CGRAM
 * Registered automatically by bgSystemInit. */
void bgVBlankUpdate(void);

/* Set whether the star parallax layer (BG2) is visible */
void bgSetParallaxVisible(u8 visible);

/* Get the current zone ID */
u8 bgGetCurrentZone(void);

#endif
```

### J:/code/snes/snes-rpg-test/src/engine/background.c
```c
/*==============================================================================
 * Background Rendering System
 *
 * Mode 1 layer usage during flight:
 *   BG1 (16-color): Primary space background, scrolls vertically
 *   BG2 (16-color): Parallax star dots (slower scroll)
 *   BG3 (4-color):  HUD text overlay (fixed, transparent BG)
 *
 * Effects:
 *   - Palette cycling on BG1 for star twinkle (colors 2-4)
 *   - HDMA gradient on backdrop color (optional)
 *============================================================================*/

#include "engine/background.h"
#include "engine/vblank.h"

/* Extern labels from data.asm */
extern char zone1_bg_tiles, zone1_bg_tiles_end;
extern char zone1_bg_pal, zone1_bg_pal_end;
extern char zone1_bg_map, zone1_bg_map_end;
/* Additional zone externs added in Phase 18 */

/* State */
static u8 current_zone;
static u8 palette_dirty;

/* Palette cycling state */
static u16 bg_palette_ram[16];  /* Working copy of BG1 palette */
static u8 twinkle_timer;
static u8 twinkle_phase;

#define TWINKLE_SPEED 8   /* Frames between twinkle updates */
#define TWINKLE_COLORS 3  /* Number of colors to cycle (indices 2, 3, 4) */

/* Procedural star tile data for BG2 parallax layer */
/* 4 tiles: empty black, single dot center, dot top-left, dot bottom-right */
/* Each 8x8 tile at 4bpp = 32 bytes */
static const u8 star_tiles[] = {
    /* Tile 0: empty (all zeros) - 32 bytes */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    /* Tile 1: single white dot at (3,3) */
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0, /* row 0-2: empty */
    0x10,0,0,0,0,0,0,0, /* row 3: pixel at x=3 (bitplane 0) */
    0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,
    /* Tile 2: dot at (1,1) */
    0,0,0,0,0,0,0,0,
    0x40,0,0,0,0,0,0,0, /* row 1: pixel at x=1 */
    0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,
    /* Tile 3: dot at (6,5) */
    0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,
    0x02,0,0,0,0,0,0,0, /* row 5: pixel at x=6 */
    0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0
};

/* Simple star dot palette: transparent + white */
static const u8 star_palette[] = {
    0x00, 0x00,  /* Color 0: transparent (black) */
    0xFF, 0x7F,  /* Color 1: bright white (BGR555: 11111 11111 11111) */
    0xDE, 0x7B,  /* Color 2: slightly dimmer white */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00
};

/* Procedural star tilemap for BG2 (32x32 tile entries)
 * Generated at runtime by scattering star tiles across the map */
static u16 star_map_ram[32 * 32];

static void generateStarMap(void)
{
    u16 i;
    u16 seed;
    u8 tile;

    seed = 0xBEEF;  /* Fixed seed for reproducible star pattern */
    for (i = 0; i < 32 * 32; i++) {
        /* Simple LCG pseudo-random */
        seed = seed * 31421 + 6927;
        /* ~5% chance of a star tile (1, 2, or 3) */
        if ((seed & 0xFF) < 13) {
            tile = 1 + ((seed >> 8) % 3);
        } else {
            tile = 0;  /* empty */
        }
        /* Palette 1 (BG2 uses palette entry 1 = CGRAM 16-31) */
        star_map_ram[i] = tile | BG_TIL_PAL(1);
    }
}

void bgSystemInit(void)
{
    current_zone = ZONE_NONE;
    palette_dirty = 0;
    twinkle_timer = 0;
    twinkle_phase = 0;
}

void bgLoadZone(u8 zoneId)
{
    u16 tileSize, palSize, mapSize;
    u8 *tileSrc, *palSrc, *mapSrc;

    current_zone = zoneId;

    /* Select data pointers based on zone */
    switch (zoneId) {
        case ZONE_DEBRIS:
            tileSrc = &zone1_bg_tiles;
            tileSize = &zone1_bg_tiles_end - &zone1_bg_tiles;
            palSrc = &zone1_bg_pal;
            palSize = &zone1_bg_pal_end - &zone1_bg_pal;
            mapSrc = &zone1_bg_map;
            mapSize = &zone1_bg_map_end - &zone1_bg_map;
            break;
        /* Zone 2, 3, battle, title cases added in Phase 18 */
        default:
            return;
    }

    /* Enter force blank for safe VRAM writes */
    setScreenOff();

    /* Load BG1 tiles and palette */
    bgInitTileSet(0, tileSrc, palSrc, 0,
                  tileSize, palSize,
                  BG_16COLORS, BG1_TILES_VRAM);

    /* Load BG1 tilemap */
    bgInitMapSet(0, mapSrc, mapSize,
                 SC_32x32, BG1_TILEMAP_VRAM);

    /* Copy palette to RAM for cycling */
    {
        u8 i;
        for (i = 0; i < 16; i++) {
            bg_palette_ram[i] = palSrc[i * 2] | ((u16)palSrc[i * 2 + 1] << 8);
        }
    }

    /* Load BG2 star parallax layer */
    generateStarMap();
    dmaCopyVram((u8 *)star_tiles, BG2_TILES_VRAM, sizeof(star_tiles));
    dmaCopyCGram((u8 *)star_palette, 16, sizeof(star_palette));
    dmaCopyVram((u8 *)star_map_ram, BG2_TILEMAP_VRAM, 32 * 32 * 2);
    bgSetMapPtr(1, BG2_TILEMAP_VRAM, SC_32x32);

    /* Enable BG1 and BG2 */
    bgSetEnable(0);
    bgSetEnable(1);

    /* Reset scroll */
    bgSetScroll(0, 0, 0);
    bgSetScroll(1, 0, 0);

    /* Reset twinkle state */
    twinkle_timer = 0;
    twinkle_phase = 0;
    palette_dirty = 0;
}

void bgUpdate(void)
{
    u16 temp;
    if (current_zone == ZONE_NONE) return;

    /* Star twinkle: rotate brightness of colors 2, 3, 4 in BG1 palette */
    twinkle_timer++;
    if (twinkle_timer >= TWINKLE_SPEED) {
        twinkle_timer = 0;
        twinkle_phase++;
        if (twinkle_phase >= TWINKLE_COLORS) twinkle_phase = 0;

        /* Rotate colors 2, 3, 4: shift each to the next position */
        temp = bg_palette_ram[2];
        bg_palette_ram[2] = bg_palette_ram[3];
        bg_palette_ram[3] = bg_palette_ram[4];
        bg_palette_ram[4] = temp;

        palette_dirty = 1;
    }
}

void bgVBlankUpdate(void)
{
    if (palette_dirty) {
        dmaCopyCGram((u8 *)bg_palette_ram, 0, 32);
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
```

## Technical Specifications

### BG Layer Scroll Rates for Parallax Effect
```
During vertical flight scrolling (Phase 7):
  BG1 (main background): scrolls at 1.0x speed (primary)
  BG2 (star parallax):   scrolls at 0.5x speed (half speed = distant stars)
  BG3 (HUD text):        no scroll (fixed on screen)

Implementation: Each frame, bg1_scroll_y += scroll_speed.
               bg2_scroll_y += scroll_speed >> 1.
               bgSetScroll(0, 0, bg1_scroll_y);
               bgSetScroll(1, 0, bg2_scroll_y);
```

### Palette Cycling for Star Twinkle
```
BG1 palette colors 2, 3, 4 are "star highlight" colors.
Original values might be:
  Color 2: bright white  (0x7FFF)
  Color 3: medium white  (0x5AD6)
  Color 4: dim white     (0x318C)

Every TWINKLE_SPEED frames, rotate: 2->3->4->2
This creates a pulsing twinkle effect on any tile using those colors.
```

### Memory Budget
```
BG1 tiles:   ~4KB (128 unique tiles max for a 256x256 bg with reduction)
BG1 palette: 32 bytes
BG1 map:     2KB (32x32)
BG2 tiles:   128 bytes (4 star tiles)
BG2 palette: 32 bytes
BG2 map:     2KB (32x32)
Palette RAM:  32 bytes (working copy for cycling)
Star map RAM: 2KB (generated once)
Total WRAM: ~2.1KB
Total VRAM: ~8.2KB (well within budget)
```

## Acceptance Criteria
1. Zone 1 background displays correctly on BG1 with proper colors.
2. Star parallax layer on BG2 shows scattered white dots on black.
3. Stars twinkle (brightness shifts) every ~8 frames.
4. No visible tearing or glitches during palette cycling.
5. bgLoadZone() can be called to swap backgrounds without crashes.
6. BG2 can be toggled on/off with bgSetParallaxVisible().
7. VRAM viewer in Mesen shows tiles at correct addresses per vram_map.h.

## SNES-Specific Constraints
- Palette DMA (dmaCopyCGram) must happen during VBlank only.
- BG2 in Mode 1 shares the same tile size setting as BG1. Both use 8x8 here.
- The star tile data must be in SNES 4bpp planar format (not packed). Verify with emulator tile viewer.
- BG palette 0 = CGRAM 0-15, BG palette 1 = CGRAM 16-31. Star layer uses palette 1.

## Estimated Complexity
**Medium** - Background loading is straightforward, but procedural star generation and palette cycling require careful timing to avoid visual glitches.
