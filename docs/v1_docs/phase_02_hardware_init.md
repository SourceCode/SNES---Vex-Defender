# Phase 2: SNES Hardware Initialization & Boot Sequence

## Objective
Implement proper SNES hardware initialization including PPU setup, VRAM clearing, palette initialization, interrupt configuration, and the game's boot sequence that transitions from power-on to the title screen state.

## Prerequisites
- Phase 1 (Project Scaffolding) complete
- ROM compiles and produces a valid .sfc file

## Detailed Tasks

### 1. Implement Full Hardware Init Routine
Configure all PPU registers, clear VRAM/CGRAM/OAM, set up Mode 1 rendering, and configure VBlank interrupt handling.

### 2. Create Boot Sequence
Power-on -> Hardware Init -> Clear Screen -> Fade In -> Ready state

### 3. Implement VBlank Handler
Set up the NMI/VBlank handler for frame synchronization and safe VRAM updates.

### 4. Create Display Control Functions
Screen fade in/out, brightness control, force blank management.

### 5. Set Up Background Layer Configuration
Configure BG1 (game field), BG2 (UI overlay), BG3 (text layer) with proper VRAM addressing.

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/main.c` | MODIFY | Full boot sequence |
| `src/game.c` | CREATE | Game state machine init |
| `src/game.h` | MODIFY | Add init function declarations |
| `include/config.h` | MODIFY | Add hardware register defines if needed |

## Technical Specifications

### game.c - Game Initialization
```c
#include "game.h"

/* Global game state */
GameState g_game;

/* Initialize all game systems */
void game_init(void) {
    /* Zero out game state */
    g_game.current_state = STATE_BOOT;
    g_game.current_zone = ZONE_DEBRIS;
    g_game.paused = 0;
    g_game.frame_counter = 0;
    g_game.score = 0;
    g_game.story_flags = 0;

    /* Initialize SNES hardware */
    consoleInit();

    /* Set video mode: Mode 1
     * BG1: 4bpp (16 colors) - Main game background
     * BG2: 4bpp (16 colors) - UI / Battle background
     * BG3: 2bpp (4 colors)  - Text overlay
     */
    setMode(BG_MODE1, 0);

    /* Configure BG tile data locations in VRAM */
    bgSetGfxPtr(0, VRAM_BG1_TILES);  /* BG1 tiles at $0000 */
    bgSetGfxPtr(1, VRAM_BG2_TILES);  /* BG2 tiles at $2000 */
    bgSetGfxPtr(2, VRAM_BG3_TILES);  /* BG3 tiles at $3000 */

    /* Configure BG tilemap locations in VRAM */
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x32);  /* BG1 map at $5000 */
    bgSetMapPtr(1, VRAM_BG2_MAP, SC_32x32);  /* BG2 map at $5800 */
    bgSetMapPtr(2, VRAM_BG3_MAP, SC_32x32);  /* BG3 map at $6000 */

    /* Initialize sprite/OAM system */
    oamInit();

    /* Disable BG2 and BG3 initially (enabled when needed) */
    bgSetDisable(1);
    bgSetDisable(2);

    /* Enable BG1 and sprites */
    bgSetEnable(0);

    /* Set initial state to title */
    g_game.current_state = STATE_TITLE;
}

/* Screen fade effect using brightness register */
void screen_fade_in(void) {
    u8 i;
    for (i = 0; i < 16; i++) {
        setBrightness(i);
        WaitForVBlank();
        WaitForVBlank(); /* 2 frames per step = ~0.5 second fade */
    }
}

void screen_fade_out(void) {
    s8 i;
    for (i = 15; i >= 0; i--) {
        setBrightness(i);
        WaitForVBlank();
        WaitForVBlank();
    }
}
```

### Updated main.c
```c
/*=============================================================
 * VEX DEFENDER - Main Entry Point
 *=============================================================*/
#include <snes.h>
#include "game.h"

int main(void) {
    /* Initialize all hardware and game systems */
    game_init();

    /* Turn on screen at full brightness */
    setScreenOn();

    /* Main game loop */
    while(1) {
        /* Process input (Phase 6) */
        /* input_update(); */

        /* Update game logic based on state (Phase 15) */
        /* game_update(); */

        /* Render current frame (Phase 15) */
        /* game_render(); */

        /* Increment frame counter */
        g_game.frame_counter++;

        /* Wait for VBlank - frame sync */
        WaitForVBlank();
    }

    return 0;
}
```

### Updated game.h
```c
#ifndef GAME_H
#define GAME_H

#include <snes.h>
#include "config.h"

/*--- Global Game State ---*/
typedef struct {
    u8 current_state;
    u8 current_zone;
    u8 paused;
    u16 frame_counter;
    u16 score;
    u8 story_flags;
} GameState;

extern GameState g_game;

/*--- Phase 2: Init & Display ---*/
void game_init(void);
void screen_fade_in(void);
void screen_fade_out(void);

/*--- Phase 6: Input ---*/
/* void input_update(void); */

/*--- Phase 15: Game Loop ---*/
/* void game_update(void); */
/* void game_render(void); */

#endif /* GAME_H */
```

### Updated Makefile (add game.c)
```makefile
# Add to OFILES:
export OFILES := src/main.obj src/game.obj
```

### Updated linkfile
```
[objects]
data/hdr.obj
data/data.obj
src/main.obj
src/game.obj
```

## Acceptance Criteria
1. ROM boots to a black screen without crashing
2. No graphical artifacts on screen
3. VBlank interrupt fires correctly (frame counter increments)
4. Mode 1 is active (verifiable in Mesen debugger: PPU > BGMODE register = $09)
5. VRAM is clean (all zeros except where initialized)
6. Screen fade in/out functions work when called

## SNES-Specific Constraints
- `consoleInit()` must be called before ANY other PVSnesLib function
- `setMode()` must be called before `bgSetGfxPtr()` / `bgSetMapPtr()`
- VRAM can only be safely written during VBlank or when Force Blank is active
- The frame counter will overflow at 65535 (~18 minutes at 60fps) - acceptable for a 10-minute game
- `setBrightness(0)` = screen off, `setBrightness(15)` = full brightness

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~2KB | 256KB     | ~254KB    |
| WRAM     | ~20B | 128KB     | ~128KB    |
| VRAM     | 0    | 64KB      | 64KB      |
| CGRAM    | 0    | 512B      | 512B      |

## Estimated Complexity
**Simple** - Standard PVSnesLib initialization patterns. The key is getting VRAM address assignments right for Mode 1.

## Agent Instructions
1. Modify `src/main.c` to call `game_init()`
2. Create `src/game.c` with the initialization code
3. Update `game.h` with new function declarations
4. Update the Makefile OFILES and linkfile to include game.obj
5. Build and run - screen should be black with no artifacts
6. Test in Mesen: check BGMODE register, verify VBlank NMI is firing
