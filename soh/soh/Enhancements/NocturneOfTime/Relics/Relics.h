#pragma once

// Nocturne of Time: Relics - Link's skill items (the Fairy Slingshot, the Kokiri Ocarina, the Deku Shield, later
// the Boomerang, the Bow...), in the spirit of SMT III's Magatama. Pure C++ (no game headers).
//
// - Link equips one Relic at a time, in a slot of its own (not weapon, armor or accessory).
// - A Relic gives no stats. It changes Link's affinities instead, usually with a trade-off (resist Fire, weak
//   to Ice): equipment and Relics never stack stat bonuses.
// - Winning battles with it equipped raises its mastery. Each skill unlocks at a mastery level (usable while
//   the Relic is equipped) and is Link's for good once the Relic's mastery passes that level.
// - Link's sword (Slash) skills don't come from Relics: he learns those by level like the others.

#include "../Battle/BattleEngine.h"

#include <string>
#include <utility>
#include <vector>

namespace Nocturne {

enum class RelicId : int { FairySlingshot, KokiriOcarina, DekuShield, Count };

struct RelicSkill {
    Skill skill;
    int mastery = 0; // unlocked at this mastery level, learned for good at mastery + 1
};

struct RelicDef {
    RelicId id = RelicId::FairySlingshot;
    std::string name;
    std::string description;
    std::vector<std::pair<Element, Affinity>> affinities; // set on the wearer (over its own)
    std::vector<RelicSkill> skills;
    std::vector<int> thresholds; // mastery points needed for mastery 1, 2, ... (cumulative)

    int MaxMastery() const {
        return (int)thresholds.size();
    }
};

const RelicDef& RelicOf(RelicId id);
const RelicDef* RelicByName(const std::string& name);

// Mastery level for `points` (0..MaxMastery), and progress toward the next level (0..1; 1 when mastered).
int RelicMastery(const RelicDef& relic, int points);
float RelicProgress(const RelicDef& relic, int points);
// Points still needed for the next level (0 when mastered).
int RelicPointsToNext(const RelicDef& relic, int points);

// "Rs Pierce  Wk Fire" for the menu
std::string RelicAffinityText(const RelicDef& relic);

} // namespace Nocturne
