/*==============================================================================
 * Mock PVSnesLib Header for Host-Side Testing
 *
 * Provides SNES type definitions and stub functions so game logic code
 * can be compiled with a standard C compiler (GCC/Clang/MSVC) for testing.
 * Hardware operations become no-ops; only pure logic is tested.
 *
 * On the real SNES, these types and functions come from PVSnesLib's <snes.h>,
 * which maps to the 65816 CPU's register sizes and PPU hardware registers.
 * Here we map them to standard C99 fixed-width integer types.
 *
 * The stub functions have the same signatures as PVSnesLib's API but do
 * nothing (void casts suppress "unused parameter" warnings).  This allows
 * game logic source files to be #included directly into the test runner
 * without modification.
 *
 * Special cases:
 *   - SRAM stubs (consoleCopySram/consoleLoadSram): use a 256-byte mock
 *     buffer to simulate battery-backed SRAM for save/load testing
 *   - Sound stub (soundPlaySFX): records the last SFX ID for verification
 *============================================================================*/

#ifndef MOCK_SNES_H
#define MOCK_SNES_H

#include <stdint.h>
#include <string.h>

/*=== SNES Types (matching PVSnesLib) ===*/
/* On the real SNES, u8/s8 are native 8-bit values, u16/s16 are 16-bit
 * (the 65816's native register width in 16-bit mode), and u32/s32 are
 * used for fixed-point math and 24-bit address calculations. */
typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef int8_t    s8;
typedef int16_t   s16;
typedef int32_t   s32;

/*=== OAM (Object Attribute Memory) Constants ===*/
/* Sprite size and visibility flags used by oamSetEx().
 * On real hardware, these map to bits in OAM table 2. */
#define OBJ_SMALL  0   /* Use the small sprite size (16x16 in our config) */
#define OBJ_LARGE  1   /* Use the large sprite size (32x32 in our config) */
#define OBJ_SHOW   0   /* Sprite is visible */
#define OBJ_HIDE   1   /* Sprite is hidden (moved off-screen by PPU) */

/*=== BG Map Size Constants ===*/
/* Tilemap dimensions for bgSetMapPtr(). On the SNES, the tilemap can be
 * configured as 32x32, 64x32, 32x64, or 64x64 tiles per screen. */
#define SC_32x32   0   /* 32 tiles wide x 32 tiles tall (256x256 pixels) */
#define SC_64x32   1   /* 64 tiles wide x 32 tiles tall (512x256 pixels) */
#define SC_32x64   2   /* 32 tiles wide x 64 tiles tall (256x512 pixels) */
#define SC_64x64   3   /* 64 tiles wide x 64 tiles tall (512x512 pixels) */

/*=== OAM Stub Functions ===*/
/* These would write to the OAM mirror buffer in WRAM on real hardware.
 * The NMI handler then DMA-transfers the mirror to actual OAM during VBlank. */

/* oamSet - Set all attributes for a single OAM entry.
 * id: OAM entry index (0-127, multiplied by 4 for byte offset).
 * x, y: Screen position.  priority: Rendering priority (0-3).
 * hflip, vflip: Horizontal/vertical mirror.  tile: VRAM tile index.
 * pal: Palette index (8-15 for sprites). */
static inline void oamSet(u16 id, u16 x, u16 y, u8 priority,
                           u8 hflip, u8 vflip, u16 tile, u8 pal)
{
    (void)id; (void)x; (void)y; (void)priority;
    (void)hflip; (void)vflip; (void)tile; (void)pal;
}

/* oamSetEx - Set the extended attributes (size + visibility) for a sprite.
 * These are stored in OAM table 2 (the upper 32 bytes). */
static inline void oamSetEx(u16 id, u8 size, u8 visible)
{
    (void)id; (void)size; (void)visible;
}

/* oamSetVisible - Show or hide a single sprite. */
static inline void oamSetVisible(u16 id, u8 visible)
{
    (void)id; (void)visible;
}

/* oamClear - Hide sprites starting at offset.
 * nbr=0 means "all sprites from offset to 127". */
static inline void oamClear(u16 offset, u8 nbr)
{
    (void)offset; (void)nbr;
}

/* oamInitGfxAttr - Configure sprite tile base VRAM address and size mode.
 * address: VRAM word address for sprite tiles (e.g., 0x4000).
 * objSize: Size configuration (e.g., OBJ_SIZE16_L32 = 16x16 small, 32x32 large). */
static inline void oamInitGfxAttr(u16 address, u8 objSize)
{
    (void)address; (void)objSize;
}

/* oamInitGfxSet - Load sprite tile data and palette into VRAM/CGRAM via DMA.
 * Used by playerInit(), bulletLoadGraphics(), etc. to load art assets. */
static inline void oamInitGfxSet(void *tileSource, u16 tileSize,
                                  void *palSource, u16 palSize,
                                  u8 oamSlot, u16 address, u8 oamObjSize)
{
    (void)tileSource; (void)tileSize; (void)palSource; (void)palSize;
    (void)oamSlot; (void)address; (void)oamObjSize;
}

/*=== DMA Stub Functions ===*/
/* On real hardware, these trigger GPDMA channels to transfer data between
 * WRAM and VRAM/CGRAM at high speed during VBlank. */

/* dmaCopyVram - DMA transfer from WRAM source to VRAM at the given word address. */
static inline void dmaCopyVram(u8 *source, u16 address, u16 size)
{
    (void)source; (void)address; (void)size;
}

/* dmaCopyCGram - DMA transfer from WRAM source to CGRAM (color palette RAM).
 * CGRAM holds 256 entries of 15-bit BGR color values. */
static inline void dmaCopyCGram(u8 *source, u16 address, u16 size)
{
    (void)source; (void)address; (void)size;
}

/*=== Console/Text Stubs ===*/
/* PVSnesLib's console text system writes to BG tilemaps to display text.
 * In tests, these are no-ops since we're not testing rendering. */

/* consoleDrawText - Write a text string to the BG tilemap at tile position (x, y). */
static inline void consoleDrawText(u8 x, u8 y, char *text)
{
    (void)x; (void)y; (void)text;
}

/* Text system configuration functions - set VRAM addresses for font tiles and tilemap */
static inline void consoleSetTextMapPtr(u16 addr)  { (void)addr; }
static inline void consoleSetTextGfxPtr(u16 addr)  { (void)addr; }
static inline void consoleSetTextOffset(u16 off)   { (void)off; }

/* consoleInitText - Load the 4bpp font into VRAM and configure palette.
 * In the real game this DMA-copies font tile data and palette to VRAM/CGRAM. */
static inline void consoleInitText(u8 palId, u16 palAdr, void *font, void *pal)
{
    (void)palId; (void)palAdr; (void)font; (void)pal;
}

/*=== SRAM Stubs ===*/
/* Simulate battery-backed SRAM with a small in-memory buffer.
 * consoleCopySram writes to this buffer; consoleLoadSram reads from it.
 * This allows the save/load system to be tested without real SRAM hardware. */
static u8 mock_sram[256];

/* consoleCopySram - Copy data from WRAM to SRAM (save).
 * On real hardware: switches to bank $70 and DMA-copies. */
static inline void consoleCopySram(u8 *source, u16 size)
{
    if (size > sizeof(mock_sram)) size = sizeof(mock_sram);
    memcpy(mock_sram, source, size);
}

/* consoleLoadSram - Copy data from SRAM to WRAM (load).
 * On real hardware: switches to bank $70 and DMA-copies. */
static inline void consoleLoadSram(u8 *dest, u16 size)
{
    if (size > sizeof(mock_sram)) size = sizeof(mock_sram);
    memcpy(dest, mock_sram, size);
}

/*=== Video Register Stubs ===*/
/* These map to writes to PPU register $2100 (brightness / force blank).
 * On real hardware, setBrightness writes the master brightness (0-15)
 * to the low 4 bits of $2100.  setScreenOff/On set/clear bit 7 (force blank). */
static inline void setBrightness(u8 b)   { (void)b; }
static inline void setScreenOn(void)     {}
static inline void setScreenOff(void)    {}

/* BG scroll register stubs - write to $210D-$2114 on real hardware */
static inline void bgSetScroll(u8 bg, u16 x, u16 y)
{
    (void)bg; (void)x; (void)y;
}

/* BG layer enable/disable - set/clear bits in register $212C (main screen) */
static inline void bgSetEnable(u8 bg)    { (void)bg; }
static inline void bgSetDisable(u8 bg)   { (void)bg; }

/* BG VRAM address configuration - write to $210B/$210C (tile base) and $2107-$210A (map base) */
static inline void bgSetGfxPtr(u8 bg, u16 addr) { (void)bg; (void)addr; }
static inline void bgSetMapPtr(u8 bg, u16 addr, u8 size)
{
    (void)bg; (void)addr; (void)size;
}

/*=== System Stubs ===*/
/* WaitForVBlank - On real hardware, halts CPU via WAI instruction until NMI.
 * In tests, this is an instant no-op. */
static inline void WaitForVBlank(void)   {}

/* systemWaitFrames - Blocking delay for N frames. No-op in tests. */
static inline void systemWaitFrames(u16 c) { (void)c; }

/*=== Sound Stubs ===*/
/* Records the last SFX ID played so tests can verify sound effects are
 * triggered at the right times.  On real hardware, soundPlaySFX() sends
 * a command to the SPC700 APU to start playing a BRR sample. */
static u8 mock_last_sfx;
static inline void soundPlaySFX(u8 sfxId) { mock_last_sfx = sfxId; }

/*=== Fade Stubs ===*/
/* On real hardware, these gradually ramp brightness up/down over N frames.
 * In tests, they complete instantly. */
static inline void fadeInBlocking(u8 frames) { (void)frames; }
static inline void fadeOutBlocking(u8 frames) { (void)frames; }

#endif /* MOCK_SNES_H */
