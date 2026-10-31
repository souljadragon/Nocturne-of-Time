#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/cvar_prefixes.h"
#include "AdultTimeline.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

extern "C" void Nocturne_BeforePlayInit(void) {
    if (CVarGetInteger(CVAR_ENHANCEMENT("Nocturne.DungeonCrawler"), 1) == 0 ||
        CVarGetInteger(CVAR_ENHANCEMENT("Nocturne.AdultLink"), 1) == 0) {
        return;
    }
    if (gSaveContext.gameMode != GAMEMODE_NORMAL || gSaveContext.linkAge == LINK_AGE_ADULT) {
        return;
    }
    // The way the game itself grows Link up: swap to the adult equipment set (a first visit gets the
    // vanilla adult defaults), then load the scene as adult. Play_Destroy sees no age change afterwards.
    Inventory_SwapAgeEquipment();
    gSaveContext.linkAge = LINK_AGE_ADULT;
}
