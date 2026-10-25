#include "BattleData.h"

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

Skill Describe(Skill s, const char* description) {
    s.description = description;
    return s;
}

void SetAffinity(Combatant& c, Element e, Affinity a) {
    c.affinity[(size_t)e] = a;
}

// Grow a party member from level 1 to `level` using their archetype's growth.
void GrowTo(Combatant& c, int level) {
    while (c.level < level && LevelUp(c)) {
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
    c.st = 6;
    c.ma = 2;
    c.vi = 5;
    c.ag = 5;
    c.lu = 5;
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
    c.st = 3;
    c.ma = 6;
    c.vi = 4;
    c.ag = 6;
    c.lu = 6;
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
                Make("Flame Dive", Element::Fire, TargetType::OneEnemy, 9, 3),
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

int XpForLevel(int level) {
    // 1->2 needs 40 XP, each level a little more
    int total = 0;
    for (int l = 2; l <= level; l++) {
        total += 20 + l * 10;
    }
    return total;
}

bool LevelUp(Combatant& c) {
    if (c.level >= kLevelCap) {
        return false;
    }
    c.level++;
    if (c.name == "Saria") {
        c.maxHp += 5;
        c.maxMp += 4;
        c.ma += 1;
        c.ag += (c.level % 2 == 0) ? 1 : 0;
        c.vi += (c.level % 3 == 0) ? 1 : 0;
        c.st += (c.level % 4 == 0) ? 1 : 0;
        c.lu += (c.level % 2 == 1) ? 1 : 0;
    } else {
        // Link and anyone without their own growth: balanced
        c.maxHp += 7;
        c.maxMp += 1;
        c.st += 1;
        c.vi += (c.level % 2 == 0) ? 1 : 0;
        c.ag += (c.level % 2 == 1) ? 1 : 0;
        c.ma += (c.level % 4 == 0) ? 1 : 0;
        c.lu += (c.level % 3 == 0) ? 1 : 0;
    }
    return true;
}

} // namespace Nocturne
