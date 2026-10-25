// Nocturne of Time: keeps the mod's progress in the save file, in a "nocturne" section beside OoT's own data:
// the party, gear and items, the moon (Save/NocturneSave), plus the bestiary and the explored maps.
//
// Loading order matters: the save file is read before the game announces the load (OnLoadGame), and the mod's
// modules forget their state on that announcement. So the section is only remembered while reading; on
// OnLoadGame everything is reset and then the remembered section applied. A save without the section (a new
// file, or one from before saving existed) simply starts fresh.

#include "soh/SaveManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

#include "NocturneSave.h"
#include "../Automap.h"
#include "../Party.h"
#include "../Battle/BattleScene.h"
#include "../Moon/Moon.h"
#include "../Moon/MoonHud.h"

#include <optional>

using nlohmann::json;
using namespace Nocturne;

namespace {

std::optional<json> sPending; // the section read from the file being loaded

void InitSave(bool) {
    sPending.reset(); // a new file, or about to read one
}

void LoadSection() {
    json data;
    SaveManager::Instance->LoadData("data", data, json());
    sPending = data;
}

void SaveSection(const SaveContext&, int, bool) {
    json data = Save_Write();
    json bestiary = json::array();
    for (int kind : Battle_Bestiary()) {
        bestiary.push_back(kind);
    }
    data["bestiary"] = bestiary;
    data["maps"] = Automap_Save();
    SaveManager::Instance->SaveData("data", data);
}

void ApplyLoad() {
    // Forget everything from the last play session, then take what the file has
    Battle_ResetForLoad();
    Automap_Reset();
    MoonHud_ResetNotice();
    Party_Reset();
    Moon_Reset();
    if (!sPending) {
        return; // fresh start: the party is created when it's first needed
    }
    const json& data = *sPending;
    Save_Read(data);
    if (data.contains("bestiary") && data["bestiary"].is_array()) {
        for (const json& kind : data["bestiary"]) {
            if (kind.is_number_integer()) {
                Battle_Bestiary().insert(kind.get<int>());
            }
        }
    }
    if (data.contains("maps")) {
        Automap_Load(data["maps"]);
    }
    sPending.reset();
}

void RegisterSaveHooks() {
    static bool sRegistered = false;
    if (!sRegistered) {
        // Saved with every regular save (saveWithBase); version 1 of the section
        SaveManager::Instance->AddInitFunction(InitSave);
        SaveManager::Instance->AddLoadFunction("nocturne", 1, LoadSection);
        SaveManager::Instance->AddSaveFunction("nocturne", 1, SaveSection, true, SECTION_PARENT_NONE);
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>([](int32_t) { ApplyLoad(); });
        sRegistered = true;
    }
}

} // namespace

static RegisterShipInitFunc initFunc(RegisterSaveHooks, {});
