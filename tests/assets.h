/*==============================================================================
 * Mock Assets Header for Host-Side Testing
 * Provides stub asset symbols matching the real extern declarations.
 *============================================================================*/
#ifndef ASSETS_H
#define ASSETS_H

#include "mock_snes.h"

/* Mock ASSET_SIZE returns a fixed small size */
#define ASSET_SIZE(label) 32

/* Define stub asset symbols as char (matching real assets.h extern char) */
static char player_ship_til, player_ship_til_end;
static char player_ship_pal, player_ship_pal_end;
static char enemy_scout_til, enemy_scout_til_end;
static char enemy_scout_pal, enemy_scout_pal_end;
static char enemy_fighter_til, enemy_fighter_til_end;
static char enemy_fighter_pal, enemy_fighter_pal_end;
static char enemy_heavy_til, enemy_heavy_til_end;
static char enemy_heavy_pal, enemy_heavy_pal_end;
static char enemy_elite_til, enemy_elite_til_end;
static char enemy_elite_pal, enemy_elite_pal_end;
static char bullet_player_til, bullet_player_til_end;
static char bullet_player_pal, bullet_player_pal_end;
static char bullet_enemy_til, bullet_enemy_til_end;
static char bullet_enemy_pal, bullet_enemy_pal_end;
static char zone1_bg_til, zone1_bg_til_end;
static char zone1_bg_pal, zone1_bg_pal_end;
static char zone1_bg_map, zone1_bg_map_end;
static char zone2_bg_til, zone2_bg_til_end;
static char zone2_bg_pal, zone2_bg_pal_end;
static char zone2_bg_map, zone2_bg_map_end;
static char zone3_bg_til, zone3_bg_til_end;
static char zone3_bg_pal, zone3_bg_pal_end;
static char zone3_bg_map, zone3_bg_map_end;
static char star_tiles, star_tiles_end;
static char star_pal, star_pal_end;
static char snesfont, snesfont_end;
static char snespal, snespal_end;

#endif /* ASSETS_H */
