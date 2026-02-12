/*==============================================================================
 * Background Rendering System
 *
 * Loads zone backgrounds on BG1, generates a procedural star parallax
 * layer on BG2, and runs a star twinkle effect via palette cycling.
 *
 * SNES Mode 1 background configuration during flight:
 *   BG1 (4bpp): Zone-specific space background (nebula, asteroids, etc.)
 *     - Tiles at VRAM_BG1_GFX, tilemap at VRAM_BG1_MAP
 *     - Uses BG palette 0 (CGRAM colors 0-15)
 *     - Scrolls vertically at full speed via scroll engine
 *
 *   BG2 (4bpp): Procedural star parallax layer
 *     - Tiles at VRAM_BG2_GFX, tilemap at VRAM_BG2_MAP
 *     - Uses BG palette 1 (CGRAM colors 16-31)
 *     - Scrolls at half speed for depth parallax effect
 *     - Star brightness cycles via palette rotation (twinkle)
 *
 * The star layer is procedurally generated to save ROM space.
 * A 1024-entry tilemap (32x32 tiles, 2 bytes each) is filled with a
 * seeded PRNG, using ~5% star density across 3 brightness tiles.
 *
 * Palette cycling updates only 3 CGRAM entries (6 bytes) per cycle,
 * making it extremely cheap in terms of VBlank DMA bandwidth.
 *============================================================================*/

#include "engine/background.h"
#include "engine/scroll.h"
#include "assets.h"

/*--- Star twinkle colors (SNES BGR555 format) ---*/
/* BGR555: 5 bits per channel, blue in high bits, red in low bits.
 * 0x7FFF = %0111_1111_1111_1111 = max white (31,31,31)
 * 0x56B5 = approx (21,21,21) = light grey
 * 0x318C = approx (12,12,12) = dark grey */
#define STAR_BRIGHT  0x7FFF   /* Pure white - brightest star state */
#define STAR_MEDIUM  0x56B5   /* Light grey - medium brightness */
#define STAR_DIM     0x318C   /* Dark grey  - dimmest star state */

/* Frames between twinkle color rotations (zone-dependent, modified in bgLoadZone).
 * Lower values = faster twinkling = more visual intensity for later zones. */
static u8 twinkle_speed;

/*--- State ---*/
static u8 current_zone;    /* Currently loaded zone ID (ZONE_* or BG_ZONE_NONE) */
static u8 palette_dirty;   /* 1 if star palette needs uploading to CGRAM during VBlank */
static u8 twinkle_timer;   /* Frame counter for twinkle timing, counts up to twinkle_speed */

/* Working buffer for the 3 cycling star colors.
 * These are uploaded to CGRAM entries 17, 18, 19 (BG palette 1, colors 1-3).
 * Color 0 of palette 1 (CGRAM 16) is transparent/unused.
 * The rotation is: [0] -> [1] -> [2] -> [0] (circular shift). */
static u16 star_cycle[3];

/* Procedural star tilemap for BG2 (32x32 entries, 2 bytes each = 2KB).
 * Generated at zone load time and uploaded to VRAM once. After that,
 * the PPU reads it directly from VRAM during rendering. */
static u16 star_map[1024];

/*---------------------------------------------------------------------------
 * generateStarMap - Fill star_map[] with scattered star dot tiles.
 *
 * Uses a seeded xorshift16 PRNG (seed 0xBEEF) so the pattern is
 * deterministic and reproducible across runs. The seed choice is arbitrary
 * but produces a visually pleasing distribution.
 *
 * Algorithm:
 *   For each of 1024 tilemap entries:
 *     1. Advance the xorshift16 PRNG (3 shifts + 3 XORs, no multiply)
 *     2. If (seed & 0xFF) < 13, place a star (~5% probability: 13/256)
 *     3. Choose star tile 1, 2, or 3 based on upper byte of seed
 *     4. Otherwise, place empty tile 0
 *     5. All entries use BG palette 1 (bit 10 set in tilemap word)
 *
 * The "& 3 with clamp 3->0" trick avoids modulo 3 (expensive software
 * division on the 65816) while producing a close-enough-to-uniform
 * distribution across tiles 1, 2, 3. The slight bias (tile 1 has ~33%
 * more probability) is visually imperceptible.
 *
 * SNES tilemap word format (for Mode 1, 4bpp):
 *   Bits 0-9:   Tile number (0-1023)
 *   Bits 10-12: Palette number (0-7 for BG, palette 1 = stars)
 *   Bit 13:     Priority (0 or 1)
 *   Bit 14:     Horizontal flip
 *   Bit 15:     Vertical flip
 *---------------------------------------------------------------------------*/
static void generateStarMap(void)
{
    u16 i;
    u16 seed;
    u16 tile;
    u8 tmp;

    seed = 0xBEEF;  /* Fixed seed for deterministic star pattern */
    for (i = 0; i < 1024; i++) {
        /* xorshift16 PRNG: 3 shifts + 3 XORs, no multiply needed.
         * Period = 65535 (full 16-bit range minus zero). Fast and
         * produces good enough randomness for star placement. */
        seed ^= seed << 7;
        seed ^= seed >> 9;
        seed ^= seed << 8;

        /* ~5% star density: low byte < 13 out of 256 possible values */
        if ((seed & 0xFF) < 13) {
            /* Select star tile (1, 2, or 3).
             * Uses upper byte of seed for independence from the density check.
             * & 3 maps to {0,1,2,3}; clamp 3 to 0 gives approximate uniform
             * distribution over {0,1,2}. Final tile = 1 + result. */
            tmp = (seed >> 8) & 3;
            if (tmp == 3) tmp = 0;  /* Avoid % 3 (software division on 65816) */
            tile = 1 + tmp;         /* Star tiles are numbered 1, 2, 3 */
        } else {
            tile = 0;  /* Empty tile (transparent) */
        }
        /* OR in palette 1 via bit 10. BG_TIL_PAL(1) = 1 << 10 = 0x0400.
         * This tells the PPU to use CGRAM colors 16-31 for this tile. */
        star_map[i] = tile | (u16)(1 << 10);
    }
}

/*---------------------------------------------------------------------------*/

/*
 * bgSystemInit - Initialize background system state.
 *
 * Resets to "no zone loaded" state with default twinkle parameters.
 * Star cycle colors are initialized to the three brightness levels.
 */
void bgSystemInit(void)
{
    current_zone = BG_ZONE_NONE;
    palette_dirty = 0;
    twinkle_timer = 0;
    twinkle_speed = 8;  /* Default: rotate every 8 frames */
    star_cycle[0] = STAR_BRIGHT;
    star_cycle[1] = STAR_MEDIUM;
    star_cycle[2] = STAR_DIM;
}

/*
 * bgLoadZone - Load a complete zone background into VRAM.
 *
 * This is a heavy function that performs multiple DMA transfers to VRAM
 * and CGRAM. It MUST be called during force blank (screen off) because
 * VRAM is only writable when the PPU is not actively rendering.
 *
 * Loading sequence:
 *   1. Set zone-dependent twinkle speed (faster for later, more intense zones)
 *   2. Enter force blank (setScreenOff)
 *   3. Load BG1 tiles + palette + tilemap for the zone
 *   4. Generate procedural star map for BG2
 *   5. Upload star tiles, palette, and map to BG2 VRAM/CGRAM
 *   6. Enable BG1 and BG2, reset scroll positions
 *   7. Reset twinkle animation state
 *
 * The screen remains in force blank after this function returns.
 * The caller is responsible for calling setScreenOn() or fadeIn()
 * when ready to display the loaded background.
 *
 * Parameters:
 *   zoneId - ZONE_DEBRIS (0), ZONE_ASTEROID (1), or ZONE_FLAGSHIP (2)
 */
void bgLoadZone(u8 zoneId)
{
    current_zone = zoneId;

    /* Zone-dependent twinkle speed: later zones twinkle faster for
     * increasing visual intensity. Debris (calm) = 8 frames per cycle,
     * Flagship (intense) = 4 frames per cycle. */
    switch (zoneId) {
        case ZONE_DEBRIS:   twinkle_speed = 8; break;
        case ZONE_ASTEROID: twinkle_speed = 6; break;
        case ZONE_FLAGSHIP: twinkle_speed = 4; break;
        default:            twinkle_speed = 8; break;
    }

    /* Enter force blank. The SNES PPU disables rendering and allows
     * unrestricted VRAM/CGRAM/OAM access via DMA. setScreenOff() writes
     * 0x80 to INIDISP ($2100), setting bit 7 (force blank). */
    setScreenOff();

    /*--- BG1: Load zone-specific background ---*/
    /* bgInitTileSet loads tiles to VRAM and palette to CGRAM.
     * bgInitMapSet loads the tilemap to VRAM.
     * Parameters: BG number, tile data, palette data, palette offset,
     *             tile size, palette size, color depth, VRAM address.
     * BG_16COLORS = 4bpp (16 colors per palette). */
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
            return;  /* Unknown zone - bail out (screen still in force blank) */
    }

    /* Enable BG1 layer in the PPU and reset its scroll position */
    bgSetEnable(0);
    bgSetScroll(0, 0, 0);

    /*--- BG2: Procedural star parallax layer ---*/
    /* Generate the 32x32 star tilemap in WRAM using the xorshift16 PRNG */
    generateStarMap();

    /* Upload star dot tiles (from data.asm) to BG2 VRAM.
     * 4 tiles (empty, bright, medium, dim) * 32 bytes each = 128 bytes.
     * These are small 8x8 tiles with a single colored pixel for the star dot. */
    dmaCopyVram(&star_tiles, VRAM_BG2_GFX, 128);

    /* Upload star palette to BG palette 1 (CGRAM entries 16-31).
     * 16 colors * 2 bytes = 32 bytes. Only colors 1-3 are actually used
     * (color 0 is transparent). Colors 1-3 correspond to the three
     * star brightness levels that get palette-cycled. */
    dmaCopyCGram(&star_pal, 16, 32);

    /* Upload the procedurally generated star tilemap to BG2 VRAM.
     * 32x32 entries * 2 bytes each = 2048 bytes. */
    dmaCopyVram((u8 *)star_map, VRAM_BG2_MAP, 1024 * 2);

    /* Enable BG2 layer and reset its scroll position */
    bgSetEnable(1);
    bgSetScroll(1, 0, 0);

    /* Reset twinkle state to initial brightness order */
    star_cycle[0] = STAR_BRIGHT;
    star_cycle[1] = STAR_MEDIUM;
    star_cycle[2] = STAR_DIM;
    twinkle_timer = 0;
    palette_dirty = 0;

    /* Screen remains in force blank. Caller calls setScreenOn() or fadeIn()
     * when the full scene setup (sprites, scroll, etc.) is complete. */
}

/*
 * bgLoadStarsOnly - Load only the BG2 star parallax layer.
 *
 * Extracts the BG2 star loading logic from bgLoadZone() into a standalone
 * function for use by screens that want animated stars without a zone
 * background on BG1 (e.g., the title screen).
 *
 * Sets current_zone to ZONE_DEBRIS so bgUpdate() twinkle logic works.
 * The caller must already be in force blank (setScreenOff).
 */
void bgLoadStarsOnly(void)
{
    /* Set current_zone so bgUpdate() twinkle animation is active */
    current_zone = ZONE_DEBRIS;
    twinkle_speed = 8;  /* Calm, slow twinkle for title atmosphere */

    /* Generate procedural 32x32 star tilemap */
    generateStarMap();

    /* Upload star tiles (128 bytes) to BG2 VRAM */
    dmaCopyVram(&star_tiles, VRAM_BG2_GFX, 128);

    /* Upload star palette to CGRAM palette 1 (entries 16-31) */
    dmaCopyCGram(&star_pal, 16, 32);

    /* Upload star tilemap to BG2 VRAM */
    dmaCopyVram((u8 *)star_map, VRAM_BG2_MAP, 1024 * 2);

    /* Configure BG2 hardware registers */
    bgSetGfxPtr(1, VRAM_BG2_GFX);
    bgSetMapPtr(1, VRAM_BG2_MAP, SC_32x32);
    bgSetEnable(1);
    bgSetScroll(1, 0, 0);

    /* Reset twinkle state */
    star_cycle[0] = STAR_BRIGHT;
    star_cycle[1] = STAR_MEDIUM;
    star_cycle[2] = STAR_DIM;
    twinkle_timer = 0;
    palette_dirty = 0;
}

/*
 * bgUpdate - Per-frame background update for star twinkle animation.
 *
 * The twinkle effect works by rotating the three star brightness colors
 * in a circular pattern every N frames:
 *   Frame 0: [BRIGHT, MEDIUM, DIM]
 *   Frame N: [MEDIUM, DIM, BRIGHT]
 *   Frame 2N: [DIM, BRIGHT, MEDIUM]
 *   Frame 3N: [BRIGHT, MEDIUM, DIM]  (repeats)
 *
 * Since different star tiles use different palette entries, and the
 * palette entries rotate, each star appears to change brightness over
 * time - creating a twinkling effect with zero CPU cost per star.
 * Only 3 CGRAM entries (6 bytes) are updated per rotation.
 *
 * During fast scroll (SCROLL_SPEED_FAST or higher), twinkle speed
 * is overridden to 2 frames for more energetic visuals.
 *
 * Does nothing if:
 *   - No zone is loaded (BG_ZONE_NONE)
 *   - Scroll speed is 0 (game is paused or transitioning)
 */
void bgUpdate(void)
{
    u16 temp;
    u16 scroll_speed;
    u8 effective_speed;
    if (current_zone == BG_ZONE_NONE) return;  /* No zone loaded, nothing to animate */

    /* Cache scroll speed to avoid multiple function calls.
     * This is safe because scrollGetSpeed cannot change between
     * bgUpdate and bgVBlankUpdate in the same frame (single-threaded). */
    scroll_speed = scrollGetSpeed();

    /* Skip star twinkle when scrolling is stopped (pause/transition).
     * This creates a subtle visual cue that the game is not actively scrolling. */
    if (scroll_speed == 0) return;

    /* Override twinkle speed during fast scroll for more intense visuals */
    effective_speed = twinkle_speed;
    if (scroll_speed >= SCROLL_SPEED_FAST) {
        effective_speed = 2;  /* Very fast twinkling during rush sequences */
    }

    /* Star twinkle: rotate brightness colors every effective_speed frames */
    twinkle_timer++;
    if (twinkle_timer >= effective_speed) {
        twinkle_timer = 0;

        /* Circular rotation: [0] <- [1] <- [2] <- [0] (saved in temp).
         * This is equivalent to: bright star becomes medium, medium becomes
         * dim, dim becomes bright. */
        temp = star_cycle[0];
        star_cycle[0] = star_cycle[1];
        star_cycle[1] = star_cycle[2];
        star_cycle[2] = temp;

        /* Mark palette as needing upload during next VBlank */
        palette_dirty = 1;
    }
}

/*
 * bgVBlankUpdate - Upload modified star palette to CGRAM during VBlank.
 *
 * Only uploads when the palette has been rotated (dirty flag set by bgUpdate).
 * Writes 3 colors (6 bytes) to CGRAM entries 17, 18, 19.
 * These are colors 1, 2, 3 of BG palette 1 (the star dot colors).
 *
 * CGRAM writes MUST happen during VBlank because the SNES PPU reads
 * CGRAM during active display. Writing during rendering causes color
 * glitches (incorrect colors for one or more scanlines).
 *
 * dmaCopyCGram uses DMA for the transfer, which completes in ~6 cycles
 * per byte (36 cycles total for 6 bytes). Well within VBlank budget.
 */
void bgVBlankUpdate(void)
{
    if (palette_dirty) {
        /* Upload the 3 rotated star colors to CGRAM entries 17-19 */
        dmaCopyCGram((u8 *)star_cycle, 17, 6);
        palette_dirty = 0;
    }
}

/*
 * bgSetParallaxVisible - Toggle the BG2 star parallax layer.
 *
 * Uses PVSnesLib's bgSetEnable/bgSetDisable which modify the TM register
 * ($212C) to enable or disable the BG2 layer in the main screen.
 *
 * Parameters:
 *   visible - 1 to show BG2, 0 to hide BG2
 */
void bgSetParallaxVisible(u8 visible)
{
    if (visible) {
        bgSetEnable(1);   /* Enable BG2 in PPU TM register */
    } else {
        bgSetDisable(1);  /* Disable BG2 in PPU TM register */
    }
}

/*
 * bgGetCurrentZone - Get the currently loaded zone ID.
 * Returns: ZONE_DEBRIS, ZONE_ASTEROID, ZONE_FLAGSHIP, or BG_ZONE_NONE (0xFF).
 */
u8 bgGetCurrentZone(void)
{
    return current_zone;
}
