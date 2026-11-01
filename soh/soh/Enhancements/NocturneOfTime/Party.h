#pragma once

// Nocturne of Time: the party (stats, XP, equipment) and the shared item bag. Pure C++ (no game headers), shared
// by battles and the equipment menu. Lives for the play session; saving comes with the save-data work.

#include "Battle/BattleEngine.h"
#include "Loot/Loot.h"
#include "Items/Consumables.h"
#include "Relics/Relics.h"

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
    // Skills taken into battle (names, at most kMaxActiveSkills; the basic Attack is always there). Skills learned
    // later join automatically while there's room; `seenSkills` remembers which ones were offered, so a skill
    // the player took off stays off.
    std::vector<std::string> activeSkills;
    std::vector<std::string> seenSkills;
};

// As many skills as SMT III lets the Demi-Fiend and each demon carry
constexpr int kMaxActiveSkills = 8;

// A skill a member could take into battle
struct PoolSkill {
    Skill skill;
    bool fromRelic = false; // taught by a Relic
    bool permanent = true;  // false: only while that Relic is equipped (not mastered yet)
};

// Every skill `member` can choose from: level-learned ones and, for Link, Relic skills (mastered ones for good,
// the equipped Relic's unlocked ones for now). Excludes the basic Attack.
std::vector<PoolSkill> Party_SkillPool(const PartyMember& member);
// Add newly available skills to the active list while there's room, and drop ones no longer available.
void Party_RefreshSkills(PartyMember& member);
void Party_RefreshAllSkills();
// Take a pool skill into battle (false if the list is full or it isn't available) / leave it out.
bool Party_ActivateSkill(PartyMember& member, const std::string& name);
void Party_DeactivateSkill(PartyMember& member, const std::string& name);
bool Party_IsActive(const PartyMember& member, const std::string& name);

// ---- Relics (Link's) ----
struct RelicState {
    bool owned = false;
    int points = 0; // mastery points
};
std::array<RelicState, (size_t)RelicId::Count>& Party_Relics();
// The Relic Link has equipped (-1: none)
int Party_EquippedRelic();
// Give Link a Relic (true if it's new). The first Relic he gets is equipped.
bool Party_GiveRelic(RelicId id);
bool Party_EquipRelic(RelicId id); // false if not owned
void Party_UnequipRelic();
// A battle was won: the equipped Relic gains `points` of mastery. Returns a line per mastery level reached, for
// the battle to show ("Fairy Slingshot reached Mastery level 1! Slingshot is now learned.").
std::vector<std::string> Party_AddRelicMastery(int points);

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
