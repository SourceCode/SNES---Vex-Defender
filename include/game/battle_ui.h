/*==============================================================================
 * Battle UI Module - Phase 12
 *
 * Visual enhancements for the battle system:
 *   - Text-based HP bars (10-segment, no division math)
 *   - Battle sprites (enemy + player) using OAM_UI slots 64-65
 *   - Hit shake animation on damage
 *   - All BG3 text drawing functions for battle screen
 *
 * Called from battle.c state machine. Drawing separated from logic.
 *============================================================================*/

#ifndef BATTLE_UI_H
#define BATTLE_UI_H

#include "game.h"

/*=== Battle Sprite Screen Positions ===*/
#define BUI_ENEMY_SPR_X     28
#define BUI_ENEMY_SPR_Y     28
#define BUI_PLAYER_SPR_X    184
#define BUI_PLAYER_SPR_Y    96

/*=== OAM IDs for Battle Sprites (from OAM_UI range, slot*4) ===*/
#define BUI_ENEMY_OAM_ID    256   /* OAM slot 64 */
#define BUI_PLAYER_OAM_ID   260   /* OAM slot 65 */

/*=== HP Bar Width (characters, excluding brackets) ===*/
#define BUI_HP_BAR_WIDTH    10

/*=== Shake Duration (frames) ===*/
#define BUI_SHAKE_FRAMES    8

/* Initialize battle UI state (call once at startup) */
void battleUIInit(void);

/* Show battle sprites on screen (call during transition in, after spriteHideAll) */
void battleUIShowSprites(u8 enemy_type);

/* Hide battle sprites (call during transition out) */
void battleUIHideSprites(void);

/* Draw the complete initial battle screen (stats, bars, "ENCOUNTER!") */
void battleUIDrawScreen(void);

/* Redraw enemy name and HP bar */
void battleUIDrawEnemyStats(void);

/* Redraw player HP bar and SP */
void battleUIDrawPlayerStats(void);

/* Draw action menu with cursor at given position */
void battleUIDrawMenu(u8 cursor);

/* Clear the menu area */
void battleUIClearMenu(void);

/* Draw a message on the message row */
void battleUIDrawMessage(char *msg);

/* Draw damage/heal amount on the damage row */
void battleUIDrawDamage(s16 damage);

/* Start shake animation (0=shake enemy, 1=shake player) */
void battleUIStartShake(u8 target);

/* Start shake animation with custom duration in frames */
void battleUIStartShakeN(u8 target, u8 frames);

/* Update shake animation each frame (no-op when inactive) */
void battleUIUpdateShake(void);

/* Draw victory screen with XP */
void battleUIDrawVictory(u16 xp);

/* Draw defeat screen */
void battleUIDrawDefeat(void);

/* Draw level-up notification (Phase 13) */
void battleUIDrawLevelUp(u8 new_level);

/* Draw item sub-menu with cursor (Phase 14) */
void battleUIDrawItemMenu(u8 *item_ids, u8 *qtys, u8 count, u8 cursor);

/* Draw item drop notification on victory screen (Phase 14) */
void battleUIDrawItemDrop(char *item_name);

/* Draw turn counter in bottom-right corner */
void battleUIDrawTurnCounter(void);

/* Animate HP bars toward actual values (call each frame during battle) */
void battleUIAnimateHP(void);

/* Draw XP reward preview during player turn (#185) */
void battleUIDrawXPPreview(u16 xp);

/* Draw turn count during player turn (#189) */
void battleUIDrawTurnCount(u8 turn);

#endif /* BATTLE_UI_H */
