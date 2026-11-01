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

// Stat spells (power 0, Almighty so no affinity applies)
Skill Stages(const char* name, TargetType target, int atk, int def, int agi, int mpCost, int learnLevel) {
    Skill s = Make(name, Element::Almighty, target, 0, mpCost, 0, 1.0f);
    s.buffAtk = atk;
    s.buffDef = def;
    s.buffAgi = agi;
    s.learnLevel = learnLevel;
    return s;
}

Skill Learned(Skill s, int level) {
    s.learnLevel = level;
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
    // Hero: balanced swordsman
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
        // Sword skills come with levels; everything else Link learns from Relics (see Relics/Relics.cpp)
        Describe(Learned(Make("Jump Slash", Element::Slash, TargetType::OneEnemy, 20, 0, 8), 5),
                 "A leaping overhead cut on one foe. Costs some HP."),
        Describe(Learned(Make("Great Spin", Element::Slash, TargetType::AllEnemies, 16, 0, 14), 12),
                 "A charged spin that tears through every foe. Costs HP."),
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
        Describe(Make("Song of Healing", Element::Almighty, TargetType::OneAlly, 40, 5, 0, 1.0f, true),
                 "A gentle tune that restores one ally's HP."),
        // Core skills by level; the rest is bought in her skill tree (Battle/SkillTree.cpp)
        Describe(Stages("Nayru's Blessing", TargetType::AllAllies, 0, 1, 0, 4, 3),
                 "Nayru's calm wards the party: defense rises."),
        Describe(Stages("Mire Hex", TargetType::AllEnemies, 0, 0, -1, 4, 5),
                 "Roots and bog slow every foe: their agility falls."),
        Describe([] {
                     Skill d = Stages("Cleansing Lullaby", TargetType::AllAllies, 0, 0, 0, 6, 9);
                     d.dispelDebuffs = true;
                     return d;
                 }(),
                 "Zelda's Lullaby, softly: lifts every hex from the party."),
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
                Stages("Silk Snare", TargetType::AllEnemies, 0, 0, -1, 0, 1),
            };
            c.xpReward = 28;
            c.rupeeMin = 8;
            c.rupeeMax = 15;
            break;

        case EnemyKind::QueenGohma:
            // Boss: see MakeQueenGohma for her larvae
            c.name = "Queen Gohma";
            c.boss = true;
            c.ai = (int)EnemyAi::QueenGohma;
            c.pressTurns = 2;
            c.level = 11;
            c.maxHp = 280;
            c.maxMp = 99;
            c.st = 14;
            c.ma = 9;
            c.vi = 11;
            c.ag = 7;
            c.lu = 7;
            SetAffinity(c, Element::Pierce, Affinity::Weak); // the eye
            SetAffinity(c, Element::Fire, Affinity::Weak);
            SetAffinity(c, Element::Strike, Affinity::Resist); // armoured shell
            SetAffinity(c, Element::Shadow, Affinity::Null);
            c.skills = {
                Make("Claw Swipe", Element::Slash, TargetType::OneEnemy, 16, 0),
                Make("Quake Stomp", Element::Strike, TargetType::AllEnemies, 11, 0, 0, 0.9f),
                Inflicts(Make("Stunning Gaze", Element::Light, TargetType::OneEnemy, 5, 4, 0, 0.9f), Ailment::Stun,
                         0.45f),
                [] {
                    Skill sh = Stages("Hardened Shell", TargetType::Self, 0, 2, 1, 6, 1);
                    sh.useText = "Queen Gohma's carapace hardens!";
                    return sh;
                }(),
                [] {
                    Skill f = Stages("Brood Fury", TargetType::Self, 0, 0, 0, 5, 1);
                    f.charge = true;
                    f.useText = "Queen Gohma's eye blazes red! She is gathering her strength...";
                    return f;
                }(),
                Stages("Rattling Screech", TargetType::AllEnemies, 0, -1, 0, 4, 1),
            };
            c.xpReward = 450;
            c.rupeeMin = 50;
            c.rupeeMax = 80;
            break;

        case EnemyKind::GohmaLarva:
            c.name = "Gohma Larva";
            c.level = 6;
            c.maxHp = 30;
            c.st = 8;
            c.ma = 1;
            c.vi = 3;
            c.ag = 7;
            c.lu = 3;
            SetAffinity(c, Element::Slash, Affinity::Weak);
            SetAffinity(c, Element::Pierce, Affinity::Weak);
            SetAffinity(c, Element::Fire, Affinity::Weak);
            c.ai = (int)EnemyAi::Brood;
            c.skills = {
                Make("Headbutt", Element::Strike, TargetType::OneEnemy, 8, 0),
                [] {
                    Skill ch = Stages("Brood Chant", TargetType::AllAllies, 1, 0, 0, 0, 1);
                    ch.useText = "The larvae shriek for their queen!";
                    return ch;
                }(),
            };
            c.xpReward = 15;
            c.rupeeMin = 1;
            c.rupeeMax = 4;
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

Combatant MakeQueenGohma(int larvae, int partyLevel) {
    Combatant c = MakeEnemy(EnemyKind::QueenGohma);
    // She never falls behind the party: a party that out-levelled her still meets her a level above them
    int lift = partyLevel + 1 - c.level;
    if (lift > 0) {
        c.level += lift;
        c.maxHp += lift * 12;
        c.st += lift;
        c.vi += (lift * 2) / 3;
        c.ag += (lift + 1) / 2;
        c.ma += (lift + 1) / 2;
        c.lu += lift / 2;
        c.hp = c.maxHp;
    }
    c.summonAt = 0.5f;
    c.summonText = "Queen Gohma lays her eggs!";
    for (int i = 0; i < larvae; i++) {
        Combatant l = MakeEnemy(EnemyKind::GohmaLarva);
        l.name += std::string(" ") + (char)('A' + i);
        c.summons.push_back(l);
    }
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

float XpRate(const std::string& name) {
    if (name == "Link") {
        return 1.0f; // the hero
    }
    if (name == "Saria") {
        return 1.18f;
    }
    return 1.12f; // later companions: tuned as they join
}

int XpForLevel(int level, const std::string& name) {
    return (int)(XpForLevel(level) * XpRate(name) + 0.5f);
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
