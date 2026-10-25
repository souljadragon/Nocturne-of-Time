#pragma once

// Nocturne of Time: what a save file holds for the mod - the party (stats, levels, EXP, unspent points,
// ailments, equipment), the gear bag, consumables and the moon - as JSON. Pure C++ so it can be tested; the
// game side (SaveHooks.cpp) stores it in the save file's "nocturne" section and adds the bestiary and maps.

#include <nlohmann/json.hpp>

namespace Nocturne {

constexpr int kSaveVersion = 1;

// Everything above, from the current state.
nlohmann::json Save_Write();
// Replace the current state with a saved one. Unknown or damaged parts fall back to a fresh start.
void Save_Read(const nlohmann::json& data);

} // namespace Nocturne
