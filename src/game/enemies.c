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
 * VRAM layout (OBJ, 16-name-wide grid):
 *   Player ship:    name 0   (offset 0x0000, cols 0-3, rows 0-3)
 *   Player bullets: name 4   (offset 0x0040, cols 4-5, rows 0-1)
 *   Enemy bullets:  name 6   (offset 0x0060, cols 6-7, rows 0-1)
 *   Enemy type A:   name 128 (offset 0x0800, cols 0-3, rows 8-11)
 *   Enemy type B:   name 132 (offset 0x0840, cols 4-7, rows 8-11)
 *
 * The SNES OBJ tile space is laid out as a 16x16 grid of 8x8 tile "names".
 * A 32x32 sprite uses a 4x4 block of these names. Names 128+ are in the
 * second half of OBJ VRAM (offset 0x0800 words), which corresponds to
 * rows 8-15 of the 16-name grid. Two 32x32 enemy sprites fit side-by-side
 * in this region at names 128 (slot A) and 132 (slot B).
 *============================================================================*/

#include "game/enemies.h"
#include "engine/sprites.h"
#include "engine/bullets.h"
#include "engine/scroll.h"
#include "engine/sound.h"
#include "engine/vblank.h"
#include "engine/collision.h"
#include "game/player.h"
#include "config.h"
#include "assets.h"

/*=== VRAM Layout for Enemy Tiles (2 slots per zone, 32x32 each) ===*/
/* These are VRAM word offsets relative to the OBJ tile base.
 * Divided by 16 (>>4) to get the "name" index for oamSet(). */
#define VRAM_OBJ_ENEMY_A  0x0800  /* Name 128: first enemy tileset position */
#define VRAM_OBJ_ENEMY_B  0x0840  /* Name 132: second enemy tileset position */
#define TILE_ENEMY_A  (VRAM_OBJ_ENEMY_A >> 4)  /* 128: OAM tile name for slot A */
#define TILE_ENEMY_B  (VRAM_OBJ_ENEMY_B >> 4)  /* 132: OAM tile name for slot B */

/* OBJ palette indices for oamSet (0-7 range, not CGRAM absolute).
 * SNES OBJ palettes are CGRAM slots 8-15 (colors 128-255), but oamSet()
 * expects the relative index (0-7). PAL_OBJ_ENEMY is the absolute CGRAM
 * slot number, so we subtract 8 to get the OAM-relative palette index. */
#define PAL_ENEMY_A  (PAL_OBJ_ENEMY - 8)   /* 1: palette for slot A enemies */
#define PAL_ENEMY_B  (PAL_OBJ_ENEMY2 - 8)  /* 5: palette for slot B enemies */

/*=== Enemy Type Definitions (ROM data) ===*/
/* Base stats for each enemy type. Stored as const (ROM) to save WRAM.
 * These define the archetype: scouts are fast/fragile, heavies are slow/tanky.
 *
 * Design rationale for stats:
 *   SCOUT:   HP 10, speed 2 -> dies in 1-2 hits, moves fast. Fodder enemy.
 *   FIGHTER: HP 20, sine wave -> weaves around, harder to hit. Mid-tier.
 *   HEAVY:   HP 40, hover+strafe -> stays on screen, tanks hits. Mini-boss feel.
 *   ELITE:   HP 30, chases player -> aggressive pursuer. High score value. */
/*                    HP  spd  fire  ai_pattern    score  dmg */
static const EnemyTypeDef enemy_types[ENEMY_TYPE_COUNT] = {
    { 10,  2,   90,  AI_LINEAR,    100,   10 },  /* SCOUT:   easy, fires every 1.5s */
    { 20,  1,   60,  AI_SINE_WAVE, 200,   15 },  /* FIGHTER: moderate, fires every 1s */
    { 40,  1,   45,  AI_HOVER,     350,   20 },  /* HEAVY:   tanky, fires every 0.75s */
    { 30,  2,   50,  AI_CHASE,     500,   20 },  /* ELITE:   aggressive, fires every 0.83s */
};

/*=== Sine Lookup Table (16 entries, +/-7 pixels amplitude) ===*/
/* Used by AI_SINE_WAVE for horizontal oscillation. The table represents
 * one full period of a sine wave sampled at 16 points, scaled to +/-7 pixels.
 * The SINE_WAVE AI indexes this table at (ai_timer >> 2) & 0x0F, advancing
 * one step every 4 frames -> 64-frame period (~1.07 seconds at 60fps).
 * Using a lookup table avoids expensive trig math on the 65816 CPU. */
static const s8 ai_sine[16] = {
    0, 3, 5, 7, 7, 7, 5, 3, 0, -3, -5, -7, -7, -7, -5, -3
};

/*--- Module State ---*/
static Enemy enemy_pool[MAX_ENEMIES]; /* Static enemy pool (no heap allocation) */

/* Zone-specific enemy type -> VRAM slot mapping.
 * Each zone loads different enemy types into VRAM slots A and B.
 * These variables track which ENEMY_TYPE_* is currently in each slot
 * so the renderer can select the correct tile/palette. */
static u8 zone_type_a;  /* Enemy type loaded at tile slot A (name 128) */
static u8 zone_type_b;  /* Enemy type loaded at tile slot B (name 132) */

/* Precomputed tile/palette for each zone slot (set by enemyLoadGraphics).
 * These cache the tile name and palette index for slot A and B to avoid
 * recomputing them every frame during rendering. */
static u16 cached_tile_a, cached_tile_b;
static u8  cached_pal_a, cached_pal_b;

/* Combined tile/palette LUT indexed by (type == zone_type_a) (#118).
 * Instead of branching on enemy type in the render loop, we use a 2-entry
 * lookup table: gfx_lut[1] for type A enemies, gfx_lut[0] for type B.
 * This replaces an if/else chain with a single indexed read, saving cycles
 * in the tight per-enemy render loop on the 65816. */
typedef struct { u16 tile; u8 pal; } EnemyGfxEntry;
static EnemyGfxEntry gfx_lut[2];  /* [0]=type_b, [1]=type_a */

/* Active enemy count (exported for external queries - used by render
 * fast-path optimization and boss trigger logic) */
u8 g_enemy_active_count = 0;

/* #178: Adaptive fire rate - wave counter for escalating pressure */
static u8 wave_count = 0;
static u16 wave_last_frame = 0xFFFF;  /* Dedup: only count one wave per frame */

/*===========================================================================*/
/* Initialization                                                            */
/*===========================================================================*/

/*
 * enemyInit
 * ---------
 * Clear the enemy pool and assign OAM byte offsets to each slot.
 * OAM offsets are precomputed once here to avoid repeated multiplication
 * in the render loop. Each enemy gets a dedicated OAM slot starting at
 * OAM_ENEMIES (slot 20), with byte offset = slot_index * 4.
 */
void enemyInit(void)
{
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        enemy_pool[i].active = ENTITY_INACTIVE;
        /* Precompute OAM byte offset: (base_slot + i) * 4 bytes per OAM entry */
        enemy_pool[i].oam_id = (OAM_ENEMIES + i) * 4;
    }
    zone_type_a = ENEMY_TYPE_SCOUT;
    zone_type_b = ENEMY_TYPE_SCOUT;
    g_enemy_active_count = 0;
}

/*
 * enemyLoadGraphics
 * -----------------
 * Load enemy sprite tilesets and palettes for the given zone into VRAM.
 * Must be called during force blank (screen off) because it performs
 * DMA transfers to VRAM and CGRAM.
 *
 * Each zone uses 2 enemy types. Their 32x32 tile data is loaded into
 * two VRAM slots (A at name 128, B at name 132), and their palettes
 * into two CGRAM slots (PAL_ENEMY_A and PAL_ENEMY_B).
 *
 * After loading, the gfx_lut[] is populated for fast rendering:
 *   gfx_lut[1] = slot A (for enemies matching zone_type_a)
 *   gfx_lut[0] = slot B (for all other enemies = zone_type_b)
 *
 * Zone enemy assignments (progressive difficulty):
 *   Zone 1 (Debris):    Scout + Fighter   -> intro enemies
 *   Zone 2 (Asteroid):  Fighter + Heavy   -> mid-game
 *   Zone 3 (Flagship):  Heavy + Elite     -> endgame
 *
 * Parameters:
 *   zoneId - ZONE_DEBRIS(0), ZONE_ASTEROID(1), or ZONE_FLAGSHIP(2)
 */
void enemyLoadGraphics(u8 zoneId)
{
    switch (zoneId) {
        case ZONE_DEBRIS:
            /* Slot A: Scout, Slot B: Fighter */
            zone_type_a = ENEMY_TYPE_SCOUT;
            zone_type_b = ENEMY_TYPE_FIGHTER;
            spriteLoadTiles32((u8 *)&enemy_scout_til,
                              VRAM_OBJ_ENEMY_A);
            spriteLoadPalette((u8 *)&enemy_scout_pal,
                              ASSET_SIZE(enemy_scout_pal),
                              PAL_ENEMY_A);
            spriteLoadTiles32((u8 *)&enemy_fighter_til,
                              VRAM_OBJ_ENEMY_B);
            spriteLoadPalette((u8 *)&enemy_fighter_pal,
                              ASSET_SIZE(enemy_fighter_pal),
                              PAL_ENEMY_B);
            break;

        case ZONE_ASTEROID:
            /* Slot A: Fighter, Slot B: Heavy */
            zone_type_a = ENEMY_TYPE_FIGHTER;
            zone_type_b = ENEMY_TYPE_HEAVY;
            spriteLoadTiles32((u8 *)&enemy_fighter_til,
                              VRAM_OBJ_ENEMY_A);
            spriteLoadPalette((u8 *)&enemy_fighter_pal,
                              ASSET_SIZE(enemy_fighter_pal),
                              PAL_ENEMY_A);
            spriteLoadTiles32((u8 *)&enemy_heavy_til,
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
            spriteLoadTiles32((u8 *)&enemy_heavy_til,
                              VRAM_OBJ_ENEMY_A);
            spriteLoadPalette((u8 *)&enemy_heavy_pal,
                              ASSET_SIZE(enemy_heavy_pal),
                              PAL_ENEMY_A);
            spriteLoadTiles32((u8 *)&enemy_elite_til,
                              VRAM_OBJ_ENEMY_B);
            spriteLoadPalette((u8 *)&enemy_elite_pal,
                              ASSET_SIZE(enemy_elite_pal),
                              PAL_ENEMY_B);
            break;
    }

    /* Cache tile names and palette indices for render-time lookup */
    cached_tile_a = TILE_ENEMY_A;
    cached_pal_a = PAL_ENEMY_A;
    cached_tile_b = TILE_ENEMY_B;
    cached_pal_b = PAL_ENEMY_B;

    /* Populate combined LUT for fast render lookup (#118).
     * Index 1 = type A (matches zone_type_a), index 0 = type B (everything else).
     * Render code does: gfx_lut[(e->type == zone_type_a) ? 1 : 0] */
    gfx_lut[0].tile = cached_tile_b;
    gfx_lut[0].pal  = cached_pal_b;
    gfx_lut[1].tile = cached_tile_a;
    gfx_lut[1].pal  = cached_pal_a;
}

/*===========================================================================*/
/* Spawning                                                                  */
/*===========================================================================*/

/*
 * enemySpawn
 * ----------
 * Spawn a single enemy at the given screen position.
 * Searches the pool for the first INACTIVE slot and initializes it from
 * the EnemyTypeDef table.
 *
 * Parameters:
 *   type - ENEMY_TYPE_* index (0-3)
 *   x, y - initial screen position in pixels (can be off-screen for entries)
 *
 * Returns:
 *   Pointer to the spawned Enemy, or NULL (0) if pool is full or type invalid.
 *
 * Notes:
 *   - Velocity is converted from whole pixels (def->speed) to 8.8 fixed-point
 *     by shifting left 8 bits. This allows sub-pixel movement precision.
 *   - ai_param1 stores the initial X for AI_SINE_WAVE's oscillation center.
 *   - flash_timer starts at 4 for a brief spawn-in blink effect.
 */
Enemy* enemySpawn(u8 type, s16 x, s16 y)
{
    u8 i;
    const EnemyTypeDef *def;

    /* Bounds check: reject invalid enemy types to prevent array overrun */
    if (type >= ENEMY_TYPE_COUNT) return (Enemy *)0;

    /* #178: Track wave count (once per frame, multiple spawns in same frame = one wave) */
    if (g_frame_count != wave_last_frame) {
        wave_last_frame = g_frame_count;
        if (wave_count < 255) wave_count++;
    }

    /* Linear search for first free slot. Pool is small (8) so this is fast. */
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (enemy_pool[i].active == ENTITY_INACTIVE) {
            def = &enemy_types[type];
            enemy_pool[i].active = ENTITY_ACTIVE;
            enemy_pool[i].type = type;
            enemy_pool[i].x = x;
            enemy_pool[i].y = y;
            enemy_pool[i].vx = 0;
            enemy_pool[i].vy = (s16)def->speed << 8;  /* Convert speed to 8.8 fixed-point */
            /* #133: Zone-scaled enemy HP progression.
             * Enemies get tougher in later zones:
             *   Zone 0 (Debris):   base HP (1x)
             *   Zone 1 (Asteroid): +50% HP (base + base>>1)
             *   Zone 2 (Flagship): +100% HP (base << 1)
             * Uses shifts instead of multiply for 65816 performance. */
            enemy_pool[i].hp = def->max_hp;
            if (g_game.current_zone == 1) {
                enemy_pool[i].hp += (def->max_hp >> 1);  /* +50% */
            } else if (g_game.current_zone >= 2) {
                enemy_pool[i].hp += def->max_hp;          /* +100% */
            }
            enemy_pool[i].fire_timer = def->fire_rate; /* Countdown to first shot */
            /* #178: Adaptive fire rate - 12.5% faster after 8+ waves */
            if (wave_count >= 8 && enemy_pool[i].fire_timer > 8) {
                enemy_pool[i].fire_timer -= (enemy_pool[i].fire_timer >> 3);
            }
            enemy_pool[i].ai_state = 0;                /* Start in initial AI sub-state */
            enemy_pool[i].ai_timer = 0;
            enemy_pool[i].ai_param1 = x;  /* Store initial X for sine center of oscillation */
            enemy_pool[i].flash_timer = 4; /* Brief spawn-in blink (4 frames) */
            enemy_pool[i].age = 0;         /* #146: track frames since spawn */

            /* #181: Heavy enemies spawn with a 1-hit shield */
            enemy_pool[i].shield = (type == ENEMY_TYPE_HEAVY) ? 1 : 0;
            /* #186: Default to non-hazard (hazards set by spawn callback) */
            enemy_pool[i].is_hazard = 0;

            /* #197: Track wave enemy count and reset wave timer */
            g_wave_enemy_count++;
            g_wave_timer = 300;  /* 5 seconds for wave clear window */

            /* #147: Rare golden enemy variant (1/16 chance).
             * Golden enemies have 2x HP, 3x score (handled in collision.c),
             * and guaranteed item drops. Permanent flash via flash_timer. */
            if ((g_frame_count & 0x0F) == 0x07) {
                enemy_pool[i].is_golden = 1;
                enemy_pool[i].hp <<= 1;       /* Double HP */
                enemy_pool[i].flash_timer = 255; /* Permanent blink = visual cue */
            } else {
                enemy_pool[i].is_golden = 0;
            }

            return &enemy_pool[i];
        }
    }
    return (Enemy *)0;  /* Pool full - enemy not spawned */
}

/*
 * enemySpawnWave
 * --------------
 * Spawn a horizontal/diagonal formation of enemies with uniform spacing.
 * Commonly used by zone trigger callbacks to create patterns like:
 *   - Horizontal line: spacingY=0, spacingX=50 -> row of enemies
 *   - Diagonal line: spacingX=60, spacingY=-10 -> descending diagonal
 *
 * Parameters:
 *   type     - ENEMY_TYPE_* for all enemies in the wave
 *   count    - number of enemies to spawn
 *   startX/Y - position of the first enemy
 *   spacingX/Y - offset between consecutive enemies
 */
/*
 * enemySpawnVFormation (#164)
 * --------------------------
 * Spawn 5 enemies in a V-shape pattern for visual variety.
 */
void enemySpawnVFormation(u8 type, s16 centerX, s16 topY)
{
    enemySpawn(type, centerX, topY);            /* Point */
    enemySpawn(type, centerX - 30, topY - 20);  /* Left wing */
    enemySpawn(type, centerX + 30, topY - 20);  /* Right wing */
    enemySpawn(type, centerX - 60, topY - 40);  /* Far left */
    enemySpawn(type, centerX + 60, topY - 40);  /* Far right */
}

void enemySpawnWave(u8 type, u8 count, s16 startX, s16 startY, s16 spacingX, s16 spacingY)
{
    s16 x, y;
    x = startX;
    y = startY;
    while (count > 0) {
        enemySpawn(type, x, y);
        x += spacingX;
        y += spacingY;
        count--;
    }
}

/*
 * enemySpawnFromLeft
 * ------------------
 * Spawn an enemy entering from the left side of the screen.
 *
 * For LINEAR AI enemies (scouts): spawns off-screen left at x=-24 with a
 * rightward velocity of 1.5 px/frame (0x0180 in 8.8 fixed-point). This
 * creates a diagonal entry from the left side.
 *
 * For non-linear AI (fighters, etc.): spawns at visible left edge (x=24)
 * and lets the AI pattern handle subsequent movement.
 */
void enemySpawnFromLeft(u8 type, s16 y)
{
    u8 ai;
    Enemy *e;

    ai = enemy_types[type].ai_pattern;
    if (ai == AI_LINEAR) {
        /* Scouts enter from off-screen left with diagonal trajectory */
        e = enemySpawn(type, -24, y);
        if (e) e->vx = 0x0180;  /* 1.5 px/f right in 8.8 fixed-point */
    } else {
        /* Non-linear: spawn at visible left edge, AI handles movement */
        enemySpawn(type, 24, y);
    }
}

/*
 * enemySpawnFromRight
 * -------------------
 * Mirror of enemySpawnFromLeft for right-side entries.
 * LINEAR enemies get leftward velocity (-1.5 px/f) from off-screen right.
 */
void enemySpawnFromRight(u8 type, s16 y)
{
    u8 ai;
    Enemy *e;

    ai = enemy_types[type].ai_pattern;
    if (ai == AI_LINEAR) {
        /* Scouts enter from off-screen right with diagonal trajectory */
        e = enemySpawn(type, SCREEN_W + 8, y);
        if (e) e->vx = (s16)-0x0180;  /* 1.5 px/f left (negative = leftward) */
    } else {
        /* Non-linear: spawn at visible right edge, AI handles movement */
        enemySpawn(type, 200, y);
    }
}

/*===========================================================================*/
/* AI Movement Patterns                                                      */
/*===========================================================================*/

/*
 * aiUpdate (static)
 * -----------------
 * Per-frame AI movement update for one enemy. Implements all movement
 * patterns based on the enemy's ai_pattern from its type definition.
 *
 * Parameters:
 *   e   - pointer to the active enemy instance
 *   def - pointer to the enemy's ROM type definition (for ai_pattern)
 *
 * Movement uses 8.8 fixed-point velocity: vy and vx store sub-pixel
 * precision, and >>8 extracts the whole-pixel movement per frame.
 * This allows speeds like 1.5 px/frame without floating point.
 *
 * Pattern details:
 *   AI_LINEAR:    Pure translation along velocity vector. Simple and cheap.
 *   AI_SINE_WAVE: Vertical descent + horizontal sine oscillation. Uses a
 *                 16-entry lookup table, advancing one entry per 4 frames.
 *   AI_SWOOP:     Enters from side, gradually reduces lateral velocity over
 *                 time to curve into a downward trajectory. (Reserved/future)
 *   AI_HOVER:     Two-phase: descend to y=60, then strafe horizontally
 *                 bouncing between screen edges. Creates a "guard" pattern.
 *   AI_CHASE:     Tracks player X position while descending. Moves 1 pixel
 *                 horizontally every other frame (~0.5 px/f) with a 4-pixel
 *                 deadzone to prevent jitter when aligned with player.
 */
static void aiUpdate(Enemy *e, const EnemyTypeDef *def)
{
    s16 dx, dy;
    /* Extract whole-pixel movement from 8.8 fixed-point velocity */
    dy = e->vy >> 8;
    dx = e->vx >> 8;

    switch (def->ai_pattern) {
        case AI_LINEAR:
            /* Straight down + optional lateral velocity from spawnFromLeft/Right.
             * Simplest pattern: just apply velocity vector each frame. */
            e->y += dy;
            e->x += dx;
            break;

        case AI_SINE_WAVE:
            /* Descend + horizontal oscillation using sine table.
             * ai_timer increments each frame. Index into 16-entry sine table
             * is (timer >> 2) & 0x0F, so the index advances once every 4 frames.
             * Full period = 16 * 4 = 64 frames = ~1.07 seconds at 60fps.
             * ai_param1 stores the initial X position as the center of oscillation,
             * so the enemy weaves around its spawn column. */
            e->y += dy;
            e->ai_timer++;
            e->x = e->ai_param1 + ai_sine[(e->ai_timer >> 2) & 0x0F];
            break;

        case AI_SWOOP:
            /* Enter from side, gradually curve downward.
             * vx is set by the caller after spawn. Over time, lateral velocity
             * decelerates by 0x0040 (0.25 px/f) every 8 frames after frame 30.
             * This creates a curved trajectory from side-entry to downward fall.
             * The 0x0040 threshold prevents oscillation around zero. */
            e->ai_timer++;
            e->y += dy;
            e->x += dx;
            /* Start decelerating laterally after 30 frames (0.5 sec) */
            if (e->ai_timer > 30 && (e->ai_timer & 7) == 0) {
                if (e->vx > 0x0040) e->vx -= 0x0040;        /* Decelerate rightward */
                else if (e->vx < (s16)-0x0040) e->vx += 0x0040; /* Decelerate leftward */
                else e->vx = 0;                               /* Fully stopped laterally */
            }
            break;

        case AI_HOVER:
            /* Two-phase movement for tanky enemies:
             * Phase 0 (ai_state=0): Descend at normal speed until reaching y=60.
             *   This positions the enemy near the top-third of the screen.
             * Phase 1 (ai_state=1): Stop vertical movement, begin strafing
             *   horizontally at 1.0 px/frame. Bounce off left (x=16) and
             *   right (x=224) edges to patrol across the screen. */
            if (e->ai_state == 0) {
                /* Phase 0: descend to target Y position */
                e->y += dy;
                if (e->y >= 60) {
                    e->y = 60;
                    e->ai_state = 1;       /* Transition to strafe phase */
                    e->vy = 0;             /* Stop vertical movement */
                    e->vx = 0x0100;        /* Start strafing right at 1.0 px/frame (8.8 FP) */
                }
            } else {
                /* Phase 1: horizontal strafe with edge bouncing */
                e->x += dx;
                if (e->x <= 16) {
                    e->x = 16;
                    e->vx = 0x0100;        /* Bounce: start moving right */
                } else if (e->x >= 224) {
                    e->x = 224;
                    e->vx = (s16)-0x0100;  /* Bounce: start moving left */
                }
            }
            break;

        case AI_CHASE:
            /* Descend while tracking player X position.
             * Moves 1 pixel horizontally every other frame (ai_timer & 1),
             * giving an effective horizontal speed of ~0.5 px/frame.
             * A 4-pixel deadzone (player.x +/- 4 from enemy.x) prevents
             * jitter when the enemy is roughly aligned with the player.
             * This creates a threatening "homing" behavior without being
             * impossible to dodge. */
            e->y += dy;
            e->ai_timer++;
            if (e->ai_timer & 1) {  /* Move horizontally every other frame */
                if (g_player.x > e->x + 4) {
                    e->x++;  /* Track player rightward */
                } else if (g_player.x < e->x - 4) {
                    e->x--;  /* Track player leftward */
                }
                /* Within deadzone: no horizontal movement */
            }
            break;
    }
}

/*===========================================================================*/
/* Update & Render                                                           */
/*===========================================================================*/

/*
 * enemyUpdateAll
 * --------------
 * Main per-frame update loop for all enemies. Iterates the entire pool
 * once, processing AI, firing, and lifecycle transitions.
 *
 * For each pool slot:
 *   INACTIVE: skip (no cost besides the branch check)
 *   DYING:    decrement flash_timer; when 0, transition to INACTIVE
 *   ACTIVE:   run AI movement, check off-screen removal, handle firing
 *
 * Firing logic:
 *   fire_timer counts down each frame. When it hits 0, reset to fire_rate
 *   and spawn a bullet. HOVER and CHASE enemies fire aimed bullets (toward
 *   player position); others fire straight down. This makes aggressive AI
 *   types more dangerous.
 *
 * Performance note: pointer arithmetic (e = enemy_pool; e++) is used
 * instead of array indexing (enemy_pool[i]) to avoid per-iteration
 * index multiplication on the 65816, which lacks a hardware multiply.
 */
void enemyUpdateAll(void)
{
    u8 i;
    u8 active_count = 0;
    Enemy *e;
    const EnemyTypeDef *def;

    e = enemy_pool;
    for (i = 0; i < MAX_ENEMIES; i++, e++) {
        u8 state;
        state = e->active;
        if (state == ENTITY_INACTIVE) continue;

        if (state == ENTITY_DYING) {
            /* Dying enemies blink for a few frames then disappear */
            e->flash_timer--;
            if (e->flash_timer == 0) {
                e->active = ENTITY_INACTIVE; /* Death animation complete */
            }
            continue;
        }

        /* ENTITY_ACTIVE processing below */
        active_count++;

        /* Cache type definition pointer once per enemy to avoid repeated
         * array indexing (enemy_types[e->type] involves a multiply by
         * sizeof(EnemyTypeDef) which is expensive on the 65816). (#4) */
        def = &enemy_types[e->type];

        /* #146: Increment age (capped at 255) for speed kill bonus */
        if (e->age < 255) e->age++;

        /* Run AI movement pattern for this enemy */
        aiUpdate(e, def);

        /* Off-screen removal: deactivate enemies that have left the visible area.
         * Generous margins (-48 to 288 horizontal, -48 to 240 vertical) allow
         * enemies to be fully off-screen before removal, preventing pop-out. */
        if (e->y > 240 || e->y < -48 || e->x < -48 || e->x > 288) {
            /* #159: Partial score for enemies that scroll off-screen downward.
             * Only award for natural downward exit (y > 240), not sideways. */
            if (e->y > 240) {
                u16 partial;
                partial = def->score_value >> 2;  /* 25% of base score */
                if (g_score + partial < g_score) {
                    g_score = 0xFFFF;  /* Saturate */
                } else {
                    g_score += partial;
                }
            }
            e->active = ENTITY_INACTIVE;
            continue;
        }

        /* Firing logic: decrement-and-check pattern (improvement #12).
         * Decrement first, then check for zero, which is one comparison
         * instead of the decrement-then-compare-to-threshold pattern. */
        if (def->fire_rate > 0) {
            /* #172: Fire telegraph - blink 3 frames before firing */
            if (e->fire_timer == 3 && e->flash_timer == 0) {
                e->flash_timer = 3;
            }
            e->fire_timer--;
            if (e->fire_timer == 0) {
                e->fire_timer = def->fire_rate; /* Reset countdown */
                soundPlaySFX(SFX_ENEMY_SHOOT);
                /* HOVER and CHASE fire aimed bullets toward the player.
                 * Other patterns fire straight-down bullets (simpler to dodge). */
                if (def->ai_pattern == AI_HOVER || def->ai_pattern == AI_CHASE) {
                    bulletEnemyFire(e->x + 8, e->y + 32,       /* Bullet origin: center-bottom of sprite */
                                   g_player.x + 16, g_player.y + 16, /* Target: center of player */
                                   BULLET_TYPE_ENEMY_AIMED);
                } else {
                    bulletEnemyFireDown(e->x + 8, e->y + 24);  /* Bullet origin: center of sprite */
                }
            }
        }

        /* Damage blink countdown: flash_timer > 0 causes the renderer to
         * hide the sprite on odd frames, creating a flash effect. */
        if (e->flash_timer > 0) {
            e->flash_timer--;
        }
    }
    /* Export active count for use by render fast-path and external systems */
    g_enemy_active_count = active_count;
}

/*
 * enemyRenderAll
 * --------------
 * Write all enemy sprite data to OAM (Object Attribute Memory).
 * Must be called AFTER spriteRenderAll() because the sprite engine
 * hides all OAM slots it doesn't own, including our enemy slots.
 * We overwrite those slots here with the correct enemy sprite data.
 *
 * Rendering states:
 *   INACTIVE: hide OAM slot (set Y to off-screen)
 *   DYING:    render with horizontal shudder effect (alternating +/-2px)
 *             and 50% blink (hidden on odd flash_timer frames)
 *   ACTIVE:   normal render with damage-flash blink when flash_timer > 0
 *
 * Performance optimizations:
 *   - Fast path (#104): when no enemies are active AND none are dying,
 *     bulk-hide all 8 OAM slots without iterating individual states.
 *   - Graphics LUT (#118): tile/palette selection uses a precomputed
 *     2-entry table instead of an if/else chain, saving branch overhead.
 *   - Off-screen culling: enemies outside visible area get OBJ_HIDE
 *     without writing tile/position data to OAM.
 */
void enemyRenderAll(void)
{
    u8 i;
    u16 tile;
    u8 pal;
    s16 shake_off;
    Enemy *e;

    /* Fast path: no active or dying enemies -> bulk-hide all OAM slots (#104).
     * This avoids per-slot processing when the screen is empty of enemies. */
    if (g_enemy_active_count == 0) {
        u16 oam;
        u8 any_dying;
        any_dying = 0;
        e = enemy_pool;
        /* Check if any dying enemies exist (they need rendering even with 0 active) */
        for (i = 0; i < MAX_ENEMIES; i++, e++) {
            if (e->active == ENTITY_DYING) { any_dying = 1; break; }
        }
        if (!any_dying) {
            /* No active or dying enemies: hide all 8 OAM slots in one pass */
            oam = OAM_ENEMIES * 4;
            for (i = 0; i < MAX_ENEMIES; i++) {
                oamSetVisible(oam, OBJ_HIDE);
                oam += 4;
            }
            return;
        }
    }

    /* Main render loop: process each pool slot */
    e = enemy_pool;
    for (i = 0; i < MAX_ENEMIES; i++, e++) {
        if (e->active == ENTITY_DYING) {
            /* Dying enemy: alternating blink (hide on odd flash_timer frames).
             * This creates a rapid flicker as the enemy "breaks apart". (#115) */
            if (e->flash_timer & 1) {
                oamSetVisible(e->oam_id, OBJ_HIDE);
                continue;
            }
            /* Skip render for dying enemies that have drifted off-screen */
            if (e->y < -32 || e->y > 224 || e->x < -32 || e->x > 256) {
                oamSetVisible(e->oam_id, OBJ_HIDE);
                continue;
            }
            /* Enhanced death effect: horizontal shudder.
             * Alternates X position by +/-2 pixels each frame based on
             * flash_timer bit 1, creating a vibration/shaking effect. */
            {
                EnemyGfxEntry *gfx;
                shake_off = (e->flash_timer & 2) ? 2 : -2;
                gfx = &gfx_lut[(e->type == zone_type_a) ? 1 : 0];
                tile = gfx->tile;
                pal = gfx->pal;
                oamSet(e->oam_id, (u16)(e->x + shake_off), (u16)e->y, 2, 0, 0, tile, pal);
                oamSetEx(e->oam_id, OBJ_LARGE, OBJ_SHOW);
            }
            continue;
        }
        if (e->active != ENTITY_ACTIVE) {
            /* INACTIVE slot: hide OAM entry */
            oamSetVisible(e->oam_id, OBJ_HIDE);
            continue;
        }

        /* Damage blink: hide sprite on odd flash_timer frames for visual feedback.
         * During the blink, the enemy appears to "flash" white due to rapid
         * show/hide cycling. */
        if (e->flash_timer > 0 && (e->flash_timer & 1)) {
            oamSetVisible(e->oam_id, OBJ_HIDE);
            continue;
        }

        /* Off-screen culling: don't waste OAM writes on invisible sprites */
        if (e->y < -32 || e->y > 224 || e->x < -32 || e->x > 256) {
            oamSetVisible(e->oam_id, OBJ_HIDE);
            continue;
        }

        /* Select tile name and palette via combined LUT.
         * Replaces if/else with indexed lookup: (#11, #118)
         *   (e->type == zone_type_a) evaluates to 1 -> gfx_lut[1] = slot A
         *   (e->type != zone_type_a) evaluates to 0 -> gfx_lut[0] = slot B */
        {
            EnemyGfxEntry *gfx;
            gfx = &gfx_lut[(e->type == zone_type_a) ? 1 : 0];
            tile = gfx->tile;
            pal = gfx->pal;
        }

        /* Write sprite data to OAM.
         * SNES OAM format: X, Y, priority (2 = above BG1/BG2 but below priority 3),
         * hflip, vflip, tile name, palette index.
         * Priority 2 keeps enemies below the player ship (priority 3) but above
         * background layers, maintaining correct visual layering. */
        oamSet(e->oam_id,
               (u16)e->x, (u16)e->y,
               2,             /* priority 2: above BG1/BG2, below player */
               0, 0,          /* no horizontal or vertical flip */
               tile,
               pal);
        /* Set extended OAM attributes: OBJ_LARGE = 32x32 sprite size,
         * OBJ_SHOW = enable sprite display */
        oamSetEx(e->oam_id, OBJ_LARGE, OBJ_SHOW);
    }
}

/*===========================================================================*/
/* Damage & Destruction                                                      */
/*===========================================================================*/

/*
 * enemyDamage
 * -----------
 * Apply damage to an enemy. Handles HP subtraction and state transitions.
 *
 * Parameters:
 *   e      - pointer to the enemy to damage
 *   damage - amount of damage to apply
 *
 * Returns:
 *   1 if the enemy was destroyed (HP reached 0), 0 if it survived.
 *
 * On destruction: HP is zeroed, enemy transitions to ENTITY_DYING with a
 * 10-frame blink-out death animation (controlled by flash_timer).
 * On survival: flash_timer is set to 6 for a brief damage blink effect.
 */
u8 enemyDamage(Enemy *e, u8 damage)
{
    if (e->hp <= damage) {
        /* Enemy destroyed */
        e->hp = 0;
        e->active = ENTITY_DYING;
        /* #127: Extend death flash if killed during damage blink.
         * If the enemy was already flashing (flash_timer > 0), the death
         * animation uses 14 frames instead of 10 for a more visible effect.
         * Prevents the death animation from being "cut short" visually. */
        /* #235: Speed kill flash - longer death flash for quick kills (<90 frames) */
        if (e->age < 90) {
            e->flash_timer = (e->flash_timer > 0) ? 16 : 12;
        } else {
            e->flash_timer = (e->flash_timer > 0) ? 14 : 10;
        }
        return 1;  /* Destroyed */
    }
    /* Enemy survived - apply damage and trigger visual feedback */
    e->hp -= damage;
    e->flash_timer = 6;  /* 6-frame damage flash (3 visible, 3 hidden cycles) */
    return 0;
}

/*
 * enemyKillAll
 * ------------
 * Immediately deactivate all enemies without death animations.
 * Used during zone transitions and battle transitions to clear the field
 * instantly. Unlike enemyDamage(), this skips the DYING state entirely.
 */
void enemyKillAll(void)
{
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        enemy_pool[i].active = ENTITY_INACTIVE;
    }
}

/*
 * enemyGetPool
 * ------------
 * Return a pointer to the start of the enemy pool array for external
 * iteration (primarily used by the collision detection system in Phase 10).
 * Callers iterate MAX_ENEMIES entries, checking 'active' field for each.
 */
Enemy* enemyGetPool(void)
{
    return enemy_pool;
}

/*
 * enemyGetTypeDef
 * ---------------
 * Return a pointer to the ROM-based EnemyTypeDef for the given type.
 * Clamps out-of-range type values to 0 (SCOUT) for safety.
 */
const EnemyTypeDef* enemyGetTypeDef(u8 type)
{
    if (type >= ENEMY_TYPE_COUNT) type = 0;
    return &enemy_types[type];
}

/*===========================================================================*/
/* Zone 1: Debris Field - Wave Trigger Callbacks                             */
/*===========================================================================*/
/* These static functions are registered as scroll-distance triggers in
 * enemySetupZoneTriggers(). Each fires once when the background scroll
 * distance reaches its threshold.
 *
 * Zone 1 design: gradual introduction of enemies.
 *   - Waves 1-5:  Scout-only encounters, teaching basic gameplay
 *   - Wave 6:     First Fighter (sine-wave pattern, harder to hit)
 *   - Waves 7-13: Mixed scouts + fighters, increasing density
 *   - Wave 14:    Final fighter before zone boss
 *   - Wave 15:    Scroll slowdown to signal approaching zone end */

static void z1_w01(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 2, 60, -20, 60, 0); }
static void z1_w02(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 3, 40, -20, 50, 0); }
static void z1_w03(void) { enemySpawnFromLeft(ENEMY_TYPE_SCOUT, -20); }
static void z1_w04(void) { enemySpawnFromRight(ENEMY_TYPE_SCOUT, -20); }
static void z1_w05(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 4, 30, -20, 48, 0); }
static void z1_w06(void) { enemySpawn(ENEMY_TYPE_FIGHTER, 120, -32); }
static void z1_w07(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 3, 50, -30, 60, -10); }
static void z1_w08(void)
{
    /* Pincer attack: scouts from both sides simultaneously */
    enemySpawnFromLeft(ENEMY_TYPE_SCOUT, -20);
    enemySpawnFromRight(ENEMY_TYPE_SCOUT, -20);
}
static void z1_w09(void) { enemySpawn(ENEMY_TYPE_FIGHTER, 60, -32); }
static void z1_w10(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 3, 80, -20, 40, 0); }
static void z1_w11(void)
{
    /* Double fighter formation: two fighters side by side */
    enemySpawn(ENEMY_TYPE_FIGHTER, 80, -32);
    enemySpawn(ENEMY_TYPE_FIGHTER, 160, -32);
}
static void z1_w12(void) { enemySpawnWave(ENEMY_TYPE_SCOUT, 5, 20, -20, 44, 0); }
static void z1_w13(void)
{
    /* Mixed wave: scout from left + pair from center */
    enemySpawnFromLeft(ENEMY_TYPE_SCOUT, -20);
    enemySpawnWave(ENEMY_TYPE_SCOUT, 2, 100, -20, 50, 0);
}
static void z1_w14(void) { enemySpawn(ENEMY_TYPE_FIGHTER, 120, -32); }
/* Zone-end signal: slow scroll speed and restore full brightness */
static void z1_w15(void) { scrollTransitionSpeed(SCROLL_SPEED_SLOW, 60); setBrightness(15); }

/*===========================================================================*/
/* Zone 2: Asteroid Belt - Wave Trigger Callbacks                            */
/*===========================================================================*/
/* Zone 2 design: increased difficulty with fighters as the baseline enemy.
 *   - Waves 1-3:  Fighter formations, establishing the new baseline
 *   - Wave 4:     First Heavy (hover + strafe pattern, high HP)
 *   - Waves 5-9:  Mixed fighters + pincers, increasing pressure
 *   - Waves 10-11: Dense fighter waves + heavies for climax
 *   - Wave 12:    Scroll slowdown before zone boss */

static void z2_w01(void) { enemySpawnWave(ENEMY_TYPE_FIGHTER, 2, 80, -20, 80, 0); }
static void z2_w02(void)
{
    /* Pincer attack with fighters from both sides */
    enemySpawnFromLeft(ENEMY_TYPE_FIGHTER, -20);
    enemySpawnFromRight(ENEMY_TYPE_FIGHTER, -40);
}
static void z2_w03(void) { enemySpawnWave(ENEMY_TYPE_FIGHTER, 3, 40, -20, 60, 0); }
static void z2_w04(void) { enemySpawn(ENEMY_TYPE_HEAVY, 120, -32); }
static void z2_w05(void)
{
    /* Combined formation: fighter pair + side entry */
    enemySpawnWave(ENEMY_TYPE_FIGHTER, 2, 60, -20, 100, 0);
    enemySpawnFromLeft(ENEMY_TYPE_FIGHTER, -40);
}
/* #164: V-formation replaces flat fighter row */
static void z2_w06(void) { enemySpawnVFormation(ENEMY_TYPE_FIGHTER, 120, -20); }
static void z2_w07(void)
{
    /* Double heavy: two tanky enemies that strafe and fire aimed bullets */
    enemySpawn(ENEMY_TYPE_HEAVY, 60, -32);
    enemySpawn(ENEMY_TYPE_HEAVY, 180, -32);
}
static void z2_w08(void) { enemySpawnWave(ENEMY_TYPE_FIGHTER, 3, 50, -30, 60, -10); }
static void z2_w09(void)
{
    /* Side entry + center pair for cross-fire pressure */
    enemySpawnFromLeft(ENEMY_TYPE_FIGHTER, -20);
    enemySpawnWave(ENEMY_TYPE_FIGHTER, 2, 120, -20, 50, 0);
}
static void z2_w10(void) { enemySpawnWave(ENEMY_TYPE_FIGHTER, 5, 20, -20, 44, 0); }
static void z2_w11(void)
{
    /* Heavy + fighter support */
    enemySpawn(ENEMY_TYPE_HEAVY, 120, -32);
    enemySpawnFromRight(ENEMY_TYPE_FIGHTER, -20);
}
/* #186: Zone 2 asteroid hazards - scouts that pass through bullets */
static void z2_w_hazard(void)
{
    Enemy *e;
    u8 k;
    for (k = 0; k < 3; k++) {
        e = enemySpawn(ENEMY_TYPE_SCOUT, 40 + (k * 70), -20 - (k * 20));
        if (e) e->is_hazard = 1;
    }
}
/* Zone-end signal */
static void z2_w12(void) { scrollTransitionSpeed(SCROLL_SPEED_SLOW, 60); setBrightness(15); }

/*===========================================================================*/
/* Zone 3: Flagship Approach - Wave Trigger Callbacks                        */
/*===========================================================================*/
/* Zone 3 design: final zone with heavies and elites as standard enemies.
 * Significantly harder than previous zones:
 *   - Waves 1-2:  Heavies + elites introduced immediately
 *   - Waves 3-5:  Pincer attacks with elites (chase AI = very aggressive)
 *   - Waves 6-8:  Dense mixed formations, multi-type combinations
 *   - Wave 9:     Full elite formation as pre-boss challenge
 *   - Wave 10:    Scroll slowdown before final boss */

static void z3_w01(void)
{
    /* Double heavy opening: establishes the zone's difficulty level */
    enemySpawn(ENEMY_TYPE_HEAVY, 80, -32);
    enemySpawn(ENEMY_TYPE_HEAVY, 160, -32);
}
static void z3_w02(void) { enemySpawnWave(ENEMY_TYPE_ELITE, 2, 60, -20, 120, 0); }
static void z3_w03(void)
{
    /* Elite pincer: player-tracking enemies from both sides */
    enemySpawnFromLeft(ENEMY_TYPE_ELITE, -20);
    enemySpawnFromRight(ENEMY_TYPE_ELITE, -40);
}
static void z3_w04(void) { enemySpawnWave(ENEMY_TYPE_HEAVY, 3, 40, -20, 70, 0); }
static void z3_w05(void)
{
    /* Elite center + heavy flankers */
    enemySpawn(ENEMY_TYPE_ELITE, 120, -32);
    enemySpawnWave(ENEMY_TYPE_HEAVY, 2, 40, -20, 140, 0);
}
/* #164: V-formation replaces diagonal elite row */
static void z3_w06(void) { enemySpawnVFormation(ENEMY_TYPE_ELITE, 120, -20); }
static void z3_w07(void)
{
    /* Triple threat: heavies from both sides + elite center */
    enemySpawnFromLeft(ENEMY_TYPE_HEAVY, -20);
    enemySpawnFromRight(ENEMY_TYPE_HEAVY, -20);
    enemySpawn(ENEMY_TYPE_ELITE, 120, -32);
}
static void z3_w08(void)
{
    /* Mixed swarm: elite pair + wide-spread heavy pair */
    enemySpawnWave(ENEMY_TYPE_ELITE, 2, 80, -20, 80, 0);
    enemySpawnWave(ENEMY_TYPE_HEAVY, 2, 40, -40, 160, 0);
}
static void z3_w09(void) { enemySpawnWave(ENEMY_TYPE_ELITE, 4, 30, -20, 50, 0); }
/* #193: Zone 3 elite swarm wave - diagonal cascade from both sides */
static void z3_w_elite_swarm(void)
{
    enemySpawnFromLeft(ENEMY_TYPE_ELITE, -10);
    enemySpawnFromRight(ENEMY_TYPE_ELITE, -20);
    enemySpawnFromLeft(ENEMY_TYPE_ELITE, -30);
    enemySpawnFromRight(ENEMY_TYPE_ELITE, -40);
    enemySpawnFromLeft(ENEMY_TYPE_ELITE, -50);
    enemySpawnFromRight(ENEMY_TYPE_ELITE, -60);
}
/* Zone-end signal: slow down before final boss */
static void z3_w10(void) { scrollTransitionSpeed(SCROLL_SPEED_SLOW, 60); setBrightness(15); }

/* #157: Bonus score zone trigger callback.
 * Activates a 120-frame period where all scores are doubled. */
static void bonusZoneTrigger(void)
{
    g_score_bonus_timer = 120;
    soundPlaySFX(SFX_LEVEL_UP);
    setBrightness(15);
}

/*===========================================================================*/
/* Zone Trigger Registration                                                 */
/*===========================================================================*/

/*
 * enemySetupZoneTriggers
 * ----------------------
 * Register all enemy wave spawn callbacks for the given zone.
 * Each trigger fires once when the accumulated scroll distance (in pixels)
 * crosses the specified threshold.
 *
 * Trigger spacing design:
 *   - ~300px apart initially (at SCROLL_SPEED_NORMAL = 0.5 px/frame, that's
 *     ~600 frames = ~10 seconds between waves)
 *   - Spacing decreases in later waves for increased pressure
 *   - Final trigger at ~4700px slows scroll for boss approach
 *   - Boss trigger at 4800px is registered separately by the boss system
 *
 * Parameters:
 *   zoneId - ZONE_DEBRIS(0), ZONE_ASTEROID(1), or ZONE_FLAGSHIP(2)
 */
void enemySetupZoneTriggers(u8 zoneId)
{
    /* Clear any triggers from the previous zone */
    scrollClearTriggers();

    /* #178: Reset wave counter for adaptive fire rate */
    wave_count = 0;
    wave_last_frame = 0xFFFF;

    /* #157: Register bonus score zone triggers at fixed distances */
    scrollAddTrigger(1000, bonusZoneTrigger);
    scrollAddTrigger(2500, bonusZoneTrigger);
    scrollAddTrigger(4000, bonusZoneTrigger);

    switch (zoneId) {
        case ZONE_DEBRIS:
            /* Zone 1: 15 waves, gradual introduction of enemies.
             * Scouts only for first 5 waves, then mixed with fighters. */
            scrollAddTrigger(300,  z1_w01);
            scrollAddTrigger(600,  z1_w02);
            scrollAddTrigger(900,  z1_w03);
            scrollAddTrigger(1100, z1_w04);
            scrollAddTrigger(1400, z1_w05);
            scrollAddTrigger(1700, z1_w06);   /* First fighter appears */
            scrollAddTrigger(2000, z1_w07);
            scrollAddTrigger(2300, z1_w08);   /* First pincer attack */
            scrollAddTrigger(2700, z1_w09);
            scrollAddTrigger(3100, z1_w10);
            scrollAddTrigger(3500, z1_w11);   /* Double fighter */
            scrollAddTrigger(3900, z1_w12);   /* 5 scouts - largest wave */
            scrollAddTrigger(4200, z1_w13);
            scrollAddTrigger(4500, z1_w14);
            scrollAddTrigger(4700, z1_w15);   /* Scroll slowdown for boss */
            break;

        case ZONE_ASTEROID:
            /* Zone 2: 12 waves, fighters as baseline, heavies introduced.
             * Tighter spacing = more pressure than Zone 1. */
            scrollAddTrigger(300,  z2_w01);
            scrollAddTrigger(600,  z2_w02);   /* Fighter pincer */
            scrollAddTrigger(900,  z2_w03);
            scrollAddTrigger(1200, z2_w04);   /* First heavy */
            scrollAddTrigger(1600, z2_w05);
            scrollAddTrigger(2000, z2_w06);
            scrollAddTrigger(2400, z2_w07);   /* Double heavy */
            scrollAddTrigger(2800, z2_w08);
            scrollAddTrigger(3200, z2_w09);
            scrollAddTrigger(3600, z2_w10);   /* 5 fighters */
            scrollAddTrigger(2000, z2_w_hazard);  /* #186: Asteroid hazards */
            scrollAddTrigger(3500, z2_w_hazard);  /* #186: More hazards */
            scrollAddTrigger(4200, z2_w11);
            scrollAddTrigger(4700, z2_w12);   /* Scroll slowdown for boss */
            break;

        case ZONE_FLAGSHIP:
            /* Zone 3: 10 waves, heavies and elites, aggressive endgame.
             * Fewer waves but each is more dangerous. */
            scrollAddTrigger(300,  z3_w01);   /* Double heavy opening */
            scrollAddTrigger(700,  z3_w02);   /* Elite pair */
            scrollAddTrigger(1100, z3_w03);   /* Elite pincer */
            scrollAddTrigger(1500, z3_w04);
            scrollAddTrigger(1900, z3_w05);
            scrollAddTrigger(2300, z3_w06);
            scrollAddTrigger(2800, z3_w07);   /* Triple threat */
            scrollAddTrigger(3300, z3_w08);   /* Mixed swarm */
            scrollAddTrigger(3800, z3_w09);   /* 4 elites - hardest wave */
            scrollAddTrigger(4200, z3_w_elite_swarm); /* #193: Elite swarm */
            scrollAddTrigger(4700, z3_w10);   /* Scroll slowdown for final boss */
            break;
    }
}
