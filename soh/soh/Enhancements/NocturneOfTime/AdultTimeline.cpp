#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/cvar_prefixes.h"
#include "AdultTimeline.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "src/overlays/actors/ovl_Bg_Treemouth/z_bg_treemouth.h"
}

namespace {

bool AdultTimelineOn() {
    return CVarGetInteger(CVAR_ENHANCEMENT("Nocturne.DungeonCrawler"), 1) != 0 &&
           CVarGetInteger(CVAR_ENHANCEMENT("Nocturne.AdultLink"), 1) != 0;
}

void RegisterAdultTimeline() {
    // The Deku Tree (Hollowed Husk) keeps its mouth open for adult Link, as the randomizer does when adult
    // can reach the dungeon: the mouth's collision moves out of the way and the entrance is walkable.
    COND_HOOK(OnActorInit, AdultTimelineOn(), [](void* actorRef) {
        Actor* actor = static_cast<Actor*>(actorRef);
        if (actor->id == ACTOR_BG_TREEMOUTH && LINK_IS_ADULT) {
            static_cast<BgTreemouth*>(actorRef)->unk_168 = 1.0f;
        }
    });
}

RegisterShipInitFunc initFunc(RegisterAdultTimeline,
                              { CVAR_ENHANCEMENT("Nocturne.DungeonCrawler"), CVAR_ENHANCEMENT("Nocturne.AdultLink") });

} // namespace

extern "C" int Nocturne_WalletCapacity(int vanillaCapacity) {
    return CVarGetInteger(CVAR_ENHANCEMENT("Nocturne.DungeonCrawler"), 1) != 0 ? NOCTURNE_WALLET_MAX : vanillaCapacity;
}

extern "C" int Nocturne_AdultTimelineOn(void) {
    return AdultTimelineOn() ? 1 : 0;
}

extern "C" void Nocturne_BeforePlayInit(void) {
    if (!AdultTimelineOn()) {
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
