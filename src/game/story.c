/*==============================================================================
 * Story Scripts & Dialog Triggers - Phase 16
 *
 * Contains all story dialog scripts as ROM data and scroll trigger callbacks.
 * Scripts are triggered by scroll distance during flight mode.
 * Story flags in g_game.story_flags prevent replay of seen dialogs.
 *
 * The story_flags word is divided into two regions:
 *   Lower byte (bits 0-7): Game progress flags (STORY_* from game.h).
 *     Used for zone clear tracking and story branching.
 *   Upper byte (bits 8-15): Dialog trigger flags (SFLAG_* below).
 *     Used to prevent re-showing a dialog when the player loads a save
 *     and replays a zone they've already seen the dialog in.
 *
 * Story triggers are placed between enemy wave triggers in the scroll
 * timeline to avoid overlapping with wave spawns:
 *   Zone 1 waves: 300..4700, Story: 150, 1550, 3300
 *   Zone 2 waves: 300..4700, Story: 1400, 3000
 *   Zone 3 waves: 300..4700, Story: 2050
 *
 * Dialog sequences from the design docs:
 *   - Intro: Commander briefs Vex at mission start
 *   - Zone 1 Mid: Engineer warns about strange readings
 *   - Zone 1 End: Enemy taunts before boss area
 *   - Zone 2 Mid: Commander suspicious, deeper space warnings
 *   - Zone 2 End: Engineer discovers alien signal
 *   - Twist: The truth about the Ark revealed (Zone 3)
 *   - Victory: Two endings based on player choice (Phase 18+)
 *
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#include "game/dialog.h"
#include "engine/scroll.h"

/*=== Story Flags for Dialog Triggers (upper byte of g_game.story_flags) ===*/
/* Each flag prevents its corresponding dialog from replaying after a save/load.
 * These are separate from the game progress flags (STORY_*) in the lower byte. */
#define SFLAG_INTRO_SEEN     0x0100  /* Intro briefing already played */
#define SFLAG_Z1_MID_SEEN    0x0200  /* Zone 1 mid-dialog already played */
#define SFLAG_Z1_END_SEEN    0x0400  /* Zone 1 end-dialog already played */
#define SFLAG_Z2_MID_SEEN    0x0800  /* Zone 2 mid-dialog already played */
#define SFLAG_Z2_END_SEEN    0x1000  /* Zone 2 end-dialog already played */
#define SFLAG_Z3_MID_SEEN    0x2000  /* Zone 3 twist dialog already played */

/*===========================================================================*/
/* Zone 1: Debris Field - Story Scripts                                      */
/*===========================================================================*/

/*--- Intro: Commander briefs Vex at mission start ---*/
/* This is the very first dialog the player sees (triggered at 150px scroll).
 * Establishes the premise: the Ark colony ship needs Vex to clear debris. */
static DialogLine intro_lines[] = {
    { SPEAKER_COMMANDER,
      "Vex, this is Command.",
      "The Ark needs you out there." },
    { SPEAKER_COMMANDER,
      "Debris field ahead. Stay",
      "sharp and clear a path." },
    { SPEAKER_VEX,
      "Copy that, Commander.",
      "Engaging thrusters now." },
};

static DialogScript script_intro = {
    intro_lines, 3  /* 3 pages of dialog */
};

/*--- Zone 1 Mid: Engineer warns about strange readings ---*/
/* Foreshadows the hostile presence discovered later.
 * Triggered at 1550px, between enemy waves 3 and 4. */
static DialogLine z1_mid_lines[] = {
    { SPEAKER_ENGINEER,
      "Vex, I'm reading strange",
      "energy signatures ahead." },
    { SPEAKER_VEX,
      "Hostile?",
      "" },  /* Empty second line for dramatic effect (short response) */
    { SPEAKER_ENGINEER,
      "Unknown. Could be old mines",
      "or... something else." },
    { SPEAKER_COMMANDER,
      "Stay focused. Clear the",
      "sector and report back." },
};

static DialogScript script_z1_mid = {
    z1_mid_lines, 4
};

/*--- Zone 1 End: First enemy contact ---*/
/* The aliens reveal themselves. Triggered at 3300px, before the boss area.
 * Establishes the conflict: the aliens claim their space was invaded. */
static DialogLine z1_end_lines[] = {
    { SPEAKER_ENEMY,
      "Human vessel detected.",
      "You trespass in our space." },
    { SPEAKER_VEX,
      "Who are you? This sector",
      "was supposed to be empty!" },
    { SPEAKER_ENEMY,
      "Your kind always lies.",
      "Prepare to be destroyed." },
    { SPEAKER_COMMANDER,
      "Vex! Enemy contacts!",
      "Weapons free!" },
};

static DialogScript script_z1_end = {
    z1_end_lines, 4
};

/*===========================================================================*/
/* Zone 2: Asteroid Belt - Story Scripts                                     */
/*===========================================================================*/

/*--- Zone 2 Mid: Deeper space, mysterious structure ---*/
/* Builds tension: a large active alien structure is detected.
 * The engineer's concern starts hinting at a larger mystery. */
static DialogLine z2_mid_lines[] = {
    { SPEAKER_COMMANDER,
      "Vex, long range sensors",
      "show a large structure." },
    { SPEAKER_VEX,
      "Another derelict?",
      "" },
    { SPEAKER_COMMANDER,
      "Negative. It's active.",
      "Proceed with caution." },
    { SPEAKER_ENGINEER,
      "The energy readings match",
      "nothing in our database..." },
};

static DialogScript script_z2_mid = {
    z2_mid_lines, 4
};

/*--- Zone 2 End: Engineer discovers the alien signal ---*/
/* Key plot point: the alien signal says "return what is ours".
 * The Commander's evasive response suggests humanity is hiding something. */
static DialogLine z2_end_lines[] = {
    { SPEAKER_ENGINEER,
      "Commander, I've decoded",
      "the alien signal." },
    { SPEAKER_COMMANDER,
      "Not now, Doctor.",
      "" },
    { SPEAKER_ENGINEER,
      "But sir, the signal... it",
      "says 'return what is ours'!" },
    { SPEAKER_VEX,
      "What does that mean?",
      "What did we take?" },
    { SPEAKER_COMMANDER,
      "That's classified. Focus",
      "on the mission, Vex." },
};

static DialogScript script_z2_end = {
    z2_end_lines, 5
};

/*===========================================================================*/
/* Zone 3: Flagship Approach - Story Scripts                                 */
/*===========================================================================*/

/*--- Zone 3 Mid: The Twist ---*/
/* The story's major reveal: humanity stole the Ark's power core from the aliens.
 * The Engineer breaks ranks to tell Vex the truth, revealing that the aliens
 * are fighting to recover what was taken from them, not attacking unprovoked.
 * This dialog sets STORY_TWIST_SEEN which enables future branching paths
 * (Phase 18+: choice between truth and loyalty). */
static DialogLine z3_mid_lines[] = {
    { SPEAKER_ENGINEER,
      "Vex, I need to tell you",
      "the truth. Commander-" },
    { SPEAKER_COMMANDER,
      "Doctor! That is enough!",
      "" },
    { SPEAKER_ENGINEER,
      "The Ark's core... we stole",
      "it from the aliens." },
    { SPEAKER_ENGINEER,
      "Admiral Holt ordered the",
      "raid. 10,000 prisoners..." },
    { SPEAKER_VEX,
      "Is this true, Commander?",
      "" },
    { SPEAKER_COMMANDER,
      "...It was necessary for",
      "humanity's survival." },
    { SPEAKER_SYSTEM,
      "The truth weighs heavy.",
      "Your choice lies ahead." },  /* Narrator foreshadows a future choice */
};

static DialogScript script_z3_mid = {
    z3_mid_lines, 7  /* Longest dialog in the game */
};

/*===========================================================================*/
/* Scroll Trigger Callbacks                                                  */
/*===========================================================================*/
/* Each callback is registered at a specific scroll distance (in pixels)
 * via scrollAddTrigger().  When the scroll engine reaches that distance,
 * the callback fires.  The callback checks whether this dialog has already
 * been seen (via SFLAG_* flag), and if not, sets the flag and assigns
 * g_dialog_pending so the main loop can transition to STATE_DIALOG.
 *
 * The flag-check-then-set pattern is idempotent: if the player somehow
 * triggers the same distance twice, the dialog won't replay. */

/* Zone 1: Intro at 150px (before first enemy wave at 300px) */
static void triggerIntro(void)
{
    if (g_game.story_flags & SFLAG_INTRO_SEEN) return;  /* Already seen */
    g_game.story_flags |= SFLAG_INTRO_SEEN;
    g_dialog_pending = &script_intro;  /* Deferred: main loop will open this */
}

/* Zone 1: Mid dialog at 1550px (between enemy waves 3 and 4) */
static void triggerZ1Mid(void)
{
    if (g_game.story_flags & SFLAG_Z1_MID_SEEN) return;
    g_game.story_flags |= SFLAG_Z1_MID_SEEN;
    g_dialog_pending = &script_z1_mid;
}

/* Zone 1: End dialog at 3300px (between waves 4 and 5, before boss area) */
static void triggerZ1End(void)
{
    if (g_game.story_flags & SFLAG_Z1_END_SEEN) return;
    g_game.story_flags |= SFLAG_Z1_END_SEEN;
    g_dialog_pending = &script_z1_end;
}

/* Zone 2: Mid dialog at 1400px */
static void triggerZ2Mid(void)
{
    if (g_game.story_flags & SFLAG_Z2_MID_SEEN) return;
    g_game.story_flags |= SFLAG_Z2_MID_SEEN;
    g_dialog_pending = &script_z2_mid;
}

/* Zone 2: End dialog at 3000px (the alien signal revelation) */
static void triggerZ2End(void)
{
    if (g_game.story_flags & SFLAG_Z2_END_SEEN) return;
    g_game.story_flags |= SFLAG_Z2_END_SEEN;
    g_dialog_pending = &script_z2_end;
}

/* Zone 3: Mid dialog at 2050px (the twist -- the big story reveal).
 * Also sets STORY_TWIST_SEEN (a game-progress flag in the lower byte)
 * which can be used for future story branching. */
static void triggerZ3Mid(void)
{
    if (g_game.story_flags & SFLAG_Z3_MID_SEEN) return;
    g_game.story_flags |= SFLAG_Z3_MID_SEEN;
    g_game.story_flags |= STORY_TWIST_SEEN;  /* Game progress flag for branching */
    g_dialog_pending = &script_z3_mid;
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

/*
 * storyInit - Initialize the story system.
 *
 * Story flags live in g_game.story_flags which is cleared to 0 by gsInit().
 * The upper byte holds dialog trigger flags (SFLAG_*) and the lower byte
 * holds game progress flags (STORY_*).  Both are saved to SRAM as part
 * of the GameState struct.
 *
 * No additional initialization is needed here; this function exists as
 * a placeholder for potential future story state setup.
 */
void storyInit(void)
{
    /* Story flags live in g_game.story_flags (cleared by gsInit).
     * Dialog trigger flags (SFLAG_*) use upper byte.
     * Game flags (STORY_*) use lower byte. */
}

/*
 * storyRegisterTriggers - Register scroll-distance callbacks for a zone's dialogs.
 *
 * Appends story dialog triggers to the scroll trigger list for the given zone.
 * MUST be called AFTER enemySetupZoneTriggers() because both share the same
 * fixed-size trigger array (MAX_SCROLL_TRIGGERS entries) and we append
 * rather than replace.
 *
 * Trigger distances are chosen to fall between enemy wave trigger distances
 * so that dialog and combat don't overlap.
 *
 * zoneId: ZONE_DEBRIS, ZONE_ASTEROID, or ZONE_FLAGSHIP.
 */
void storyRegisterTriggers(u8 zoneId)
{
    /* NOTE: Do NOT call scrollClearTriggers here!
     * Enemy wave triggers are already registered by enemySetupZoneTriggers().
     * We append story triggers to the same list. */

    switch (zoneId) {
        case ZONE_DEBRIS:
            /* Zone 1: Three dialog events spread across the zone */
            scrollAddTrigger(150, triggerIntro);     /* Before first wave (300px) */
            scrollAddTrigger(1550, triggerZ1Mid);    /* Between waves 3 and 4 */
            scrollAddTrigger(3300, triggerZ1End);    /* Between waves 4 and 5 */
            break;

        case ZONE_ASTEROID:
            /* Zone 2: Two dialog events */
            scrollAddTrigger(1400, triggerZ2Mid);    /* Between waves 3 and 4 */
            scrollAddTrigger(3000, triggerZ2End);    /* Between waves 4 and 5 */
            break;

        case ZONE_FLAGSHIP:
            /* Zone 3: One major dialog (the twist reveal) */
            scrollAddTrigger(2050, triggerZ3Mid);    /* Mid-zone, the big reveal */
            break;
    }
}

/*
 * storyHasFlag - Check if a specific story flag is set.
 *
 * flag:    A STORY_* or SFLAG_* bitmask to check.
 * Returns: 1 if all bits in the flag are set, 0 otherwise.
 */
u8 storyHasFlag(u16 flag)
{
    return (g_game.story_flags & flag) ? 1 : 0;
}

/*
 * storySetFlag - Set a story flag bit.
 *
 * Flags are OR'd in and never cleared during a playthrough.
 * Both STORY_* (game progress) and SFLAG_* (dialog seen) flags
 * use this function.
 *
 * flag: A STORY_* or SFLAG_* bitmask to set.
 */
void storySetFlag(u16 flag)
{
    g_game.story_flags |= flag;
}
