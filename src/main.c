/*==============================================================================
 * VEX DEFENDER - Main Entry Point
 * Phase 20: Polish & Final (save system, enhanced menus, play time)
 *
 * This is the top-level game loop for a SNES shoot-em-up / RPG hybrid.
 * The architecture follows a simple state machine pattern:
 *
 *   1. Boot: Hardware init (systemInit), SPC700 sound driver boot, input init
 *   2. Title: Menu screen with NEW GAME / CONTINUE options
 *   3. Flight: Side-scrolling shoot-em-up gameplay
 *   4. Battle: Turn-based RPG combat (triggered by collisions or scroll events)
 *   5. Dialog: Story text overlay (triggered by scroll distance events)
 *   6. Game Over: Defeat menu (retry zone / return to title)
 *   7. Victory: End-of-game stats screen
 *
 * The main loop runs at 60fps (NTSC), synced to the VBlank interrupt.
 * Each iteration: WaitForVBlank -> VBlank-critical updates -> input poll ->
 * state dispatch -> play time tracking -> SPC700 keepalive -> callbacks.
 *
 * SNES hardware note: WaitForVBlank() halts the 65816 CPU via the WAI
 * instruction until the PPU's vertical blanking NMI fires.  This ensures
 * all VRAM/OAM/CGRAM writes happen during the safe VBlank window.
 *============================================================================*/

#include "game.h"
#include "assets.h"
#include "engine/system.h"
#include "engine/vblank.h"
#include "engine/fade.h"
#include "engine/background.h"
#include "engine/sprites.h"
#include "engine/input.h"
#include "engine/scroll.h"
#include "engine/bullets.h"
#include "engine/collision.h"
#include "game/player.h"
#include "game/enemies.h"
#include "game/battle.h"
#include "game/rpg_stats.h"
#include "game/inventory.h"
#include "game/game_state.h"
#include "game/boss.h"
#include "game/dialog.h"
#include "engine/sound.h"

/* Timer for the brief brightness flash when switching weapons.
 * Counts down from 3 to 0, dimming brightness to 13 during the flash. */
static u8 weapon_flash_timer = 0;

/*
 * bootSequence - One-time hardware initialization at power-on / reset.
 *
 * Calls systemInit() which configures the PPU (Mode 1, BG layers, sprites),
 * clears VRAM/OAM, and sets brightness to 0.
 *
 * After init, the screen is turned on (still black at brightness 0) and
 * we wait 30 frames (~0.5 seconds) for hardware to settle.  This delay
 * prevents visual artifacts on real hardware where the PPU and APU may
 * not be fully ready immediately after reset.
 */
void bootSequence(void)
{
    /* Initialize all SNES hardware: PPU mode, BG addresses, sprite config,
     * OAM clear, scroll reset, brightness 0, VBlank framework */
    systemInit();

    /* Turn on screen output (still black since brightness is 0) */
    setScreenOn();

    /* Wait 30 frames for hardware settle.
     * On real SNES hardware, the SPC700 (audio CPU) needs time to boot,
     * and some CRT displays need a few frames to sync. */
    systemWaitFrames(30);
}

/*
 * main - The game's main entry point and infinite game loop.
 *
 * After boot and subsystem initialization, the loop runs one iteration
 * per VBlank (60fps NTSC / 50fps PAL).  The loop structure is:
 *
 *   1. WaitForVBlank - Sync to display timing, ensure safe VRAM access
 *   2. VBlank-critical updates - BG tilemap DMA, scroll register writes
 *   3. Screen shake - Horizontal BG displacement for hit feedback
 *   4. Input polling - Read controller state (held + edge-triggered)
 *   5. State machine dispatch - Run the active state's update function
 *   6. Play time tracking - Increment seconds counter every 60 frames
 *   7. SPC700 keepalive - Must call soundUpdate() every frame
 *   8. Deferred callbacks - Process any queued VBlank callback functions
 */
int main(void)
{
    u16 pad_held, pad_pressed;

    bootSequence();

    /* Initialize the SPC700 sound driver.
     * This boots the APU coprocessor, uploads the sound driver code to
     * the SPC700's 64KB RAM, and loads all BRR-encoded sound samples.
     * Must be done after systemInit() but before any soundPlaySFX() calls. */
    soundInit();

    /* Initialize input system (reads joypad registers via auto-read).
     * Must be called before the title screen since it reads input immediately. */
    inputInit();

    /* Initialize game state machine and display the title screen */
    gsInit();
    gsTitleEnter();

    /* ============ MAIN GAME LOOP ============
     * Runs forever at 60fps, one iteration per VBlank.
     * The SNES has no OS to return to, so this loop never exits. */
    while (1) {
        /* Halt CPU until VBlank NMI fires.
         * The NMI handler (consoleVblank) transfers OAM data, processes
         * joypad auto-read results, and sets the "VBlank occurred" flag. */
        WaitForVBlank();

        /* VBlank-critical updates: these write to PPU registers that can
         * only be safely modified during VBlank (VRAM, scroll registers).
         * When not in flight mode, these are designed to be safe no-ops. */
        bgVBlankUpdate();      /* DMA pending tilemap updates to VRAM */
        scrollVBlankUpdate();  /* Write scroll position to BG scroll registers */

        /* Screen shake effect: displaces both BG layers horizontally for
         * a few frames when the player takes a hit.  g_screen_shake is
         * set by the collision system and counts down to 0.
         * The shake alternates +2/-2 pixels based on the counter's bit 1
         * for a rapid jitter effect. */
        if (g_screen_shake > 0) {
            s16 shake_offset;
            shake_offset = (g_screen_shake & 2) ? 2 : -2;  /* Alternate direction */
            bgSetScroll(0, shake_offset, 0);  /* Displace BG1 */
            bgSetScroll(1, shake_offset, 0);  /* Displace BG2 */
            g_screen_shake--;
            if (g_screen_shake == 0) {
                /* Shake ended: reset scroll to neutral position */
                bgSetScroll(0, 0, 0);
                bgSetScroll(1, 0, 0);
            }
        }

        /* Read controller input.
         * inputHeld() returns buttons currently pressed (for continuous actions).
         * inputPressed() returns buttons that transitioned from up to down this
         * frame (for edge-triggered actions like menu selection). */
        inputUpdate();
        pad_held = inputHeld();
        pad_pressed = inputPressed();

        /* ============ STATE MACHINE DISPATCH ============
         * Each state has its own update function that handles input and
         * per-frame logic.  State transitions are performed by calling
         * the target state's gs*Enter() function, which sets current_state. */
        switch (g_game.current_state) {

            case STATE_TITLE:
                /* Title screen: menu navigation and selection */
                gsTitleUpdate(pad_pressed);
                break;

            case STATE_FLIGHT:
                /* --- Flight mode: the main shoot-em-up gameplay --- */

                /* Start button toggles pause */
                if (pad_pressed & ACTION_PAUSE) {
                    gsPauseToggle();
                    break;  /* Skip all other updates this frame */
                }

                /* While paused, skip gameplay but show a pulsing brightness
                 * effect as a visual "breathing" indicator of the pause state.
                 * The 16-entry LUT produces a smooth 7-8-9-10-10-9-8-7 wave
                 * sampled every 4 frames for a ~1-second period. */
                if (g_game.paused) {
                    {
                        static const u8 pause_pulse[16] = {
                            7, 7, 8, 8, 9, 9, 10, 10,
                            10, 10, 9, 9, 8, 8, 7, 7
                        };
                        setBrightness(pause_pulse[(g_frame_count >> 2) & 0x0F]);
                    }
                    break;  /* Skip all gameplay updates while paused */
                }

                /* --- Active flight gameplay updates --- */

                /* Update background tile animation (if any) */
                bgUpdate();
                /* Update scroll position based on current speed */
                scrollUpdate();
                /* Handle player ship movement from D-pad input */
                playerHandleInput(pad_held);
                /* Update player state (animation, invincibility timer, etc.) */
                playerUpdate();

                /* #200: Passive SP regeneration during flight.
                 * rpgRegenSP() regenerates 1 SP every 600 frames (10 sec).
                 * Previously never called, so SP only recovered via items/level-up. */
                rpgRegenSP();

                /* Fire player bullets (Y button = ACTION_FIRE).
                 * Uses pad_held (not pad_pressed) for auto-fire while held. */
                if (pad_held & ACTION_FIRE) {
                    bulletPlayerFire(g_player.x, g_player.y);
                }

                /* Weapon cycling (L/R shoulder buttons, edge-triggered).
                 * Triggers a brief brightness flash to indicate the switch. */
                if (pad_pressed & ACTION_NEXT_WPN) {
                    bulletNextWeapon();
                    weapon_flash_timer = 3;  /* 3-frame flash */
                }
                if (pad_pressed & ACTION_PREV_WPN) {
                    bulletPrevWeapon();
                    weapon_flash_timer = 3;
                }

                /* Move all active bullets and despawn those off-screen */
                bulletUpdateAll();

                /* Run enemy AI: movement patterns, firing, despawn when off-screen */
                enemyUpdateAll();

                /* Check all collision pairs: player-vs-enemy, bullet-vs-enemy,
                 * enemy-bullet-vs-player.  May set g_battle_trigger on hit. */
                collisionCheckAll();

                /* Zone advance check (highest priority): set when boss is defeated.
                 * Must be checked before dialog/battle to prevent starting a new
                 * encounter during the transition. */
                if (g_zone_advance) {
                    gsZoneAdvance();
                    break;
                }

                /* Dialog trigger check (second priority): set by scroll callbacks.
                 * Transitions to STATE_DIALOG which halts flight and shows text. */
                if (g_dialog_pending != 0) {
                    g_game.current_state = STATE_DIALOG;
                    dlgOpen(g_dialog_pending);
                    g_dialog_pending = 0;  /* Consume the trigger */
                }
                /* Battle trigger check (third priority): set by collision system.
                 * Transitions to STATE_BATTLE for turn-based RPG combat. */
                else if (g_battle_trigger != BATTLE_TRIGGER_NONE) {
                    g_game.current_state = STATE_BATTLE;
                    battleStart(g_battle_trigger);
                    g_battle_trigger = BATTLE_TRIGGER_NONE;  /* Consume */
                } else {
                    /* No state transition: render all sprites for this frame.
                     * spriteUpdateAll() advances animation frames.
                     * spriteRenderAll() writes active sprites to the OAM mirror.
                     * bulletRenderAll() and enemyRenderAll() append their entries
                     * to OAM AFTER the sprite system, using separate OAM slot ranges
                     * (defined in config.h) to avoid conflicts. */
                    spriteUpdateAll();
                    spriteRenderAll();
                    bulletRenderAll();
                    enemyRenderAll();
                }

                /* Weapon switch brightness flash: dim to 13 for a few frames,
                 * then snap back to 15.  Provides visual feedback for weapon cycling. */
                if (weapon_flash_timer > 0) {
                    weapon_flash_timer--;
                    setBrightness(weapon_flash_timer > 0 ? 13 : 15);
                }
                break;

            case STATE_DIALOG:
                /* Update dialog engine (typewriter, input, page advance) */
                dlgUpdate(pad_pressed);
                if (!dlgIsActive()) {
                    /* Dialog finished: return to flight mode.
                     * dlgTransitionOut() already restored BG1 and resumed scroll. */
                    g_game.current_state = STATE_FLIGHT;
                }
                break;

            case STATE_BATTLE:
                /* Update turn-based battle (menus, combat, animations) */
                battleUpdate(pad_pressed);
                if (battle.state == BSTATE_NONE) {
                    /* Battle ended: determine outcome */
                    if (battle.player.hp <= 0) {
                        /* Player defeated: transition to game-over screen.
                         * Screen is already dark from battle's defeat exit. */
                        gsGameOverEnter();
                    } else if (battle.is_boss) {
                        /* Boss defeated: advance to next zone.
                         * Screen is dark from boss exit path.
                         * gsZoneAdvance() handles the zone transition. */
                        gsZoneAdvance();
                    } else {
                        /* Normal victory: battleTransitionOut already restored
                         * flight mode graphics and scroll, so just update state. */
                        g_game.current_state = STATE_FLIGHT;
                    }
                }
                break;

            case STATE_GAMEOVER:
                /* Game-over screen: menu navigation (retry / title) */
                gsGameOverUpdate(pad_pressed);
                break;

            case STATE_VICTORY:
                /* Victory screen: stat count-up animation and "PRESS START" */
                gsVictoryUpdate(pad_pressed);
                break;

            default:
                /* #202: Catch invalid state values - reset to title screen.
                 * This prevents the game from hanging in an undefined state
                 * if memory corruption or a bug sets an invalid current_state. */
                g_game.current_state = STATE_TITLE;
                gsTitleEnter();
                break;
        }

        /* Track play time during active gameplay states.
         * Counts VBlank frames (60 per second at NTSC) and increments
         * play_time_seconds when 60 frames have elapsed.  The counter
         * is capped at 0xFFFF (~18.2 hours) to avoid overflow.
         * Only tracks time during flight, battle, and dialog (not menus). */
        if (g_game.current_state == STATE_FLIGHT ||
            g_game.current_state == STATE_BATTLE ||
            g_game.current_state == STATE_DIALOG) {
            g_game.frame_counter++;
            if (g_game.frame_counter >= 60) {
                g_game.frame_counter = 0;
                if (g_game.play_time_seconds < 0xFFFF) {
                    g_game.play_time_seconds++;
                }
            }
        }

        /* Keep the SPC700 sound driver alive.
         * The SPC700 APU runs independently on its own 1.024 MHz CPU.
         * soundUpdate() sends timing data and checks for pending sample
         * transfers.  MUST be called every frame or audio will stutter/hang. */
        soundUpdate();

        /* Process any deferred VBlank callbacks that were queued during
         * this frame (e.g., delayed DMA transfers, palette updates). */
        vblankProcessCallbacks();
    }

    return 0;  /* Never reached on SNES (no OS to return to) */
}
