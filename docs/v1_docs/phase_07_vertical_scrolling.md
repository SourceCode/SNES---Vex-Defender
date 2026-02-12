# Phase 7: Vertical Scrolling Engine

## Objective
Expand the background scrolling system into a full vertical scrolling engine with multi-layer parallax, dynamic scroll speed changes for gameplay moments, and seamless background tiling for endless vertical scrolling during flight sequences.

## Prerequisites
- Phase 4 (Background Rendering) complete
- Phase 6 (Input System) complete

## Detailed Tasks

### 1. Implement Multi-Layer Parallax Scrolling
Use BG1 as the main starfield (fast scroll) and BG2 as a distant nebula/star layer (slow scroll) to create depth.

### 2. Create Seamless Vertical Tiling
Backgrounds must tile vertically without visible seams. The 32x32 tilemap (256x256 pixels) wraps naturally via hardware, but we need to ensure art tiles at top and bottom edges match.

### 3. Implement Dynamic Scroll Speed Control
Speed changes for gameplay events:
- Normal flight: moderate scroll
- Entering battle: scroll slows to stop
- Boss approach: scroll speeds up dramatically then stops
- Zone transition: scroll accelerates, flash, new zone loads

### 4. Add HDMA Horizontal Wave Effect (Optional Enhancement)
Use HDMA to create a subtle horizontal wave distortion on BG2 for a "heat shimmer" or "space distortion" effect.

### 5. Implement Scroll Position Tracking for Spawn Triggers
Track cumulative scroll distance to trigger enemy spawns and story events at specific positions.

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `src/scroll.h` | MODIFY | Add parallax, speed control, triggers |
| `src/scroll.c` | MODIFY | Implement full scrolling engine |
| `src/game.c` | MODIFY | Connect scroll to game state |
| `include/config.h` | MODIFY | Add scroll-related constants |

## Technical Specifications

### Parallax Configuration
```
Layer Setup (Mode 1):
  BG1: Main starfield    - Scroll speed: 1.0x (base speed)
  BG2: Distant nebula    - Scroll speed: 0.25x (parallax effect)
  BG3: Text/UI overlay   - No scroll (fixed)

Fixed-point scroll speeds (8.8 format):
  Normal:   BG1 = $0080 (0.5 px/frame),  BG2 = $0020 (0.125 px/frame)
  Fast:     BG1 = $0200 (2.0 px/frame),  BG2 = $0080 (0.5 px/frame)
  Slow:     BG1 = $0020 (0.125 px/frame), BG2 = $0008
  Stopped:  BG1 = $0000,                  BG2 = $0000
```

### Enhanced scroll.h additions
```c
/* Scroll speed presets (8.8 fixed-point) */
#define SCROLL_SPEED_STOP     0x0000
#define SCROLL_SPEED_SLOW     0x0020
#define SCROLL_SPEED_NORMAL   0x0080
#define SCROLL_SPEED_FAST     0x0200
#define SCROLL_SPEED_WARP     0x0400

/* Scroll state enhancements */
#define SCROLL_MODE_CONSTANT  0  /* Steady scroll */
#define SCROLL_MODE_ACCEL     1  /* Accelerating */
#define SCROLL_MODE_DECEL     2  /* Decelerating */
#define SCROLL_MODE_STOPPED   3  /* Not scrolling */

/* Scroll trigger - fires when cumulative distance reaches threshold */
typedef struct {
    u16 distance;          /* Trigger at this cumulative scroll distance */
    u8  event_type;        /* What event to fire */
    u8  event_data;        /* Event parameter */
    u8  fired;             /* Already triggered? */
} ScrollTrigger;

#define MAX_SCROLL_TRIGGERS 16

/* Add to BackgroundState */
/*
    u8  scroll_mode;
    u16 target_speed;
    u16 accel_rate;
    u32 cumulative_scroll;
    ScrollTrigger triggers[MAX_SCROLL_TRIGGERS];
    u8  trigger_count;
*/
```

### Enhanced scroll.c additions
```c
void bg_update(void) {
    /* Handle scroll mode transitions */
    switch(g_bg.scroll_mode) {
        case SCROLL_MODE_ACCEL:
            if (g_bg.scroll_speed < g_bg.target_speed) {
                g_bg.scroll_speed += g_bg.accel_rate;
                if (g_bg.scroll_speed > g_bg.target_speed)
                    g_bg.scroll_speed = g_bg.target_speed;
            }
            break;

        case SCROLL_MODE_DECEL:
            if (g_bg.scroll_speed > g_bg.target_speed) {
                if (g_bg.scroll_speed > g_bg.accel_rate)
                    g_bg.scroll_speed -= g_bg.accel_rate;
                else
                    g_bg.scroll_speed = 0;
                if (g_bg.scroll_speed <= g_bg.target_speed) {
                    g_bg.scroll_speed = g_bg.target_speed;
                    if (g_bg.target_speed == 0)
                        g_bg.scroll_mode = SCROLL_MODE_STOPPED;
                }
            }
            break;

        case SCROLL_MODE_STOPPED:
            /* Do nothing */
            break;

        default: /* CONSTANT */
            break;
    }

    /* Apply scroll */
    g_bg.scroll_y += g_bg.scroll_speed;
    g_bg.cumulative_scroll += g_bg.scroll_speed;

    /* Set BG1 scroll (integer part of 8.8 fixed) */
    bgSetScroll(0, 0, (u16)(g_bg.scroll_y >> 8));

    /* Parallax: BG2 at 1/4 speed */
    g_bg.parallax_y += (g_bg.scroll_speed >> 2);
    bgSetScroll(1, 0, (u16)(g_bg.parallax_y >> 8));

    /* Check scroll triggers */
    bg_check_triggers();
}

void bg_check_triggers(void) {
    u8 i;
    u16 dist = (u16)(g_bg.cumulative_scroll >> 8); /* Integer pixels */

    for (i = 0; i < g_bg.trigger_count; i++) {
        if (!g_bg.triggers[i].fired && dist >= g_bg.triggers[i].distance) {
            g_bg.triggers[i].fired = 1;
            /* Fire event - handled by game state machine */
            game_handle_scroll_event(
                g_bg.triggers[i].event_type,
                g_bg.triggers[i].event_data
            );
        }
    }
}

void bg_set_speed_gradual(u16 target, u16 accel) {
    g_bg.target_speed = target;
    g_bg.accel_rate = accel;
    if (target > g_bg.scroll_speed)
        g_bg.scroll_mode = SCROLL_MODE_ACCEL;
    else if (target < g_bg.scroll_speed)
        g_bg.scroll_mode = SCROLL_MODE_DECEL;
}

void bg_stop_scroll(void) {
    bg_set_speed_gradual(SCROLL_SPEED_STOP, 0x0004);
}

void bg_resume_scroll(void) {
    bg_set_speed_gradual(SCROLL_SPEED_NORMAL, 0x0004);
}
```

### HDMA Wave Effect (Optional)
```c
/* HDMA table for horizontal wave distortion on BG2 */
/* Each entry: [scanline_count] [scroll_offset_lo] [scroll_offset_hi] */
u8 hdma_wave_table[224 * 3]; /* 3 bytes per scanline */

/* Sine lookup table for wave (pre-calculated, 0-255 range) */
const u8 sine_table[64] = {
    0,6,12,18,25,31,37,43,49,54,60,65,71,76,81,85,
    90,94,98,102,106,109,112,115,118,120,122,124,125,
    126,127,127,127,127,127,126,125,124,122,120,118,
    115,112,109,106,102,98,94,90,85,81,76,71,65,60,
    54,49,43,37,31,25,18,12,6,0
};

void hdma_build_wave_table(u8 phase, u8 amplitude) {
    u16 i;
    s8 offset;
    for (i = 0; i < 224; i++) {
        u8 sine_idx = ((i + phase) >> 1) & 0x3F;
        offset = (s8)((sine_table[sine_idx] * amplitude) >> 7);
        hdma_wave_table[i * 3 + 0] = 1;       /* 1 scanline */
        hdma_wave_table[i * 3 + 1] = offset;   /* BG2HOFS lo */
        hdma_wave_table[i * 3 + 2] = 0;        /* BG2HOFS hi */
    }
    hdma_wave_table[224 * 3 - 3] = 0; /* End marker */
}
```

## Acceptance Criteria
1. Background scrolls vertically at a consistent speed
2. Parallax effect visible - BG2 scrolls slower than BG1
3. `bg_stop_scroll()` gradually decelerates to stopped
4. `bg_resume_scroll()` gradually accelerates back to normal speed
5. Background wraps seamlessly (no visible seam line at tile boundary)
6. Scroll triggers fire at correct distance thresholds
7. Zone transitions work: fade -> load new BG -> fade in -> resume scroll

## SNES-Specific Constraints
- BG scroll registers (210D-2114h) are write-twice registers - PVSnesLib handles this
- HDMA must be configured during VBlank
- Max 8 HDMA channels (shared with DMA)
- Scroll values wrap at 1024 pixels (10 bits) - hardware handles wrapping
- Background tilemaps must be aligned on 1KB boundaries in VRAM

## Memory Budget (Cumulative)
| Resource | Used | Available | Remaining |
|----------|------|-----------|-----------|
| ROM      | ~35KB | 256KB    | ~221KB    |
| WRAM     | ~750B | 128KB   | ~127KB    |
| VRAM     | ~8.5KB| 64KB    | ~55.5KB   |
| CGRAM    | 64B   | 512B    | 448B      |

## Estimated Complexity
**Medium** - Fixed-point math and scroll trigger system are straightforward. HDMA wave effect is optional but adds significant visual polish.

## Agent Instructions
1. Modify `src/scroll.h` to add new structs and function declarations
2. Modify `src/scroll.c` to implement enhanced scrolling
3. Add `bg_check_triggers()` and `bg_set_speed_gradual()` functions
4. Test gradual speed changes: call bg_stop_scroll() and verify smooth deceleration
5. Set up test triggers and verify they fire at correct scroll distances
6. The HDMA wave effect is optional - implement only if base scrolling works perfectly
7. Test in Mesen: use the BG viewer to verify both layers scroll at different rates
