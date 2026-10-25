#include "BattleData.h"

#include <algorithm>

namespace Nocturne {

namespace {

constexpr int kLevelCap = 99;

Skill Attack(Element element, int power) {
    Skill s;
    s.name = "Attack";
    s.element = element;
    s.power = power;
    return s;
}

Skill Make(const char* name, Element element, TargetType target, int power, int mpCost, int hpCost = 0,
           float accuracy = 0.95f, bool heal = false) {
    Skill s;
    s.name = name;
    s.element = element;
    s.target = target;
    s.power = power;
    s.mpCost = mpCost;
    s.hpCost = hpCost;
    s.accuracy = accuracy;
    s.heal = heal;
    return s;
}

Skill Inflicts(Skill s, Ailment a, float chance) {
    s.inflict = a;
    s.inflictChance = chance;
    return s;
}

Skill Describe(Skill s, const char* description) {
    s.description = description;
    return s;
}

void SetAffinity(Combatant& c, Element e, Affinity a) {
    c.affinity[(size_t)e] = a;
}

// Archetype growth: HP/MP per level and how strongly each stat attracts automatic points (St Ma Vi Ag Lu)
struct Growth {
    int hp, mp;
    std::array<float, (size_t)Stat::Count> weights;
};

Growth GrowthOf(const Combatant& c) {
    if (c.name == "Saria") {
        return { 5, 4, { 0.4f, 4.0f, 2.0f, 2.0f, 1.6f } }; // druid: Magic first, then Vitality/Agility
    }
    return { 7, 1, { 3.2f, 0.7f, 2.4f, 2.0f, 1.2f } }; // Link / default hero: balanced, Strength first
}

Stat TopStat(const Combatant& c) {
    Growth g = GrowthOf(c);
    size_t best = 0;
    for (size_t i = 1; i < g.weights.size(); i++) {
        if (g.weights[i] > g.weights[best]) {
            best = i;
        }
    }
    return (Stat)best;
}

// Grow a party member from level 1 to `level`. Deterministic (fixed seed) so a new party is always the same;
// the free points go to the archetype's main stat.
void GrowTo(Combatant& c, int level) {
    uint32_t seed = 12345u + (uint32_t)c.name.size();
    auto rng = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return (float)((seed >> 8) & 0xFFFF) / 65536.0f;
    };
    while (c.level < level) {
        LevelGains g = LevelUp(c, rng);
        if (!g.leveled) {
            break;
        }
        if (GetStat(c, TopStat(c)) < kStatCap) {
            AddStat(c, TopStat(c), kManualStatPoints);
        }
    }
    c.hp = c.maxHp;
    c.mp = c.maxMp;
}

} // namespace

Combatant MakeLink(int level) {
    // Hero: balanced, sword and slingshot
    Combatant c;
    c.name = "Link";
    c.level = 1;
    c.maxHp = 50;
    c.maxMp = 6;
    c.st = 5;
    c.ma = 1;
    c.vi = 3;
    c.ag = 3;
    c.lu = 2;
    c.skills = {
        Describe(Attack(Element::Slash, 12), "A sword strike."),
        Describe(Make("Spin Attack", Element::Slash, TargetType::AllEnemies, 9, 0, 6),
                 "A whirling slash that hits every foe. Costs a little HP."),
        Describe(Make("Slingshot", Element::Pierce, TargetType::OneEnemy, 10, 0, 0, 0.9f),
                 "A Deku Seed shot. Pierces; good against fliers."),
    };
    GrowTo(c, level);
    return c;
}

Combatant MakeSaria(int level) {
    // Druid: wind magic and healing; staff blows are weak
    Combatant c;
    c.name = "Saria";
    c.level = 1;
    c.maxHp = 34;
    c.maxMp = 28;
    c.st = 1;
    c.ma = 5;
    c.vi = 2;
    c.ag = 3;
    c.lu = 3;
    SetAffinity(c, Element::Wind, Affinity::Drain);
    SetAffinity(c, Element::Strike, Affinity::Resist);
    SetAffinity(c, Element::Fire, Affinity::Weak);
    c.skills = {
        Describe(Attack(Element::Strike, 8), "A swing of Saria's staff."),
        Describe(Make("Gale", Element::Wind, TargetType::OneEnemy, 14, 4), "A blade of wind strikes one foe."),
        Describe(Make("Forest Gust", Element::Wind, TargetType::AllEnemies, 9, 8),
                 "A forest wind that sweeps over every foe."),
        Describe(Make("Song of Healing", Element::Almighty, TargetType::OneAlly, 40, 5, 0, 1.0f, true),
                 "A gentle tune that restores one ally's HP."),
        Describe(Make("Minuet", Element::Almighty, TargetType::AllAllies, 25, 10, 0, 1.0f, true),
                 "The Minuet of Forest restores the whole party's HP."),
    };
    GrowTo(c, level);
    return c;
}

Combatant MakeEnemy(EnemyKind kind) {
    Combatant c;
    c.isEnemy = true;
    c.kind = (int)kind;

    switch (kind) {
        case EnemyKind::DekuBaba:
            c.name = "Deku Baba";
            c.level = 3;
            c.maxHp = 40;
            c.st = 7;
            c.ma = 1;
            c.vi = 4;
            c.ag = 4;
            c.lu = 3;
            SetAffinity(c, Element::Slash, Affinity::Weak);
            SetAffinity(c, Element::Fire, Affinity::Weak);
            SetAffinity(c, Element::Strike, Affinity::Resist);
            SetAffinity(c, Element::Wind, Affinity::Drain);
            c.skills = { Make("Bite", Element::Pierce, TargetType::OneEnemy, 10, 0) };
            c.xpReward = 20;
            c.rupeeMin = 5;
            c.rupeeMax = 10;
            break;

        case EnemyKind::Skulltula:
            c.name = "Skulltula";
            c.level = 4;
            c.maxHp = 55;
            c.st = 8;
            c.ma = 2;
            c.vi = 6;
            c.ag = 3;
            c.lu = 3;
            SetAffinity(c, Element::Slash, Affinity::Resist);
            SetAffinity(c, Element::Strike, Affinity::Weak);
            SetAffinity(c, Element::Fire, Affinity::Weak);
            SetAffinity(c, Element::Pierce, Affinity::Null);
            c.skills = {
                Make("Ram", Element::Strike, TargetType::OneEnemy, 12, 0),
                Make("Spin", Element::Strike, TargetType::AllEnemies, 7, 0),
                Inflicts(Make("Venom Fang", Element::Pierce, TargetType::OneEnemy, 8, 0), Ailment::Poison, 0.4f),
            };
            c.xpReward = 28;
            c.rupeeMin = 8;
            c.rupeeMax = 15;
            break;

        case EnemyKind::FireKeese:
        default:
            c.name = "Fire Keese";
            c.kind = (int)EnemyKind::FireKeese;
            c.level = 3;
            c.maxHp = 30;
            c.maxMp = 20;
            c.st = 5;
            c.ma = 6;
            c.vi = 3;
            c.ag = 10;
            c.lu = 5;
            SetAffinity(c, Element::Pierce, Affinity::Weak);
            SetAffinity(c, Element::Wind, Affinity::Weak);
            SetAffinity(c, Element::Ice, Affinity::Weak);
            SetAffinity(c, Element::Fire, Affinity::Null);
            SetAffinity(c, Element::Strike, Affinity::Null); // too quick to club out of the air
            c.skills = {
                Make("Wing Slash", Element::Slash, TargetType::OneEnemy, 7, 0),
                Inflicts(Make("Flame Dive", Element::Fire, TargetType::OneEnemy, 9, 3), Ailment::Burn, 0.35f),
            };
            c.xpReward = 18;
            c.rupeeMin = 3;
            c.rupeeMax = 8;
            break;
    }
    c.hp = c.maxHp;
    c.mp = c.maxMp;
    return c;
}

int XpToNext(int level) {
    // 30 XP for 1->2, 150 for 5->6, 480 for 10->11, 1720 for 20->21
    return 4 * level * level + 6 * level + 20;
}

int XpForLevel(int level) {
    int total = 0;
    for (int l = 1; l < level; l++) {
        total += XpToNext(l);
    }
    return total;
}

int ScaledXp(int baseXp, int memberLevel, int enemyLevel) {
    int above = memberLevel - enemyLevel;
    float scale = 1.0f;
    if (above > 10) {
        scale = 0.55f;
    } else if (above > 5) {
        scale = 0.77f;
    } else if (above < -2) {
        scale = 1.25f; // a bonus for beating stronger foes
    }
    return std::max(1, (int)(baseXp * scale + 0.5f));
}

const char* StatName(Stat s) {
    switch (s) {
        case Stat::St:
            return "St";
        case Stat::Ma:
            return "Ma";
        case Stat::Vi:
            return "Vi";
        case Stat::Ag:
            return "Ag";
        case Stat::Lu:
            return "Lu";
        default:
            return "?";
    }
}

int GetStat(const Combatant& c, Stat s) {
    switch (s) {
        case Stat::St:
            return c.st;
        case Stat::Ma:
            return c.ma;
        case Stat::Vi:
            return c.vi;
        case Stat::Ag:
            return c.ag;
        case Stat::Lu:
            return c.lu;
        default:
            return 0;
    }
}

void AddStat(Combatant& c, Stat s, int amount) {
    switch (s) {
        case Stat::St:
            c.st += amount;
            break;
        case Stat::Ma:
            c.ma += amount;
            break;
        case Stat::Vi:
            c.vi += amount;
            break;
        case Stat::Ag:
            c.ag += amount;
            break;
        case Stat::Lu:
            c.lu += amount;
            break;
        default:
            break;
    }
}

LevelGains LevelUp(Combatant& c, const std::function<float()>& rand01) {
    LevelGains g;
    if (c.level >= kLevelCap) {
        return g;
    }
    Growth growth = GrowthOf(c);
    c.level++;
    g.leveled = true;
    g.hp = growth.hp;
    g.mp = growth.mp;
    c.maxHp += growth.hp;
    c.maxMp += growth.mp;

    for (size_t i = 0; i < growth.weights.size(); i++) {
        if (GetStat(c, (Stat)i) >= kStatCap) {
            growth.weights[i] = 0.0f; // capped stats get no more points
        }
    }
    float total = 0.0f;
    for (float w : growth.weights) {
        total += w;
    }
    for (int p = 0; p < kAutoStatPoints && total > 0.0f; p++) {
        float roll = rand01() * total;
        size_t pick = growth.weights.size() - 1;
        for (size_t i = 0; i < growth.weights.size(); i++) {
            if (roll < growth.weights[i]) {
                pick = i;
                break;
            }
            roll -= growth.weights[i];
        }
        g.stats[pick]++;
        AddStat(c, (Stat)pick, 1);
        if (GetStat(c, (Stat)pick) >= kStatCap) {
            total -= growth.weights[pick];
            growth.weights[pick] = 0.0f;
        }
    }
    return g;
}

} // namespace Nocturne
