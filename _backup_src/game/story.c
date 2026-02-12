/*==============================================================================
 * Story Scripts & Dialog Triggers - Phase 16
 *
 * Contains all story dialog scripts as ROM data and scroll trigger callbacks.
 * Scripts are triggered by scroll distance during flight mode.
 * Story flags in g_game.story_flags prevent replay of seen dialogs.
 *
 * Story triggers placed between enemy wave triggers:
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
 *   - Twist: The truth about the Ark revealed
 *   - Victory: Two endings based on player choice (Phase 18+)
 *
 * Bank 0 is full; this file auto-overflows to Bank 1 via WLA-DX linker.
 *============================================================================*/

#include "game/dialog.h"
#include "engine/scroll.h"

/*=== Story Flags for Dialog Triggers ===*/
#define SFLAG_INTRO_SEEN     0x0100
#define SFLAG_Z1_MID_SEEN    0x0200
#define SFLAG_Z1_END_SEEN    0x0400
#define SFLAG_Z2_MID_SEEN    0x0800
#define SFLAG_Z2_END_SEEN    0x1000
#define SFLAG_Z3_MID_SEEN    0x2000

/*===========================================================================*/
/* Zone 1: Debris Field - Story Scripts                                      */
/*===========================================================================*/

/*--- Intro: Commander briefs Vex ---*/
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
    intro_lines, 3
};

/*--- Zone 1 Mid: Engineer warns ---*/
static DialogLine z1_mid_lines[] = {
    { SPEAKER_ENGINEER,
      "Vex, I'm reading strange",
      "energy signatures ahead." },
    { SPEAKER_VEX,
      "Hostile?",
      "" },
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

/*--- Zone 1 End: Enemy contact ---*/
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

/*--- Zone 2 Mid: Deeper space ---*/
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

/*--- Zone 2 End: Discovery ---*/
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
      "Your choice lies ahead." },
};

static DialogScript script_z3_mid = {
    z3_mid_lines, 7
};

/*===========================================================================*/
/* Scroll Trigger Callbacks                                                  */
/*===========================================================================*/

/* Zone 1: Intro at 50px (before first enemy wave at 100px) */
static void triggerIntro(void)
{
    if (g_game.story_flags & SFLAG_INTRO_SEEN) return;
    g_game.story_flags |= SFLAG_INTRO_SEEN;
    g_dialog_pending = &script_intro;
}

/* Zone 1: Mid dialog at 450px (between waves 3 and 4) */
static void triggerZ1Mid(void)
{
    if (g_game.story_flags & SFLAG_Z1_MID_SEEN) return;
    g_game.story_flags |= SFLAG_Z1_MID_SEEN;
    g_dialog_pending = &script_z1_mid;
}

/* Zone 1: End dialog at 850px (between waves 4 and 5) */
static void triggerZ1End(void)
{
    if (g_game.story_flags & SFLAG_Z1_END_SEEN) return;
    g_game.story_flags |= SFLAG_Z1_END_SEEN;
    g_dialog_pending = &script_z1_end;
}

/* Zone 2: Mid dialog at 450px */
static void triggerZ2Mid(void)
{
    if (g_game.story_flags & SFLAG_Z2_MID_SEEN) return;
    g_game.story_flags |= SFLAG_Z2_MID_SEEN;
    g_dialog_pending = &script_z2_mid;
}

/* Zone 2: End dialog at 850px */
static void triggerZ2End(void)
{
    if (g_game.story_flags & SFLAG_Z2_END_SEEN) return;
    g_game.story_flags |= SFLAG_Z2_END_SEEN;
    g_dialog_pending = &script_z2_end;
}

/* Zone 3: Mid dialog (the twist) at 450px */
static void triggerZ3Mid(void)
{
    if (g_game.story_flags & SFLAG_Z3_MID_SEEN) return;
    g_game.story_flags |= SFLAG_Z3_MID_SEEN;
    g_game.story_flags |= STORY_TWIST_SEEN;
    g_dialog_pending = &script_z3_mid;
}

/*===========================================================================*/
/* Public API                                                                */
/*===========================================================================*/

void storyInit(void)
{
    /* Story flags live in g_game.story_flags (cleared by gsInit).
     * Dialog trigger flags (SFLAG_*) use upper byte.
     * Game flags (STORY_*) use lower byte. */
}

void storyRegisterTriggers(u8 zoneId)
{
    /* NOTE: Do NOT call scrollClearTriggers here!
     * Enemy wave triggers are already registered.
     * Just append story triggers. */

    switch (zoneId) {
        case ZONE_DEBRIS:
            scrollAddTrigger(150, triggerIntro);
            scrollAddTrigger(1550, triggerZ1Mid);
            scrollAddTrigger(3300, triggerZ1End);
            break;

        case ZONE_ASTEROID:
            scrollAddTrigger(1400, triggerZ2Mid);
            scrollAddTrigger(3000, triggerZ2End);
            break;

        case ZONE_FLAGSHIP:
            scrollAddTrigger(2050, triggerZ3Mid);
            break;
    }
}

u8 storyHasFlag(u16 flag)
{
    return (g_game.story_flags & flag) ? 1 : 0;
}

void storySetFlag(u16 flag)
{
    g_game.story_flags |= flag;
}
