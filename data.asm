.include "hdr.asm"

;--- PVSnesLib Font (from hello_world example) ---
.section ".rodata_font" superfree
snesfont:  .incbin "res/pvsneslibfont.pic"
snesfont_end:
snespal:   .incbin "res/pvsneslibfont.pal"
snespal_end:
.ends

;--- Player Ship Sprite (32x32, 4bpp) ---
.section ".rodata_spr_player" superfree
player_ship_til:  .incbin "assets/sprites/player/player_ship.pic"
player_ship_til_end:
player_ship_pal:  .incbin "assets/sprites/player/player_ship.pal"
player_ship_pal_end:
.ends

;--- Enemy Scout Sprite (32x32, 4bpp) ---
.section ".rodata_spr_enemy_scout" superfree
enemy_scout_til:  .incbin "assets/sprites/enemies/enemy_scout.pic"
enemy_scout_til_end:
enemy_scout_pal:  .incbin "assets/sprites/enemies/enemy_scout.pal"
enemy_scout_pal_end:
.ends

;--- Enemy Fighter Sprite (32x32, 4bpp) ---
.section ".rodata_spr_enemy_fighter" superfree
enemy_fighter_til:  .incbin "assets/sprites/enemies/enemy_fighter.pic"
enemy_fighter_til_end:
enemy_fighter_pal:  .incbin "assets/sprites/enemies/enemy_fighter.pal"
enemy_fighter_pal_end:
.ends

;--- Enemy Heavy Sprite (32x32, 4bpp) ---
.section ".rodata_spr_enemy_heavy" superfree
enemy_heavy_til:  .incbin "assets/sprites/enemies/enemy_heavy.pic"
enemy_heavy_til_end:
enemy_heavy_pal:  .incbin "assets/sprites/enemies/enemy_heavy.pal"
enemy_heavy_pal_end:
.ends

;--- Enemy Elite Sprite (32x32, 4bpp) ---
.section ".rodata_spr_enemy_elite" superfree
enemy_elite_til:  .incbin "assets/sprites/enemies/enemy_elite.pic"
enemy_elite_til_end:
enemy_elite_pal:  .incbin "assets/sprites/enemies/enemy_elite.pal"
enemy_elite_pal_end:
.ends

;--- Player Bullet Sprite (16x16, 4bpp) ---
.section ".rodata_spr_bullet_player" superfree
bullet_player_til:  .incbin "assets/sprites/bullets/bullet_player.pic"
bullet_player_til_end:
bullet_player_pal:  .incbin "assets/sprites/bullets/bullet_player.pal"
bullet_player_pal_end:
.ends

;--- Enemy Bullet Sprite (16x16, 4bpp) ---
.section ".rodata_spr_bullet_enemy" superfree
bullet_enemy_til:  .incbin "assets/sprites/bullets/bullet_enemy.pic"
bullet_enemy_til_end:
bullet_enemy_pal:  .incbin "assets/sprites/bullets/bullet_enemy.pal"
bullet_enemy_pal_end:
.ends

;--- Zone 1 Background tiles (256x256, 4bpp) ---
; Split across sections: tiles exceed 32KB bank limit alone
.section ".rodata_bg_zone1_til" superfree
zone1_bg_til:  .incbin "assets/backgrounds/zone1_bg.pic"
zone1_bg_til_end:
.ends

;--- Zone 1 Background palette and map ---
.section ".rodata_bg_zone1_meta" superfree
zone1_bg_pal:  .incbin "assets/backgrounds/zone1_bg.pal"
zone1_bg_pal_end:
zone1_bg_map:  .incbin "assets/backgrounds/zone1_bg.map"
zone1_bg_map_end:
.ends

;--- Zone 2 Background tiles (256x256, 4bpp) ---
.section ".rodata_bg_zone2_til" superfree
zone2_bg_til:  .incbin "assets/backgrounds/zone2_bg.pic"
zone2_bg_til_end:
.ends

;--- Zone 2 Background palette and map ---
.section ".rodata_bg_zone2_meta" superfree
zone2_bg_pal:  .incbin "assets/backgrounds/zone2_bg.pal"
zone2_bg_pal_end:
zone2_bg_map:  .incbin "assets/backgrounds/zone2_bg.map"
zone2_bg_map_end:
.ends

;--- Zone 3 Background tiles (256x256, 4bpp) ---
.section ".rodata_bg_zone3_til" superfree
zone3_bg_til:  .incbin "assets/backgrounds/zone3_bg.pic"
zone3_bg_til_end:
.ends

;--- Zone 3 Background palette and map ---
.section ".rodata_bg_zone3_meta" superfree
zone3_bg_pal:  .incbin "assets/backgrounds/zone3_bg.pal"
zone3_bg_pal_end:
zone3_bg_map:  .incbin "assets/backgrounds/zone3_bg.map"
zone3_bg_map_end:
.ends

;--- BG2 Star Parallax Tiles (procedural, 4bpp planar) ---
; 4 tiles x 32 bytes = 128 bytes
; Format: 16 bytes bp0/bp1 interleaved + 16 bytes bp2/bp3 interleaved
; Tile 0: empty black
; Tile 1: dot at (3,3) using color 1 (bright star)
; Tile 2: dot at (1,1) using color 2 (medium star)
; Tile 3: dot at (5,6) using color 3 (dim star)
.section ".rodata_bg2_stars" superfree
star_tiles:
  ; Tile 0: empty (32 zero bytes)
  .db $00,$00, $00,$00, $00,$00, $00,$00
  .db $00,$00, $00,$00, $00,$00, $00,$00
  .db $00,$00, $00,$00, $00,$00, $00,$00
  .db $00,$00, $00,$00, $00,$00, $00,$00
  ; Tile 1: dot at row 3, col 3 -> color 1 (bp0=1, bp1=0)
  ; Col 3 = bit (7-3) = bit 4 = $10
  .db $00,$00, $00,$00, $00,$00, $10,$00  ; rows 0-3 bp0/bp1
  .db $00,$00, $00,$00, $00,$00, $00,$00  ; rows 4-7 bp0/bp1
  .db $00,$00, $00,$00, $00,$00, $00,$00  ; rows 0-3 bp2/bp3
  .db $00,$00, $00,$00, $00,$00, $00,$00  ; rows 4-7 bp2/bp3
  ; Tile 2: dot at row 1, col 1 -> color 2 (bp0=0, bp1=1)
  ; Col 1 = bit (7-1) = bit 6 = $40
  .db $00,$00, $00,$40, $00,$00, $00,$00  ; rows 0-3 bp0/bp1
  .db $00,$00, $00,$00, $00,$00, $00,$00  ; rows 4-7 bp0/bp1
  .db $00,$00, $00,$00, $00,$00, $00,$00  ; rows 0-3 bp2/bp3
  .db $00,$00, $00,$00, $00,$00, $00,$00  ; rows 4-7 bp2/bp3
  ; Tile 3: dot at row 5, col 6 -> color 3 (bp0=1, bp1=1)
  ; Col 6 = bit (7-6) = bit 1 = $02
  .db $00,$00, $00,$00, $00,$00, $00,$00  ; rows 0-3 bp0/bp1
  .db $00,$00, $02,$02, $00,$00, $00,$00  ; rows 4-7 bp0/bp1
  .db $00,$00, $00,$00, $00,$00, $00,$00  ; rows 0-3 bp2/bp3
  .db $00,$00, $00,$00, $00,$00, $00,$00  ; rows 4-7 bp2/bp3
star_tiles_end:
star_pal:
  .db $00,$00  ; Color 0: transparent (black)
  .db $FF,$7F  ; Color 1: bright white (BGR555)
  .db $B5,$56  ; Color 2: medium grey
  .db $8C,$31  ; Color 3: dim grey
  ; Colors 4-15: black (unused)
  .db $00,$00, $00,$00, $00,$00, $00,$00
  .db $00,$00, $00,$00, $00,$00, $00,$00
  .db $00,$00, $00,$00, $00,$00, $00,$00
star_pal_end:
.ends

;----------------------------------------------------------------------
; Sound Effects (BRR samples, Phase 17)
; Converted from WAV sources via snesbrr -e
; Each sample is mono 16kHz, truncated to short SFX duration
;----------------------------------------------------------------------
.section ".rodata_sfx" superfree

sfx_player_shoot:
.incbin "assets/sfx/player_shoot.brr"
sfx_player_shoot_end:

sfx_enemy_shoot:
.incbin "assets/sfx/enemy_shoot.brr"
sfx_enemy_shoot_end:

sfx_explosion:
.incbin "assets/sfx/explosion.brr"
sfx_explosion_end:

sfx_hit:
.incbin "assets/sfx/hit.brr"
sfx_hit_end:

sfx_menu_select:
.incbin "assets/sfx/menu_select.brr"
sfx_menu_select_end:

sfx_menu_move:
.incbin "assets/sfx/menu_move.brr"
sfx_menu_move_end:

sfx_dialog_blip:
.incbin "assets/sfx/dialog_blip.brr"
sfx_dialog_blip_end:

sfx_level_up:
.incbin "assets/sfx/level_up.brr"
sfx_level_up_end:

sfx_heal:
.incbin "assets/sfx/heal.brr"
sfx_heal_end:

.ends
