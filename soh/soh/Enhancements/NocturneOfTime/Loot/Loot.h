#pragma once

// Nocturne of Time: randomized equipment (Diablo / Path of Exile style), independent of the game engine.
//
// An item is a base type (Kokiri Sword, Leaf Robe, Deku Charm...) plus a rarity and random affixes:
//   Common (white)     no affixes
//   Uncommon (blue)    1-2 affixes, named "Prefix Base of Suffix"
//   Rare (purple)      3-4 affixes, a random two-part name ("Gloom Fang")
//   Legendary (orange) hand-made items with fixed stats and a unique effect

#include "../Battle/BattleEngine.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Nocturne {

enum class Rarity : uint8_t { Common, Uncommon, Rare, Legendary, Count };
const char* RarityName(Rarity r);

struct Rgb {
    uint8_t r, g, b;
};
Rgb RarityColor(Rarity r); // white, blue, purple, orange

enum class Slot : uint8_t { Weapon, Armor, Accessory, Count };
const char* SlotName(Slot s);

// Who can equip a base. Per character for the slice; archetypes (tank, mage...) later.
enum class Wielder : uint8_t { Link, Saria, Anyone };

enum class StatKind : uint8_t {
    St,
    Ma,
    Vi,
    Ag,
    Lu,
    MaxHp,
    MaxMp,
    WeaponPower, // added to physical skills' power
    MagicPower,  // added to magic skills' power and heals
    CritPct,     // + critical chance, in percent
    HealPct,     // + healing, in percent
    Resist,      // resist `element` (covers a weakness)
};

struct StatMod {
    StatKind kind = StatKind::St;
    int value = 0;
    Element element = Element::Slash; // for Resist
};

enum class UniqueEffect : uint8_t {
    None,
    AutoAnalyze, // all foes' affinities are known at the start of a battle
};

struct ItemBase {
    int id = 0;
    std::string name;
    Slot slot = Slot::Weapon;
    Wielder wielder = Wielder::Anyone;
    int tier = 1; // 1 = early dungeon ... 3 = later
    std::vector<StatMod> mods;
    bool legendary = false;
    UniqueEffect effect = UniqueEffect::None;
    std::string effectText;
};

struct Item {
    uint32_t uid = 0; // unique per item, for the inventory
    int baseId = 0;
    Rarity rarity = Rarity::Common;
    int level = 1;
    std::string name;
    std::vector<StatMod> mods; // base stats followed by affixes
};

using LootRng = std::function<float()>; // uniform in [0, 1)

const std::vector<ItemBase>& AllBases();
const ItemBase& BaseOf(const Item& item);

// Rarity odds. `luck` in [0, 1] shifts them toward rarer results (the moon phase will drive this).
Rarity RollRarity(float luck, const LootRng& rng);

// A random item suited to `itemLevel` (roughly the enemy level). `rarity` forces a tier when given.
Item RollItem(int itemLevel, float luck, const LootRng& rng);
Item RollItemOfRarity(int itemLevel, Rarity rarity, const LootRng& rng);

// A specific base with no affixes (starting gear).
Item MakeCommon(int baseId);

bool CanEquip(const Item& item, const std::string& memberName);

// Apply equipped items to a combatant's stats (call on a copy of the member's base stats).
void ApplyGear(Combatant& c, const std::vector<const Item*>& gear);

// One line per stat, e.g. "Power +6", "Max HP +10", "Resist Fire".
std::vector<std::string> DescribeItem(const Item& item);
std::string DescribeMod(const StatMod& mod);

// Base-type ids used for starting equipment.
namespace BaseId {
constexpr int KokiriSword = 1;
constexpr int KokiriTunic = 10;
constexpr int ForestStaff = 4;
constexpr int LeafRobe = 13;
} // namespace BaseId

} // namespace Nocturne
