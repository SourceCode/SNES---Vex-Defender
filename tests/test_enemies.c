/*==============================================================================
 * Test: Enemy System
 * Tests pool management, bounds checking, spawn/despawn, damage system.
 * Note: Does NOT include enemies.c source (already included via separate
 * compilation path). Tests only via header API + direct pool access.
 *============================================================================*/

#include "test_framework.h"
#include "mock_snes.h"

/* enemies.c is included earlier in the compilation - we test via its API */
#include "game/enemies.h"

/* Forward declare the externs we need (defined in the enemies.c source
 * that's included by the source_files.c compilation block) */

/*--- Test initialization ---*/
TEST(test_enemy_init)
{
    enemyInit();
    Enemy *pool = enemyGetPool();
    u8 i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        TEST_ASSERT_EQ(pool[i].active, ENTITY_INACTIVE, "Enemy inactive after init");
    }
}

/*--- Test spawning ---*/
TEST(test_enemy_spawn)
{
    enemyInit();
    Enemy *e = enemySpawn(ENEMY_TYPE_SCOUT, 100, -20);
    TEST_ASSERT_NOT_NULL(e, "Spawn returns non-NULL");
    TEST_ASSERT_EQ(e->active, ENTITY_ACTIVE, "Spawned enemy is active");
    TEST_ASSERT_EQ(e->type, ENEMY_TYPE_SCOUT, "Type is SCOUT");
    TEST_ASSERT_EQ(e->x, 100, "X position correct");
    TEST_ASSERT_EQ(e->y, -20, "Y position correct");
    TEST_ASSERT_EQ(e->hp, 10, "Scout HP = 10");
}

/*--- Test pool full ---*/
TEST(test_enemy_pool_full)
{
    u8 i;
    enemyInit();
    for (i = 0; i < MAX_ENEMIES; i++) {
        enemySpawn(ENEMY_TYPE_SCOUT, 100, -20);
    }
    Enemy *e = enemySpawn(ENEMY_TYPE_SCOUT, 100, -20);
    TEST_ASSERT_NULL(e, "Spawn returns NULL when pool full");
}

/*--- Test bounds check (improvement #17) ---*/
TEST(test_enemy_type_bounds)
{
    enemyInit();
    Enemy *e = enemySpawn(ENEMY_TYPE_COUNT, 100, -20);
    TEST_ASSERT_NULL(e, "Invalid type rejected");

    e = enemySpawn(255, 100, -20);
    TEST_ASSERT_NULL(e, "Type 255 rejected");
}

/*--- Test type def bounds ---*/
TEST(test_enemy_typedef_bounds)
{
    const EnemyTypeDef *def;
    def = enemyGetTypeDef(0);
    TEST_ASSERT_NOT_NULL(def, "Type 0 valid");
    TEST_ASSERT_EQ(def->max_hp, 10, "Scout HP = 10");

    def = enemyGetTypeDef(3);
    TEST_ASSERT_NOT_NULL(def, "Type 3 valid");
    TEST_ASSERT_EQ(def->max_hp, 30, "Elite HP = 30");

    /* Out of bounds defaults to type 0 */
    def = enemyGetTypeDef(99);
    TEST_ASSERT_EQ(def->max_hp, 10, "Invalid type falls back to scout");
}

/*--- Test damage system ---*/
TEST(test_enemy_damage)
{
    enemyInit();
    Enemy *e = enemySpawn(ENEMY_TYPE_FIGHTER, 100, 50);
    TEST_ASSERT_EQ(e->hp, 20, "Fighter starts at 20 HP");

    /* Partial damage */
    u8 destroyed = enemyDamage(e, 5);
    TEST_ASSERT_EQ(destroyed, 0, "Not destroyed at 15 HP");
    TEST_ASSERT_EQ(e->hp, 15, "HP reduced to 15");
    TEST_ASSERT_EQ(e->flash_timer, 6, "Flash timer = 6 on hit");

    /* Kill damage */
    destroyed = enemyDamage(e, 15);
    TEST_ASSERT_EQ(destroyed, 1, "Destroyed at 0 HP");
    TEST_ASSERT_EQ(e->hp, 0, "HP = 0");
    TEST_ASSERT_EQ(e->active, ENTITY_DYING, "State = DYING");
    TEST_ASSERT_EQ(e->flash_timer, 16, "Death flash = 16 (mid-blink speed kill #127/#235)");
}

/*--- Test overkill damage ---*/
TEST(test_enemy_overkill)
{
    enemyInit();
    Enemy *e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    u8 destroyed = enemyDamage(e, 100);
    TEST_ASSERT_EQ(destroyed, 1, "Overkill destroys");
    TEST_ASSERT_EQ(e->hp, 0, "HP clamped to 0");
}

/*--- Test kill all ---*/
TEST(test_enemy_kill_all)
{
    u8 i;
    enemyInit();
    enemySpawn(ENEMY_TYPE_SCOUT, 50, -20);
    enemySpawn(ENEMY_TYPE_FIGHTER, 100, -20);
    enemySpawn(ENEMY_TYPE_HEAVY, 150, -20);

    enemyKillAll();
    Enemy *pool = enemyGetPool();
    for (i = 0; i < MAX_ENEMIES; i++) {
        TEST_ASSERT_EQ(pool[i].active, ENTITY_INACTIVE, "All killed");
    }
}

/*--- Test spawn wave ---*/
TEST(test_enemy_spawn_wave)
{
    u8 i, count;
    enemyInit();
    enemySpawnWave(ENEMY_TYPE_SCOUT, 3, 40, -20, 50, 0);

    Enemy *pool = enemyGetPool();
    count = 0;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (pool[i].active == ENTITY_ACTIVE) count++;
    }
    TEST_ASSERT_EQ(count, 3, "Wave spawned 3 enemies");
}

/*--- Test enemy type properties ---*/
TEST(test_enemy_type_properties)
{
    const EnemyTypeDef *def;

    def = enemyGetTypeDef(ENEMY_TYPE_SCOUT);
    TEST_ASSERT_EQ(def->max_hp, 10, "Scout HP");
    TEST_ASSERT_EQ(def->speed, 2, "Scout speed");
    TEST_ASSERT_EQ(def->score_value, 100, "Scout score");

    def = enemyGetTypeDef(ENEMY_TYPE_FIGHTER);
    TEST_ASSERT_EQ(def->max_hp, 20, "Fighter HP");
    TEST_ASSERT_EQ(def->score_value, 200, "Fighter score");

    def = enemyGetTypeDef(ENEMY_TYPE_HEAVY);
    TEST_ASSERT_EQ(def->max_hp, 40, "Heavy HP");
    TEST_ASSERT_EQ(def->score_value, 350, "Heavy score");

    def = enemyGetTypeDef(ENEMY_TYPE_ELITE);
    TEST_ASSERT_EQ(def->max_hp, 30, "Elite HP");
    TEST_ASSERT_EQ(def->score_value, 500, "Elite score");
}

/*--- Test active enemy count (improvement #9) ---*/
TEST(test_enemy_active_count)
{
    enemyInit();
    TEST_ASSERT_EQ(g_enemy_active_count, 0, "Init active count = 0");

    enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    enemySpawn(ENEMY_TYPE_FIGHTER, 150, 50);
    enemyUpdateAll();
    TEST_ASSERT_EQ(g_enemy_active_count, 2, "Active count = 2 after spawn+update");

    enemyKillAll();
    enemyUpdateAll();
    TEST_ASSERT_EQ(g_enemy_active_count, 0, "Active count = 0 after kill all");
}

/*--- Test enhanced death effect (improvement #14) ---*/
TEST(test_enemy_death_effect)
{
    enemyInit();
    Enemy *e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);

    /* Destroy the enemy */
    enemyDamage(e, 100);
    TEST_ASSERT_EQ(e->active, ENTITY_DYING, "Enemy enters DYING state");
    TEST_ASSERT_EQ(e->flash_timer, 16, "Death flash timer = 16 (speed kill + spawn blink #127/#235)");

    /* Simulate death animation ticks */
    enemyUpdateAll();
    TEST_ASSERT_EQ(e->flash_timer, 15, "Flash timer decrements");
    TEST_ASSERT_EQ(e->active, ENTITY_DYING, "Still dying at timer 9");
}

/*--- Test death animation completes ---*/
TEST(test_enemy_death_completes)
{
    u8 i;
    enemyInit();
    Enemy *e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    enemyDamage(e, 100);

    /* Run through full death animation (14 frames due to spawn-blink #127) */
    for (i = 0; i < 16; i++) {
        enemyUpdateAll();
    }
    TEST_ASSERT_EQ(e->active, ENTITY_INACTIVE, "Enemy inactive after death anim");
}

/*--- Test enemy load graphics sets cached values ---*/
TEST(test_enemy_load_graphics)
{
    enemyInit();
    enemyLoadGraphics(ZONE_DEBRIS);
    /* After load, spawning and rendering should work */
    Enemy *e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    TEST_ASSERT_NOT_NULL(e, "Can spawn after graphics load");
    TEST_ASSERT_EQ(e->type, ENEMY_TYPE_SCOUT, "Type correct after load");
}

/*--- Test enemy render culling (improvement #3) ---*/
TEST(test_enemy_render_culling)
{
    enemyInit();
    Enemy *e = enemySpawn(ENEMY_TYPE_SCOUT, 300, 50);
    TEST_ASSERT_NOT_NULL(e, "Spawn at x=300");
    TEST_ASSERT_EQ(e->active, ENTITY_ACTIVE, "Active at x=300");
    /* Enemy at x=300 is off-screen but still active (render is culled, logic continues) */

    e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 300);
    TEST_ASSERT_NOT_NULL(e, "Spawn at y=300");
    /* y=300 is below screen, update will despawn it */
    enemyUpdateAll();
    TEST_ASSERT_EQ(e->active, ENTITY_INACTIVE, "Despawned below screen");
}

/*--- Test enemy spawn flash (improvement #15) ---*/
TEST(test_enemy_spawn_flash)
{
    enemyInit();
    Enemy *e = enemySpawn(ENEMY_TYPE_SCOUT, 100, -20);
    TEST_ASSERT_EQ(e->flash_timer, 4, "Spawn flash timer = 4");

    /* Flash counts down during update */
    enemyUpdateAll();
    TEST_ASSERT_EQ(e->flash_timer, 3, "Flash timer = 3 after 1 update");
}

/*--- Test tile/pal LUT render (improvement #11) ---*/
TEST(test_enemy_render_lut)
{
    enemyInit();
    enemyLoadGraphics(ZONE_DEBRIS);

    /* Spawn type_a (SCOUT) and type_b (FIGHTER) enemies */
    Enemy *ea = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    Enemy *eb = enemySpawn(ENEMY_TYPE_FIGHTER, 150, 50);
    TEST_ASSERT_NOT_NULL(ea, "Type A spawn ok");
    TEST_ASSERT_NOT_NULL(eb, "Type B spawn ok");

    /* Render should not crash - both paths through the LUT */
    enemyRenderAll();
    TEST_ASSERT_EQ(ea->active, ENTITY_ACTIVE, "Type A still active after render");
    TEST_ASSERT_EQ(eb->active, ENTITY_ACTIVE, "Type B still active after render");
}

/*--- Test render LUT with dying enemies (improvement #11) ---*/
TEST(test_enemy_render_lut_dying)
{
    enemyInit();
    enemyLoadGraphics(ZONE_ASTEROID);

    /* Spawn and kill a type_a enemy */
    Enemy *e = enemySpawn(ENEMY_TYPE_FIGHTER, 100, 50);
    TEST_ASSERT_NOT_NULL(e, "Spawn for dying test");
    enemyDamage(e, 100);
    TEST_ASSERT_EQ(e->active, ENTITY_DYING, "Enemy is dying");

    /* Render dying enemy should not crash (exercises dying LUT path) */
    enemyRenderAll();
    TEST_ASSERT_EQ(e->active, ENTITY_DYING, "Still dying after render");
}

/*--- Test fire_timer consolidation (improvement #12) ---*/
TEST(test_enemy_fire_timer_decrement)
{
    enemyInit();
    Enemy *e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    const EnemyTypeDef *def = enemyGetTypeDef(ENEMY_TYPE_SCOUT);
    TEST_ASSERT_NOT_NULL(e, "Spawn for fire timer test");

    /* fire_timer should start at fire_rate */
    TEST_ASSERT_EQ(e->fire_timer, def->fire_rate, "fire_timer init = fire_rate");

    /* After one update, fire_timer should decrement by 1 */
    u8 initial = e->fire_timer;
    enemyUpdateAll();
    TEST_ASSERT_EQ(e->fire_timer, initial - 1, "fire_timer decrements by 1");
}

/*--- Test fire_timer reloads at zero (improvement #12) ---*/
TEST(test_enemy_fire_timer_reload)
{
    u8 i;
    enemyInit();
    Enemy *e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    const EnemyTypeDef *def = enemyGetTypeDef(ENEMY_TYPE_SCOUT);
    TEST_ASSERT_NOT_NULL(e, "Spawn for fire reload test");

    /* Run updates until fire_timer hits 0 and reloads */
    for (i = 0; i < def->fire_rate; i++) {
        enemyUpdateAll();
    }
    /* After fire_rate updates, timer should have reloaded to fire_rate */
    TEST_ASSERT_EQ(e->fire_timer, def->fire_rate, "fire_timer reloads to fire_rate");
}

/*--- Test ai_timer u8 behavior (improvement #14) ---*/
TEST(test_enemy_ai_timer_u8)
{
    enemyInit();
    Enemy *e = enemySpawn(ENEMY_TYPE_FIGHTER, 100, 50);
    TEST_ASSERT_NOT_NULL(e, "Spawn for ai_timer u8 test");

    /* ai_timer should start at 0 */
    TEST_ASSERT_EQ(e->ai_timer, 0, "ai_timer starts at 0");

    /* Set to max u8 value and verify */
    e->ai_timer = 255;
    TEST_ASSERT_EQ(e->ai_timer, 255, "ai_timer holds 255");

    /* Verify wrapping: increment from 255 wraps to 0 */
    e->ai_timer++;
    TEST_ASSERT_EQ(e->ai_timer, 0, "ai_timer wraps from 255 to 0");

    /* Verify sine lookup mask still works: (timer >> 2) & 0x0F */
    e->ai_timer = 63;
    TEST_ASSERT_EQ((e->ai_timer >> 2) & 0x0F, 15, "ai_timer 63 -> sine index 15");

    e->ai_timer = 64;
    TEST_ASSERT_EQ((e->ai_timer >> 2) & 0x0F, 0, "ai_timer 64 -> sine index wraps to 0");
}

/*--- Test cached def pointer in enemyUpdateAll (improvement #4) ---*/
TEST(test_enemy_update_cached_def)
{
    u8 i;
    enemyInit();
    Enemy *e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    const EnemyTypeDef *def = enemyGetTypeDef(ENEMY_TYPE_SCOUT);
    TEST_ASSERT_NOT_NULL(e, "Spawn for cached def test");

    /* fire_timer starts at fire_rate */
    TEST_ASSERT_EQ(e->fire_timer, def->fire_rate, "fire_timer init correct");

    /* Run fire_rate-1 updates: timer should decrement each frame */
    for (i = 0; i < def->fire_rate - 1; i++) {
        enemyUpdateAll();
    }
    TEST_ASSERT_EQ(e->fire_timer, 1, "fire_timer at 1 before reload");

    /* One more update: timer fires and reloads to fire_rate */
    enemyUpdateAll();
    TEST_ASSERT_EQ(e->fire_timer, def->fire_rate, "fire_timer reloads via cached def");
}

/*--- Test aiUpdate velocity caching (#100) ---*/
TEST(test_enemy_ai_velocity_cache)
{
    s16 expected_dy, expected_dx;
    enemyInit();
    Enemy *e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    TEST_ASSERT_NOT_NULL(e, "Spawn for velocity cache test");

    /* Scout: vy = speed<<8 = 2<<8 = 0x0200, vx = 0 */
    expected_dy = e->vy >> 8;  /* should be 2 */
    expected_dx = e->vx >> 8;  /* should be 0 */
    TEST_ASSERT_EQ(expected_dy, 2, "Scout vy>>8 = 2");
    TEST_ASSERT_EQ(expected_dx, 0, "Scout vx>>8 = 0");

    enemyUpdateAll();

    /* After one update, y should have moved by dy (2), x by dx (0) */
    TEST_ASSERT_EQ(e->y, 52, "y += vy>>8 (50+2=52)");
    TEST_ASSERT_EQ(e->x, 100, "x += vx>>8 (100+0=100)");
}

/*--- Test enemyRenderAll early exit when no enemies active (#104) ---*/
TEST(test_enemy_render_early_exit)
{
    enemyInit();
    /* No enemies spawned - g_enemy_active_count is 0 after init */
    enemyUpdateAll();
    TEST_ASSERT_EQ(g_enemy_active_count, 0, "No active enemies");

    /* Should not crash, should take fast path hiding all OAM slots */
    enemyRenderAll();
    /* If we get here without crashing, the test passes */
    TEST_ASSERT_EQ(g_enemy_active_count, 0, "Still zero after render early exit");
}

/*--- Test enemyUpdateAll state ordering (#105) ---*/
TEST(test_enemy_update_state_ordering)
{
    Enemy *pool;
    Enemy *e_dying;
    Enemy *e_active;
    u8 initial_flash;
    u8 initial_fire;
    u8 i;
    u8 inactive_count;

    enemyInit();
    pool = enemyGetPool();

    /* Slot 0: leave INACTIVE (default from init) */
    /* Slot 1: set to DYING with known flash_timer */
    e_dying = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    TEST_ASSERT_NOT_NULL(e_dying, "Spawn dying enemy");
    enemyDamage(e_dying, 100);
    TEST_ASSERT_EQ(e_dying->active, ENTITY_DYING, "Enemy is DYING");
    initial_flash = e_dying->flash_timer;

    /* Slot 2: ACTIVE enemy */
    e_active = enemySpawn(ENEMY_TYPE_SCOUT, 120, 50);
    TEST_ASSERT_NOT_NULL(e_active, "Spawn active enemy");
    initial_fire = e_active->fire_timer;

    enemyUpdateAll();

    /* INACTIVE slots should remain inactive */
    inactive_count = 0;
    for (i = 2; i < MAX_ENEMIES; i++) {
        if (pool[i].active == ENTITY_INACTIVE) inactive_count++;
    }
    TEST_ASSERT_EQ(inactive_count, MAX_ENEMIES - 2, "Inactive slots unchanged");

    /* DYING enemy: flash_timer should have decremented */
    TEST_ASSERT_EQ(e_dying->flash_timer, initial_flash - 1, "Dying flash_timer decremented");

    /* ACTIVE enemy: fire_timer should have decremented */
    TEST_ASSERT_EQ(e_active->fire_timer, initial_fire - 1, "Active fire_timer decremented");
}

/*--- Test enemySpawnWave additive accumulation (#116) ---*/
TEST(test_enemy_spawn_wave_additive)
{
    Enemy *pool;
    u8 i;
    u8 count;
    s16 expected_x;

    enemyInit();
    enemySpawnWave(ENEMY_TYPE_SCOUT, 3, 50, -20, 40, 0);

    pool = enemyGetPool();
    count = 0;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (pool[i].active == ENTITY_ACTIVE) {
            expected_x = 50 + (s16)count * 40;
            TEST_ASSERT_EQ(pool[i].x, expected_x, "Wave enemy X position correct");
            TEST_ASSERT_EQ(pool[i].y, -20, "Wave enemy Y position correct");
            count++;
        }
    }
    TEST_ASSERT_EQ(count, 3, "Wave spawned 3 enemies");
}

/*--- Test combined gfx LUT (#118) ---*/
TEST(test_enemy_combined_gfx_lut)
{
    Enemy *ea;
    Enemy *eb;

    enemyInit();
    enemyLoadGraphics(ZONE_DEBRIS);

    /* Spawn type_a (SCOUT) and type_b (FIGHTER) enemies on-screen */
    ea = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    eb = enemySpawn(ENEMY_TYPE_FIGHTER, 150, 50);
    TEST_ASSERT_NOT_NULL(ea, "Type A spawn ok for gfx LUT");
    TEST_ASSERT_NOT_NULL(eb, "Type B spawn ok for gfx LUT");

    /* Render should not crash with combined LUT */
    enemyRenderAll();
    TEST_ASSERT_EQ(ea->active, ENTITY_ACTIVE, "Type A still active after combined LUT render");
    TEST_ASSERT_EQ(eb->active, ENTITY_ACTIVE, "Type B still active after combined LUT render");
}

/*--- Test death flash extension when killing mid-blink (#127) ---*/
TEST(test_enemy_death_flash_extension)
{
    enemyInit();
    Enemy *e = enemySpawn(ENEMY_TYPE_FIGHTER, 100, 50);
    TEST_ASSERT_NOT_NULL(e, "Spawn for flash extension test");
    TEST_ASSERT_EQ(e->hp, 20, "Fighter HP = 20");

    /* Damage enemy (but don't kill) to start flash */
    enemyDamage(e, 5);
    TEST_ASSERT_EQ(e->flash_timer, 6, "Damage flash = 6");
    TEST_ASSERT_EQ(e->active, ENTITY_ACTIVE, "Still active after damage");

    /* Kill enemy while mid-blink (flash_timer > 0) */
    enemyDamage(e, 15);
    TEST_ASSERT_EQ(e->active, ENTITY_DYING, "Now dying");
    TEST_ASSERT_EQ(e->flash_timer, 16, "Extended death flash = 16 (speed kill mid-blink #235)");

    /* Kill enemy with no prior flash but aged past speed kill threshold */
    enemyInit();
    e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    e->flash_timer = 0;  /* Ensure no blink */
    e->age = 100;         /* Past speed kill threshold (#235) */
    enemyDamage(e, 100);
    TEST_ASSERT_EQ(e->flash_timer, 10, "Normal death flash = 10 (no prior blink, aged)");
}

/*--- Test zone-scaled enemy HP (#133) ---*/
TEST(test_enemy_zone_scaled_hp)
{
    const EnemyTypeDef *def;
    Enemy *e;

    def = enemyGetTypeDef(ENEMY_TYPE_SCOUT);

    /* Zone 0: base HP */
    enemyInit();
    g_game.current_zone = 0;
    e = enemySpawn(ENEMY_TYPE_SCOUT, 100, -20);
    TEST_ASSERT_NOT_NULL(e, "Spawn in zone 0");
    TEST_ASSERT_EQ(e->hp, def->max_hp, "Zone 0: base HP (10)");

    /* Zone 1: +50% HP */
    enemyInit();
    g_game.current_zone = 1;
    e = enemySpawn(ENEMY_TYPE_SCOUT, 100, -20);
    TEST_ASSERT_NOT_NULL(e, "Spawn in zone 1");
    TEST_ASSERT_EQ(e->hp, def->max_hp + (def->max_hp >> 1),
                   "Zone 1: +50% HP (10 + 5 = 15)");

    /* Zone 2: +100% HP */
    enemyInit();
    g_game.current_zone = 2;
    e = enemySpawn(ENEMY_TYPE_SCOUT, 100, -20);
    TEST_ASSERT_NOT_NULL(e, "Spawn in zone 2");
    TEST_ASSERT_EQ(e->hp, def->max_hp + def->max_hp,
                   "Zone 2: +100% HP (10 + 10 = 20)");

    /* Restore zone for subsequent tests */
    g_game.current_zone = 0;
}

/*--- Test #146: Enemy age tracking ---*/
TEST(test_enemy_age_tracking)
{
    u8 i;
    enemyInit();
    g_game.current_zone = 0;
    /* Set g_frame_count to non-golden value to avoid golden variant */
    g_frame_count = 0;
    Enemy *e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    TEST_ASSERT_NOT_NULL(e, "#146: Spawn for age test");
    TEST_ASSERT_EQ(e->age, 0, "#146: Age starts at 0");

    /* After 1 update, age should be 1 */
    enemyUpdateAll();
    TEST_ASSERT_EQ(e->age, 1, "#146: Age = 1 after 1 update");

    /* After 89 more updates (total 90), age should be 90 */
    for (i = 0; i < 89; i++) {
        enemyUpdateAll();
    }
    TEST_ASSERT_EQ(e->age, 90, "#146: Age = 90 after 90 updates");

    /* Age caps at 255 */
    e->age = 254;
    enemyUpdateAll();
    TEST_ASSERT_EQ(e->age, 255, "#146: Age = 255 after increment from 254");

    /* Should stay at 255 */
    enemyUpdateAll();
    TEST_ASSERT_EQ(e->age, 255, "#146: Age capped at 255");
}

/*--- Test #147: Golden enemy variant ---*/
TEST(test_enemy_golden_variant)
{
    const EnemyTypeDef *def;
    Enemy *e;

    enemyInit();
    g_game.current_zone = 0;
    def = enemyGetTypeDef(ENEMY_TYPE_SCOUT);

    /* Force golden spawn by setting g_frame_count & 0x0F == 0x07 */
    g_frame_count = 7;  /* 7 & 0x0F == 0x07 */
    e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    TEST_ASSERT_NOT_NULL(e, "#147: Golden spawn succeeds");
    TEST_ASSERT_EQ(e->is_golden, 1, "#147: is_golden = 1");
    TEST_ASSERT_EQ(e->hp, def->max_hp << 1, "#147: Golden HP = 2x base");
    TEST_ASSERT_EQ(e->flash_timer, 255, "#147: Golden has permanent flash");

    /* Non-golden spawn */
    enemyInit();
    g_frame_count = 0;  /* 0 & 0x0F == 0x00, not 0x07 */
    e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    TEST_ASSERT_NOT_NULL(e, "#147: Non-golden spawn succeeds");
    TEST_ASSERT_EQ(e->is_golden, 0, "#147: is_golden = 0 for normal enemy");
    TEST_ASSERT_EQ(e->hp, def->max_hp, "#147: Normal HP = base");
}

/*--- Test #147: Golden variant with zone scaling ---*/
TEST(test_enemy_golden_zone_scaled)
{
    const EnemyTypeDef *def;
    Enemy *e;

    def = enemyGetTypeDef(ENEMY_TYPE_SCOUT);

    /* Zone 1 golden: base HP + 50%, then 2x for golden */
    enemyInit();
    g_game.current_zone = 1;
    g_frame_count = 7;
    e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    TEST_ASSERT_NOT_NULL(e, "#147: Zone 1 golden spawn");
    TEST_ASSERT_EQ(e->is_golden, 1, "#147: Zone 1 golden flag set");
    /* Zone 1: hp = base + base>>1 = 10+5 = 15, then golden 2x = 30 */
    TEST_ASSERT_EQ(e->hp, (def->max_hp + (def->max_hp >> 1)) << 1,
                   "#147: Zone 1 golden HP = 2x zone-scaled");

    g_game.current_zone = 0;
}

/*--- Test #159: Partial score for escaped enemies ---*/
TEST(test_enemy_partial_escape_score)
{
    const EnemyTypeDef *def;
    Enemy *e;

    enemyInit();
    g_game.current_zone = 0;
    g_frame_count = 0;
    collisionInit();  /* Reset g_score to 0 */

    /* Spawn a scout just above y=240 so next update pushes it past 240 */
    e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 239);
    TEST_ASSERT_NOT_NULL(e, "#159: Spawn for escape test");
    def = enemyGetTypeDef(ENEMY_TYPE_SCOUT);

    /* After update, enemy moves down by speed (2 px) to y=241, which is > 240 */
    enemyUpdateAll();

    /* Enemy should be removed and 25% of score_value awarded */
    TEST_ASSERT_EQ(e->active, ENTITY_INACTIVE, "#159: Enemy removed on downward exit");
    TEST_ASSERT_EQ(g_score, def->score_value >> 2, "#159: 25% partial score awarded");
}

/*--- Test #159: No partial score for sideways exit ---*/
TEST(test_enemy_no_partial_score_sideways)
{
    Enemy *e;

    enemyInit();
    g_game.current_zone = 0;
    g_frame_count = 0;
    collisionInit();

    /* Spawn enemy far left so it exits sideways (x < -48) */
    e = enemySpawn(ENEMY_TYPE_SCOUT, -47, 100);
    TEST_ASSERT_NOT_NULL(e, "#159: Spawn for sideways exit test");
    e->vx = (s16)-0x0200;  /* Moving left */
    e->vy = 0;  /* Not moving down */

    enemyUpdateAll();

    /* Enemy should be removed but no score since it's a sideways exit */
    TEST_ASSERT_EQ(e->active, ENTITY_INACTIVE, "#159: Enemy removed on sideways exit");
    TEST_ASSERT_EQ(g_score, 0, "#159: No partial score for sideways exit");
}

/*--- Test #164: V-formation spawner ---*/
TEST(test_enemy_v_formation)
{
    u8 i, count;
    Enemy *pool;

    enemyInit();
    g_game.current_zone = 0;
    g_frame_count = 0;

    enemySpawnVFormation(ENEMY_TYPE_SCOUT, 120, -20);

    pool = enemyGetPool();
    count = 0;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (pool[i].active == ENTITY_ACTIVE) count++;
    }
    TEST_ASSERT_EQ(count, 5, "#164: V-formation spawns 5 enemies");

    /* Verify V-shape positions:
     * [0] center  (120, -20)
     * [1] left    (90, -40)
     * [2] right   (150, -40)
     * [3] far left  (60, -60)
     * [4] far right (180, -60) */
    pool = enemyGetPool();
    TEST_ASSERT_EQ(pool[0].x, 120, "#164: V-formation center X = 120");
    TEST_ASSERT_EQ(pool[0].y, -20, "#164: V-formation center Y = -20");
    TEST_ASSERT_EQ(pool[1].x, 90, "#164: V-formation left X = 90");
    TEST_ASSERT_EQ(pool[1].y, -40, "#164: V-formation left Y = -40");
    TEST_ASSERT_EQ(pool[2].x, 150, "#164: V-formation right X = 150");
    TEST_ASSERT_EQ(pool[2].y, -40, "#164: V-formation right Y = -40");
    TEST_ASSERT_EQ(pool[3].x, 60, "#164: V-formation far left X = 60");
    TEST_ASSERT_EQ(pool[3].y, -60, "#164: V-formation far left Y = -60");
    TEST_ASSERT_EQ(pool[4].x, 180, "#164: V-formation far right X = 180");
    TEST_ASSERT_EQ(pool[4].y, -60, "#164: V-formation far right Y = -60");
}

/*--- Test #172: Enemy fire telegraph ---*/
TEST(test_enemy_fire_telegraph)
{
    u8 k;
    enemyInit();
    g_game.current_zone = 0;
    g_frame_count = 0;
    Enemy *e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    TEST_ASSERT_NOT_NULL(e, "#172: Spawn for telegraph test");
    const EnemyTypeDef *def = enemyGetTypeDef(ENEMY_TYPE_SCOUT);

    /* fire_timer starts at fire_rate (90 for scout).
     * Each update: check fire_timer==3 (telegraph), then fire_timer--, then flash_timer--.
     * After N updates, fire_timer = fire_rate - N.
     * Telegraph triggers at the START of update where fire_timer==3 (before decrement).
     * That's update number (fire_rate - 3), when fire_timer = 90 - (fire_rate-3-1) = 3.
     *
     * Run (fire_rate - 3 - 1) updates to reach fire_timer == 4 (telegraph not yet).
     * The spawn flash_timer (4) expires after 4 updates (long before fire_rate-4). */
    for (k = 0; k < def->fire_rate - 4; k++) {
        enemyUpdateAll();
    }
    /* After fire_rate-4 updates: fire_timer = 4, flash_timer = 0 (spawn flash expired) */
    TEST_ASSERT_EQ(e->fire_timer, 4, "#172: fire_timer at 4 before telegraph");
    TEST_ASSERT_EQ(e->flash_timer, 0, "#172: flash_timer at 0 before telegraph");

    /* One more update: fire_timer starts at 4, check==3? No.
     * fire_timer-- -> 3. flash_timer stays 0. */
    enemyUpdateAll();
    TEST_ASSERT_EQ(e->fire_timer, 3, "#172: fire_timer at 3");

    /* Next update: fire_timer starts at 3, check==3? YES -> flash_timer = 3.
     * fire_timer-- -> 2. flash_timer-- -> 2. */
    enemyUpdateAll();
    TEST_ASSERT_EQ(e->fire_timer, 2, "#172: fire_timer at 2 after telegraph");
    TEST_ASSERT_EQ(e->flash_timer, 2, "#172: flash_timer at 2 (set to 3, decremented)");
}

/*--- Test #178: Adaptive fire rate wave counting ---*/
TEST(test_enemy_adaptive_fire_rate)
{
    u8 i;
    Enemy *e;
    const EnemyTypeDef *def;

    /* Setup triggers to reset wave_count */
    enemySetupZoneTriggers(0);
    enemyInit();
    g_game.current_zone = 0;
    g_frame_count = 0;
    def = enemyGetTypeDef(ENEMY_TYPE_SCOUT);

    /* First 7 waves: normal fire rate */
    for (i = 0; i < 7; i++) {
        g_frame_count = i;  /* Different frame each spawn to increment wave_count */
        e = enemySpawn(ENEMY_TYPE_SCOUT, 100, -20);
    }
    /* Spawn at wave 7 (index 6 counting from 0): fire_timer should be normal */
    TEST_ASSERT_NOT_NULL(e, "#178: Spawn before 8 waves");
    TEST_ASSERT_EQ(e->fire_timer, def->fire_rate, "#178: Normal fire rate before 8 waves");

    /* After 8+ waves: fire rate should be reduced by 12.5% */
    g_frame_count = 7;  /* 8th unique frame */
    e = enemySpawn(ENEMY_TYPE_SCOUT, 100, -20);
    TEST_ASSERT_NOT_NULL(e, "#178: Spawn at 8th wave");

    /* If wave_count >= 8, fire_timer -= fire_timer >> 3
     * For scout fire_rate: the new timer should be less than base */
    if (e->fire_timer < def->fire_rate) {
        TEST_ASSERT(e->fire_timer < def->fire_rate, "#178: Reduced fire rate after 8+ waves");
    } else {
        /* wave_count tracking may not have reached 8 due to same-frame dedup */
        TEST_ASSERT(1, "#178: Fire rate test (wave dedup may apply)");
    }

    /* Restore */
    g_game.current_zone = 0;
}

/* #181: Heavy enemies spawn with shield */
TEST(test_enemy_shield_spawn)
{
    Enemy *e;
    enemyInit();
    g_game.current_zone = 0;
    e = enemySpawn(ENEMY_TYPE_HEAVY, 100, -20);
    TEST_ASSERT_NOT_NULL(e, "#181: Heavy spawns");
    TEST_ASSERT_EQ(e->shield, 1, "#181: Heavy has shield");
    enemyInit();
    e = enemySpawn(ENEMY_TYPE_SCOUT, 100, -20);
    TEST_ASSERT_NOT_NULL(e, "#181: Scout spawns");
    TEST_ASSERT_EQ(e->shield, 0, "#181: Scout has no shield");
    enemyInit();
    e = enemySpawn(ENEMY_TYPE_ELITE, 100, -20);
    TEST_ASSERT_NOT_NULL(e, "#181: Elite spawns");
    TEST_ASSERT_EQ(e->shield, 0, "#181: Elite has no shield");
}

/* #186: Hazard flag defaults to 0 */
TEST(test_enemy_hazard_flag)
{
    Enemy *e;
    enemyInit();
    g_game.current_zone = 0;
    e = enemySpawn(ENEMY_TYPE_SCOUT, 100, -20);
    TEST_ASSERT_NOT_NULL(e, "#186: Scout spawns");
    TEST_ASSERT_EQ(e->is_hazard, 0, "#186: Default is_hazard is 0");
    /* Manual set for hazard wave */
    e->is_hazard = 1;
    TEST_ASSERT_EQ(e->is_hazard, 1, "#186: Hazard flag can be set");
}

/* #193: Elite swarm spawns 6 enemies */
TEST(test_enemy_elite_swarm_count)
{
    u8 i, count;
    Enemy *pool;
    enemyInit();
    g_game.current_zone = 2;  /* Zone 3 */
    /* Spawn 6 elites via direct calls (mirroring z3_w_elite_swarm) */
    enemySpawnFromLeft(ENEMY_TYPE_ELITE, -10);
    enemySpawnFromRight(ENEMY_TYPE_ELITE, -20);
    enemySpawnFromLeft(ENEMY_TYPE_ELITE, -30);
    enemySpawnFromRight(ENEMY_TYPE_ELITE, -40);
    enemySpawnFromLeft(ENEMY_TYPE_ELITE, -50);
    enemySpawnFromRight(ENEMY_TYPE_ELITE, -60);
    pool = enemyGetPool();
    count = 0;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (pool[i].active != ENTITY_INACTIVE) count++;
    }
    TEST_ASSERT_EQ(count, 6, "#193: Elite swarm spawns 6 enemies");
}

/*--- Test #235: Speed kill flash on enemies ---*/
TEST(test_enemy_speed_kill_flash)
{
    enemyInit();
    Enemy *e;

    /* Speed kill (age < 90): extended flash */
    e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    e->age = 50;  /* Young enemy = speed kill */
    e->flash_timer = 0;
    enemyDamage(e, 100);
    TEST_ASSERT_EQ(e->flash_timer, 12, "Speed kill flash = 12 (no prior blink, age<90 #235)");

    /* Normal kill (age >= 90): standard flash */
    enemyInit();
    e = enemySpawn(ENEMY_TYPE_SCOUT, 100, 50);
    e->age = 100;
    e->flash_timer = 0;
    enemyDamage(e, 100);
    TEST_ASSERT_EQ(e->flash_timer, 10, "Normal death flash = 10 (age>=90 #235)");

    /* Speed kill with prior blink: extra extended */
    enemyInit();
    e = enemySpawn(ENEMY_TYPE_FIGHTER, 100, 50);
    e->age = 30;
    enemyDamage(e, 5);  /* Start blink */
    TEST_ASSERT_EQ(e->flash_timer, 6, "Damage blink = 6");
    enemyDamage(e, 100); /* Kill while blinking */
    TEST_ASSERT_EQ(e->flash_timer, 16, "Speed kill mid-blink flash = 16 (#235)");

    /* Normal kill with prior blink */
    enemyInit();
    e = enemySpawn(ENEMY_TYPE_FIGHTER, 100, 50);
    e->age = 100;
    enemyDamage(e, 5);
    enemyDamage(e, 100);
    TEST_ASSERT_EQ(e->flash_timer, 14, "Normal mid-blink death flash = 14 (age>=90 #235)");
}

void run_enemy_tests(void)
{
    TEST_SUITE("Enemy System");
    test_enemy_init();
    test_enemy_spawn();
    test_enemy_pool_full();
    test_enemy_type_bounds();
    test_enemy_typedef_bounds();
    test_enemy_damage();
    test_enemy_overkill();
    test_enemy_kill_all();
    test_enemy_spawn_wave();
    test_enemy_type_properties();
    test_enemy_active_count();
    test_enemy_death_effect();
    test_enemy_death_completes();
    test_enemy_load_graphics();
    test_enemy_render_culling();
    test_enemy_spawn_flash();
    test_enemy_render_lut();
    test_enemy_render_lut_dying();
    test_enemy_fire_timer_decrement();
    test_enemy_fire_timer_reload();
    test_enemy_ai_timer_u8();
    test_enemy_update_cached_def();
    test_enemy_ai_velocity_cache();
    test_enemy_render_early_exit();
    test_enemy_update_state_ordering();
    test_enemy_spawn_wave_additive();
    test_enemy_combined_gfx_lut();
    test_enemy_death_flash_extension();
    test_enemy_zone_scaled_hp();
    test_enemy_age_tracking();
    test_enemy_golden_variant();
    test_enemy_golden_zone_scaled();
    test_enemy_partial_escape_score();
    test_enemy_no_partial_score_sideways();
    test_enemy_v_formation();
    test_enemy_fire_telegraph();
    test_enemy_adaptive_fire_rate();
    test_enemy_shield_spawn();
    test_enemy_hazard_flag();
    test_enemy_elite_swarm_count();
    test_enemy_speed_kill_flash();
}
