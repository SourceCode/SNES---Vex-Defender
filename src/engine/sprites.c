/*==============================================================================
 * Sprite Engine
 *
 * Manages a pool of sprite entities and maps them to SNES OAM slots.
 * Each pool entry corresponds to one OAM slot (oam_id = index * 4).
 *
 * The SNES PPU has 128 OAM entries, each capable of displaying one
 * hardware sprite. This engine provides a higher-level abstraction with
 * animation support, allocation/deallocation, and batch rendering.
 *
 * Memory layout:
 *   - OAM shadow buffer (managed by PVSnesLib) lives in WRAM
 *   - DMA transfer from shadow buffer to PPU OAM happens in NMI ISR
 *   - This engine writes to the shadow buffer via oamSet/oamSetEx calls
 *   - All shadow buffer writes must complete before WaitForVBlank()
 *
 * Performance notes for the 65816:
 *   - Pointer arithmetic (spr++, spr--) generates more efficient code than
 *     array indexing (sprite_pool[i]) because it avoids repeated base+offset
 *     address calculations.
 *   - Countdown loops (do { ... } while (--i)) produce DEC+BNE (4 cycles)
 *     vs increment loops CMP+BCC (5 cycles), saving 1 cycle per iteration.
 *   - Bit shifts (<<4, <<2) replace multiplies for tile offset computation
 *     since the 65816 has no general multiply instruction (only 8x8 unsigned).
 *============================================================================*/

#include "engine/sprites.h"
#include "config.h"

/* The sprite entity pool. Each entry maps 1:1 to an OAM slot.
 * Pool indices 0 through MAX_GAME_SPRITES-1 correspond to OAM slots 0-47. */
static SpriteEntity sprite_pool[MAX_GAME_SPRITES];

/* Roving allocation hint: the next index to start searching from.
 * Amortizes allocation cost by skipping over known-occupied slots.
 * Updated on both alloc (advance past allocated slot) and free
 * (pull back to freed slot if it's earlier than current hint). */
static u8 alloc_hint = 0;

/*
 * spriteSystemInit - Initialize the sprite engine.
 *
 * Sets all pool entries to safe default values:
 *   - Inactive state (will not be updated or rendered)
 *   - Position Y=240 (off-screen below the 224-line display)
 *   - Priority 2 (above BG1/BG2 but below priority-3 sprites)
 *   - Small size (16x16 as determined by the OBSEL register setting)
 *   - No animation (1 frame, speed 0)
 *   - OAM ID computed from pool index (permanent mapping)
 *
 * Also calls oamClear(0, 0) to hide all 128 OAM entries in the
 * PPU shadow buffer, ensuring no garbage sprites appear on first frame.
 */
void spriteSystemInit(void)
{
    u8 i;
    alloc_hint = 0;
    for (i = 0; i < MAX_GAME_SPRITES; i++) {
        sprite_pool[i].active = ENTITY_INACTIVE;
        sprite_pool[i].x = 0;
        sprite_pool[i].y = 240;          /* Off-screen (below 224-line display) */
        sprite_pool[i].tile_offset = 0;
        sprite_pool[i].palette = 0;
        sprite_pool[i].priority = 2;     /* Above BG1/BG2 */
        sprite_pool[i].size = OBJ_SMALL; /* 16x16 default (OBSEL small size) */
        sprite_pool[i].hflip = 0;
        sprite_pool[i].vflip = 0;
        sprite_pool[i].anim_frame = 0;
        sprite_pool[i].anim_timer = 0;
        sprite_pool[i].anim_speed = 0;   /* Static (no animation) */
        sprite_pool[i].anim_count = 1;   /* Single frame */
        sprite_pool[i].oam_id = i * 4;   /* Permanent OAM mapping: 4 bytes per entry */
        sprite_pool[i].anim_done = 0;
    }

    /* Hide all 128 OAM entries (oamClear hides from slot 0, count 0 = all) */
    oamClear(0, 0);
}

/*
 * spriteAlloc - Allocate and initialize a sprite entity from the pool.
 *
 * Uses a roving hint pointer for efficient allocation:
 *   1. Search from alloc_hint to end of pool
 *   2. If not found, wrap and search from 0 to alloc_hint
 *   3. Advance hint past the allocated slot
 *
 * This provides O(1) amortized allocation when sprites are allocated and
 * freed in roughly FIFO order (common in shmup enemy spawning). Worst
 * case is O(n) when the pool is nearly full and the hint is stale.
 *
 * The allocated sprite is initialized to safe defaults:
 *   - Position (0, 240) = off-screen
 *   - No flip, priority 2, no animation
 *   - anim_done = 0
 *
 * Returns: Pointer to initialized SpriteEntity, or NULL (0) if pool is full.
 */
SpriteEntity* spriteAlloc(void)
{
    u8 i;
    /* Search from hint position first (likely to find a free slot quickly) */
    for (i = alloc_hint; i < MAX_GAME_SPRITES; i++) {
        if (sprite_pool[i].active == ENTITY_INACTIVE) {
            sprite_pool[i].active = ENTITY_ACTIVE;
            sprite_pool[i].x = 0;
            sprite_pool[i].y = 240;          /* Start off-screen */
            sprite_pool[i].hflip = 0;
            sprite_pool[i].vflip = 0;
            sprite_pool[i].priority = 2;
            sprite_pool[i].anim_frame = 0;
            sprite_pool[i].anim_timer = 0;
            sprite_pool[i].anim_speed = 0;
            sprite_pool[i].anim_count = 1;
            sprite_pool[i].anim_done = 0;
            /* Advance hint past this slot for next allocation */
            alloc_hint = i + 1;
            if (alloc_hint >= MAX_GAME_SPRITES) alloc_hint = 0;
            return &sprite_pool[i];
        }
    }
    /* Wrap to beginning of pool (search slots before the hint) */
    for (i = 0; i < alloc_hint; i++) {
        if (sprite_pool[i].active == ENTITY_INACTIVE) {
            sprite_pool[i].active = ENTITY_ACTIVE;
            sprite_pool[i].x = 0;
            sprite_pool[i].y = 240;
            sprite_pool[i].hflip = 0;
            sprite_pool[i].vflip = 0;
            sprite_pool[i].priority = 2;
            sprite_pool[i].anim_frame = 0;
            sprite_pool[i].anim_timer = 0;
            sprite_pool[i].anim_speed = 0;
            sprite_pool[i].anim_count = 1;
            sprite_pool[i].anim_done = 0;
            alloc_hint = i + 1;
            return &sprite_pool[i];
        }
    }
    return (SpriteEntity *)0;  /* Pool is completely full */
}

/*
 * spriteFree - Release a sprite entity back to the pool.
 *
 * Marks the slot inactive and hides its OAM entry immediately so no
 * stale sprite appears next frame. Updates the allocation hint if the
 * freed slot is earlier than the current hint, ensuring the gap is
 * found quickly by the next spriteAlloc() call.
 *
 * The pool index is recovered from the OAM ID: index = oam_id / 4.
 * Since oam_id is always a multiple of 4, >>2 is exact.
 *
 * Parameters:
 *   spr - Pointer to sprite to free. NULL is safely ignored.
 */
void spriteFree(SpriteEntity *spr)
{
    u8 idx;
    if (spr) {
        spr->active = ENTITY_INACTIVE;
        oamSetVisible(spr->oam_id, OBJ_HIDE);  /* Hide immediately in OAM shadow buffer */
        /* Pull hint back if freed slot is before current hint */
        idx = spr->oam_id >> 2;  /* OAM byte offset / 4 = pool index */
        if (idx < alloc_hint) alloc_hint = idx;
    }
}

/*
 * spriteUpdateAll - Advance animation state for all active sprites.
 *
 * Uses a countdown do-while loop (#113 optimization) that processes
 * sprites from the end of the pool backward to index 0. On the 65816:
 *   - DEC reg + BNE (4 cycles) for countdown
 *   - vs CMP #imm + BCC (5 cycles) for count-up
 *   - Saves 1 cycle per iteration * 48 sprites = 48 cycles per frame
 *
 * This may seem small, but on the ~21,477 cycle/scanline 65816, every
 * saved cycle helps stay within the frame budget for a shmup with many
 * simultaneous objects.
 *
 * Animation logic:
 *   - anim_timer increments each frame
 *   - When timer >= anim_speed, timer resets and anim_frame advances
 *   - When anim_frame wraps past anim_count, it resets to 0 and sets
 *     anim_done = 1 (one-shot flag for death animations, etc.)
 *   - Sprites with anim_count <= 1 or anim_speed == 0 are static
 */
void spriteUpdateAll(void)
{
    u8 i;
    SpriteEntity *spr;

    /* Start at the last pool entry and work backward */
    spr = &sprite_pool[MAX_GAME_SPRITES - 1];
    i = MAX_GAME_SPRITES;
    do {
        if (spr->active == ENTITY_ACTIVE) {
            /* Only animate sprites with multiple frames and nonzero speed */
            if (spr->anim_count > 1 && spr->anim_speed > 0) {
                spr->anim_timer++;
                if (spr->anim_timer >= spr->anim_speed) {
                    spr->anim_timer = 0;
                    spr->anim_frame++;
                    if (spr->anim_frame >= spr->anim_count) {
                        spr->anim_frame = 0;    /* Wrap to first frame */
                        spr->anim_done = 1;      /* Signal animation cycle complete */
                    }
                }
            }
        }
        spr--;  /* Move to previous pool entry (pointer arithmetic) */
    } while (--i);  /* Countdown loop: DEC+BNE is faster than CMP+BCC */
}

/*
 * spriteRenderAll - Write all sprite data to the OAM shadow buffer.
 *
 * For each pool entry:
 *   - Inactive: hide the OAM slot (move sprite off-screen)
 *   - Active but off-screen: hide (culling optimization)
 *   - Active and on-screen: compute animated tile number, write OAM data
 *
 * Off-screen culling trick:
 *   (u16)(spr->x + 32) > 288  catches both x < -32 and x > 256
 *   (u16)(spr->y + 32) > 256  catches both y < -32 and y > 224
 *   Negative values wrap to large positive when cast to u16, so a single
 *   unsigned comparison replaces two signed comparisons. This saves ~2
 *   branch instructions per sprite (~96 cycles total for 48 sprites).
 *
 * Tile number calculation:
 *   For animated sprites, the tile number advances by one full sprite's
 *   worth of tiles per frame. In SNES OBJ VRAM:
 *     32x32 sprite = 4x4 of 8x8 tiles = 16 tiles per frame -> <<4
 *     16x16 sprite = 2x2 of 8x8 tiles = 4 tiles per frame  -> <<2
 *   Bit shifts compile to the 65816 ASL instruction, which is single-cycle
 *   per bit shifted. This is much faster than a general multiply.
 */
void spriteRenderAll(void)
{
    u8 i;
    SpriteEntity *spr;
    u16 tileNum;

    spr = sprite_pool;
    for (i = 0; i < MAX_GAME_SPRITES; i++, spr++) {
        if (spr->active != ENTITY_ACTIVE) {
            oamSetVisible(spr->oam_id, OBJ_HIDE);
            continue;
        }

        /* Offscreen culling: unsigned wrapping trick reduces 4 comparisons to 2.
         * The 32px margin accounts for the maximum sprite size (32x32). */
        if ((u16)(spr->x + 32) > 288u || (u16)(spr->y + 32) > 256u) {
            oamSetVisible(spr->oam_id, OBJ_HIDE);
            continue;
        }

        /* Calculate animated tile number.
         * Base tile_offset points to frame 0. Each animation frame is
         * offset by the number of 8x8 tiles that compose one sprite:
         *   OBJ_LARGE (32x32) = 4 rows * 4 cols = 16 tiles -> <<4 (multiply by 16)
         *   OBJ_SMALL (16x16) = 2 rows * 2 cols = 4 tiles  -> <<2 (multiply by 4)
         * Shifts compile to ASL instructions on 65816 (1 cycle each). */
        if (spr->size == OBJ_LARGE) {
            tileNum = spr->tile_offset + ((u16)spr->anim_frame << 4);
        } else {
            tileNum = spr->tile_offset + ((u16)spr->anim_frame << 2);
        }

        /* Write sprite data to OAM shadow buffer.
         * oamSet writes the main 4-byte OAM entry:
         *   - Byte 0: X position (low 8 bits)
         *   - Byte 1: Y position (8 bits)
         *   - Byte 2: Tile/character number (low 8 bits)
         *   - Byte 3: Attributes (priority, palette, flip, tile high bit) */
        oamSet(spr->oam_id,
               (u16)spr->x, (u16)spr->y,
               spr->priority,
               spr->hflip, spr->vflip,
               tileNum,
               spr->palette);

        /* oamSetEx writes the 2-bit extension data:
         *   - Size select bit (OBJ_SMALL or OBJ_LARGE)
         *   - X position bit 8 (for sprites at x >= 256)
         *   - Visibility (OBJ_SHOW = visible) */
        oamSetEx(spr->oam_id, spr->size, OBJ_SHOW);
    }
}

/*
 * spriteHideAll - Hide all 128 hardware OAM entries.
 *
 * oamClear(0, 0) sets all OAM entries in the shadow buffer to off-screen
 * positions. The NMI handler will DMA this to the PPU during VBlank.
 *
 * Used during scene transitions to clear all visible sprites instantly
 * before the new scene sets up its own sprites.
 */
void spriteHideAll(void)
{
    oamClear(0, 0);
}

/*
 * spriteLoadTiles - Load raw sprite tile data to OBJ VRAM.
 *
 * Performs a DMA transfer from the tile data in ROM/WRAM to the PPU's
 * OBJ VRAM at the specified offset from the OBJ character base address.
 *
 * VRAM_OBJ_GFX is the base address for all OBJ tile data.
 * The vramWordOffset is added to place tiles at the correct position
 * within the OBJ character grid.
 *
 * Must be called during force blank (setScreenOff) because VRAM is
 * only accessible to the CPU during blanking periods.
 *
 * Parameters:
 *   tileData       - Source tile data pointer (4bpp format)
 *   tileSize       - Number of bytes to transfer
 *   vramWordOffset - Word offset from VRAM_OBJ_GFX base
 */
void spriteLoadTiles(u8 *tileData, u16 tileSize, u16 vramWordOffset)
{
    dmaCopyVram(tileData, VRAM_OBJ_GFX + vramWordOffset, tileSize);
}

/*---------------------------------------------------------------------------
 * spriteLoadTiles32 - Load a 32x32 sprite (16 tiles) with SNES OBJ spacing.
 *
 * SNES OBJ VRAM is organized as a 16-name-wide grid. Each "name" is one
 * 8x8 tile (32 bytes in 4bpp mode). A 32x32 sprite occupies a 4x4 block
 * of names, but rows in the grid are 16 names apart:
 *
 *   OBJ VRAM grid (16 names wide):
 *   Name 0  | Name 1  | Name 2  | Name 3  | Name 4  | ... | Name 15
 *   Name 16 | Name 17 | Name 18 | Name 19 | Name 20 | ... | Name 31
 *   Name 32 | Name 33 | Name 34 | Name 35 | ...
 *   Name 48 | Name 49 | Name 50 | Name 51 | ...
 *
 * A 32x32 sprite starting at name 0 uses names:
 *   Row 0: 0, 1, 2, 3       (VRAM offset + 0 words)
 *   Row 1: 16, 17, 18, 19   (VRAM offset + 256 words)
 *   Row 2: 32, 33, 34, 35   (VRAM offset + 512 words)
 *   Row 3: 48, 49, 50, 51   (VRAM offset + 768 words)
 *
 * gfx4snes outputs the 16 tiles linearly (4 tiles per row, 128 bytes per row).
 * This function splits that linear data into 4 DMA transfers with 256-word
 * VRAM gaps between them.
 *
 * Each row = 4 tiles * 32 bytes/tile = 128 bytes.
 * Row stride in VRAM = 16 names * 16 words/name = 256 words.
 *
 * Parameters:
 *   tileData       - 512 bytes of linear 4bpp tile data (16 tiles)
 *   vramWordOffset - Starting word offset from VRAM_OBJ_GFX base
 *---------------------------------------------------------------------------*/
void spriteLoadTiles32(u8 *tileData, u16 vramWordOffset)
{
    u8 row;
    for (row = 0; row < 4; row++) {
        dmaCopyVram(
            tileData + ((u16)row * 128),      /* Source: 128 bytes per row */
            VRAM_OBJ_GFX + vramWordOffset + ((u16)row * 256),  /* Dest: 256-word row stride */
            128                                /* Size: 4 tiles * 32 bytes */
        );
    }
}

/*---------------------------------------------------------------------------
 * spriteLoadTiles16 - Load a 16x16 sprite (4 tiles) with SNES OBJ spacing.
 *
 * Same principle as spriteLoadTiles32 but for a 2x2 tile block:
 *   Row 0: 2 tiles at base offset       (64 bytes)
 *   Row 1: 2 tiles at base + 256 words  (64 bytes)
 *
 * Each row = 2 tiles * 32 bytes/tile = 64 bytes.
 *
 * Parameters:
 *   tileData       - 128 bytes of linear 4bpp tile data (4 tiles)
 *   vramWordOffset - Starting word offset from VRAM_OBJ_GFX base
 *---------------------------------------------------------------------------*/
void spriteLoadTiles16(u8 *tileData, u16 vramWordOffset)
{
    /* Row 0: first 2 tiles -> VRAM base */
    dmaCopyVram(tileData, VRAM_OBJ_GFX + vramWordOffset, 64);
    /* Row 1: next 2 tiles -> VRAM base + 256 words (next OBJ name row) */
    dmaCopyVram(tileData + 64, VRAM_OBJ_GFX + vramWordOffset + 256, 64);
}

/*
 * spriteLoadPalette - Load a sprite palette into an OBJ CGRAM slot.
 *
 * SNES CGRAM (Color Graphics RAM) layout:
 *   - Entries 0-127: BG palettes (8 palettes of 16 colors)
 *   - Entries 128-255: OBJ palettes (8 palettes of 16 colors)
 *   - Each color is 2 bytes (BGR555 format: 5 bits per channel)
 *   - Each palette = 16 colors * 2 bytes = 32 bytes
 *
 * palSlot maps to CGRAM starting entry:
 *   Slot 0 -> CGRAM 128 (first OBJ palette)
 *   Slot 1 -> CGRAM 144 (second OBJ palette)
 *   ...
 *   Slot 7 -> CGRAM 240 (last OBJ palette)
 *
 * Formula: CGRAM entry = 128 + (palSlot * 16)
 *
 * dmaCopyCGram takes the starting color index (not byte offset),
 * so we pass 128 + palSlot*16 directly.
 *
 * Must be called during force blank for safe CGRAM access.
 *
 * Parameters:
 *   palData - BGR555 palette data pointer
 *   palSize - Palette data size in bytes (typically 32)
 *   palSlot - OBJ palette slot (0-7)
 */
void spriteLoadPalette(u8 *palData, u16 palSize, u8 palSlot)
{
    dmaCopyCGram(palData, 128 + (palSlot * 16), palSize);
}
