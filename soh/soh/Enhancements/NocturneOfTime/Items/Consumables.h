#pragma once

// Nocturne of Time: consumable items (potions, milk, herbs, fairies). Pure C++, shared by battles, the party menu
// and loot drops. The party carries a count of each (see Party_Consumables).

#include "../Battle/BattleEngine.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Nocturne {

enum class ConsumableId : uint8_t {
    RedPotion,
    GreenPotion,
    BluePotion,
    LonLonMilk,
    HylianHerb,
    ZoraWater,
    BottledFairy,
    Count,
};

struct ConsumableDef {
    ConsumableId id;
    std::string name;
    std::string description;
    ItemEffect effect;
    bool battleOnly = false; // e.g. revival: allies get back up after a battle anyway
    int dropWeight = 0;      // relative chance as an enemy drop
};

const ConsumableDef& ConsumableOf(ConsumableId id);

constexpr int kMaxCarry = 20; // per item

// A random consumable drop, weighted (Red Potions common, Blue Potions and Fairies rare).
ConsumableId RollConsumable(const std::function<float()>& rand01);

} // namespace Nocturne
