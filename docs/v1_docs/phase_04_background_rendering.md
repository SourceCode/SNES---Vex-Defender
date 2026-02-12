# Phase 4: Background Rendering System

## Objective
Implement the background rendering system that loads and displays space/starfield backgrounds on BG1, with support for swapping backgrounds between zones and displaying the title screen background. This forms the visual foundation for the vertical scrolling shooter.

## Prerequisites
- Phase 1 (Project Scaffolding) complete
- Phase 2 (Hardware Init) complete
- Phase 3 (Asset Pipeline) complete - backgrounds converted to .pic/.pal/.map

## Detailed Tasks

### 1. Create Background Manager Module
A system to load, swap, and manage background tile data and tilemaps in VRAM.

### 2. Implement Background Loading Functions
Functions to DMA background tiles, palettes, and tilemaps into VRAM/CGRAM.

### 3. Create a Simple Starfield Parallax Layer
Use BG2 as a secondary starfield layer for parallax depth effect.

### 4. Implement Background Transition (Zone Changes)
Fade out -> load new BG -> fade in sequence for zone transitions.

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/scroll.h` | CREATE | Background/scroll system header |
| `src/scroll.c` | CREATE | Background rendering implementation |
| `src/main.c` | MODIFY | Call background init |
| `Makefile` | MODIFY | Add scroll.obj |
| `data/linkfile` | MODIFY | Add scroll.obj |

## Technical Specifications

### scroll.h
```c
#ifndef SCROLL_H
#define SCROLL_H

#include <snes.h>
#include "config.h"

/* Background IDs */
#define BG_DEBRIS    0
#define BG_ASTEROID  1
#define BG_FLAGSHIP  2
#define BG_TITLE     3

/* Background data structure */
typedef struct {
    u8  active_bg;         /* Currently loaded background ID */
    u16 scroll_y;          /* Current vertical scroll position (8.8 fixed) */
    u16 scroll_speed;      /* Scroll speed (8.8 fixed-point) */
    u16 parallax_y;        /* BG2 parallax scroll position */
    u8  parallax_speed;    /* BG2 scroll speed (slower) */
    u8  is_scrolling;      /* Whether auto-scroll is active */
} BackgroundState;

extern BackgroundState g_bg;

/* External ASM labels for background data */
extern char bg_debris_tiles, bg_debris_pal, bg_debris_map;
extern char bg_asteroid_tiles, bg_asteroid_pal, bg_asteroid_map;
extern char bg_flagship_tiles, bg_flagship_pal, bg_flagship_map;
extern char bg_title_tiles, bg_title_pal, bg_title_map;

/* Background tile/pal/map size constants */
/* These will be set based on actual converted asset sizes */
#define BG_TILES_SIZE   4096   /* Estimated max tile data per BG */
#define BG_PAL_SIZE     32     /* 16 colors x 2 bytes */
#define BG_MAP_SIZE     2048   /* 32x32 x 2 bytes */

/*--- Functions ---*/
void bg_init(void);
void bg_load(u8 bg_id);
void bg_update(void);
void bg_set_scroll_speed(u16 speed);
void bg_transition(u8 new_bg_id);

#endif /* SCROLL_H */
```

### scroll.c
```c
#include "scroll.h"

BackgroundState g_bg;

/* Lookup tables for background data pointers */
/* Index by BG_DEBRIS, BG_ASTEROID, etc. */

void bg_init(void) {
    g_bg.active_bg = BG_DEBRIS;
    g_bg.scroll_y = 0;
    g_bg.scroll_speed = 0x0080;  /* 0.5 pixels per frame in 8.8 fixed */
    g_bg.parallax_y = 0;
    g_bg.parallax_speed = 1;     /* BG2 scrolls at ~0.25x speed */
    g_bg.is_scrolling = 1;

    /* Load the first background */
    bg_load(BG_DEBRIS);
}

void bg_load(u8 bg_id) {
    u8 *tiles, *pal, *map;

    /* Select data pointers based on ID */
    switch(bg_id) {
        case BG_DEBRIS:
            tiles = &bg_debris_tiles;
            pal   = &bg_debris_pal;
            map   = &bg_debris_map;
            break;
        case BG_ASTEROID:
            tiles = &bg_asteroid_tiles;
            pal   = &bg_asteroid_pal;
            map   = &bg_asteroid_map;
            break;
        case BG_FLAGSHIP:
            tiles = &bg_flagship_tiles;
            pal   = &bg_flagship_pal;
            map   = &bg_flagship_map;
            break;
        case BG_TITLE:
            tiles = &bg_title_tiles;
            pal   = &bg_title_pal;
            map   = &bg_title_map;
            break;
        default:
            return;
    }

    /* Upload tile data to VRAM via DMA */
    bgInitTileSet(
        0,                  /* BG1 */
        tiles,              /* Tile source */
        pal,                /* Palette source */
        PAL_BG1_MAIN,      /* Palette index 0 */
        BG_TILES_SIZE,      /* Tile data size */
        BG_PAL_SIZE,        /* Palette size */
        BG_16COLORS,        /* 4bpp mode */
        VRAM_BG1_TILES      /* VRAM destination */
    );

    /* Upload tilemap to VRAM */
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);
    dmaCopyVram(map, VRAM_BG1_MAP, BG_MAP_SIZE);

    /* Reset scroll position */
    g_bg.scroll_y = 0;
    g_bg.parallax_y = 0;
    g_bg.active_bg = bg_id;
}

void bg_update(void) {
    if (!g_bg.is_scrolling) return;

    /* Update main background scroll (8.8 fixed-point) */
    g_bg.scroll_y += g_bg.scroll_speed;

    /* Set BG1 vertical scroll (use integer part: high byte) */
    bgSetScroll(0, 0, (u16)(g_bg.scroll_y >> 8));

    /* Update parallax layer (slower) */
    g_bg.parallax_y += g_bg.parallax_speed;
    /* bgSetScroll(1, 0, g_bg.parallax_y); */ /* Enable when BG2 parallax active */
}

void bg_set_scroll_speed(u16 speed) {
    g_bg.scroll_speed = speed;
}

void bg_transition(u8 new_bg_id) {
    /* Fade out */
    screen_fade_out();

    /* Load new background */
    bg_load(new_bg_id);

    /* Fade in */
    screen_fade_in();
}
```

### VRAM Layout for Backgrounds
```
BG1 (Main scrolling background):
  Tiles: $0000-$1FFF (16KB max, typically 2-4KB used with tile reduction)
  Map:   $5000-$57FF (2KB for 32x32 tilemap)

BG2 (Parallax / UI background):
  Tiles: $2000-$2FFF (8KB)
  Map:   $5800-$5FFF (2KB)

BG3 (Text overlay):
  Tiles: $3000-$3FFF (8KB)
  Map:   $6000-$67FF (2KB)
```

## Acceptance Criteria
1. Background loads and displays correctly (visible stars/space in emulator)
2. Vertical scrolling works smoothly at consistent speed
3. `bg_load()` can switch between all 4 background sets without artifacts
4. `bg_transition()` produces a smooth fade-out/fade-in between backgrounds
5. No VRAM corruption when switching backgrounds
6. Background wraps seamlessly when scroll_y overflows (32x32 tilemap = 256px wrap)

## SNES-Specific Constraints
- BG scroll registers are 10-bit (0-1023) - wrapping is automatic
- Tile data must be uploaded during VBlank or Force Blank
- DMA transfers should not exceed VBlank time (~2.6KB per VBlank safely)
- For backgrounds larger than 256px, use SC_64x32 or SC_32x64 map sizes
- Fixed-point 8.8 arithmetic: `value >> 8` gives integer part, `value & 0xFF` gives fraction
- `bgSetScroll()` must be called every frame (values are not latched)

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~32KB | 256KB    | ~224KB    |
| WRAM     | ~30B  | 128KB   | ~128KB    |
| VRAM     | ~8KB  | 64KB    | ~56KB     |
| CGRAM    | 32B   | 512B    | 480B      |

## Estimated Complexity
**Medium** - The main challenge is correctly sizing tile data and ensuring DMA transfers complete within VBlank. The fixed-point scrolling math is straightforward.

## Agent Instructions
1. Create `src/scroll.h` and `src/scroll.c`
2. Add `scroll.obj` to Makefile OFILES and linkfile
3. In `main.c`, call `bg_init()` after `game_init()`
4. In the main loop, call `bg_update()` before `WaitForVBlank()`
5. Test: background should scroll vertically at a slow, steady speed
6. Test: call `bg_transition(BG_ASTEROID)` to verify background switching
7. If tiles look garbled, the BG_TILES_SIZE constant may need adjustment based on actual .pic file sizes
8. Use Mesen's VRAM viewer to verify tile data is at correct addresses
