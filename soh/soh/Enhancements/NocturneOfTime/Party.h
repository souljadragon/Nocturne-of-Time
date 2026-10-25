#pragma once

// Nocturne of Time: the party (stats, XP, equipment) and the shared item bag. Pure C++ (no game headers), shared
// by battles and the equipment menu. Lives for the play session; saving comes with the save-data work.

#include "Battle/BattleEngine.h"
#include "Loot/Loot.h"
#include "Items/Consumables.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace Nocturne {

struct PartyMember {
    Combatant base; // stats without equipment; base.hp / base.mp are the *current* HP/MP
    int xp = 0;
    int unspentStatPoints = 0; // from level-ups, waiting for the player to place them
    std::array<std::optional<Item>, (size_t)Slot::Count> gear;
};

std::vector<PartyMember>& Party_Members();
std::vector<Item>& Party_Bag();

// Consumables carried, by ConsumableId. A new party starts with a few Red Potions and a Hylian Herb.
std::array<int, (size_t)ConsumableId::Count>& Party_Consumables();
// Add up to `count` (stopping at kMaxCarry); returns how many were added.
int Party_AddConsumable(ConsumableId id, int count = 1);
// Use one outside battle on `member` (every member for party-wide items). Returns what happened; empty if it
// would do nothing, in which case nothing is used up.
std::vector<std::string> Party_UseConsumable(ConsumableId id, int member);

// Create Link and Saria (with starting gear) if the party doesn't exist yet.
void Party_EnsureCreated(int startLevel);
void Party_Reset();
void Party_HealAll(); // also cures ailments

// The member's battle stats: base stats plus equipment, current HP/MP clamped to the new maximums.
Combatant Party_Effective(const PartyMember& member);

// Is any party member wearing an item with this effect?
bool Party_HasEffect(UniqueEffect effect);

// Equip bag item `bagIndex` on `member` (the replaced item goes back to the bag). False if they can't wear it.
bool Party_Equip(int member, int bagIndex);
// Move the item in `slot` back to the bag.
void Party_Unequip(int member, Slot slot);

// Keep current HP/MP within the (equipment-adjusted) maximums after gear changes.
void Party_ClampToMax(PartyMember& member);

} // namespace Nocturne
