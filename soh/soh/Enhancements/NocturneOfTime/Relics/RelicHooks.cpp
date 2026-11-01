// Nocturne of Time: where Link finds his Relics in the game (see Relics.h for what they do).
// - Fairy Slingshot: his from the start (Party.cpp).
// - Kokiri Ocarina: turns up after 4 wins in the Hollowed Husk (BattleScene.cpp; given here too for saves that
//   were past that point before Relics existed).
// - Deku Shield: the vanilla Deku Shield. Buying it in the Kokiri Shop (or owning one already) gives the Relic.

#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "../Party.h"
#include "../Boss/BossGate.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
}

using namespace Nocturne;

namespace {

void CheckRelics() {
    if (Party_Members().empty()) {
        return; // the party is made at the first battle or menu
    }
    if (CHECK_OWNED_EQUIP(EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_DEKU)) {
        Party_GiveRelic(RelicId::DekuShield);
    }
    if (BossGate_Wins() >= 4 || BossGate_IsCleared()) {
        Party_GiveRelic(RelicId::KokiriOcarina);
    }
}

void RegisterRelicHooks() {
    bool enabled = CVarGetInteger(CVAR_ENHANCEMENT("Nocturne.DungeonCrawler"), 1) != 0;
    COND_HOOK(OnSceneInit, enabled, [](int16_t) { CheckRelics(); });
    // Buying the shield happens mid-scene: check now and then
    COND_HOOK(OnPlayerUpdate, enabled, []() {
        static int sFrames = 0;
        if (++sFrames % 20 == 0) {
            CheckRelics();
        }
    });
}

RegisterShipInitFunc initFunc(RegisterRelicHooks, { CVAR_ENHANCEMENT("Nocturne.DungeonCrawler") });

} // namespace
