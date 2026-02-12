ifeq ($(strip $(PVSNESLIB_HOME)),)
$(error "Please set PVSNESLIB_HOME environment variable. Run env.bat or: set PVSNESLIB_HOME=J:/code/snes/snes-build-tools/tools/pvsneslib")
endif

# Add project include path (snes_rules appends its own paths via +=)
CFLAGS = -I$(CURDIR)/include

include ${PVSNESLIB_HOME}/devkitsnes/snes_rules

# Fix: snes_rules Windows path conversion breaks under Git Bash/MSYS.
# Override LIBDIRSOBJSW with Unix-style path (Git Bash handles it fine).
LIBDIRSOBJSW := $(LIBDIRSOBJS)

.PHONY: bitmaps all cleanAssets

PYTHON := python

#---------------------------------------------------------------------------------
# ROMNAME is used in snes_rules file
export ROMNAME := vex_defender

all: bitmaps $(ROMNAME).sfc

clean: cleanBuildRes cleanRom cleanGfx cleanAssets

#---------------------------------------------------------------------------------
# Stage 1: Python preprocessing (high-res PNG -> SNES-sized indexed PNG)
#---------------------------------------------------------------------------------

assets/sprites/player/player_ship.png:
	$(PYTHON) tools/convert_sprite.py "G:/2024-unity/0-GameAssets/shooter/ship001.png" $@ --size 32 --colors 15

assets/sprites/enemies/enemy_scout.png:
	$(PYTHON) tools/convert_sprite.py "G:/2024-unity/0-GameAssets/shooter/ship010.png" $@ --size 32 --colors 15

assets/sprites/bullets/bullet_player.png:
	$(PYTHON) tools/convert_sprite.py "G:/2024-unity/0-GameAssets/shooter/bullet-01.png" $@ --size 16 --colors 15

assets/sprites/bullets/bullet_enemy.png:
	$(PYTHON) tools/convert_sprite.py "G:/2024-unity/0-GameAssets/shooter/bullet-08.png" $@ --size 16 --colors 15

assets/sprites/enemies/enemy_fighter.png:
	$(PYTHON) tools/convert_sprite.py "G:/2024-unity/0-GameAssets/shooter/ship020.png" $@ --size 32 --colors 15

assets/sprites/enemies/enemy_heavy.png:
	$(PYTHON) tools/convert_sprite.py "G:/2024-unity/0-GameAssets/shooter/ship030.png" $@ --size 32 --colors 15

assets/sprites/enemies/enemy_elite.png:
	$(PYTHON) tools/convert_sprite.py "G:/2024-unity/0-GameAssets/shooter/ship050.png" $@ --size 32 --colors 15

assets/backgrounds/zone1_bg.png:
	$(PYTHON) tools/convert_background.py "G:/2024-unity/0-GameAssets/shooter/background-01.png" $@ --width 256 --height 256 --colors 16

assets/backgrounds/zone2_bg.png:
	$(PYTHON) tools/convert_background.py "G:/2024-unity/0-GameAssets/shooter/background-05.png" $@ --width 256 --height 256 --colors 16

assets/backgrounds/zone3_bg.png:
	$(PYTHON) tools/convert_background.py "G:/2024-unity/0-GameAssets/shooter/background-09.png" $@ --width 256 --height 256 --colors 16

#---------------------------------------------------------------------------------
# Stage 2: gfx4snes (indexed PNG -> .pic/.pal/.map)
# Sprites: -R (no tile reduction), -p (palette), no -m (no tilemap)
# Backgrounds: -m (tilemap), -p (palette), -e 0 (palette entry 0)
#---------------------------------------------------------------------------------

assets/sprites/player/player_ship.pic: assets/sprites/player/player_ship.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -R -i $<

assets/sprites/enemies/enemy_scout.pic: assets/sprites/enemies/enemy_scout.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -R -i $<

assets/sprites/enemies/enemy_fighter.pic: assets/sprites/enemies/enemy_fighter.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -R -i $<

assets/sprites/enemies/enemy_heavy.pic: assets/sprites/enemies/enemy_heavy.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -R -i $<

assets/sprites/enemies/enemy_elite.pic: assets/sprites/enemies/enemy_elite.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -R -i $<

assets/sprites/bullets/bullet_player.pic: assets/sprites/bullets/bullet_player.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -R -i $<

assets/sprites/bullets/bullet_enemy.pic: assets/sprites/bullets/bullet_enemy.png
	$(GFXCONV) -s 8 -o 16 -u 16 -p -R -i $<

assets/backgrounds/zone1_bg.pic: assets/backgrounds/zone1_bg.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -m -i $<

assets/backgrounds/zone2_bg.pic: assets/backgrounds/zone2_bg.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -m -i $<

assets/backgrounds/zone3_bg.pic: assets/backgrounds/zone3_bg.png
	$(GFXCONV) -s 8 -o 16 -u 16 -e 0 -p -m -i $<

#---------------------------------------------------------------------------------
# Collect all bitmap targets
#---------------------------------------------------------------------------------

bitmaps: assets/sprites/player/player_ship.pic \
         assets/sprites/enemies/enemy_scout.pic \
         assets/sprites/enemies/enemy_fighter.pic \
         assets/sprites/enemies/enemy_heavy.pic \
         assets/sprites/enemies/enemy_elite.pic \
         assets/sprites/bullets/bullet_player.pic \
         assets/sprites/bullets/bullet_enemy.pic \
         assets/backgrounds/zone1_bg.pic \
         assets/backgrounds/zone2_bg.pic \
         assets/backgrounds/zone3_bg.pic

cleanAssets:
	rm -f assets/sprites/player/*.pic assets/sprites/player/*.pal
	rm -f assets/sprites/enemies/*.pic assets/sprites/enemies/*.pal
	rm -f assets/sprites/bullets/*.pic assets/sprites/bullets/*.pal
	rm -f assets/backgrounds/*.pic assets/backgrounds/*.pal assets/backgrounds/*.map
