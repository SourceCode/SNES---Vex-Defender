/*==============================================================================
 * Player Ship
 * Loads player sprite graphics, processes input-driven movement with screen
 * clamping, banking animation with delay, and invincibility blink.
 *
 * VRAM usage:
 *   - Player tiles (32x32, 4bpp): OBJ VRAM offset 0x0000, name 0 (cols 0-3, rows 0-3)
 *   - Player palette: OBJ CGRAM slot 0 (CGRAM bytes 128-143 = colors 128-143)
 *
 * OAM usage:
 *   - Slot 0: main player ship sprite (32x32, priority 3 = above all BGs)
 *   - Slot 1 (OAM byte offset 4): thrust trail flicker effect
 *
 * The player ship is rendered as a single 32x32 OBJ sprite using the
 * sprite engine pool. Banking (tilting left/right) is achieved by
 * toggling the horizontal flip flag on the same tile data -- no separate
 * animation frames are needed since the ship is roughly symmetrical.
 *============================================================================*/

#include "game/player.h"
#include "engine/input.h"
#include "assets.h"

/* Global singleton player instance */
PlayerShip g_player;

/* Thrust trail state: a simple flicker effect behind the ship.
 * trail_toggle alternates 0/1 each frame, causing the trail sprite
 * to appear every other frame for a shimmer/afterburner look. */
static u8 trail_active;
static u8 trail_toggle;   /* Alternates 0/1 each frame for flicker */

/*
 * playerInit
 * ----------
 * One-time initialization: loads ship graphics into VRAM, allocates a
 * SpriteEntity from the engine pool, and sets default position/state.
 *
 * Must be called:
 *   1. After spriteSystemInit() (pool must exist)
 *   2. During force blank (screen off) since we DMA into VRAM
 *
 * SNES details:
 *   - spriteLoadTiles32() does a DMA transfer of 32x32 tile data into OBJ VRAM.
 *     The SNES OBJ tiles are arranged in a 16-name-wide grid; a 32x32 sprite
 *     occupies a 4x4 block of 8x8 names (4bpp = 32 bytes per 8x8 tile).
 *   - spriteLoadPalette() writes to CGRAM (color generator RAM) starting at
 *     palette slot 0 (which maps to OBJ colors 128+, since BG uses 0-127).
 *   - spriteAlloc() returns a SpriteEntity pointer from the fixed-size pool.
 *     The pool is small (typically 16-32 entries), and the player gets slot 0.
 */
void playerInit(void)
{
    /* Load player ship tiles (32x32) with SNES OBJ VRAM row spacing.
     * Destination offset 0 = name 0, the first 32x32 tile position. */
    spriteLoadTiles32((u8 *)&player_ship_til, 0);

    /* Load player palette into OBJ palette slot 0 (CGRAM 128-143).
     * SNES has 8 OBJ palettes of 16 colors each at CGRAM 128-255. */
    spriteLoadPalette((u8 *)&player_ship_pal,
                      ASSET_SIZE(player_ship_pal),
                      0);

    /* Allocate sprite entity from pool (gets pool slot 0 = OAM id 0) */
    g_player.sprite = spriteAlloc();
    if (g_player.sprite) {
        g_player.sprite->x = PLAYER_START_X;
        g_player.sprite->y = PLAYER_START_Y;
        g_player.sprite->tile_offset = 0;   /* Name 0 in OBJ VRAM */
        g_player.sprite->palette = 0;       /* OBJ palette slot 0 (CGRAM 128-143) */
        g_player.sprite->priority = 3;      /* Priority 3 = above all BG layers (0-2) */
        g_player.sprite->size = OBJ_LARGE;  /* 32x32 (SNES OAM large size setting) */
        g_player.sprite->hflip = 0;         /* No horizontal flip (facing forward) */
        g_player.sprite->vflip = 0;         /* No vertical flip */
        g_player.sprite->anim_count = 1;    /* Single frame for now (no multi-frame anim) */
        g_player.sprite->anim_speed = 0;    /* No animation cycling */
    }

    /* Initialize player state */
    g_player.x = PLAYER_START_X;
    g_player.y = PLAYER_START_Y;
    g_player.anim_state = PLAYER_ANIM_IDLE;
    g_player.invincible_timer = 0;
    g_player.visible = 1;
    g_player.bank_timer = 0;
    g_player.combo_flash = 0;   /* #234: Must init - SNES WRAM is garbage */

    trail_active = 0;
    trail_toggle = 0;
}

/*
 * playerHandleInput
 * -----------------
 * Process D-pad + buttons and move the player ship accordingly.
 *
 * Parameters:
 *   held - bitmask of currently held buttons from inputHeld().
 *          Relevant bits: ACTION_UP/DOWN/LEFT/RIGHT, ACTION_SLOW
 *
 * Movement logic:
 *   - Speed is 2 px/frame normally, 1 px/frame in slow/focus mode
 *   - Horizontal input triggers banking animation (ship tilts via hflip)
 *   - Bank timer provides hysteresis: banking holds for BANK_RETURN_DELAY
 *     frames after the player releases the direction, preventing flicker
 *   - Position is clamped to screen bounds after movement
 */
void playerHandleInput(u16 held)
{
    s16 speed;
    u8 moving_h = 0;

    /* Select speed: shoulder button held = slow/focus mode for precise dodging */
    speed = (held & ACTION_SLOW) ? PLAYER_SPEED_SLOW : PLAYER_SPEED_NORMAL;

    /* #203: Diagonal speed normalization.
     * Without this, diagonal movement is sqrt(2) * speed (~1.41x faster).
     * Reduce speed by 1 when moving diagonally and speed > 1 to approximate
     * proper vector normalization. At speed=2: diagonal=1 per axis (1.41 total)
     * instead of 2 per axis (2.83 total). At speed=1: no change (can't go lower). */
    if ((held & (ACTION_UP | ACTION_DOWN)) && (held & (ACTION_LEFT | ACTION_RIGHT))) {
        if (speed > 1) speed--;
    }

    /* Vertical movement (no banking effect) */
    if (held & ACTION_UP)    g_player.y -= speed;
    if (held & ACTION_DOWN)  g_player.y += speed;

    /* Horizontal movement with banking animation */
    if (held & ACTION_LEFT) {
        g_player.x -= speed;
        moving_h = 1;
        playerSetBanking(PLAYER_ANIM_LEFT);     /* Flip sprite horizontally */
        g_player.bank_timer = BANK_RETURN_DELAY; /* Reset hold timer */
    }
    if (held & ACTION_RIGHT) {
        g_player.x += speed;
        moving_h = 1;
        playerSetBanking(PLAYER_ANIM_RIGHT);    /* Un-flip sprite */
        g_player.bank_timer = BANK_RETURN_DELAY; /* Reset hold timer */
    }

    /* Return to idle banking after delay expires with no horizontal input.
     * The delay prevents the ship from snapping back to idle on brief taps. */
    if (!moving_h) {
        if (g_player.bank_timer > 0) {
            g_player.bank_timer--;
        } else {
            playerSetBanking(PLAYER_ANIM_IDLE);
        }
    }

    /* Clamp position to screen bounds to prevent the ship from going off-screen.
     * These bounds account for the 32x32 sprite size and top HUD reservation.
     * #137: Track if clamping occurred to trigger edge warning visual. */
    {
        u8 clamped = 0;
        if (g_player.x < PLAYER_MIN_X) { g_player.x = PLAYER_MIN_X; clamped = 1; }
        if (g_player.x > PLAYER_MAX_X) { g_player.x = PLAYER_MAX_X; clamped = 1; }
        if (g_player.y < PLAYER_MIN_Y) { g_player.y = PLAYER_MIN_Y; clamped = 1; }
        if (g_player.y > PLAYER_MAX_Y) { g_player.y = PLAYER_MAX_Y; clamped = 1; }

        /* #137: Brief brightness dip when hitting screen edge.
         * Provides subtle feedback that the player has reached the boundary.
         * Brightness 13/15 is a barely-noticeable dimming that resolves
         * naturally on the next frame. */
        if (clamped) {
            setBrightness(13);
        }
    }
}

/*
 * playerUpdate
 * ------------
 * Per-frame update: synchronizes sprite engine position, handles invincibility
 * blink animation, and manages the thrust trail visual effect.
 *
 * Invincibility blink algorithm:
 *   - Timer counts down from 120 (2 seconds at 60fps)
 *   - Visibility toggles on a 4-frame cycle: (timer >> 2) & 1
 *     This gives 2 frames visible, 2 frames hidden -> a moderate blink rate
 *   - Special brightness flashes at frames 117 and 115 simulate a "hit flash"
 *     at the start of invincibility (the first ~5 frames after being hit)
 *
 * Thrust trail:
 *   - Uses OAM slot 1 (byte offset 4) to show a flickering copy of the ship
 *     sprite slightly behind the main ship, creating an afterburner effect
 *   - Alternates visibility every frame via trail_toggle XOR
 *   - Only shown when not invincible (to avoid visual clutter during blink)
 */
void playerUpdate(void)
{
    if (!g_player.sprite) return;

    /* Sync sprite engine position from authoritative player position.
     * The sprite engine reads these values during spriteRenderAll(). */
    g_player.sprite->x = g_player.x;
    g_player.sprite->y = g_player.y;

    /* Invincibility blink effect */
    if (g_player.invincible_timer > 0) {
        g_player.invincible_timer--;

        /* Brief brightness flash on initial hit frames (visual "hit" punch).
         * setBrightness(15) = full brightness; these frames are near the start
         * of a 120-frame timer, so they fire ~3 and ~5 frames after being hit. */
        if (g_player.invincible_timer == 117 || g_player.invincible_timer == 115) {
            setBrightness(15);
        }

        /* Blink: visible every other 4-frame period.
         * Bitshift by 2 = divide by 4, then AND 1 = alternating on/off.
         * Results in: frames 0-3 -> visible, 4-7 -> hidden, 8-11 -> visible... */
        g_player.visible = (g_player.invincible_timer >> 2) & 1;
        if (g_player.visible) {
            oamSetEx(g_player.sprite->oam_id, OBJ_LARGE, OBJ_SHOW);
        } else {
            oamSetVisible(g_player.sprite->oam_id, OBJ_HIDE);
        }
    } else {
        g_player.visible = 1;
    }

    /* #234: Combo flash - alternate palette on even frames while active */
    if (g_player.combo_flash > 0) {
        g_player.combo_flash--;
        g_player.sprite->palette = (g_player.combo_flash & 1) ? 1 : 0;
    }

    /* Thrust trail effect: flicker a ghost sprite behind player for afterburner.
     * OAM slot 1 (byte offset 4) is used for this effect. The trail reuses the
     * same tile data as the player ship (tile 0, palette 0) but is placed at
     * a slight Y offset (+4 pixels) to appear behind the engines. */
    trail_toggle ^= 1;

    /* #237: Momentum trail - always show trail when moving at max speed */
    {
        u8 trail_visible;
        trail_visible = trail_toggle;
        if (g_player.anim_state != PLAYER_ANIM_IDLE) {
            trail_visible = 1;  /* Always show when banking (moving horizontally) */
        }

        if (g_player.invincible_timer == 0 && g_player.visible) {
            /* Show trail OAM at slot 1 (OAM byte offset = 4) on alternate frames */
            if (trail_visible) {
                oamSet(4, (u16)g_player.x, (u16)(g_player.y + 4), 2, 0, 0, 0, 0);
                oamSetEx(4, OBJ_LARGE, OBJ_SHOW);
            } else {
                oamSetVisible(4, OBJ_HIDE);
            }
            trail_active = 1;
        } else {
            /* Hide trail during invincibility to reduce visual noise */
            oamSetVisible(4, OBJ_HIDE);
            trail_active = 0;
        }
    }
}

/*
 * playerSetBanking
 * ----------------
 * Set the ship's visual banking (tilt) state by controlling horizontal flip.
 *
 * Parameters:
 *   state - PLAYER_ANIM_IDLE, PLAYER_ANIM_LEFT, or PLAYER_ANIM_RIGHT
 *
 * Implementation: since the ship sprite is roughly symmetrical, banking left
 * is achieved by setting hflip=1 (horizontal mirror). Banking right and idle
 * both use hflip=0. This avoids needing separate animation frames for banking.
 *
 * SNES OAM detail: hflip is bit 6 of OAM byte 3 for each sprite. The sprite
 * engine abstracts this via the SpriteEntity.hflip field.
 */
void playerSetBanking(u8 state)
{
    g_player.anim_state = state;
    if (!g_player.sprite) return;

    switch (state) {
        case PLAYER_ANIM_LEFT:
            g_player.sprite->hflip = 1;  /* Mirror horizontally = banking left */
            break;
        case PLAYER_ANIM_RIGHT:
        case PLAYER_ANIM_IDLE:
        default:
            g_player.sprite->hflip = 0;  /* Normal orientation */
            break;
    }
}

/*
 * playerSetPosition
 * -----------------
 * Set player position directly, bypassing input handling and screen clamping.
 * Used for scripted events like post-battle respawn positioning.
 *
 * Parameters:
 *   x, y - new screen position in pixels (top-left of 32x32 sprite)
 */
void playerSetPosition(s16 x, s16 y)
{
    g_player.x = x;
    g_player.y = y;
}

/*
 * playerShow
 * ----------
 * Make the player ship visible on screen by writing to OAM.
 * Used when transitioning back from battle or menu screens.
 * Sets the OAM extended attribute to SHOW and size to LARGE (32x32).
 */
void playerShow(void)
{
    g_player.visible = 1;
    if (g_player.sprite) {
        oamSetEx(g_player.sprite->oam_id, OBJ_LARGE, OBJ_SHOW);
    }
}

/*
 * playerHide
 * ----------
 * Hide the player ship by moving it off-screen in OAM.
 * Used during battle transitions, game over, and other non-flight states.
 * SNES OAM note: OBJ_HIDE typically sets the Y coordinate to 224+ (off-screen)
 * or uses the extended table bit to disable the sprite.
 */
void playerHide(void)
{
    g_player.visible = 0;
    if (g_player.sprite) {
        oamSetVisible(g_player.sprite->oam_id, OBJ_HIDE);
    }
}
