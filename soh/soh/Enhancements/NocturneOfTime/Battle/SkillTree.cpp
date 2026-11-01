#include "SkillTree.h"

namespace Nocturne {

namespace {

Skill Sk(const char* name, Element element, TargetType target, int power, int mpCost, const char* description) {
    Skill s;
    s.name = name;
    s.element = element;
    s.target = target;
    s.power = power;
    s.mpCost = mpCost;
    s.description = description;
    return s;
}

Skill Heal(const char* name, TargetType target, int power, int mpCost, const char* description) {
    Skill s = Sk(name, Element::Almighty, target, power, mpCost, description);
    s.heal = true;
    s.accuracy = 1.0f;
    return s;
}

Skill Stages(const char* name, TargetType target, int atk, int def, int agi, int mpCost, const char* description) {
    Skill s = Sk(name, Element::Almighty, target, 0, mpCost, description);
    s.accuracy = 1.0f;
    s.buffAtk = atk;
    s.buffDef = def;
    s.buffAgi = agi;
    return s;
}

TreeNode Active(const char* id, int branch, int tier, int cost, Skill skill) {
    TreeNode n;
    n.id = id;
    n.name = skill.name;
    n.branch = branch;
    n.tier = tier;
    n.cost = cost;
    n.description = skill.description;
    n.skill = skill;
    return n;
}

TreeNode Upgrade(const char* id, int branch, int tier, int cost, Skill skill, const char* replaces, const char* needs) {
    TreeNode n = Active(id, branch, tier, cost, skill);
    n.replaces = replaces;
    n.needs = needs;
    return n;
}

TreeNode Passive(const char* id, const char* name, int branch, int tier, int cost, PassiveKind kind,
                 const char* description) {
    TreeNode n;
    n.id = id;
    n.name = name;
    n.branch = branch;
    n.tier = tier;
    n.cost = cost;
    n.passive = kind;
    n.description = description;
    return n;
}

SkillTree BuildSaria() {
    SkillTree t;
    t.member = "Saria";
    t.branches = { "Song of Healing", "Forest Winds", "Fairy Ward" };

    // ---- Song of Healing: the healer
    t.nodes.push_back(Active("minuet", 0, 1, 1,
                             Heal("Minuet", TargetType::AllAllies, 25, 10,
                                  "The Minuet of Forest restores the whole party's HP.")));
    t.nodes.push_back(Passive("heal_boost", "Healing Boost", 0, 1, 1, PassiveKind::HealBoost,
                              "Saria's healing restores 15% more."));
    t.nodes.push_back(Upgrade("restoration", 0, 2, 2,
                              Heal("Song of Restoration", TargetType::OneAlly, 80, 7,
                                   "A fuller song of healing: restores much of one ally's HP."),
                              "Song of Healing", ""));
    {
        Skill soothe = Stages("Soothing Song", TargetType::AllAllies, 0, 0, 0, 6,
                              "A calm melody that cures the party of poison and burns.");
        soothe.curesAilments = true;
        t.nodes.push_back(Active("soothing", 0, 2, 2, soothe));
    }
    t.nodes.push_back(Upgrade("grand_minuet", 0, 3, 3,
                              Heal("Grand Minuet", TargetType::AllAllies, 55, 16,
                                   "The Minuet played in full: restores much of the party's HP."),
                              "Minuet", "minuet"));
    {
        Skill call = Heal("Fairy's Call", TargetType::OneAlly, 0, 12, "Calls a fairy to a fallen ally: revives them at half HP.");
        call.revive = 0.5f;
        t.nodes.push_back(Active("fairys_call", 0, 3, 3, call));
    }
    {
        Skill renewal = Heal("Minuet of Renewal", TargetType::AllAllies, 0, 40,
                             "The forest's oldest song: fully heals and revives the whole party.");
        renewal.revive = 1.0f;
        renewal.fullHeal = true;
        t.nodes.push_back(Active("renewal", 0, 4, 5, renewal));
    }

    // ---- Forest Winds: damage
    t.nodes.push_back(Active("forest_gust", 1, 1, 1,
                             Sk("Forest Gust", Element::Wind, TargetType::AllEnemies, 9, 8,
                                "A forest wind that sweeps over every foe.")));
    t.nodes.push_back(Passive("wind_boost", "Wind Boost", 1, 1, 1, PassiveKind::WindBoost,
                              "Saria's Wind skills deal 20% more damage."));
    t.nodes.push_back(Upgrade("gust_blade", 1, 2, 2,
                              Sk("Gust Blade", Element::Wind, TargetType::OneEnemy, 24, 7,
                                 "A honed blade of wind on one foe."),
                              "Gale", ""));
    t.nodes.push_back(Upgrade("tempest", 1, 3, 3,
                              Sk("Tempest", Element::Wind, TargetType::OneEnemy, 38, 11,
                                 "A howling storm wind on one foe."),
                              "Gust Blade", "gust_blade"));
    t.nodes.push_back(Upgrade("whirlwind", 1, 3, 3,
                              Sk("Whirlwind", Element::Wind, TargetType::AllEnemies, 16, 14,
                                 "A whirling wind that tears through every foe."),
                              "Forest Gust", "forest_gust"));
    t.nodes.push_back(Passive("piercing_wind", "Piercing Wind", 1, 3, 3, PassiveKind::PierceWind,
                              "Foes that resist Wind take full damage from Saria."));
    t.nodes.push_back(Active("kokiri_gale", 1, 4, 5,
                             Sk("Kokiri Gale", Element::Wind, TargetType::AllEnemies, 30, 28,
                                "The whole forest's breath, loosed on every foe.")));

    // ---- Fairy Ward: support
    t.nodes.push_back(Active("farore", 2, 1, 1,
                             Stages("Farore's Blessing", TargetType::AllAllies, 0, 0, 1, 4,
                                    "Farore's wind at the party's heels: agility rises.")));
    t.nodes.push_back(Active("withering", 2, 1, 1,
                             Stages("Withering Hex", TargetType::AllEnemies, -1, 0, 0, 4,
                                    "Saps the strength from every foe: their attack falls.")));
    t.nodes.push_back(Active("brittle", 2, 2, 2,
                             Stages("Brittle Hex", TargetType::AllEnemies, 0, -1, 0, 4,
                                    "Rot creeps into every foe's hide: their defense falls.")));
    t.nodes.push_back(Passive("fairy_ward", "Fairy Ward", 2, 2, 2, PassiveKind::AilmentWard,
                              "A fairy's glow keeps Saria from being poisoned or burned."));
    t.nodes.push_back(Active("withering_bloom", 2, 3, 3,
                             Stages("Withering Bloom", TargetType::AllEnemies, -1, -1, -1, 16,
                                    "Spores of decay: every foe's attack, defense and agility fall.")));
    t.nodes.push_back(Passive("first_light", "First Light", 2, 3, 3, PassiveKind::FirstLight,
                              "Saria's first skill in a battle costs only half a Press Turn."));
    t.nodes.push_back(Active("great_fairy", 2, 4, 5,
                             Stages("Great Fairy's Blessing", TargetType::AllAllies, 1, 1, 1, 20,
                                    "The Great Fairy's laughter: the party's attack, defense and agility rise.")));
    return t;
}

} // namespace

const SkillTree* TreeFor(const std::string& member) {
    static const SkillTree saria = BuildSaria();
    if (member == "Saria") {
        return &saria;
    }
    return nullptr;
}

const TreeNode* TreeNodeById(const SkillTree& tree, const std::string& id) {
    for (const TreeNode& n : tree.nodes) {
        if (n.id == id) {
            return &n;
        }
    }
    return nullptr;
}

int TierLevel(int tier) {
    static const int kLevels[] = { 1, 1, 8, 16, 25 };
    return kLevels[tier < 0 ? 0 : (tier > 4 ? 4 : tier)];
}

} // namespace Nocturne
