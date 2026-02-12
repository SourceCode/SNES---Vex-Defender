# Phase 3: Asset Pipeline & Conversion Scripts

## Objective
Build the complete asset conversion pipeline that transforms source PNG images from the asset library into SNES-compatible tile, palette, and tilemap data. Create Python preprocessing scripts to resize, recolor, and palette-reduce source art to fit SNES constraints. Add all Makefile rules so that `make` automatically converts assets.

## Prerequisites
- Phase 1 (Project Scaffolding) must be complete.

## Detailed Tasks

1. Create `tools/convert_sprite.py` - Python script that takes a source PNG, resizes it to the target SNES sprite size (16x16, 32x32, or 64x64), reduces to 15 colors + transparent, saves as indexed PNG suitable for gfx4snes.

2. Create `tools/convert_background.py` - Python script that takes a source PNG, resizes to 256x224 (or 256x256 for scrolling), reduces to 15 colors per palette, saves as indexed PNG.

3. Create `tools/convert_font.py` - Generate or adapt a small 8x8 pixel font suitable for SNES 2bpp tiles.

4. Create `tools/asset_manifest.json` - A manifest listing which source PNGs map to which game assets, their target sizes, and palette assignments.

5. Add Makefile rules for each asset category:
   - Player ship sprite (32x32, 16 colors, palette 0 of OBJ)
   - Enemy ship sprites (16x16 and 32x32, 16 colors, palette 1-2 of OBJ)
   - Bullet sprites (8x8 and 16x16, 16 colors, palette 3 of OBJ)
   - Space backgrounds (256x256 tileset, 16 colors)
   - Battle UI elements (16-color tileset)

6. Create a test conversion of one ship (ship001.png as player), one background (background-01.png as zone 1 bg), and one bullet (bullet-01.png).

7. Update data.asm to include the converted test assets.

8. Update main.c to load and display one background and one sprite as a pipeline verification test.

## Files to Create/Modify

### J:/code/snes/snes-rpg-test/tools/convert_sprite.py
```python
#!/usr/bin/env python3
"""
Convert a source PNG sprite to SNES-compatible indexed PNG.

Usage:
  python convert_sprite.py <input.png> <output.png> --size 32 --colors 15

Features:
  - Resizes to target size (8, 16, 32, or 64 pixels square)
  - Auto-detects transparency and maps to color index 0
  - Reduces to N colors + transparent using median cut
  - Outputs an indexed-color PNG ready for gfx4snes
"""

import sys
import os
from PIL import Image
import argparse

def convert_sprite(input_path, output_path, size=32, max_colors=15):
    """Convert a sprite PNG to SNES-compatible format."""
    img = Image.open(input_path).convert("RGBA")

    # Resize to target size with high-quality resampling
    img = img.resize((size, size), Image.LANCZOS)

    # Separate alpha channel
    r, g, b, a = img.split()

    # Create a mask of transparent pixels (alpha < 128)
    mask = Image.new("L", (size, size))
    for y in range(size):
        for x in range(size):
            if a.getpixel((x, y)) < 128:
                mask.putpixel((x, y), 255)

    # Convert to RGB for quantization
    rgb = Image.merge("RGB", (r, g, b))

    # Quantize to max_colors (reserve index 0 for transparent)
    rgb_quantized = rgb.quantize(colors=max_colors, method=Image.MEDIANCUT)

    # Get the palette
    palette = rgb_quantized.getpalette()

    # Create new indexed image with index 0 = transparent (black)
    # Shift all existing colors up by 1 index
    new_palette = [0, 0, 0]  # Index 0 = transparent (black)
    for i in range(max_colors):
        new_palette.extend(palette[i*3:(i+1)*3])
    # Pad palette to 256 entries
    while len(new_palette) < 768:
        new_palette.append(0)

    result = Image.new("P", (size, size))
    result.putpalette(new_palette)

    for y in range(size):
        for x in range(size):
            if mask.getpixel((x, y)) == 255:
                result.putpixel((x, y), 0)  # Transparent
            else:
                # Offset by 1 to account for transparent index 0
                result.putpixel((x, y), rgb_quantized.getpixel((x, y)) + 1)

    result.save(output_path)
    print(f"Converted: {input_path} -> {output_path} ({size}x{size}, {max_colors+1} colors)")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert sprite to SNES format")
    parser.add_argument("input", help="Input PNG file")
    parser.add_argument("output", help="Output indexed PNG file")
    parser.add_argument("--size", type=int, default=32, choices=[8, 16, 32, 64])
    parser.add_argument("--colors", type=int, default=15, help="Max colors (excl. transparent)")
    args = parser.parse_args()
    convert_sprite(args.input, args.output, args.size, args.colors)
```

### J:/code/snes/snes-rpg-test/tools/convert_background.py
```python
#!/usr/bin/env python3
"""
Convert a source PNG background to SNES-compatible indexed PNG.

Usage:
  python convert_background.py <input.png> <output.png> --width 256 --height 256 --colors 16

The output is a 256xH indexed-color PNG with the specified palette.
SNES backgrounds use 8x8 tiles, so dimensions must be multiples of 8.
"""

import sys
from PIL import Image
import argparse

def convert_background(input_path, output_path, width=256, height=256, max_colors=16):
    """Convert a background PNG to SNES tile-friendly format."""
    img = Image.open(input_path).convert("RGB")

    # Resize to target dimensions
    img = img.resize((width, height), Image.LANCZOS)

    # Quantize to max_colors
    img_quantized = img.quantize(colors=max_colors, method=Image.MEDIANCUT)

    img_quantized.save(output_path)
    print(f"Converted: {input_path} -> {output_path} ({width}x{height}, {max_colors} colors)")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert background to SNES format")
    parser.add_argument("input", help="Input PNG file")
    parser.add_argument("output", help="Output indexed PNG file")
    parser.add_argument("--width", type=int, default=256)
    parser.add_argument("--height", type=int, default=256)
    parser.add_argument("--colors", type=int, default=16)
    args = parser.parse_args()
    convert_background(args.input, args.output, args.width, args.height, args.colors)
```

### J:/code/snes/snes-rpg-test/tools/asset_manifest.json
```json
{
  "source_dir": "G:/2024-unity/0-GameAssets/shooter",
  "output_dir": "assets",
  "sprites": {
    "player_ship": {
      "source": "ship001.png",
      "output": "sprites/player_ship.png",
      "size": 32,
      "colors": 15,
      "palette_slot": 0,
      "description": "Player ship - Cadet Vex's fighter"
    },
    "enemy_scout": {
      "source": "ship010.png",
      "output": "sprites/enemy_scout.png",
      "size": 16,
      "colors": 15,
      "palette_slot": 1,
      "description": "Zone 1 basic scout enemy"
    },
    "enemy_fighter": {
      "source": "ship020.png",
      "output": "sprites/enemy_fighter.png",
      "size": 32,
      "colors": 15,
      "palette_slot": 1,
      "description": "Zone 2 fighter enemy"
    },
    "enemy_heavy": {
      "source": "ship030.png",
      "output": "sprites/enemy_heavy.png",
      "size": 32,
      "colors": 15,
      "palette_slot": 2,
      "description": "Zone 2-3 heavy cruiser"
    },
    "enemy_elite": {
      "source": "ship050.png",
      "output": "sprites/enemy_elite.png",
      "size": 32,
      "colors": 15,
      "palette_slot": 2,
      "description": "Zone 3 elite fighter"
    },
    "boss_miniboss": {
      "source": "ship070.png",
      "output": "sprites/boss_mini.png",
      "size": 32,
      "colors": 15,
      "palette_slot": 4,
      "description": "Zone 2 mini-boss"
    },
    "boss_final": {
      "source": "ship090.png",
      "output": "sprites/boss_final.png",
      "size": 32,
      "colors": 15,
      "palette_slot": 4,
      "description": "Zone 3 final boss flagship"
    },
    "npc_commander": {
      "source": "npc-01.png",
      "output": "sprites/npc_commander.png",
      "size": 32,
      "colors": 15,
      "palette_slot": 6,
      "description": "Ark commander portrait/sprite"
    }
  },
  "bullets": {
    "player_basic": {
      "source": "bullet-01.png",
      "output": "sprites/bullet_basic.png",
      "size": 8,
      "colors": 15,
      "palette_slot": 3
    },
    "player_spread": {
      "source": "bullet-03.png",
      "output": "sprites/bullet_spread.png",
      "size": 8,
      "colors": 15,
      "palette_slot": 3
    },
    "player_laser": {
      "source": "bullet-05.png",
      "output": "sprites/bullet_laser.png",
      "size": 16,
      "colors": 15,
      "palette_slot": 3
    },
    "enemy_basic": {
      "source": "bullet-08.png",
      "output": "sprites/bullet_enemy.png",
      "size": 8,
      "colors": 15,
      "palette_slot": 3
    }
  },
  "backgrounds": {
    "zone1_debris": {
      "source": "background-01.png",
      "output": "backgrounds/zone1_bg.png",
      "width": 256,
      "height": 256,
      "colors": 16,
      "description": "Zone 1 - Debris Field starscape"
    },
    "zone2_asteroid": {
      "source": "background-05.png",
      "output": "backgrounds/zone2_bg.png",
      "width": 256,
      "height": 256,
      "colors": 16,
      "description": "Zone 2 - Asteroid Belt"
    },
    "zone3_flagship": {
      "source": "background-09.png",
      "output": "backgrounds/zone3_bg.png",
      "width": 256,
      "height": 256,
      "colors": 16,
      "description": "Zone 3 - Flagship Approach"
    },
    "battle_bg": {
      "source": "background-03.png",
      "output": "backgrounds/battle_bg.png",
      "width": 256,
      "height": 224,
      "colors": 16,
      "description": "Battle scene background"
    },
    "title_bg": {
      "source": "background-07.png",
      "output": "backgrounds/title_bg.png",
      "width": 256,
      "height": 224,
      "colors": 16,
      "description": "Title screen background"
    }
  }
}
```

### J:/code/snes/snes-rpg-test/tools/batch_convert.py
```python
#!/usr/bin/env python3
"""
Batch convert all assets listed in asset_manifest.json.
Run from the project root directory.
"""

import json
import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)

def main():
    manifest_path = os.path.join(SCRIPT_DIR, "asset_manifest.json")
    with open(manifest_path, "r") as f:
        manifest = json.load(f)

    source_dir = manifest["source_dir"]
    output_dir = os.path.join(PROJECT_DIR, manifest["output_dir"])

    # Convert sprites
    for name, info in manifest.get("sprites", {}).items():
        src = os.path.join(source_dir, info["source"])
        dst = os.path.join(output_dir, info["output"])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        subprocess.run([
            sys.executable,
            os.path.join(SCRIPT_DIR, "convert_sprite.py"),
            src, dst,
            "--size", str(info["size"]),
            "--colors", str(info["colors"])
        ], check=True)

    # Convert bullets (same as sprites)
    for name, info in manifest.get("bullets", {}).items():
        src = os.path.join(source_dir, info["source"])
        dst = os.path.join(output_dir, info["output"])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        subprocess.run([
            sys.executable,
            os.path.join(SCRIPT_DIR, "convert_sprite.py"),
            src, dst,
            "--size", str(info["size"]),
            "--colors", str(info["colors"])
        ], check=True)

    # Convert backgrounds
    for name, info in manifest.get("backgrounds", {}).items():
        src = os.path.join(source_dir, info["source"])
        dst = os.path.join(output_dir, info["output"])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        subprocess.run([
            sys.executable,
            os.path.join(SCRIPT_DIR, "convert_background.py"),
            src, dst,
            "--width", str(info["width"]),
            "--height", str(info["height"]),
            "--colors", str(info["colors"])
        ], check=True)

    print("\nAll assets converted successfully.")

if __name__ == "__main__":
    main()
```

### Updated Makefile rules (append to J:/code/snes/snes-rpg-test/Makefile)
```makefile
# ==============================================================================
# Phase 3: Asset Conversion Rules
# ==============================================================================
# Step 1: Python scripts convert source PNGs to SNES-sized indexed PNGs
# Step 2: gfx4snes converts indexed PNGs to .pic (tiles), .pal (palette), .map (tilemap)
#
# gfx4snes flags:
#   -s 8    = 8x8 tile size
#   -s 16   = 16x16 tile blocks (for sprites - actually arranges as 8x8 sub-tiles)
#   -s 32   = 32x32 tile blocks
#   -o 16   = output 16-color palette (4bpp)
#   -o 4    = output 4-color palette (2bpp)
#   -u 16   = use 16 colors
#   -e N    = palette entry N in tilemap attributes
#   -p      = generate .pal file
#   -m      = generate .map file
#   -R      = no tile reduction (important for sprites!)
#   -i      = input file
#   -t png  = input type (default)

PYTHON := python3

# --- Python preprocessing (source PNG -> SNES-sized indexed PNG) ---
assets/sprites/player_ship.png: tools/convert_sprite.py
	$(PYTHON) tools/convert_sprite.py "G:/2024-unity/0-GameAssets/shooter/ship001.png" $@ --size 32 --colors 15

assets/sprites/enemy_scout.png: tools/convert_sprite.py
	$(PYTHON) tools/convert_sprite.py "G:/2024-unity/0-GameAssets/shooter/ship010.png" $@ --size 16 --colors 15

assets/sprites/bullet_basic.png: tools/convert_sprite.py
	$(PYTHON) tools/convert_sprite.py "G:/2024-unity/0-GameAssets/shooter/bullet-01.png" $@ --size 8 --colors 15

assets/sprites/bullet_enemy.png: tools/convert_sprite.py
	$(PYTHON) tools/convert_sprite.py "G:/2024-unity/0-GameAssets/shooter/bullet-08.png" $@ --size 8 --colors 15

assets/backgrounds/zone1_bg.png: tools/convert_background.py
	$(PYTHON) tools/convert_background.py "G:/2024-unity/0-GameAssets/shooter/background-01.png" $@ --width 256 --height 256 --colors 16

# --- gfx4snes conversion (indexed PNG -> .pic/.pal/.map) ---

# Player ship: 32x32 sprite, 4bpp, no tile reduction
assets/sprites/player_ship.pic: assets/sprites/player_ship.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -R -i $<

# Enemy scout: 16x16, 4bpp, no tile reduction
assets/sprites/enemy_scout.pic: assets/sprites/enemy_scout.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -R -i $<

# Bullet basic: 8x8, 4bpp, no reduction
assets/sprites/bullet_basic.pic: assets/sprites/bullet_basic.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -R -i $<

# Bullet enemy: 8x8, 4bpp, no reduction
assets/sprites/bullet_enemy.pic: assets/sprites/bullet_enemy.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -R -i $<

# Zone 1 background: 8x8 tiles, 4bpp, with tilemap and tile reduction
assets/backgrounds/zone1_bg.pic: assets/backgrounds/zone1_bg.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -m -i $<

# Collect all bitmap conversions
bitmaps: assets/sprites/player_ship.pic \
         assets/sprites/enemy_scout.pic \
         assets/sprites/bullet_basic.pic \
         assets/sprites/bullet_enemy.pic \
         assets/backgrounds/zone1_bg.pic
```

### Updated data.asm (add asset includes)
```asm
; ... (after hdr.asm include) ...

;----------------------------------------------------------------------
; Player Ship Sprite (32x32, 4bpp)
;----------------------------------------------------------------------
.section ".rodata_spr_player" superfree

player_ship_tiles:
.incbin "assets/sprites/player_ship.pic"
player_ship_tiles_end:

player_ship_pal:
.incbin "assets/sprites/player_ship.pal"
player_ship_pal_end:

.ends

;----------------------------------------------------------------------
; Zone 1 Background (space starfield)
;----------------------------------------------------------------------
.section ".rodata_bg_zone1" superfree

zone1_bg_tiles:
.incbin "assets/backgrounds/zone1_bg.pic"
zone1_bg_tiles_end:

zone1_bg_pal:
.incbin "assets/backgrounds/zone1_bg.pal"
zone1_bg_pal_end:

zone1_bg_map:
.incbin "assets/backgrounds/zone1_bg.map"
zone1_bg_map_end:

.ends

;----------------------------------------------------------------------
; Enemy Scout Sprite (16x16, 4bpp)
;----------------------------------------------------------------------
.section ".rodata_spr_enemy_scout" superfree

enemy_scout_tiles:
.incbin "assets/sprites/enemy_scout.pic"
enemy_scout_tiles_end:

enemy_scout_pal:
.incbin "assets/sprites/enemy_scout.pal"
enemy_scout_pal_end:

.ends

;----------------------------------------------------------------------
; Bullet Sprites (8x8, 4bpp)
;----------------------------------------------------------------------
.section ".rodata_spr_bullets" superfree

bullet_basic_tiles:
.incbin "assets/sprites/bullet_basic.pic"
bullet_basic_tiles_end:

bullet_basic_pal:
.incbin "assets/sprites/bullet_basic.pal"
bullet_basic_pal_end:

bullet_enemy_tiles:
.incbin "assets/sprites/bullet_enemy.pic"
bullet_enemy_tiles_end:

bullet_enemy_pal:
.incbin "assets/sprites/bullet_enemy.pal"
bullet_enemy_pal_end:

.ends
```

## Technical Specifications

### gfx4snes Output Formats
```
.pic file: Raw tile data
  - 4bpp (16 colors): 32 bytes per 8x8 tile
  - 2bpp (4 colors): 16 bytes per 8x8 tile
  - Tiles arranged in reading order (left-to-right, top-to-bottom)

.pal file: Palette data
  - 2 bytes per color entry (SNES BGR555 format)
  - 15-bit color: 0bbbbbgggggrrrrr
  - Total: 32 bytes for 16-color palette

.map file: Tilemap data
  - 2 bytes per tile entry
  - Format: vhopppcccccccccc
    v = vertical flip, h = horizontal flip
    o = priority, ppp = palette, cccccccccc = tile number
  - 32x32 tilemap = 2048 bytes
```

### Sprite Tile Arrangement in VRAM
For a 32x32 sprite (OBJ_SIZE16_L32 with large size flag):
```
The SNES uses 4 consecutive 16x16 blocks:
  Block 0: top-left     (tiles 0-3 in VRAM)
  Block 1: top-right    (tiles 16-19, offset by +16 tiles)
  Block 2: bottom-left  (tiles 32-35, offset by +32)
  Block 3: bottom-right (tiles 48-51, offset by +48)

Each 16x16 block = 4 tiles of 8x8 at 32 bytes each = 128 bytes
Total 32x32 = 512 bytes

gfx4snes with -R flag outputs tiles sequentially, so for a 32x32 source:
  Row 0: tile0, tile1, tile2, tile3 (32 pixels wide / 8 = 4 tiles)
  Row 1: tile4, tile5, tile6, tile7
  Row 2: tile8, tile9, tile10, tile11
  Row 3: tile12, tile13, tile14, tile15

This matches SNES OBJ tile layout when using 8x8 base tile size.
```

### VRAM Loading Procedure for Sprites
```c
/* Example: Load player ship 32x32 sprite */
extern char player_ship_tiles, player_ship_tiles_end;
extern char player_ship_pal, player_ship_pal_end;

/* Load tiles to OBJ VRAM at word address OBJ_TILES_VRAM */
dmaCopyVram(&player_ship_tiles, OBJ_TILES_VRAM,
            &player_ship_tiles_end - &player_ship_tiles);

/* Load palette to CGRAM OBJ palette 0 (starts at CGRAM index 128) */
dmaCopyCGram(&player_ship_pal, 128,
             &player_ship_pal_end - &player_ship_pal);
```

## Asset Requirements
For Phase 3 verification, convert these minimum assets:
| Asset | Source | Target Size | Colors |
|-------|--------|-------------|--------|
| Player ship | ship001.png | 32x32 | 16 |
| Enemy scout | ship010.png | 16x16 | 16 |
| Bullet player | bullet-01.png | 8x8 | 16 |
| Bullet enemy | bullet-08.png | 8x8 | 16 |
| Zone 1 BG | background-01.png | 256x256 | 16 |

## Acceptance Criteria
1. `python3 tools/batch_convert.py` runs without errors and produces indexed PNG files in assets/.
2. `make bitmaps` runs gfx4snes on all indexed PNGs and produces .pic, .pal, and .map files.
3. `make` produces voidrunner.sfc that loads without errors.
4. In Mesen VRAM viewer, the Zone 1 background tiles are visible in BG1 tile area.
5. In Mesen OAM viewer, the player ship sprite appears correctly.
6. The player ship displays on screen at a fixed position (center-bottom).
7. The Zone 1 background renders correctly on BG1.
8. All palette colors appear correct (no obviously wrong colors).

## SNES-Specific Constraints
- Source PNGs MUST be power-of-2 dimensions for sprites (8, 16, 32, 64).
- gfx4snes requires input PNGs to be indexed color mode, not RGB.
- Palette index 0 is always transparent for sprites (OBJ).
- For BG tiles, palette index 0 within each sub-palette is transparent.
- Maximum tile data per gfx4snes run must fit in VRAM allocation.
- The -R flag is critical for sprites to preserve tile ordering.

## Estimated Complexity
**Medium** - The Python scripts are straightforward, but getting gfx4snes parameters exactly right requires testing. Palette alignment between sprites sharing a palette slot needs care.
