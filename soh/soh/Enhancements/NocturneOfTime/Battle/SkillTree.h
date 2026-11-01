#pragma once

// Nocturne of Time: skill trees for the companions (Link learns from Relics instead). Pure C++.
// - A few core skills come with levels (in each member's template, BattleData.cpp).
// - Everything else is bought with skill points (1 per level from level 2, plus bonus points) in three
//   branches: active skills, upgrades (which replace the skill they upgrade, SMT's Bufu -> Bufula -> Bufudyne)
//   and passives. Each branch ends in a capstone.
// - A node needs the member's level for its tier, and one node of the tier before it in the same branch (an
//   upgrade needs the skill it upgrades).
// See design/saria-skill-tree.md in the project.

#include "BattleEngine.h"

#include <string>
#include <vector>

namespace Nocturne {

enum class PassiveKind { None, HealBoost, WindBoost, PierceWind, AilmentWard, FirstLight };

struct TreeNode {
    std::string id;     // stable id for saves
    std::string name;   // as shown
    int branch = 0;     // 0..2
    int tier = 1;       // 1..4 (4 = capstone)
    int cost = 1;
    Skill skill;        // actives and upgrades
    std::string replaces; // upgrades: the skill (name) this one replaces
    std::string needs;    // upgrades: the node (id) that must be learned first
    PassiveKind passive = PassiveKind::None;
    std::string description;

    bool IsPassive() const {
        return passive != PassiveKind::None;
    }
};

struct SkillTree {
    std::string member;
    std::vector<std::string> branches; // names
    std::vector<TreeNode> nodes;
};

// The tree of a party member, or nullptr (Link has none)
const SkillTree* TreeFor(const std::string& member);
const TreeNode* TreeNodeById(const SkillTree& tree, const std::string& id);

// Level a tier opens at: 1, 8, 16, 25
int TierLevel(int tier);
constexpr int kTreeTiers = 4;

// Points a member has earned by `level` (1 per level from level 2), before bonus points
inline int TreePointsForLevel(int level) {
    return level > 1 ? level - 1 : 0;
}

} // namespace Nocturne
