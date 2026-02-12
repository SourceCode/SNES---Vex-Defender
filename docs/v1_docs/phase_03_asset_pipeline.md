# Phase 3: Asset Pipeline & Conversion Scripts

## Objective
Build the complete asset conversion pipeline that transforms source PNG images from `G:/2024-unity/0-GameAssets/shooter/` into SNES-compatible tile data (.pic), palette data (.pal), and tilemap data (.map) using gfx4snes. Create batch scripts and Makefile rules for automated conversion.

## Prerequisites
- Phase 1 (Project Scaffolding) complete
- Phase 2 (Hardware Init) complete
- Source assets exist at `G:/2024-unity/0-GameAssets/shooter/`

## Detailed Tasks

### 1. Select and Catalog Assets for the Game
From the 200+ available assets, select the subset needed:

**Player Ship** (1 ship):
- `ship001.png` → Player ship "Vex's Striker"

**Enemy Ships** (6 ships for 3 zones):
- `ship010.png` → Scout (Zone 1 basic enemy)
- `ship015.png` → Raider (Zone 1 medium enemy)
- `ship025.png` → Interceptor (Zone 2 basic enemy)
- `ship030.png` → Destroyer (Zone 2 medium enemy)
- `ship045.png` → Elite Guard (Zone 3 enemy)
- `ship060.png` → Alien Commander (Zone 3 boss)

**Boss Ships** (2 bosses):
- `ship050.png` → Zone 2 Mini-Boss "Sentinel"
- `ship080.png` → Zone 3 Final Boss "The Leviathan"

**Bullets** (4 types):
- `bullet-01.png` → Player basic shot
- `bullet-05.png` → Player special attack
- `bullet-08.png` → Enemy basic shot
- `bullet-12.png` → Boss attack

**Backgrounds** (3 + title):
- `background-01.png` → Zone 1 Debris Field
- `background-03.png` → Zone 2 Asteroid Belt
- `background-06.png` → Zone 3 Flagship Space
- `background-05.png` → Title screen background

**Items/Icons** (6 key items):
- `item-icon-01.png` → Health restore
- `item-icon-05.png` → Energy restore
- `item-icon-10.png` → Attack boost
- `item-icon-15.png` → Defense boost
- `item-icon-20.png` → Speed boost
- `item-icon-25.png` → Special weapon

**Asteroids** (3 obstacles):
- `asteroid-01.png` → Small asteroid
- `asteroid-05.png` → Medium asteroid
- `asteroid-10.png` → Large asteroid

**NPCs**:
- `npc-01.png` → Admiral Holt (ally/twist villain)
- `npc-02.png` → Alien Commander Zyx (enemy/twist ally)

**Planets**:
- `planet-small-01.png` → Background decoration

### 2. Pre-Process Assets for SNES

SNES constraints for gfx4snes conversion:
- **Sprites**: Must be 8x8, 16x16, 32x32, or 64x64 pixels
- **4bpp mode**: Max 16 colors per palette (including transparent)
- **Background tiles**: 8x8 pixel tiles, max 1024 unique tiles per BG
- **Image width**: Must be multiple of 8 pixels

**Pre-processing steps** (Python scripts or manual):
1. Resize ship sprites to 32x32 pixels (standard game sprite size)
2. Resize boss sprites to 64x64 pixels
3. Resize bullet sprites to 8x8 or 16x16 pixels
4. Reduce colors to 15 per sprite (+1 transparent)
5. Resize backgrounds to 256x224 (SNES resolution)
6. Quantize background palettes to 16 colors

### 3. Create Conversion Scripts

### 4. Update Makefile with Conversion Rules

### 5. Create data.asm Includes

## Files to Create/Modify

| File | Action | Purpose |
|------|--------|---------|
| `tools/convert_assets.bat` | CREATE | Master conversion script |
| `tools/prep_sprites.py` | CREATE | Pre-process sprites (resize, palette reduce) |
| `tools/prep_backgrounds.py` | CREATE | Pre-process backgrounds |
| `Makefile` | MODIFY | Add asset conversion rules |
| `data/data.asm` | MODIFY | Add .incbin for converted assets |
| `assets/sprites/player/*.png` | CREATE | Copied/processed player sprites |
| `assets/sprites/enemies/*.png` | CREATE | Copied/processed enemy sprites |
| `assets/backgrounds/*.png` | CREATE | Copied/processed backgrounds |

## Technical Specifications

### Asset Conversion Commands

**Sprite conversion** (gfx4snes):
```bash
# 32x32 sprite, 16 colors, no tile reduction (sprite mode)
gfx4snes -s 32 -o 16 -u 16 -p -e 0 -i sprite.png

# Output: sprite.pic (tile data), sprite.pal (palette)
# -s 32  : 32x32 pixel sprite blocks
# -o 16  : 16 colors (4bpp)
# -u 16  : 16 color palette entries
# -p     : Generate palette file
# -e 0   : Palette entry 0 (transparent)
# -i     : Input file
```

**Background conversion** (gfx4snes):
```bash
# Background tile conversion with tile reduction
gfx4snes -s 8 -o 16 -u 16 -p -t -m -i background.png

# Output: background.pic (unique tiles), background.pal (palette),
#         background.map (tilemap referencing tiles)
# -t     : Enable tile reduction (remove duplicates)
# -m     : Generate tilemap
```

**Font conversion**:
```bash
# 8x8 font, 4 colors (2bpp for BG3)
gfx4snes -s 8 -o 4 -u 4 -p -e 0 -i font.png
```

### convert_assets.bat
```batch
@echo off
REM VEX DEFENDER - Asset Conversion Pipeline
REM Converts source PNGs to SNES format

SET GFX4SNES=J:\code\snes\snes-build-tools\tools\pvsneslib\devkitsnes\tools\gfx4snes.exe
SET SRC=G:\2024-unity\0-GameAssets\shooter
SET DST=assets

echo === VEX DEFENDER Asset Pipeline ===

REM --- Player Ship (32x32, 16 colors) ---
echo Converting player ship...
%GFX4SNES% -s 32 -o 16 -u 16 -p -e 0 -i %DST%/sprites/player/ship_player.png

REM --- Enemy Ships (32x32, 16 colors) ---
echo Converting enemy ships...
%GFX4SNES% -s 32 -o 16 -u 16 -p -e 0 -i %DST%/sprites/enemies/enemy_scout.png
%GFX4SNES% -s 32 -o 16 -u 16 -p -e 0 -i %DST%/sprites/enemies/enemy_raider.png
%GFX4SNES% -s 32 -o 16 -u 16 -p -e 0 -i %DST%/sprites/enemies/enemy_interceptor.png
%GFX4SNES% -s 32 -o 16 -u 16 -p -e 0 -i %DST%/sprites/enemies/enemy_destroyer.png
%GFX4SNES% -s 32 -o 16 -u 16 -p -e 0 -i %DST%/sprites/enemies/enemy_elite.png
%GFX4SNES% -s 32 -o 16 -u 16 -p -e 0 -i %DST%/sprites/enemies/enemy_commander.png

REM --- Boss Ships (64x64, 16 colors) ---
echo Converting boss ships...
%GFX4SNES% -s 32 -o 16 -u 16 -p -e 0 -i %DST%/sprites/enemies/boss_sentinel.png
%GFX4SNES% -s 32 -o 16 -u 16 -p -e 0 -i %DST%/sprites/enemies/boss_leviathan.png

REM --- Bullets (16x16, 16 colors) ---
echo Converting bullets...
%GFX4SNES% -s 16 -o 16 -u 16 -p -e 0 -i %DST%/sprites/bullets/bullet_player.png
%GFX4SNES% -s 16 -o 16 -u 16 -p -e 0 -i %DST%/sprites/bullets/bullet_special.png
%GFX4SNES% -s 16 -o 16 -u 16 -p -e 0 -i %DST%/sprites/bullets/bullet_enemy.png
%GFX4SNES% -s 16 -o 16 -u 16 -p -e 0 -i %DST%/sprites/bullets/bullet_boss.png

REM --- Backgrounds (8x8 tiles, tilemap, 16 colors) ---
echo Converting backgrounds...
%GFX4SNES% -s 8 -o 16 -u 16 -p -t -m -i %DST%/backgrounds/bg_debris.png
%GFX4SNES% -s 8 -o 16 -u 16 -p -t -m -i %DST%/backgrounds/bg_asteroid.png
%GFX4SNES% -s 8 -o 16 -u 16 -p -t -m -i %DST%/backgrounds/bg_flagship.png
%GFX4SNES% -s 8 -o 16 -u 16 -p -t -m -i %DST%/backgrounds/bg_title.png

REM --- Item Icons (16x16, 16 colors) ---
echo Converting item icons...
%GFX4SNES% -s 16 -o 16 -u 16 -p -e 0 -i %DST%/sprites/items/item_health.png
%GFX4SNES% -s 16 -o 16 -u 16 -p -e 0 -i %DST%/sprites/items/item_energy.png
%GFX4SNES% -s 16 -o 16 -u 16 -p -e 0 -i %DST%/sprites/items/item_attack.png
%GFX4SNES% -s 16 -o 16 -u 16 -p -e 0 -i %DST%/sprites/items/item_defense.png
%GFX4SNES% -s 16 -o 16 -u 16 -p -e 0 -i %DST%/sprites/items/item_speed.png
%GFX4SNES% -s 16 -o 16 -u 16 -p -e 0 -i %DST%/sprites/items/item_special.png

echo === Conversion Complete ===
```

### prep_sprites.py (Pre-processing Script)
```python
"""
VEX DEFENDER - Asset Pre-processor
Resizes and palette-reduces source images for SNES compatibility.
Requires: pip install Pillow
"""
import os
import shutil
from PIL import Image

SRC_DIR = r"G:\2024-unity\0-GameAssets\shooter"
DST_DIR = r"J:\code\snes\snes-rpg-test\assets"

# Asset mapping: (source_file, dest_subpath, target_size, max_colors)
SPRITE_MAP = [
    # Player
    ("ship001.png", "sprites/player/ship_player.png", (32, 32), 15),

    # Enemies
    ("ship010.png", "sprites/enemies/enemy_scout.png", (32, 32), 15),
    ("ship015.png", "sprites/enemies/enemy_raider.png", (32, 32), 15),
    ("ship025.png", "sprites/enemies/enemy_interceptor.png", (32, 32), 15),
    ("ship030.png", "sprites/enemies/enemy_destroyer.png", (32, 32), 15),
    ("ship045.png", "sprites/enemies/enemy_elite.png", (32, 32), 15),
    ("ship060.png", "sprites/enemies/enemy_commander.png", (32, 32), 15),

    # Bosses (larger sprites - composed of multiple 32x32)
    ("ship050.png", "sprites/enemies/boss_sentinel.png", (64, 64), 15),
    ("ship080.png", "sprites/enemies/boss_leviathan.png", (64, 64), 15),

    # Bullets
    ("bullet-01.png", "sprites/bullets/bullet_player.png", (16, 16), 15),
    ("bullet-05.png", "sprites/bullets/bullet_special.png", (16, 16), 15),
    ("bullet-08.png", "sprites/bullets/bullet_enemy.png", (16, 16), 15),
    ("bullet-12.png", "sprites/bullets/bullet_boss.png", (16, 16), 15),

    # Items
    ("item-icon-01.png", "sprites/items/item_health.png", (16, 16), 15),
    ("item-icon-05.png", "sprites/items/item_energy.png", (16, 16), 15),
    ("item-icon-10.png", "sprites/items/item_attack.png", (16, 16), 15),
    ("item-icon-15.png", "sprites/items/item_defense.png", (16, 16), 15),
    ("item-icon-20.png", "sprites/items/item_speed.png", (16, 16), 15),
    ("item-icon-25.png", "sprites/items/item_special.png", (16, 16), 15),

    # Asteroids
    ("asteroid-01.png", "sprites/enemies/asteroid_small.png", (16, 16), 15),
    ("asteroid-05.png", "sprites/enemies/asteroid_medium.png", (32, 32), 15),
    ("asteroid-10.png", "sprites/enemies/asteroid_large.png", (32, 32), 15),

    # NPCs (for dialog portraits)
    ("npc-01.png", "sprites/npcs/npc_admiral.png", (32, 32), 15),
    ("npc-02.png", "sprites/npcs/npc_alien.png", (32, 32), 15),
]

BG_MAP = [
    ("background-01.png", "backgrounds/bg_debris.png", (256, 224), 15),
    ("background-03.png", "backgrounds/bg_asteroid.png", (256, 224), 15),
    ("background-06.png", "backgrounds/bg_flagship.png", (256, 224), 15),
    ("background-05.png", "backgrounds/bg_title.png", (256, 224), 15),
]

def process_image(src_path, dst_path, size, max_colors):
    """Resize and palette-reduce an image for SNES."""
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)

    img = Image.open(src_path).convert("RGBA")

    # Resize with high-quality resampling
    img = img.resize(size, Image.LANCZOS)

    # Quantize to max_colors + 1 (color 0 = transparent)
    # First, create a version with transparency as a specific color
    bg = Image.new("RGBA", size, (0, 0, 0, 0))
    bg.paste(img, (0, 0), img)

    # Convert to palette mode
    rgb = bg.convert("RGB")
    quantized = rgb.quantize(colors=max_colors, method=Image.MEDIANCUT)

    # Save as indexed PNG
    quantized.save(dst_path)
    print(f"  Converted: {os.path.basename(src_path)} -> {dst_path} ({size[0]}x{size[1]}, {max_colors} colors)")

def main():
    print("=== VEX DEFENDER Asset Pre-processor ===")

    print("\n--- Processing Sprites ---")
    for src_name, dst_sub, size, colors in SPRITE_MAP:
        src_path = os.path.join(SRC_DIR, src_name)
        dst_path = os.path.join(DST_DIR, dst_sub)
        if os.path.exists(src_path):
            process_image(src_path, dst_path, size, colors)
        else:
            print(f"  WARNING: Source not found: {src_path}")

    print("\n--- Processing Backgrounds ---")
    for src_name, dst_sub, size, colors in BG_MAP:
        src_path = os.path.join(SRC_DIR, src_name)
        dst_path = os.path.join(DST_DIR, dst_sub)
        if os.path.exists(src_path):
            process_image(src_path, dst_path, size, colors)
        else:
            print(f"  WARNING: Source not found: {src_path}")

    print("\n=== Pre-processing Complete ===")

if __name__ == "__main__":
    main()
```

### Updated data.asm
```asm
;== == == == == == == == == == == == == == == == ==
; Data Section - Game Assets
;== == == == == == == == == == == == == == == == ==

.SECTION ".rodata_bg" SUPERFREE
; --- Backgrounds ---
bg_debris_tiles:    .INCBIN "assets/backgrounds/bg_debris.pic"
bg_debris_tiles_end:
bg_debris_pal:      .INCBIN "assets/backgrounds/bg_debris.pal"
bg_debris_map:      .INCBIN "assets/backgrounds/bg_debris.map"

bg_asteroid_tiles:  .INCBIN "assets/backgrounds/bg_asteroid.pic"
bg_asteroid_tiles_end:
bg_asteroid_pal:    .INCBIN "assets/backgrounds/bg_asteroid.pal"
bg_asteroid_map:    .INCBIN "assets/backgrounds/bg_asteroid.map"

bg_flagship_tiles:  .INCBIN "assets/backgrounds/bg_flagship.pic"
bg_flagship_tiles_end:
bg_flagship_pal:    .INCBIN "assets/backgrounds/bg_flagship.pal"
bg_flagship_map:    .INCBIN "assets/backgrounds/bg_flagship.map"

bg_title_tiles:     .INCBIN "assets/backgrounds/bg_title.pic"
bg_title_tiles_end:
bg_title_pal:       .INCBIN "assets/backgrounds/bg_title.pal"
bg_title_map:       .INCBIN "assets/backgrounds/bg_title.map"
.ENDS

.SECTION ".rodata_spr" SUPERFREE
; --- Player Sprite ---
spr_player_tiles:   .INCBIN "assets/sprites/player/ship_player.pic"
spr_player_pal:     .INCBIN "assets/sprites/player/ship_player.pal"

; --- Enemy Sprites ---
spr_scout_tiles:    .INCBIN "assets/sprites/enemies/enemy_scout.pic"
spr_scout_pal:      .INCBIN "assets/sprites/enemies/enemy_scout.pal"

spr_raider_tiles:   .INCBIN "assets/sprites/enemies/enemy_raider.pic"
spr_raider_pal:     .INCBIN "assets/sprites/enemies/enemy_raider.pal"

spr_interceptor_tiles: .INCBIN "assets/sprites/enemies/enemy_interceptor.pic"
spr_interceptor_pal:   .INCBIN "assets/sprites/enemies/enemy_interceptor.pal"

spr_destroyer_tiles: .INCBIN "assets/sprites/enemies/enemy_destroyer.pic"
spr_destroyer_pal:   .INCBIN "assets/sprites/enemies/enemy_destroyer.pal"

spr_elite_tiles:    .INCBIN "assets/sprites/enemies/enemy_elite.pic"
spr_elite_pal:      .INCBIN "assets/sprites/enemies/enemy_elite.pal"

spr_commander_tiles: .INCBIN "assets/sprites/enemies/enemy_commander.pic"
spr_commander_pal:   .INCBIN "assets/sprites/enemies/enemy_commander.pal"

; --- Boss Sprites ---
spr_sentinel_tiles: .INCBIN "assets/sprites/enemies/boss_sentinel.pic"
spr_sentinel_pal:   .INCBIN "assets/sprites/enemies/boss_sentinel.pal"

spr_leviathan_tiles:.INCBIN "assets/sprites/enemies/boss_leviathan.pic"
spr_leviathan_pal:  .INCBIN "assets/sprites/enemies/boss_leviathan.pal"

; --- Bullet Sprites ---
spr_bullet_player:  .INCBIN "assets/sprites/bullets/bullet_player.pic"
spr_bullet_special: .INCBIN "assets/sprites/bullets/bullet_special.pic"
spr_bullet_enemy:   .INCBIN "assets/sprites/bullets/bullet_enemy.pic"
spr_bullet_boss:    .INCBIN "assets/sprites/bullets/bullet_boss.pic"
spr_bullet_pal:     .INCBIN "assets/sprites/bullets/bullet_player.pal"

; --- Item Sprites ---
spr_item_health:    .INCBIN "assets/sprites/items/item_health.pic"
spr_item_energy:    .INCBIN "assets/sprites/items/item_energy.pic"
spr_item_attack:    .INCBIN "assets/sprites/items/item_attack.pic"
spr_item_defense:   .INCBIN "assets/sprites/items/item_defense.pic"
spr_item_speed:     .INCBIN "assets/sprites/items/item_speed.pic"
spr_item_special:   .INCBIN "assets/sprites/items/item_special.pic"
spr_items_pal:      .INCBIN "assets/sprites/items/item_health.pal"

; --- NPC Portraits ---
spr_npc_admiral:    .INCBIN "assets/sprites/npcs/npc_admiral.pic"
spr_npc_admiral_pal:.INCBIN "assets/sprites/npcs/npc_admiral.pal"
spr_npc_alien:      .INCBIN "assets/sprites/npcs/npc_alien.pic"
spr_npc_alien_pal:  .INCBIN "assets/sprites/npcs/npc_alien.pal"

; --- Asteroids ---
spr_asteroid_sm:    .INCBIN "assets/sprites/enemies/asteroid_small.pic"
spr_asteroid_md:    .INCBIN "assets/sprites/enemies/asteroid_medium.pic"
spr_asteroid_lg:    .INCBIN "assets/sprites/enemies/asteroid_large.pic"
spr_asteroid_pal:   .INCBIN "assets/sprites/enemies/asteroid_small.pal"
.ENDS
```

### Makefile Asset Rules (addition)
```makefile
#---------------------------------------------------------------------------------
# Asset conversion rules
#---------------------------------------------------------------------------------

# Pre-process raw assets (run once, or when source changes)
prep-assets:
	python tools/prep_sprites.py

# Background conversions
assets/backgrounds/bg_debris.pic: assets/backgrounds/bg_debris.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -t -m -i $<

assets/backgrounds/bg_asteroid.pic: assets/backgrounds/bg_asteroid.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -t -m -i $<

assets/backgrounds/bg_flagship.pic: assets/backgrounds/bg_flagship.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -t -m -i $<

assets/backgrounds/bg_title.pic: assets/backgrounds/bg_title.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -t -m -i $<

# Player sprite
assets/sprites/player/ship_player.pic: assets/sprites/player/ship_player.png
	$(GFXCONV) -s 32 -o 16 -u 16 -p -e 0 -i $<

# Enemy sprites (pattern rule for 32x32 enemies)
assets/sprites/enemies/enemy_%.pic: assets/sprites/enemies/enemy_%.png
	$(GFXCONV) -s 32 -o 16 -u 16 -p -e 0 -i $<

# Boss sprites
assets/sprites/enemies/boss_%.pic: assets/sprites/enemies/boss_%.png
	$(GFXCONV) -s 32 -o 16 -u 16 -p -e 0 -i $<

# Bullet sprites (16x16)
assets/sprites/bullets/bullet_%.pic: assets/sprites/bullets/bullet_%.png
	$(GFXCONV) -s 16 -o 16 -u 16 -p -e 0 -i $<

# Item sprites (16x16)
assets/sprites/items/item_%.pic: assets/sprites/items/item_%.png
	$(GFXCONV) -s 16 -o 16 -u 16 -p -e 0 -i $<

# NPC sprites
assets/sprites/npcs/npc_%.pic: assets/sprites/npcs/npc_%.png
	$(GFXCONV) -s 32 -o 16 -u 16 -p -e 0 -i $<

# Asteroid sprites
assets/sprites/enemies/asteroid_small.pic: assets/sprites/enemies/asteroid_small.png
	$(GFXCONV) -s 16 -o 16 -u 16 -p -e 0 -i $<

assets/sprites/enemies/asteroid_medium.pic: assets/sprites/enemies/asteroid_medium.png
	$(GFXCONV) -s 32 -o 16 -u 16 -p -e 0 -i $<

assets/sprites/enemies/asteroid_large.pic: assets/sprites/enemies/asteroid_large.png
	$(GFXCONV) -s 32 -o 16 -u 16 -p -e 0 -i $<

# Collect all bitmap targets
BITMAP_TARGETS := \
	assets/backgrounds/bg_debris.pic \
	assets/backgrounds/bg_asteroid.pic \
	assets/backgrounds/bg_flagship.pic \
	assets/backgrounds/bg_title.pic \
	assets/sprites/player/ship_player.pic \
	assets/sprites/enemies/enemy_scout.pic \
	assets/sprites/enemies/enemy_raider.pic \
	assets/sprites/enemies/enemy_interceptor.pic \
	assets/sprites/enemies/enemy_destroyer.pic \
	assets/sprites/enemies/enemy_elite.pic \
	assets/sprites/enemies/enemy_commander.pic \
	assets/sprites/enemies/boss_sentinel.pic \
	assets/sprites/enemies/boss_leviathan.pic \
	assets/sprites/bullets/bullet_player.pic \
	assets/sprites/bullets/bullet_special.pic \
	assets/sprites/bullets/bullet_enemy.pic \
	assets/sprites/bullets/bullet_boss.pic

bitmaps: $(BITMAP_TARGETS)
```

## Acceptance Criteria
1. `python tools/prep_sprites.py` runs without errors and creates all processed PNGs
2. `tools/convert_assets.bat` converts all PNGs to .pic/.pal/.map files
3. `make bitmaps` converts all assets via Makefile rules
4. Each sprite .pic file is non-empty and correct size (32x32x4bpp = 512 bytes for a single tile)
5. Each .pal file is 32 bytes (16 colors x 2 bytes per color in BGR555)
6. Background .map files are 2048 bytes (32x32 tilemap x 2 bytes per entry)
7. No palette overflow errors from gfx4snes

## SNES-Specific Constraints
- gfx4snes color 0 is ALWAYS transparent for sprites
- BGR555 format: 5 bits each for B, G, R (0bbbbbgggggrrrrr)
- Tile data is planar format (not chunky) - gfx4snes handles this
- Max 1024 unique 8x8 tiles per background layer
- Sprite tiles are stored contiguously in VRAM
- Source PNGs MUST have width/height as multiples of 8

## Memory Budget (Asset Sizes)
| Asset | Tiles | Palette | Map | Total |
|-------|-------|---------|-----|-------|
| Each BG (est.) | ~4KB | 32B | 2KB | ~6KB |
| 4 Backgrounds | ~16KB | 128B | 8KB | ~24KB |
| Player (32x32) | 512B | 32B | - | 544B |
| 6 Enemies (32x32) | 3KB | 192B | - | ~3.2KB |
| 2 Bosses (64x64) | 2KB | 64B | - | ~2.1KB |
| 4 Bullets (16x16) | 512B | 32B | - | 544B |
| **ROM Total** | | | | **~30KB** |

## Estimated Complexity
**Medium** - The Python pre-processing script and gfx4snes conversion require careful attention to color counts and tile sizes. Palette conflicts are the most common issue.

## Agent Instructions
1. First run `python tools/prep_sprites.py` to create processed PNGs
2. If Python/Pillow not available, manually copy and resize images using any available image tool
3. Run gfx4snes on each asset and check for error messages about color counts
4. If gfx4snes reports too many colors, further reduce palette in the PNG
5. Verify .pic file sizes match expected tile data sizes
6. The data.asm extern symbols must match exactly between ASM and C code
7. Build the ROM to verify all .incbin paths resolve correctly
