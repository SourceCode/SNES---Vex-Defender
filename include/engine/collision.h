/*==============================================================================
 * Collision Detection System
 *
 * AABB (Axis-Aligned Bounding Box) checks between entity pools.
 * Three collision passes per frame:
 *   1. Player bullets (pool indices 0-15) vs enemies (pool 0-7)
 *   2. Enemy bullets (pool indices 16-23) vs player
 *   3. Player body vs enemies (contact damage / battle trigger)
 *
 * All collision uses integer-only AABB overlap tests (additions and
 * comparisons only, no multiply/divide). The 65816 has no hardware
 * multiply for 16-bit values, so this is the most efficient approach.
 *
 * Hitboxes are intentionally smaller than sprite visuals for fair gameplay:
 *   Player 32x32 sprite -> 16x16 hitbox (cockpit area, offset 8,8)
 *   Enemy  32x32 sprite -> 24x24 hitbox (body area, offset 4,4)
 *   Bullet 16x16 sprite ->  8x8  hitbox (projectile core, offset 4,4)
 *   Laser  16x16 sprite -> 12x12 hitbox (larger impact area, offset 2,2)
 *
 * Performance: Worst case is 16 bullets x 8 enemies + 8 enemy bullets +
 * 8 enemies = 144 AABB checks. Each check is ~8 integer ops, well within
 * the ~4000 available operations per VBlank frame budget.
 *
 * Call collisionCheckAll() once per frame after movement updates but
 * before rendering, so deactivated entities are not drawn.
 *============================================================================*/

#ifndef COLLISION_H
#define COLLISION_H

#include "game.h"

/*
 * Hitbox structure - defines a bounding box relative to an entity's position.
 *
 * The offsets allow the hitbox to be smaller than and centered differently
 * from the sprite's visual extent. For example, a 32x32 sprite with
 * x_off=8, y_off=8, width=16, height=16 has a centered 16x16 hitbox.
 *
 * Absolute hitbox edges are computed as:
 *   left   = entity.x + x_off
 *   right  = entity.x + x_off + width
 *   top    = entity.y + y_off
 *   bottom = entity.y + y_off + height
 */
typedef struct {
    s8 x_off;       /* Horizontal offset from sprite top-left to hitbox left edge */
    s8 y_off;       /* Vertical offset from sprite top-left to hitbox top edge */
    u8 width;       /* Hitbox width in pixels */
    u8 height;      /* Hitbox height in pixels */
} Hitbox;

/*
 * collisionInit - Initialize the collision system.
 * Zeroes the score, screen shake timer, and combo state.
 */
void collisionInit(void);

/*
 * collisionCheckAll - Run all three collision passes for one frame.
 *
 * Internally:
 *   1. Decays the combo timer; resets combo count when timer expires
 *   2. If enemies are active: checks player bullets vs enemies, then
 *      player body vs enemies (skipped when no enemies for performance)
 *   3. Always checks enemy bullets vs player (bullets may outlive enemies)
 *
 * Side effects on collision:
 *   - Bullet is deactivated (consumed)
 *   - Enemy takes damage or is destroyed (score awarded with combo multiplier)
 *   - Player gets invincibility frames and screen shake on hit
 *   - Strong enemies trigger turn-based battle instead of taking damage
 *   - Sound effects are played (SFX_HIT, SFX_EXPLOSION)
 */
void collisionCheckAll(void);

/*
 * collisionCheckAABB - Test AABB overlap between two positioned hitboxes.
 *
 * Uses the separating axis theorem: no overlap if any gap exists on
 * either axis. This requires only 4 comparisons (after edge computation).
 *
 * Parameters:
 *   ax, ay - Position of entity A (top-left pixel)
 *   ha     - Hitbox definition for entity A
 *   bx, by - Position of entity B (top-left pixel)
 *   hb     - Hitbox definition for entity B
 *
 * Returns: 1 if the hitboxes overlap, 0 if they do not.
 */
u8 collisionCheckAABB(s16 ax, s16 ay, const Hitbox *ha,
                      s16 bx, s16 by, const Hitbox *hb);

/*
 * Player score - accumulated points from destroying enemies.
 * Score increases are scaled by the combo multiplier (1x to 4x).
 */
extern u16 g_score;

/*
 * Screen shake counter - frames remaining of camera shake effect.
 * Set to a positive value when the player takes a hit.
 * Decremented elsewhere (typically in the render/camera code).
 */
extern u8 g_screen_shake;

/*
 * Combo scoring state:
 *   g_combo_count - Number of consecutive enemy kills within the timer window.
 *                   Multiplier is min(combo_count, 4) applied to score.
 *   g_combo_timer - Frames remaining in the combo window. Starts at 60
 *                   (1 second) on each kill. When it reaches 0, combo resets.
 */
extern u8 g_combo_count;
extern u8 g_combo_timer;

/* Combo multiplier derived from combo count (1-4), used for decay window (#141) */
extern u8 g_combo_multiplier;

/* Kill streak: consecutive kills without taking player damage (#143) */
extern u8 g_kill_streak;

/* Bonus score zone timer: frames remaining of 2x score period (#157) */
extern u8 g_score_bonus_timer;

/* Weapon switch combo tracking (#167) */
extern u8 g_weapon_combo_buf[3];
extern u8 g_weapon_combo_idx;

/* Combo multiplier display timer: frames remaining of HUD display (#183) */
extern u8 g_combo_display_timer;

/* Wave clear tracking (#197) */
extern u8  g_wave_enemy_count;   /* Enemies spawned in current wave */
extern u8  g_wave_kill_count;    /* Kills in current wave */
extern u16 g_wave_timer;         /* Frames remaining in wave window */

#endif /* COLLISION_H */
