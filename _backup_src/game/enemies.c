/*==============================================================================
 * Enemy Ship System & AI - Phase 18
 *
 * Pool of 8 enemies with dedicated OAM slots (20-27).
 * Each enemy has an AI pattern that controls movement and firing.
 * Each zone loads 2 enemy sprites into VRAM slots A and B.
 *
 * enemyRenderAll() must be called AFTER spriteRenderAll() to overwrite
 * the sprite engine's default hiding of these OAM slots.
 *
 * VRAM layout (OBJ):
 *   Player ship:    offset 0x0000 (tile 0)
 *   Player bullets: offset 0x0400 (tile 64)
 *   Enemy bullets:  offset 0x0600 (tile 96)
 *   Enemy type A:   offset 0x0800 (tile 128)
 *   Enemy type B:   offset 0x0900 (tile 144)
 *============================================================================*/

#include "game/enemies.h"
#include "engine/sprites.h"
#include "engine/bullets.h"
#include "engine/scroll.h"
#include "engine/sound.h"
#include "game/player.h"
#include "config.h"
#include "assets.h"

/*=== VRAM Layout for Enemy Tiles (2 slots per zone) ===*/
#define VRAM_OBJ_ENEMY_A  0x0800
#define VRAM_OBJ_ENEMY_B  0x0900
#define TILE_ENEMY_A  (VRAM_OBJ_ENEMY_A >> 4)  /* 128 */
#define TILE_ENEMY_B  (VRAM_OBJ_ENEMY_B >> 4)  /* 144 */

/* OBJ palette indices for oamSet (0-7) */
#define PAL_ENEMY_A  (PAL_OBJ_ENEMY - 8)   /* 1 */
#define PAL_ENEMY_B  (PAL_OBJ_ENEMY2 - 8)  /* 5 */

/*=== Enemy Type Definitions (ROM data) ===*/
/*                    HP  spd  fire  ai_pattern    score  dmg */
static const EnemyTypeDef enemy_types[ENEMY_TYPE_COUNT] = {
    { 10,  2,   90,  AI_LINEAR,    100,   10 },  /* SCOUT */
    { 20,  1,   60,  AI_SINE_WAVE, 200,   15 },  /* FIGHTER */
    { 40,  1,   45,  AI_HOVER,     350,   20 },  /* HEAVY */
    { 30,  2,   50,  AI_CHASE,     500,   20 },  /* ELITE */
};

/*=== Sine Lookup Table (16 entries, +/-7 pixels amplitude) ===*/
static const s8 ai_sine[16] = {
    0, 3, 5, 7, 7, 7, 5, 3, 0, -3, -5, -7, -7, -7, -5, -3
};

/*--- Module State ---*/
static Enemy enemy_pool[MAX_ENEMIES];

/* Zone-specific enemy type -> VRAM slot mapping */
static u8 zone_type_a;  /* Enemy type loaded at tile slot A */
static u8 zone_type_b;  /* Enemy type loaded at tile slot B */

/*===========================================================================*/
/* Initialization                                                            */
/*===========================================================================*/

void enemyInit(void)
{
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        enemy_pool[i].active = ENTITY_INACTIVE;
        enemy_pool[i].oam_id = (OAM_ENEMIES + i) * 4;
    }
    zone_type_a = ENEMY_TYPE_SCOUT;
    zone_type_b = ENEMY_TYPE_SCOUT;
}

void enemyLoadGraphics(u8 zoneId)
{
    switch (zoneId) {
        case ZONE_DEBRIS:
            /* Slot A: Scout, Slot B: Fighter */
            zone_type_a = ENEMY_TYPE_SCOUT;
            zone_type_b = ENEMY_TYPE_FIGHTER;
            spriteLoadTiles((u8 *)&enemy_scout_til,
                            ASSET_SIZE(enemy_scout_til),
                            VRAM_OBJ_ENEMY_A);
            spriteLoadPalette((u8 *)&enemy_scout_pal,
                              ASSET_SIZE(enemy_scout_pal),
                              PAL_ENEMY_A);
            spriteLoadTiles((u8 *)&enemy_fighter_til,
                            ASSET_SIZE(enemy_fighter_til),
                            VRAM_OBJ_ENEMY_B);
            spriteLoadPalette((u8 *)&enemy_fighter_pal,
                              ASSET_SIZE(enemy_fighter_pal),
                              PAL_ENEMY_B);
            break;

        case ZONE_ASTEROID:
            /* Slot A: Fighter, Slot B: Heavy */
            zone_type_a = ENEMY_TYPE_FIGHTER;
            zone_type_b = ENEMY_TYPE_HEAVY;
            spriteLoadTiles((u8 *)&enemy_fighter_til,
                            ASSET_SIZE(enemy_fighter_til),
                            VRAM_OBJ_ENEMY_A);
            spriteLoadPalette((u8 *)&enemy_fighter_pal,
                              ASSET_SIZE(enemy_fighter_pal),
                              PAL_ENEMY_A);
            spriteLoadTiles((u8 *)&enemy_heavy_til,
                            ASSET_SIZE(enemy_heavy_til),
                            VRAM_OBJ_ENEMY_B);
            spriteLoadPalette((u8 *)&enemy_heavy_pal,
                              ASSET_SIZE(enemy_heavy_pal),
                              PAL_ENEMY_B);
            break;

        case ZONE_FLAGSHIP:
        default:
            /* Slot A: Heavy, Slot B: Elite */
            zone_type_a = ENEMY_TYPE_HEAVY;
            zone_type_b = ENEMY_TYPE_ELITE;
            spriteLoadTiles((u8 *)&enemy_heavy_til,
                            ASSET_SIZE(enemy_heavy_til),
                            VRAM_OBJ_ENEMY_A);
            spriteLoadPalette((u8 *)&enemy_heavy_pal,
                              ASSET_SIZE(enemy_heavy_pal),
                              PAL_ENEMY_A);
            spriteLoadTiles((u8 *)&enemy_elite_til,
                            ASSET_SIZE(enemy_elite_til),
                            VRAM_OBJ_ENEMY_B);
            spriteLoadPalette((u8 *)&enemy_elite_pal,
                              ASSET_SIZE(enemy_elite_pal),
                              PAL_ENEMY_B);
            break;
    }
}

/*===========================================================================*/
/* Spawning                                                                  */
/*===========================================================================*/

Enemy* enemySpawn(u8 type, s16 x, s16 y)
{
    u8 i;
    const EnemyTypeDef *def;

    for (i = 0; i < MAX_ENEMIES; i++) {
        if (enemy_pool[i].active == ENTITY_INACTIVE) {
            def = &enemy_types[type];
            enemy_pool[i].active = ENTITY_ACTIVE;
            enemy_pool[i].type = type;
            enemy_pool[i].x = x;
            enemy_pool[i].y = y;
            enemy_pool[i].vx = 0;
            enemy_pool[i].vy = (s16)def->speed << 8;  /* Convert to 8.8 */
            enemy_pool[i].hp = def->max_hp;
            enemy_pool[i].fire_timer = def->fire_rate;
            enemy_pool[i].ai_state = 0;
            enemy_pool[i].ai_timer = 0;
            enemy_pool[i].ai_param1 = x;  /* Store initial X for sine center */
            enemy_pool[i].flash_timer = 0;
            return &enemy_pool[i];
        }
    }
    return (Enemy *)0;  /* Pool full */
}

void enemySpawnWave(u8 type, u8 count, s16 startX, s16 startY, s16 spacingX, s16 spacingY)
{
    u8 i;
    for (i = 0; i < count; i++) {
        enemySpawn(type, startX + (s16)i * spacingX, startY + (s16)i * spacingY);
    }
}

void enemySpawnFromLeft(u8 type, s16 y)
{
    u8 ai;
    Enemy *e;

    ai = enemy_types[type].ai_pattern;
    if (ai == AI_LINEAR) {
        /* Scouts enter from off-screen left with diagonal trajectory */
        e = enemySpawn(type, -24, y);
        if (e) e->vx = 0x0180;  /* 1.5 px/f right (8.8 FP) */
    } else {
        /* Non-linear: spawn at visible left edge, AI handles movement */
        enemySpawn(type, 24, y);
    }
}

void enemySpawnFromRight(u8 type, s16 y)
{
    u8 ai;
    Enemy *e;

    ai = enemy_types[type].ai_pattern;
    if (ai == AI_LINEAR) {
        /* Scouts enter from off-screen right with diagonal trajectory */
        e = enemySpawn(type, SCREEN_W + 8, y);
        if (e) e->vx = (s16)-0x0180;  /* 1.5 px/f left */
    } else {
        /* Non-linear: spawn at visible right edge, AI handles movement */
        enemySpawn(type, 200, y);
    }
}

/*===========================================================================*/
/* AI Movement Patterns                                                      */
/*===========================================================================*/

static void aiUpdate(Enemy *e)
{
    switch (enemy_types[e->type].ai_pattern) {
        case AI_LINEAR:
            /* Straight down + optional lateral from spawnFromLeft/Right */
            e->y += (e->vy >> 8);
            e->x += (e->vx >> 8);
            break;

        case AI_SINE_WAVE:
            /* Descend + horizontal oscillation using sine table.
             * Index advances every 4 frames -> 64 frame period (~1 sec).
             * ai_param1 = initial X (center of oscillation). */
            e->y += (e->vy >> 8);
            e->ai_timer++;
            e->x = e->ai_param1 + ai_sine[(e->ai_timer >> 2) & 0x0F];
            break;

        case AI_SWOOP:
            /* Enter from side, gradually curve downward.
             * vx set by caller after spawn. Decelerates laterally over time. */
            e->ai_timer++;
            e->y += (e->vy >> 8);
            e->x += (e->vx >> 8);
            if (e->ai_timer > 30 && (e->ai_timer & 7) == 0) {
                if (e->vx > 0x0040) e->vx -= 0x0040;
                else if (e->vx < (s16)-0x0040) e->vx += 0x0040;
                else e->vx = 0;
            }
            break;

        case AI_HOVER:
            /* Phase 0: descend to y=60.
             * Phase 1: strafe left/right, bouncing off edges. */
            if (e->ai_state == 0) {
                e->y += (e->vy >> 8);
                if (e->y >= 60) {
                    e->y = 60;
                    e->ai_state = 1;
                    e->vy = 0;
                    e->vx = 0x0100;  /* 1.0 px/frame right */
                }
            } else {
                e->x += (e->vx >> 8);
                if (e->x <= 16) {
                    e->x = 16;
                    e->vx = 0x0100;
                } else if (e->x >= 224) {
                    e->x = 224;
                    e->vx = (s16)-0x0100;
                }
            }
            break;

        case AI_CHASE:
            /* Descend while tracking player X.
             * Moves 1 pixel horizontally every other frame (~0.5 px/f). */
            e->y += (e->vy >> 8);
            e->ai_timer++;
            if (e->ai_timer & 1) {
                if (g_player.x > e->x + 4) {
                    e->x++;
                } else if (g_player.x < e->x - 4) {
                    e->x--;
                }
            }
            break;
    }
}

/*===========================================================================*/
/* Update & Render                                                           */
/*===========================================================================*/

void enemyUpdateAll(void)
{
    u8 i;
    Enemy *e;
    const EnemyTypeDef *def;

    for (i = 0; i < MAX_ENEMIES; i++) {
        e = &enemy_pool[i];
        if (e->active != ENTITY_ACTIVE) continue;

        /* Run AI movement */
        aiUpdate(e);

        /* Off-screen removal */
        if (e->y > 240 || e->y < -48 || e->x < -48 || e->x > 288) {
            e->active = ENTITY_INACTIVE;
            continue;
        }

        /* Firing logic */
        def = &enemy_types[e->type];
        if (def->fire_rate > 0) {
            if (e->fire_timer > 0) {
                e->fire_timer--;
            }
            if (e->fire_timer == 0) {
                e->fire_timer = def->fire_rate;
                soundPlaySFX(SFX_ENEMY_SHOOT);
                /* HOVER and CHASE fire aimed bullets; others fire straight down */
                if (def->ai_pattern == AI_HOVER || def->ai_pattern == AI_CHASE) {
                    bulletEnemyFire(e->x + 8, e->y + 32,
                                   g_player.x + 16, g_player.y + 16,
                                   BULLET_TYPE_ENEMY_AIMED);
                } else {
                    bulletEnemyFireDown(e->x + 8, e->y + 24);
                }
            }
        }

        /* Damage blink countdown */
        if (e->flash_timer > 0) {
            e->flash_timer--;
        }
    }
}

void enemyRenderAll(void)
{
    u8 i;
    u16 tile;
    u8 pal;
    Enemy *e;

    for (i = 0; i < MAX_ENEMIES; i++) {
        e = &enemy_pool[i];

        if (e->active != ENTITY_ACTIVE) {
            oamSetVisible(e->oam_id, OBJ_HIDE);
            continue;
        }

        /* Blink during damage flash (hide on odd frames) */
        if (e->flash_timer > 0 && (e->flash_timer & 1)) {
            oamSetVisible(e->oam_id, OBJ_HIDE);
            continue;
        }

        /* Select tile and palette based on which VRAM slot this type uses */
        if (e->type == zone_type_a) {
            tile = TILE_ENEMY_A;
            pal = PAL_ENEMY_A;
        } else {
            tile = TILE_ENEMY_B;
            pal = PAL_ENEMY_B;
        }

        oamSet(e->oam_id,
               (u16)e->x, (u16)e->y,
               2,             /* priority (above BG1/BG2) */
               0, 0,          /* no flip */
               tile,
               pal);
        oamSetEx(e->oam_id, OBJ_LARGE, OBJ_SHOW);
    }
}

/*===========================================================================*/
/* Damage & Destruction                                                      */
/*===========================================================================*/

u8 enemyDamage(Enemy *e, u8 damage)
{
    if (e->hp <= damage) {
        e->hp = 0;
        e->active = ENTITY_INACTIVE;
        return 1;  /* Destroyed */
    }
    e->hp -= damage;
    e->flash_timer = 6;  /* Blink for 6 frames */
    return 0;
}

void enemyKillAll(void)
{
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        enemy_pool[i].active = ENTITY_INACTIVE;
    }
}

Enemy* enemyGetPool(void)
{
    return enemy_pool;
}

const EnemyTypeDef* enemyGetTypeDef(u8 type)
{
    return &enemy_types[type];
}

/*===========================================================================*/
/* Zone 1: Debris Field - Wave Trigger Callbacks                             */
/*===========================================================================*/

static void z1_w01(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 2, 60, -20, 60, 0); }
static void z1_w02(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 3, 40, -20, 50, 0); }
static void z1_w03(void) { enemySpawnFromLeft(ENEMY_TYPE_SCOUT, -20); }
static void z1_w04(void) { enemySpawnFromRight(ENEMY_TYPE_SCOUT, -20); }
static void z1_w05(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 4, 30, -20, 48, 0); }
static void z1_w06(void) { enemySpawn(ENEMY_TYPE_FIGHTER, 120, -32); }
static void z1_w07(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 3, 50, -30, 60, -10); }
static void z1_w08(void)
{
    enemySpawnFromLeft(ENEMY_TYPE_SCOUT, -20);
    enemySpawnFromRight(ENEMY_TYPE_SCOUT, -20);
}
static void z1_w09(void) { enemySpawn(ENEMY_TYPE_FIGHTER, 60, -32); }
static void z1_w10(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 3, 80, -20, 40, 0); }
static void z1_w11(void)
{
    enemySpawn(ENEMY_TYPE_FIGHTER, 80, -32);
    enemySpawn(ENEMY_TYPE_FIGHTER, 160, -32);
}
static void z1_w12(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 5, 20, -20, 44, 0); }
static void z1_w13(void)
{
    enemySpawnFromLeft(ENEMY_TYPE_SCOUT, -20);
    enemySpawnWave(ENEMY_TYPE_SCOUT, 2, 100, -20, 50, 0);
}
static void z1_w14(void) { enemySpawn(ENEMY_TYPE_FIGHTER, 120, -32); }
static void z1_w15(void) { scrollTransitionSpeed(SCROLL_SPEED_SLOW, 60); }

/*===========================================================================*/
/* Zone 2: Asteroid Belt - Wave Trigger Callbacks                            */
/*===========================================================================*/

static void z2_w01(void) { enemySpawnWave(ENEMY_TYPE_FIGHTER, 2, 80, -20, 80, 0); }
static void z2_w02(void)
{
    enemySpawnFromLeft(ENEMY_TYPE_FIGHTER, -20);
    enemySpawnFromRight(ENEMY_TYPE_FIGHTER, -40);
}
static void z2_w03(void) { enemySpawnWave(ENEMY_TYPE_FIGHTER, 3, 40, -20, 60, 0); }
static void z2_w04(void) { enemySpawn(ENEMY_TYPE_HEAVY, 120, -32); }
static void z2_w05(void)
{
    enemySpawnWave(ENEMY_TYPE_FIGHTER, 2, 60, -20, 100, 0);
    enemySpawnFromLeft(ENEMY_TYPE_FIGHTER, -40);
}
static void z2_w06(void) { enemySpawnWave(ENEMY_TYPE_FIGHTER, 4, 30, -20, 50, 0); }
static void z2_w07(void)
{
    enemySpawn(ENEMY_TYPE_HEAVY, 60, -32);
    enemySpawn(ENEMY_TYPE_HEAVY, 180, -32);
}
static void z2_w08(void) { enemySpawnWave(ENEMY_TYPE_FIGHTER, 3, 50, -30, 60, -10); }
static void z2_w09(void)
{
    enemySpawnFromLeft(ENEMY_TYPE_FIGHTER, -20);
    enemySpawnWave(ENEMY_TYPE_FIGHTER, 2, 120, -20, 50, 0);
}
static void z2_w10(void) { enemySpawnWave(ENEMY_TYPE_FIGHTER, 5, 20, -20, 44, 0); }
static void z2_w11(void)
{
    enemySpawn(ENEMY_TYPE_HEAVY, 120, -32);
    enemySpawnFromRight(ENEMY_TYPE_FIGHTER, -20);
}
static void z2_w12(void) { scrollTransitionSpeed(SCROLL_SPEED_SLOW, 60); }

/*===========================================================================*/
/* Zone 3: Flagship Approach - Wave Trigger Callbacks                        */
/*===========================================================================*/

static void z3_w01(void)
{
    enemySpawn(ENEMY_TYPE_HEAVY, 80, -32);
    enemySpawn(ENEMY_TYPE_HEAVY, 160, -32);
}
static void z3_w02(void) { enemySpawnWave(ENEMY_TYPE_ELITE, 2, 60, -20, 120, 0); }
static void z3_w03(void)
{
    enemySpawnFromLeft(ENEMY_TYPE_ELITE, -20);
    enemySpawnFromRight(ENEMY_TYPE_ELITE, -40);
}
static void z3_w04(void) { enemySpawnWave(ENEMY_TYPE_HEAVY, 3, 40, -20, 70, 0); }
static void z3_w05(void)
{
    enemySpawn(ENEMY_TYPE_ELITE, 120, -32);
    enemySpawnWave(ENEMY_TYPE_HEAVY, 2, 40, -20, 140, 0);
}
static void z3_w06(void) { enemySpawnWave(ENEMY_TYPE_ELITE, 3, 40, -30, 70, -10); }
static void z3_w07(void)
{
    enemySpawnFromLeft(ENEMY_TYPE_HEAVY, -20);
    enemySpawnFromRight(ENEMY_TYPE_HEAVY, -20);
    enemySpawn(ENEMY_TYPE_ELITE, 120, -32);
}
static void z3_w08(void)
{
    enemySpawnWave(ENEMY_TYPE_ELITE, 2, 80, -20, 80, 0);
    enemySpawnWave(ENEMY_TYPE_HEAVY, 2, 40, -40, 160, 0);
}
static void z3_w09(void) { enemySpawnWave(ENEMY_TYPE_ELITE, 4, 30, -20, 50, 0); }
static void z3_w10(void) { scrollTransitionSpeed(SCROLL_SPEED_SLOW, 60); }

/*===========================================================================*/
/* Zone Trigger Registration                                                 */
/*===========================================================================*/

void enemySetupZoneTriggers(u8 zoneId)
{
    scrollClearTriggers();

    switch (zoneId) {
        case ZONE_DEBRIS:
            /* Zone 1: gradual introduction of enemies */
            scrollAddTrigger(300,  z1_w01);
            scrollAddTrigger(600,  z1_w02);
            scrollAddTrigger(900,  z1_w03);
            scrollAddTrigger(1100, z1_w04);
            scrollAddTrigger(1400, z1_w05);
            scrollAddTrigger(1700, z1_w06);
            scrollAddTrigger(2000, z1_w07);
            scrollAddTrigger(2300, z1_w08);
            scrollAddTrigger(2700, z1_w09);
            scrollAddTrigger(3100, z1_w10);
            scrollAddTrigger(3500, z1_w11);
            scrollAddTrigger(3900, z1_w12);
            scrollAddTrigger(4200, z1_w13);
            scrollAddTrigger(4500, z1_w14);
            scrollAddTrigger(4700, z1_w15);
            break;

        case ZONE_ASTEROID:
            /* Zone 2: more fighters, introduce heavies */
            scrollAddTrigger(300,  z2_w01);
            scrollAddTrigger(600,  z2_w02);
            scrollAddTrigger(900,  z2_w03);
            scrollAddTrigger(1200, z2_w04);
            scrollAddTrigger(1600, z2_w05);
            scrollAddTrigger(2000, z2_w06);
            scrollAddTrigger(2400, z2_w07);
            scrollAddTrigger(2800, z2_w08);
            scrollAddTrigger(3200, z2_w09);
            scrollAddTrigger(3600, z2_w10);
            scrollAddTrigger(4200, z2_w11);
            scrollAddTrigger(4700, z2_w12);
            break;

        case ZONE_FLAGSHIP:
            /* Zone 3: heavies and elites, aggressive */
            scrollAddTrigger(300,  z3_w01);
            scrollAddTrigger(700,  z3_w02);
            scrollAddTrigger(1100, z3_w03);
            scrollAddTrigger(1500, z3_w04);
            scrollAddTrigger(1900, z3_w05);
            scrollAddTrigger(2300, z3_w06);
            scrollAddTrigger(2800, z3_w07);
            scrollAddTrigger(3300, z3_w08);
            scrollAddTrigger(3800, z3_w09);
            scrollAddTrigger(4700, z3_w10);
            break;
    }
}
