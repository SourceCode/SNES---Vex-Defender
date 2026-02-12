/*==============================================================================
 * Sprite Engine
 *
 * Manages a pool of sprite entities and maps them to SNES OAM (Object
 * Attribute Memory) slots. All game objects that need on-screen sprites
 * (player, enemies, items) allocate through this system.
 *
 * SNES OAM overview:
 *   - 128 OAM entries, each 4 bytes (X, Y, tile, attributes) + 2-bit
 *     extension table for X bit 8 and size select.
 *   - PVSnesLib maintains a shadow OAM buffer in WRAM; oamSet() writes
 *     to this buffer, and the NMI handler DMAs it to PPU OAM during VBlank.
 *   - Each entry's byte offset = slot_index * 4 (hence oam_id = i * 4).
 *
 * Pool allocation uses a roving hint pointer (alloc_hint) to amortize
 * the cost of scanning for free slots. After freeing a sprite, the hint
 * is pulled back to that index so subsequent allocations fill gaps first.
 *
 * SNES OBJ VRAM layout:
 *   OBJ tiles are arranged in a 16-name-wide character grid. Each "name"
 *   is one 8x8 tile (32 bytes in 4bpp). For multi-tile sprites:
 *     - 16x16 = 2x2 names, rows separated by 16 names (256 VRAM words)
 *     - 32x32 = 4x4 names, rows separated by 16 names (256 VRAM words)
 *   The spriteLoadTiles16/32 functions handle this row-strided DMA layout.
 *============================================================================*/

#ifndef SPRITES_H
#define SPRITES_H

#include "game.h"

/*=== Entity States ===*/
/* Used by the 'active' field to track lifecycle. ENTITY_DYING allows
 * death animations to play before the slot is recycled. */
#define ENTITY_INACTIVE  0  /* Slot is free for allocation */
#define ENTITY_ACTIVE    1  /* Slot is in use and should be updated/rendered */
#define ENTITY_DYING     2  /* Slot is playing a death animation, not yet freed */

/*
 * Maximum number of active game sprites (OAM pool size).
 * 48 covers: 1 player + 8 enemies + 16 player bullets + 8 enemy bullets
 * + items + UI elements, with headroom. Must not exceed 128 (SNES OAM limit).
 */
#define MAX_GAME_SPRITES 48

/*
 * Sprite entity structure.
 *
 * Each instance maps to one SNES OAM entry. The struct stores both
 * logical game state (position, animation) and hardware mapping (oam_id,
 * tile_offset, palette). This avoids a separate mapping layer.
 *
 * Animation: frame-based system where anim_timer counts up to anim_speed,
 * then advances anim_frame. Tile number is computed at render time as:
 *   tile_offset + (anim_frame * tiles_per_frame)
 * where tiles_per_frame = 16 for 32x32 (4x4 of 8x8) or 4 for 16x16 (2x2).
 */
typedef struct {
    s16 x;              /* Screen X position in pixels (signed for off-screen) */
    s16 y;              /* Screen Y position in pixels (signed for off-screen) */
    u16 tile_offset;    /* Base OBJ character name in VRAM (first frame, first tile) */
    u8  palette;        /* OBJ palette index (0-7). Maps to CGRAM 128 + palette*16 */
    u8  priority;       /* Sprite priority relative to BG layers (0-3, 3 = topmost) */
    u8  size;           /* OBJ_SMALL (16x16) or OBJ_LARGE (32x32) as set in OBSEL */
    u8  hflip;          /* Horizontal flip flag (0 or 1) for oamSet */
    u8  vflip;          /* Vertical flip flag (0 or 1) for oamSet */
    u8  active;         /* Entity state: ENTITY_INACTIVE, ENTITY_ACTIVE, ENTITY_DYING */
    u8  anim_frame;     /* Current animation frame index (0 to anim_count-1) */
    u8  anim_timer;     /* Frame counter; increments each frame until >= anim_speed */
    u8  anim_speed;     /* Frames per animation step (0 = static, no animation) */
    u8  anim_count;     /* Total number of animation frames (1 = static sprite) */
    u16 oam_id;         /* OAM byte offset = pool_index * 4. Never changes after init. */
    u8  anim_done;      /* Set to 1 when animation wraps back to frame 0 (one-shot detect) */
} SpriteEntity;

/*
 * spriteSystemInit - Initialize the sprite engine.
 * Clears all pool entries to default inactive state, resets the allocation
 * hint, and calls oamClear() to hide all 128 OAM entries in the PPU shadow buffer.
 */
void spriteSystemInit(void);

/*
 * spriteAlloc - Allocate a sprite entity from the pool.
 *
 * Uses a roving hint pointer for O(1) amortized allocation in the common case.
 * Searches from alloc_hint forward, wrapping to the beginning if needed.
 * Initializes the allocated sprite to safe defaults (off-screen at Y=240,
 * no animation, priority 2, no flip).
 *
 * Returns: Pointer to an initialized SpriteEntity, or NULL (0) if all
 *          MAX_GAME_SPRITES slots are occupied.
 */
SpriteEntity* spriteAlloc(void);

/*
 * spriteFree - Return a sprite entity to the pool.
 *
 * Marks the slot inactive, hides its OAM entry, and updates the allocation
 * hint if the freed index is earlier than the current hint (ensuring the
 * gap is found quickly by the next spriteAlloc call).
 *
 * Parameters:
 *   spr - Pointer to the sprite entity to free. NULL is safely ignored.
 */
void spriteFree(SpriteEntity *spr);

/*
 * spriteUpdateAll - Advance animation for all active sprites.
 *
 * Uses a countdown do-while loop that processes sprites from end to start.
 * On the 65816, DEC+BNE (4 cycles) is cheaper than CMP+BCC (5 cycles),
 * saving ~48 cycles per frame across all sprites (#113 optimization).
 *
 * For each active sprite with anim_count > 1 and anim_speed > 0:
 *   - Increments anim_timer
 *   - When timer reaches anim_speed, advances anim_frame and resets timer
 *   - When anim_frame wraps to 0, sets anim_done = 1 (for one-shot detection)
 */
void spriteUpdateAll(void);

/*
 * spriteRenderAll - Write all active sprite data to the OAM shadow buffer.
 *
 * For each active sprite:
 *   1. Performs off-screen culling using unsigned wrapping (negative coords
 *      wrap to large values, reducing 4 comparisons to 2)
 *   2. Computes tile number: base + (anim_frame * tiles_per_frame)
 *      using bit shifts instead of multiply (<<4 for 32x32, <<2 for 16x16)
 *   3. Writes position, tile, palette, priority, and flip flags via oamSet()
 *   4. Sets size and visibility via oamSetEx()
 *
 * Inactive sprites are hidden via oamSetVisible(OBJ_HIDE).
 *
 * Call before WaitForVBlank() so the NMI handler can DMA the OAM buffer
 * to the PPU during the vertical blanking period.
 */
void spriteRenderAll(void);

/*
 * spriteHideAll - Hide all 128 OAM entries.
 * Calls oamClear(0, 0) which moves all sprites off-screen in the shadow buffer.
 * Used during scene transitions to ensure no stale sprites are visible.
 */
void spriteHideAll(void);

/*
 * spriteLoadTiles - Load raw tile data to OBJ VRAM at a specified offset.
 *
 * Parameters:
 *   tileData       - Pointer to tile graphics data (4bpp format, output of gfx4snes)
 *   tileSize       - Size of tile data in bytes
 *   vramWordOffset - Word offset from VRAM_OBJ_GFX base (added to base address)
 *
 * Uses DMA channel to transfer tile data to VRAM. Must be called during
 * force blank (screen off) since VRAM is not accessible during active display.
 */
void spriteLoadTiles(u8 *tileData, u16 tileSize, u16 vramWordOffset);

/*
 * spriteLoadTiles32 - Load a 32x32 sprite (4x4 = 16 tiles of 8x8) with
 *                     SNES OBJ VRAM row spacing.
 *
 * The SNES OBJ VRAM is organized as a 16-name-wide grid. A 32x32 sprite
 * occupies a 4x4 block of names, but rows are 16 names apart (256 VRAM words).
 * gfx4snes outputs tiles linearly (row 0 tiles, row 1 tiles, ...), so this
 * function performs 4 separate DMA transfers with the correct stride:
 *   Row 0: tileData[0..127]   -> VRAM base + 0
 *   Row 1: tileData[128..255] -> VRAM base + 256
 *   Row 2: tileData[256..383] -> VRAM base + 512
 *   Row 3: tileData[384..511] -> VRAM base + 768
 *
 * Parameters:
 *   tileData       - Pointer to 512 bytes of 4bpp tile data (16 tiles * 32 bytes)
 *   vramWordOffset - Word offset from VRAM_OBJ_GFX base for the first name
 */
void spriteLoadTiles32(u8 *tileData, u16 vramWordOffset);

/*
 * spriteLoadTiles16 - Load a 16x16 sprite (2x2 = 4 tiles of 8x8) with
 *                     SNES OBJ VRAM row spacing.
 *
 * Similar to spriteLoadTiles32 but for 16x16 sprites:
 *   Row 0: tileData[0..63]   -> VRAM base + 0       (2 tiles, 64 bytes)
 *   Row 1: tileData[64..127] -> VRAM base + 256      (2 tiles, 64 bytes)
 *
 * Parameters:
 *   tileData       - Pointer to 128 bytes of 4bpp tile data (4 tiles * 32 bytes)
 *   vramWordOffset - Word offset from VRAM_OBJ_GFX base for the first name
 */
void spriteLoadTiles16(u8 *tileData, u16 vramWordOffset);

/*
 * spriteLoadPalette - Load a palette into an OBJ CGRAM slot.
 *
 * SNES CGRAM layout for OBJ (sprites):
 *   - OBJ palettes occupy CGRAM entries 128-255 (128 colors total)
 *   - 8 palettes of 16 colors each (32 bytes per palette)
 *   - Slot 0 = CGRAM 128, Slot 1 = CGRAM 144, ..., Slot 7 = CGRAM 240
 *
 * Parameters:
 *   palData - Pointer to palette data (BGR555 format, 2 bytes per color)
 *   palSize - Size of palette data in bytes (typically 32 for 16 colors)
 *   palSlot - OBJ palette slot index (0-7)
 */
void spriteLoadPalette(u8 *palData, u16 palSize, u8 palSlot);

#endif /* SPRITES_H */
