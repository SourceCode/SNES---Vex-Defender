/*==============================================================================
 * Player Ship
 * Loads player sprite graphics, processes input-driven movement with screen
 * clamping, banking animation with delay, and invincibility blink.
 *============================================================================*/

#include "game/player.h"
#include "engine/input.h"
#include "assets.h"

PlayerShip g_player;

void playerInit(void)
{
    /* Load player ship tiles into OBJ VRAM at offset 0 (base of sprite VRAM) */
    spriteLoadTiles((u8 *)&player_ship_til,
                    ASSET_SIZE(player_ship_til),
                    0);

    /* Load player palette into OBJ palette slot 0 (CGRAM 128-143) */
    spriteLoadPalette((u8 *)&player_ship_pal,
                      ASSET_SIZE(player_ship_pal),
                      0);

    /* Allocate sprite entity from pool (gets pool slot 0 = OAM id 0) */
    g_player.sprite = spriteAlloc();
    if (g_player.sprite) {
        g_player.sprite->x = PLAYER_START_X;
        g_player.sprite->y = PLAYER_START_Y;
        g_player.sprite->tile_offset = 0;
        g_player.sprite->palette = 0;       /* OBJ palette slot 0 */
        g_player.sprite->priority = 3;      /* Above all backgrounds */
        g_player.sprite->size = OBJ_LARGE;  /* 32x32 */
        g_player.sprite->hflip = 0;
        g_player.sprite->vflip = 0;
        g_player.sprite->anim_count = 1;    /* Single frame for now */
        g_player.sprite->anim_speed = 0;
    }

    g_player.x = PLAYER_START_X;
    g_player.y = PLAYER_START_Y;
    g_player.anim_state = PLAYER_ANIM_IDLE;
    g_player.invincible_timer = 0;
    g_player.visible = 1;
    g_player.bank_timer = 0;
}

void playerHandleInput(u16 held)
{
    s16 speed;
    u8 moving_h = 0;

    speed = (held & ACTION_SLOW) ? PLAYER_SPEED_SLOW : PLAYER_SPEED_NORMAL;

    if (held & ACTION_UP)    g_player.y -= speed;
    if (held & ACTION_DOWN)  g_player.y += speed;

    if (held & ACTION_LEFT) {
        g_player.x -= speed;
        moving_h = 1;
        playerSetBanking(PLAYER_ANIM_LEFT);
        g_player.bank_timer = BANK_RETURN_DELAY;
    }
    if (held & ACTION_RIGHT) {
        g_player.x += speed;
        moving_h = 1;
        playerSetBanking(PLAYER_ANIM_RIGHT);
        g_player.bank_timer = BANK_RETURN_DELAY;
    }

    /* Return to idle banking after delay */
    if (!moving_h) {
        if (g_player.bank_timer > 0) {
            g_player.bank_timer--;
        } else {
            playerSetBanking(PLAYER_ANIM_IDLE);
        }
    }

    /* Clamp to screen bounds */
    if (g_player.x < PLAYER_MIN_X) g_player.x = PLAYER_MIN_X;
    if (g_player.x > PLAYER_MAX_X) g_player.x = PLAYER_MAX_X;
    if (g_player.y < PLAYER_MIN_Y) g_player.y = PLAYER_MIN_Y;
    if (g_player.y > PLAYER_MAX_Y) g_player.y = PLAYER_MAX_Y;
}

void playerUpdate(void)
{
    if (!g_player.sprite) return;

    /* Sync sprite position from player position */
    g_player.sprite->x = g_player.x;
    g_player.sprite->y = g_player.y;

    /* Invincibility blink effect */
    if (g_player.invincible_timer > 0) {
        g_player.invincible_timer--;
        /* Blink: visible every other 4-frame period */
        g_player.visible = (g_player.invincible_timer >> 2) & 1;
        if (g_player.visible) {
            oamSetEx(g_player.sprite->oam_id, OBJ_LARGE, OBJ_SHOW);
        } else {
            oamSetVisible(g_player.sprite->oam_id, OBJ_HIDE);
        }
    } else {
        g_player.visible = 1;
    }
}

void playerSetBanking(u8 state)
{
    g_player.anim_state = state;
    if (!g_player.sprite) return;

    switch (state) {
        case PLAYER_ANIM_LEFT:
            g_player.sprite->hflip = 1;
            break;
        case PLAYER_ANIM_RIGHT:
        case PLAYER_ANIM_IDLE:
        default:
            g_player.sprite->hflip = 0;
            break;
    }
}

void playerSetPosition(s16 x, s16 y)
{
    g_player.x = x;
    g_player.y = y;
}

void playerShow(void)
{
    g_player.visible = 1;
    if (g_player.sprite) {
        oamSetEx(g_player.sprite->oam_id, OBJ_LARGE, OBJ_SHOW);
    }
}

void playerHide(void)
{
    g_player.visible = 0;
    if (g_player.sprite) {
        oamSetVisible(g_player.sprite->oam_id, OBJ_HIDE);
    }
}
