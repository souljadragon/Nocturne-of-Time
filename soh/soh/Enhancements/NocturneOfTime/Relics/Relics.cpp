#include "Relics.h"

#include <algorithm>

namespace Nocturne {

namespace {

Skill Sk(const char* name, Element element, TargetType target, int power, int mpCost, const char* description,
         int hpCost = 0, float accuracy = 0.95f) {
    Skill s;
    s.name = name;
    s.element = element;
    s.target = target;
    s.power = power;
    s.mpCost = mpCost;
    s.hpCost = hpCost;
    s.accuracy = accuracy;
    s.description = description;
    return s;
}

Skill Stage(const char* name, TargetType target, int atk, int def, int agi, int mpCost, const char* description) {
    Skill s = Sk(name, Element::Almighty, target, 0, mpCost, description, 0, 1.0f);
    s.buffAtk = atk;
    s.buffDef = def;
    s.buffAgi = agi;
    return s;
}

std::vector<RelicDef> Build() {
    std::vector<RelicDef> list;

    // Link's from the start. Its first skill is learned for good after a battle or two, so he keeps a Pierce
    // attack whatever he equips later.
    RelicDef slingshot;
    slingshot.id = RelicId::FairySlingshot;
    slingshot.name = "Fairy Slingshot";
    slingshot.description = "A Kokiri slingshot strung with fairy silk. Every child in the forest had one.";
    slingshot.affinities = { { Element::Pierce, Affinity::Resist }, { Element::Fire, Affinity::Weak } };
    slingshot.skills = {
        { Sk("Slingshot", Element::Pierce, TargetType::OneEnemy, 10, 0, "A Deku Seed shot. Pierces; good against fliers.",
             0, 0.9f),
          0 },
        { Sk("Seed Scatter", Element::Pierce, TargetType::AllEnemies, 7, 3, "A spray of Deku Seeds at every foe."), 1 },
        { Sk("Piercing Shot", Element::Pierce, TargetType::OneEnemy, 20, 5,
             "A seed loosed with all the slingshot's strength.", 0, 0.9f),
          2 },
    };
    slingshot.thresholds = { 40, 140, 300 };
    list.push_back(slingshot);

    // Found in the Hollowed Husk: the answer to Queen Gohma's shell
    RelicDef ocarina;
    ocarina.id = RelicId::KokiriOcarina;
    ocarina.name = "Kokiri Ocarina";
    ocarina.description = "A wooden ocarina carved by the Kokiri. The old songs still live in it.";
    ocarina.affinities = { { Element::Light, Affinity::Resist }, { Element::Shadow, Affinity::Weak } };
    {
        Skill sun = Stage("Dispelling Sun", TargetType::AllEnemies, 0, 0, 0, 5,
                          "A flash of the Sun's Song strips every foe of its blessings.");
        sun.dispelBuffs = true;
        ocarina.skills = {
            { Stage("Din's Blessing", TargetType::AllAllies, 1, 0, 0, 4, "Din's fire in every arm: the party's attack rises."),
              0 },
            { sun, 1 },
            { Sk("Song of Storms", Element::Shock, TargetType::AllEnemies, 10, 7, "Thunder answers the song: lightning on every foe."),
              2 },
            { Sk("Prelude of Light", Element::Light, TargetType::OneEnemy, 22, 8, "A blinding chord of light on one foe."), 3 },
        };
    }
    ocarina.thresholds = { 60, 180, 360, 600 };
    list.push_back(ocarina);

    // Bought in the Kokiri Shop (the vanilla Deku Shield): wood holds blades and clubs, but it burns
    RelicDef shield;
    shield.id = RelicId::DekuShield;
    shield.name = "Deku Shield";
    shield.description = "A wooden shield from the Kokiri Shop. It burns, but it holds.";
    shield.affinities = { { Element::Slash, Affinity::Resist },
                          { Element::Strike, Affinity::Resist },
                          { Element::Fire, Affinity::Weak } };
    {
        Skill bash = Sk("Shield Bash", Element::Strike, TargetType::OneEnemy, 12, 0, "A shove with the shield. May stun.", 4);
        bash.inflict = Ailment::Stun;
        bash.inflictChance = 0.2f;
        shield.skills = {
            { bash, 0 },
            { Stage("Shield Wall", TargetType::AllAllies, 0, 1, 0, 4, "Link stands in front: the party's defense rises."), 1 },
            { Sk("Shield Charge", Element::Strike, TargetType::AllEnemies, 9, 0, "A charge through every foe, shield first.", 8),
              2 },
        };
    }
    shield.thresholds = { 50, 160, 340 };
    list.push_back(shield);

    return list;
}

} // namespace

const RelicDef& RelicOf(RelicId id) {
    static const std::vector<RelicDef> relics = Build();
    int i = std::clamp((int)id, 0, (int)relics.size() - 1);
    return relics[i];
}

const RelicDef* RelicByName(const std::string& name) {
    for (int i = 0; i < (int)RelicId::Count; i++) {
        if (RelicOf((RelicId)i).name == name) {
            return &RelicOf((RelicId)i);
        }
    }
    return nullptr;
}

int RelicMastery(const RelicDef& relic, int points) {
    int level = 0;
    for (int t : relic.thresholds) {
        if (points >= t) {
            level++;
        }
    }
    return level;
}

float RelicProgress(const RelicDef& relic, int points) {
    int level = RelicMastery(relic, points);
    if (level >= relic.MaxMastery()) {
        return 1.0f;
    }
    int from = level == 0 ? 0 : relic.thresholds[level - 1];
    int to = relic.thresholds[level];
    return std::clamp((float)(points - from) / (float)std::max(1, to - from), 0.0f, 1.0f);
}

int RelicPointsToNext(const RelicDef& relic, int points) {
    int level = RelicMastery(relic, points);
    return level >= relic.MaxMastery() ? 0 : relic.thresholds[level] - points;
}

std::string RelicAffinityText(const RelicDef& relic) {
    std::string out;
    for (const auto& [element, affinity] : relic.affinities) {
        out += (out.empty() ? "" : " ") + std::string(AffinityName(affinity)) + " " + ElementName(element);
    }
    return out;
}

} // namespace Nocturne
