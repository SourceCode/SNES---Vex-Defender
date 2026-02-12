# Phase 2: SNES Hardware Initialization & Boot Sequence

## Objective
Implement a proper multi-stage boot sequence that initializes all SNES hardware subsystems, displays a brief splash/logo frame, and transitions to a stable idle state. Establish the NMI (VBlank) handler framework that all subsequent phases will use.

## Prerequisites
- Phase 1 (Project Scaffolding) must be complete.

## Detailed Tasks

1. Create the VBlank (NMI) handler framework in `src/engine/vblank.c` that manages per-frame updates via function pointer dispatch.

2. Create `src/engine/system.c` with hardware initialization that goes beyond consoleInit():
   - Clear all VRAM, OAM, and CGRAM explicitly
   - Set Mode 1 with correct tile sizes
   - Configure OBJ size (16x16 small, 32x32 large)
   - Set sprite tile base address
   - Initialize all BG scroll registers to 0
   - Configure HDMA channels (reserve channel 7 for effects)
   - Set brightness to 0 (for fade-in capability)

3. Create `src/engine/fade.c` with brightness fade-in/fade-out functions that operate over N frames.

4. Create the game's boot sequence in main.c:
   - System hardware init
   - Display black screen for 30 frames (0.5 sec)
   - Fade in to show a simple "VOIDRUNNER" text on BG3 using consoleDrawText
   - Hold for 120 frames (2 sec)
   - Fade out
   - Enter main game loop (idle state for now)

5. Implement a frame counter and basic timing utility.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/include/engine/vblank.h
```c
#ifndef VBLANK_H
#define VBLANK_H

#include "game.h"

/* VBlank callback function type */
typedef void (*VBlankCallback)(void);

/* Maximum number of VBlank callbacks (DMA transfers, OAM update, etc.) */
#define MAX_VBLANK_CALLBACKS 4

/* Initialize VBlank handler system */
void vblankInit(void);

/* Register a callback to be called during VBlank
 * Returns slot index (0-3) or 0xFF on failure */
u8 vblankRegisterCallback(VBlankCallback cb);

/* Remove a callback by slot index */
void vblankRemoveCallback(u8 slot);

/* Remove all callbacks */
void vblankClearCallbacks(void);

/* The actual NMI handler installed via nmiSet() */
void vblankHandler(void);

/* Global frame counter (wraps at 65535) */
extern u16 g_frame_count;

/* Frames elapsed this game session */
extern u16 g_session_frames;

#endif
```

### J:/code/snes/snes-rpg-test/src/engine/vblank.c
```c
/*==============================================================================
 * VBlank Handler Framework
 * Called every frame during vertical blank interrupt.
 * Must be fast - do only DMA transfers and register writes here.
 *============================================================================*/

#include "engine/vblank.h"

u16 g_frame_count;
u16 g_session_frames;

static VBlankCallback vblank_callbacks[MAX_VBLANK_CALLBACKS];
static u8 vblank_callback_count;

void vblankInit(void)
{
    u8 i;
    g_frame_count = 0;
    g_session_frames = 0;
    vblank_callback_count = 0;
    for (i = 0; i < MAX_VBLANK_CALLBACKS; i++) {
        vblank_callbacks[i] = (VBlankCallback)0;
    }
    /* Install our handler via PVSnesLib */
    nmiSet(&vblankHandler);
}

u8 vblankRegisterCallback(VBlankCallback cb)
{
    u8 i;
    for (i = 0; i < MAX_VBLANK_CALLBACKS; i++) {
        if (vblank_callbacks[i] == (VBlankCallback)0) {
            vblank_callbacks[i] = cb;
            vblank_callback_count++;
            return i;
        }
    }
    return 0xFF;
}

void vblankRemoveCallback(u8 slot)
{
    if (slot < MAX_VBLANK_CALLBACKS && vblank_callbacks[slot]) {
        vblank_callbacks[slot] = (VBlankCallback)0;
        vblank_callback_count--;
    }
}

void vblankClearCallbacks(void)
{
    u8 i;
    for (i = 0; i < MAX_VBLANK_CALLBACKS; i++) {
        vblank_callbacks[i] = (VBlankCallback)0;
    }
    vblank_callback_count = 0;
}

void vblankHandler(void)
{
    u8 i;

    /* Only do work on non-lag frames */
    if (vblank_flag) {
        /* Execute registered callbacks */
        for (i = 0; i < MAX_VBLANK_CALLBACKS; i++) {
            if (vblank_callbacks[i]) {
                vblank_callbacks[i]();
            }
        }
    }

    g_frame_count++;
    g_session_frames++;
}
```

### J:/code/snes/snes-rpg-test/include/engine/system.h
```c
#ifndef SYSTEM_H
#define SYSTEM_H

#include "game.h"

/* Full hardware initialization
 * Call once at boot before anything else. */
void systemInit(void);

/* Re-initialize video for a scene change
 * Enters force blank, clears relevant VRAM regions */
void systemResetVideo(void);

/* Wait for N frames (blocking) */
void systemWaitFrames(u16 count);

#endif
```

### J:/code/snes/snes-rpg-test/src/engine/system.c
```c
/*==============================================================================
 * System Initialization
 *============================================================================*/

#include "engine/system.h"
#include "engine/vblank.h"

void systemInit(void)
{
    /* PVSnesLib core init: sets force blank, clears hardware */
    consoleInit();

    /* Set Mode 1: BG1=16col, BG2=16col, BG3=4col, 8x8 tiles */
    setMode(BG_MODE1, 0);

    /* Configure sprite size: small=16x16, large=32x32 */
    /* OBJ tile base at VRAM word address $0000 */
    oamInitGfxAttr(OBJ_TILES_VRAM, OBJ_SIZE16_L32);

    /* Initialize OAM - hide all 128 sprites */
    oamInit();
    oamClear(0, 0);

    /* Set BG tile and map addresses per our VRAM map */
    bgSetGfxPtr(0, BG1_TILES_VRAM);
    bgSetMapPtr(0, BG1_TILEMAP_VRAM, SC_32x32);

    bgSetGfxPtr(1, BG2_TILES_VRAM);
    bgSetMapPtr(1, BG2_TILEMAP_VRAM, SC_32x32);

    bgSetGfxPtr(2, BG3_TILES_VRAM);
    bgSetMapPtr(2, BG3_TILEMAP_VRAM, SC_32x32);

    /* Clear all BG scroll registers */
    bgSetScroll(0, 0, 0);
    bgSetScroll(1, 0, 0);
    bgSetScroll(2, 0, 0);

    /* Disable all BGs initially */
    bgSetDisable(0);
    bgSetDisable(1);
    bgSetDisable(2);

    /* Set brightness to 0 for fade-in capability */
    setBrightness(0);

    /* Initialize VBlank handler system */
    vblankInit();
}

void systemResetVideo(void)
{
    /* Enter force blank for safe VRAM access */
    setScreenOff();

    /* Clear VRAM (sets all 64KB to 0) */
    dmaClearVram();

    /* Re-hide all sprites */
    oamClear(0, 0);

    /* Reset scroll */
    bgSetScroll(0, 0, 0);
    bgSetScroll(1, 0, 0);
    bgSetScroll(2, 0, 0);
}

void systemWaitFrames(u16 count)
{
    u16 i;
    for (i = 0; i < count; i++) {
        WaitForVBlank();
    }
}
```

### J:/code/snes/snes-rpg-test/include/engine/fade.h
```c
#ifndef FADE_H
#define FADE_H

#include "game.h"

/* Start a fade-in from black over 'frames' number of frames
 * brightness goes from 0 to 15 */
void fadeIn(u8 frames);

/* Start a fade-out to black over 'frames' number of frames
 * brightness goes from 15 to 0 */
void fadeOut(u8 frames);

/* Non-blocking fade update - call once per frame in main loop
 * Returns 1 while fade is active, 0 when complete */
u8 fadeUpdate(void);

/* Blocking fade: starts fade and waits until complete */
void fadeInBlocking(u8 frames);
void fadeOutBlocking(u8 frames);

#endif
```

### J:/code/snes/snes-rpg-test/src/engine/fade.c
```c
/*==============================================================================
 * Brightness Fade Engine
 * Uses fixed-point accumulator for smooth brightness transitions.
 *============================================================================*/

#include "engine/fade.h"

static u8 fade_active;
static u8 fade_direction;   /* 1 = fading in, 2 = fading out */
static u16 fade_brightness; /* 8.8 fixed point: high byte = actual brightness */
static s16 fade_step;       /* 8.8 fixed point step per frame */

void fadeIn(u8 frames)
{
    if (frames == 0) {
        setBrightness(15);
        fade_active = 0;
        return;
    }
    fade_active = 1;
    fade_direction = 1;
    fade_brightness = 0;
    /* Step = (15 << 8) / frames */
    fade_step = (s16)((15 * 256) / frames);
    if (fade_step < 1) fade_step = 1;
    setBrightness(0);
}

void fadeOut(u8 frames)
{
    if (frames == 0) {
        setBrightness(0);
        fade_active = 0;
        return;
    }
    fade_active = 1;
    fade_direction = 2;
    fade_brightness = (u16)(15 << 8);
    fade_step = (s16)((15 * 256) / frames);
    if (fade_step < 1) fade_step = 1;
    setBrightness(15);
}

u8 fadeUpdate(void)
{
    u8 bright;
    if (!fade_active) return 0;

    if (fade_direction == 1) {
        /* Fading in */
        fade_brightness += fade_step;
        if (fade_brightness >= (u16)(15 << 8)) {
            fade_brightness = (u16)(15 << 8);
            fade_active = 0;
        }
    } else {
        /* Fading out */
        if (fade_brightness <= (u16)fade_step) {
            fade_brightness = 0;
            fade_active = 0;
        } else {
            fade_brightness -= fade_step;
        }
    }

    bright = (u8)(fade_brightness >> 8);
    if (bright > 15) bright = 15;
    setBrightness(bright);

    return fade_active;
}

void fadeInBlocking(u8 frames)
{
    fadeIn(frames);
    while (fadeUpdate()) {
        WaitForVBlank();
    }
}

void fadeOutBlocking(u8 frames)
{
    fadeOut(frames);
    while (fadeUpdate()) {
        WaitForVBlank();
    }
}
```

### J:/code/snes/snes-rpg-test/src/main.c (updated)
```c
/*==============================================================================
 * VOIDRUNNER - Main Entry Point
 * Phase 2: Boot sequence with splash text
 *============================================================================*/

#include "game.h"
#include "engine/system.h"
#include "engine/vblank.h"
#include "engine/fade.h"

void bootSequence(void)
{
    /* Initialize all hardware */
    systemInit();

    /* Wait 30 frames (0.5s) black screen for hardware settle */
    setScreenOn();
    systemWaitFrames(30);

    /* Enable BG3 for text display */
    /* consoleInitText uses BG3 by default with PVSnesLib font */
    consoleInitText(0, BG_4COLORS, 0, 0);

    /* The consoleInitText call above loads PVSnesLib's built-in font.
     * We can use consoleDrawText to write to the screen. */
    bgSetEnable(2);  /* BG3 = index 2 */

    consoleDrawText(8, 12, "VOIDRUNNER");
    consoleDrawText(5, 14, "PRESS START");

    /* Fade in over 30 frames */
    fadeInBlocking(30);

    /* Hold splash for 120 frames (2 seconds) */
    systemWaitFrames(120);

    /* Fade out over 30 frames */
    fadeOutBlocking(30);

    /* Clean up text BG for next scene */
    bgSetDisable(2);
}

int main(void)
{
    bootSequence();

    /* Main game loop - idle for now (subsequent phases add logic) */
    while (1) {
        WaitForVBlank();
    }

    return 0;
}
```

## Technical Specifications

### NMI Handler Architecture
```
VBlank Interrupt fires
  -> PVSnesLib VBlank ISR
    -> Transfers oamMemory to OAM (non-lag frames only)
    -> Calls nmi_handler (our vblankHandler)
      -> Checks vblank_flag (skip if lag frame)
      -> Iterates callback array [0..3]
        -> Slot 0: typically OAM/DMA updates
        -> Slot 1: typically scroll register updates
        -> Slot 2: typically palette updates
        -> Slot 3: typically HDMA buffer swap
    -> Increments frame counters
    -> Reads controller inputs (non-lag frames)
    -> Clears vblank_flag
```

### Brightness Fade Math
```
Fixed-point 8.8 brightness:
  0x0000 = brightness 0 (black)
  0x0F00 = brightness 15 (full)

For 30-frame fade-in:
  step = (15 * 256) / 30 = 128 = 0x0080
  Frame 0:  brightness = 0x0000 >> 8 = 0
  Frame 1:  brightness = 0x0080 >> 8 = 0
  Frame 2:  brightness = 0x0100 >> 8 = 1
  Frame 4:  brightness = 0x0200 >> 8 = 2
  ...
  Frame 30: brightness = 0x0F00 >> 8 = 15
```

### consoleInitText Default Behavior
PVSnesLib's consoleInitText with NULL tile/palette pointers uses a built-in 8x8 ASCII font. It configures:
- Font tiles loaded to the BG that consoleSetTextGfxPtr points to (default $0800)
- Text map at consoleSetTextMapPtr address
- For our project, we override these in later phases, but Phase 2 uses defaults for splash text.

Note: consoleInitText defaults may conflict with our VRAM map. For Phase 2, this is acceptable because we only use BG3 for text. In Phase 16 (Dialog System), we will use consoleSetTextGfxPtr and consoleSetTextMapPtr to relocate the font to our designated VRAM addresses.

## Acceptance Criteria
1. ROM boots to a black screen, waits 0.5 seconds.
2. "VOIDRUNNER" and "PRESS START" text appears on screen via a smooth brightness fade-in (over ~0.5 seconds).
3. Text stays visible for 2 seconds.
4. Screen smoothly fades to black.
5. ROM remains stable in idle loop (no crashes, no visual glitches) indefinitely after fade-out.
6. g_frame_count increments by ~60 per second on NTSC (verified via debug register or emulator debugger).
7. No VRAM corruption visible in emulator VRAM viewer.

## SNES-Specific Constraints
- nmiSet() disables IRQ and re-enables NMI + joypad auto-read. Must be called after consoleInit().
- setBrightness() writes to REG_INIDISP ($2100). Only safe during VBlank or force blank.
- consoleDrawText() modifies a RAM buffer that is DMA'd to VRAM during VBlank. Must call WaitForVBlank() after to see changes.
- The VBlank handler must be very fast (< ~2200 CPU cycles at 3.58MHz for safety). Avoid loops, function calls with many params, or DMA transfers over 4KB per frame.

## Estimated Complexity
**Simple** - Standard SNES initialization pattern, well-documented in PVSnesLib examples.
