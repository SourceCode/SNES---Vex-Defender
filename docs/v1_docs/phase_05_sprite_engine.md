# Phase 5: Sprite Engine & Player Ship

## Objective
Implement the sprite rendering engine using PVSnesLib's OAM system. Load the player ship sprite into VRAM, display it on screen, and create the sprite management layer that all game objects (enemies, bullets, items) will use.

## Prerequisites
- Phase 2 (Hardware Init) complete
- Phase 3 (Asset Pipeline) complete - player sprite converted

## Detailed Tasks

### 1. Create Sprite Manager Module
Abstraction layer over PVSnesLib's OAM for managing multiple sprites with different sizes, palettes, and priorities.

### 2. Load Player Ship Sprite into VRAM
DMA the player ship tile data and palette into the correct VRAM/CGRAM locations.

### 3. Display Player Ship on Screen
Render the player's 32x32 ship sprite at a starting position.

### 4. Create Sprite Animation Framework
Support for sprite frame cycling (ship engine glow, damage flash).

### 5. Create Player Data Structure
The core player struct with position, state, sprite reference.

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/player.h` | CREATE | Player data structures and declarations |
| `src/player.c` | CREATE | Player initialization and sprite setup |
| `src/main.c` | MODIFY | Call player init, render player |
| `Makefile` | MODIFY | Add player.obj |
| `data/linkfile` | MODIFY | Add player.obj |

## Technical Specifications

### Sprite VRAM Layout
```
Sprite tiles in VRAM start at $4000 (word address):
  $4000-$41FF : Player ship (32x32 = 4 tiles of 8x8 in 4bpp = 512 bytes)
  $4200-$43FF : Player bullets (4 x 16x16 = 4 x 128B = 512 bytes)
  $4400-$49FF : Enemy sprites (6 x 32x32 = 6 x 512B = 3072 bytes)
  $4A00-$4BFF : Boss sprites slot (reusable, loaded per encounter)
  $4C00-$4FFF : Item sprites (6 x 16x16 = 768 bytes)
  $5000+      : Effects, asteroids, etc.

OAM Sprite slot allocation:
  Slot 0:      Player ship
  Slots 1-4:   Player bullets (active)
  Slots 5-12:  Enemies (up to 8)
  Slots 13-16: Enemy bullets
  Slots 17-20: Items/pickups
  Slots 21-24: Effects (explosions)
  Slots 25-26: Boss (multi-sprite)
  Slots 27-31: UI cursors, indicators
  Slots 32+:   Reserved
```

### player.h
```c
#ifndef PLAYER_H
#define PLAYER_H

#include <snes.h>
#include "config.h"

/* Player animation frames */
#define PLAYER_FRAME_IDLE    0
#define PLAYER_FRAME_LEFT    1
#define PLAYER_FRAME_RIGHT   2
#define PLAYER_FRAME_DAMAGED 3

/* OAM slot for player */
#define OAM_PLAYER  0

/* Player state structure */
typedef struct {
    s16 x;                 /* X position (screen coords, signed for offscreen) */
    s16 y;                 /* Y position */
    s16 vx;                /* X velocity */
    s16 vy;                /* Y velocity */
    u8  speed;             /* Movement speed */
    u8  anim_frame;        /* Current animation frame */
    u8  anim_timer;        /* Animation timer */
    u8  invincible;        /* Invincibility frames remaining */
    u8  visible;           /* Whether sprite is visible */
    u8  oam_id;            /* OAM slot number */

    /* RPG Stats (expanded in Phase 13) */
    u16 hp;
    u16 max_hp;
    u16 mp;
    u16 max_mp;
    u16 attack;
    u16 defense;
    u16 speed_stat;
    u16 xp;
    u8  level;
} PlayerState;

extern PlayerState g_player;

/* External ASM labels */
extern char spr_player_tiles, spr_player_pal;

/*--- Functions ---*/
void player_init(void);
void player_load_sprite(void);
void player_update_sprite(void);
void player_set_position(s16 x, s16 y);
void player_set_visible(u8 visible);
void player_flash(void);

#endif /* PLAYER_H */
```

### player.c
```c
#include "player.h"

PlayerState g_player;

void player_init(void) {
    /* Initialize player state */
    g_player.x = PLAYER_START_X;
    g_player.y = PLAYER_START_Y;
    g_player.vx = 0;
    g_player.vy = 0;
    g_player.speed = PLAYER_SPEED;
    g_player.anim_frame = PLAYER_FRAME_IDLE;
    g_player.anim_timer = 0;
    g_player.invincible = 0;
    g_player.visible = 1;
    g_player.oam_id = OAM_PLAYER;

    /* Initial RPG stats */
    g_player.hp = PLAYER_MAX_HP;
    g_player.max_hp = PLAYER_MAX_HP;
    g_player.mp = PLAYER_MAX_MP;
    g_player.max_mp = PLAYER_MAX_MP;
    g_player.attack = 10;
    g_player.defense = 5;
    g_player.speed_stat = 8;
    g_player.xp = 0;
    g_player.level = 1;

    /* Load sprite graphics */
    player_load_sprite();

    /* Set initial position */
    player_set_position(g_player.x, g_player.y);
}

void player_load_sprite(void) {
    /* Upload player tile data to VRAM sprite area */
    /* 32x32 sprite = 4 tiles of 8x8 at 4bpp = 512 bytes */
    dmaCopyVram(&spr_player_tiles, VRAM_SPR_TILES, 512);

    /* Upload player palette to CGRAM */
    /* Sprite palettes start at CGRAM index 128 (palettes 8-15 in Mode 1) */
    /* PAL_SPR_PLAYER (5) maps to CGRAM address 128 + (5-0)*32 = 128+160=288? */
    /* Actually: sprite palettes 0-7 map to CGRAM 128-255 (128 + pal*16*2) */
    dmaCopyCGram(&spr_player_pal, 128 + (PAL_SPR_PLAYER * 16 * 2), 32);
}

void player_update_sprite(void) {
    if (!g_player.visible) {
        /* Hide sprite by moving offscreen */
        oamSetEx(g_player.oam_id, OBJ_SMALL, OBJ_HIDE);
        return;
    }

    /* Handle invincibility flashing */
    if (g_player.invincible > 0) {
        g_player.invincible--;
        if (g_player.invincible & 0x02) {
            oamSetEx(g_player.oam_id, OBJ_SMALL, OBJ_HIDE);
            return;
        }
    }

    /* Set sprite in OAM */
    /* oamSet(id, x, y, priority, hflip, vflip, gfxOffset, paletteOffset) */
    oamSet(
        g_player.oam_id * 4,  /* OAM byte offset (4 bytes per entry) */
        g_player.x,            /* X position */
        g_player.y,            /* Y position */
        3,                     /* Priority (3 = on top of BGs) */
        0,                     /* H-flip */
        0,                     /* V-flip */
        0,                     /* Tile number (first sprite tile) */
        PAL_SPR_PLAYER         /* Palette number */
    );

    /* Set sprite size to 32x32 (large) */
    oamSetEx(g_player.oam_id * 4, OBJ_LARGE, OBJ_SHOW);
}

void player_set_position(s16 x, s16 y) {
    /* Clamp to screen bounds */
    if (x < 0) x = 0;
    if (x > SCREEN_WIDTH - 32) x = SCREEN_WIDTH - 32;
    if (y < 0) y = 0;
    if (y > SCREEN_HEIGHT - 32) y = SCREEN_HEIGHT - 32;

    g_player.x = x;
    g_player.y = y;
}

void player_set_visible(u8 visible) {
    g_player.visible = visible;
}

void player_flash(void) {
    g_player.invincible = 60; /* 1 second of flashing at 60fps */
}
```

### OAM Size Configuration
In `game_init()`, add sprite size configuration:
```c
/* Configure sprite sizes: Small=8x8, Large=32x32 */
/* This allows both small bullets and large ships */
oamInitGfxSet(&spr_player_tiles, 512, &spr_player_pal, 32,
              PAL_SPR_PLAYER, VRAM_SPR_TILES, OBJ_SIZE8_L32);
```

## Acceptance Criteria
1. Player ship sprite appears on screen at position (120, 180)
2. Ship sprite has correct colors (not garbled palette)
3. Ship is 32x32 pixels, rendered as a "large" OAM sprite
4. `player_set_visible(0)` hides the sprite completely
5. `player_flash()` causes the sprite to flicker on/off for 1 second
6. No sprite artifacts or flickering during normal display
7. Player position clamps correctly at screen edges

## SNES-Specific Constraints
- OAM has 128 sprite entries, each 4 bytes + 1 bit in high table
- `oamSet()` takes byte offset (id * 4), not slot number
- Sprite palette numbers 0-7 correspond to CGRAM addresses 128-255
- OBJ_SIZE8_L32 means: small sprites are 8x8, large sprites are 32x32
- Only TWO size modes available simultaneously (small + large)
- Sprite X position is 9 bits (0-511), negative X wraps to right side
- Max 32 sprites per scanline - careful with horizontal alignment

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~33KB | 256KB    | ~223KB    |
| WRAM     | ~50B  | 128KB   | ~128KB    |
| VRAM     | ~8.5KB| 64KB    | ~55.5KB   |
| CGRAM    | 64B   | 512B    | 448B      |

## Estimated Complexity
**Medium** - OAM management and VRAM sprite tile addressing are the trickiest parts. Getting the palette index mapping right between CGRAM addresses and oamSet parameters requires care.

## Agent Instructions
1. Create `src/player.h` and `src/player.c`
2. Update Makefile and linkfile to include player.obj
3. In `game_init()`, add sprite system init (oamInit + size config)
4. Call `player_init()` after game_init()
5. Call `player_update_sprite()` in the main loop before WaitForVBlank
6. Build and run - you should see the player ship on screen
7. If sprite appears but colors are wrong, check CGRAM palette address calculation
8. If sprite is garbled, verify VRAM_SPR_TILES address matches oamInitGfxSet
9. Use Mesen sprite viewer to debug OAM and tile data
