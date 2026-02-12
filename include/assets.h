/*---------------------------------------------------------------------------------
    VEX DEFENDER - Asset Declarations

    Extern references to labels defined in data.asm via .incbin directives.
    These allow C code to reference binary art/sound data that was assembled
    directly into the ROM by WLA-DX.

    Each asset has a start label (e.g., player_ship_til) and an end label
    (e.g., player_ship_til_end).  The ASSET_SIZE macro computes the byte
    size at compile time by subtracting the two label addresses, which is
    needed for DMA transfer size parameters.

    Assets are grouped by type:
      - Sprite tile data (_til) + palette (_pal): loaded into OBJ VRAM + CGRAM
      - Background tile data, palette, and tilemap (_map): loaded into BG VRAM
      - PVSnesLib console font: used by consoleInitText() for BG1 text rendering

    All tile data is in SNES 4bpp planar format (sprites and BG1/BG2).
    Palettes are 15-bit BGR format (5 bits per channel, written to CGRAM).
    Tilemaps are arrays of 16-bit tile entries (tile index + palette + flip bits).
---------------------------------------------------------------------------------*/
#ifndef ASSETS_H
#define ASSETS_H

#include <snes.h>

/* Compute byte size of a binary asset from its start/end label pair.
 * Labels are declared as char (1 byte each), so pointer subtraction
 * yields the correct byte count. */
#define ASSET_SIZE(label) (&label##_end - &label)

/*=== Player Ship (32x32 sprite, 4bpp) ===*/
/* The player's ship sprite: 4 tiles (each 16x16) arranged in a 32x32 block.
 * Loaded into OBJ VRAM at VRAM_OBJ_GFX by playerInit(). */
extern char player_ship_til, player_ship_til_end;
extern char player_ship_pal, player_ship_pal_end;

/*=== Enemy Scout (32x32 sprite, 4bpp) ===*/
/* Weakest enemy type. Small, fast, low HP. Zone 1 primary enemy. */
extern char enemy_scout_til, enemy_scout_til_end;
extern char enemy_scout_pal, enemy_scout_pal_end;

/*=== Enemy Fighter (32x32 sprite, 4bpp) ===*/
/* Mid-tier enemy. Fires bullets. Appears in Zone 1 and Zone 2. */
extern char enemy_fighter_til, enemy_fighter_til_end;
extern char enemy_fighter_pal, enemy_fighter_pal_end;

/*=== Enemy Heavy (32x32 sprite, 4bpp) ===*/
/* Tanky enemy with high HP. Appears in Zone 2+. Drops better loot. */
extern char enemy_heavy_til, enemy_heavy_til_end;
extern char enemy_heavy_pal, enemy_heavy_pal_end;

/*=== Enemy Elite (32x32 sprite, 4bpp) ===*/
/* Strongest regular enemy. High stats, rare item drops. Zone 2-3. */
extern char enemy_elite_til, enemy_elite_til_end;
extern char enemy_elite_pal, enemy_elite_pal_end;

/*=== Player Bullet (16x16 sprite, 4bpp) ===*/
/* Projectile fired by the player ship. Uses small (16x16) OAM size. */
extern char bullet_player_til, bullet_player_til_end;
extern char bullet_player_pal, bullet_player_pal_end;

/*=== Enemy Bullet (16x16 sprite, 4bpp) ===*/
/* Projectile fired by enemy ships. Uses small (16x16) OAM size. */
extern char bullet_enemy_til, bullet_enemy_til_end;
extern char bullet_enemy_pal, bullet_enemy_pal_end;

/*=== Zone 1 Background: Debris Field ===*/
/* BG1 tile set, palette, and tilemap for the first zone.
 * Loaded by bgLoadZone(ZONE_DEBRIS) into VRAM_BG1_GFX / VRAM_BG1_MAP. */
extern char zone1_bg_til, zone1_bg_til_end;
extern char zone1_bg_pal, zone1_bg_pal_end;
extern char zone1_bg_map, zone1_bg_map_end;

/*=== Zone 2 Background: Asteroid Belt ===*/
/* BG1 tiles/palette/map for the second zone. */
extern char zone2_bg_til, zone2_bg_til_end;
extern char zone2_bg_pal, zone2_bg_pal_end;
extern char zone2_bg_map, zone2_bg_map_end;

/*=== Zone 3 Background: Flagship Approach ===*/
/* BG1 tiles/palette/map for the final zone. */
extern char zone3_bg_til, zone3_bg_til_end;
extern char zone3_bg_pal, zone3_bg_pal_end;
extern char zone3_bg_map, zone3_bg_map_end;

/*=== BG2 Star Parallax Layer ===*/
/* Procedurally-designed star tile set and palette for the scrolling
 * starfield background on BG2. Loaded once at init, shared across zones. */
extern char star_tiles, star_tiles_end;
extern char star_pal, star_pal_end;

/*=== PVSnesLib Console Font (4bpp, for BG1 text) ===*/
/* Standard PVSnesLib font used by consoleInitText() for dialog boxes,
 * title screen text, game-over/victory screens, and HUD elements.
 * This is a 4bpp font and MUST be placed on a 4bpp BG layer (BG1 or BG2).
 * BG3 in Mode 1 is only 2bpp and cannot render this font correctly. */
extern char snesfont, snesfont_end;
extern char snespal, snespal_end;

#endif /* ASSETS_H */
