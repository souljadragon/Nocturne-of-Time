// Nocturne of Time: a scripted party for Queen Gohma, shared by GohmaSim.cpp and BattleEngineTests.cpp.
//   naive - attacks her weakness and heals, ignores her buffs, the telegraph and the hexes
//   smart - also strips her shell, guards the charged blow, cleanses hexes and blesses the party
#pragma once
#include "../../soh/soh/Enhancements/NocturneOfTime/Battle/BattleData.h"
#include <cstdio>
#include <random>

namespace GohmaPolicy {
using namespace Nocturne;

inline Combatant Known(Combatant c) {
    std::vector<Skill> k;
    for (size_t i = 0; i < c.skills.size(); i++) {
        if (i == 0 || c.skills[i].learnLevel <= c.level) {
            k.push_back(c.skills[i]);
        }
    }
    c.skills = k;
    return c;
}

inline int Find(const Combatant& c, const char* n) {
    for (int i = 0; i < (int)c.skills.size(); i++) {
        if (c.skills[i].name == n && c.mp >= c.skills[i].mpCost && c.hp > c.skills[i].hpCost) {
            return i;
        }
    }
    return -1;
}

struct Result {
    bool win;
    bool timeout;
    int turns;
};

inline bool gVerbose = false;
inline void Show(const Events& ev) {
    if (!gVerbose) return;
    for (const BattleEvent& e : ev) printf("  %s\n", e.text.c_str());
}

inline Result Play(int level, bool smart, std::mt19937& rng, int potions) {
    std::uniform_real_distribution<float> u(0.0f, 0.99999f);
    Combatant link = Known(MakeLink(level)), saria = Known(MakeSaria(level));
    link.weaponPower = 4; // uncommon gear by now
    saria.magicPower = 3;
    link.maxHp += 8; // gear HP
    link.hp = link.maxHp;
    Battle b({ link, saria }, { MakeQueenGohma(level >= 9 ? 2 : 2, level) }, [&] { return u(rng); });
    b.SetTurnLimit(kGohmaTurnLimit);
    b.SetNoEscape(true);
    int turns = 0;
    int guard = 0;
    while (b.Phase() == BattlePhase::PartyCommand || b.Phase() == BattlePhase::EnemyTurn) {
        if (++guard > 2000) {
            break;
        }
        if (b.Phase() == BattlePhase::EnemyTurn) {
            Show(b.EnemyAct());
            continue;
        }
        turns = kGohmaTurnLimit - b.TurnsLeft() + 1;
        int a = b.ActivePartyMember();
        const Combatant& me = b.Party()[a];
        const Combatant& g = b.Enemies()[0];
        int larva = -1;
        for (int i = 1; i < (int)b.Enemies().size(); i++) {
            if (b.Enemies()[i].Alive()) {
                larva = i;
            }
        }
        // Heal anyone low (both policies)
        int low = -1;
        for (int i = 0; i < 2; i++) {
            const Combatant& c = b.Party()[i];
            if (c.Alive() && c.hp * 100 < c.maxHp * 40) {
                low = i;
            }
        }
        if (smart && g.charged) {
            // The telegraph: guard, unless someone's about to die (heal) 
            if (low >= 0 && a == 1 && Find(me, "Song of Healing") >= 0) {
                Show(b.UseSkill(Find(me, "Song of Healing"), low));
            } else {
                Show(b.Guard());
            }
            continue;
        }
        if (low >= 0) {
            if (a == 1 && Find(me, "Minuet") >= 0 && b.Party()[0].hp * 2 < b.Party()[0].maxHp &&
                b.Party()[1].hp * 2 < b.Party()[1].maxHp) {
                Show(b.UseSkill(Find(me, "Minuet"), -1));
                continue;
            }
            if (a == 1 && Find(me, "Song of Healing") >= 0) {
                Show(b.UseSkill(Find(me, "Song of Healing"), low));
                continue;
            }
            if (potions > 0) {
                potions--;
                ItemEffect e;
                e.hp = 60;
                Events ue = b.UseItem("Red Potion", e, low);
                Show(ue);
                if (ue.size()) {
                    continue;
                }
            }
        }
        if (smart) {
            if (g.defStage > 0 || g.agiStage > 0) {
                int ds = Find(me, "Dispelling Sun");
                if (ds >= 0) {
                    Show(b.UseSkill(ds, -1));
                    continue;
                }
                if (a == 1 && g.agiStage > 0 && Find(me, "Mire Hex") >= 0) {
                    Show(b.UseSkill(Find(me, "Mire Hex"), -1));
                    continue;
                }
            }
            bool hexed = b.Party()[0].defStage < 0 || b.Party()[1].defStage < 0;
            if (a == 1 && hexed && Find(me, "Cleansing Lullaby") >= 0) {
                Show(b.UseSkill(Find(me, "Cleansing Lullaby"), -1));
                continue;
            }
            if (a == 1 && b.Party()[1].defStage < 1 && Find(me, "Nayru's Blessing") >= 0 && turns <= 4) {
                Show(b.UseSkill(Find(me, "Nayru's Blessing"), -1));
                continue;
            }
            if (a == 0 && b.Party()[0].atkStage < 1 && Find(me, "Din's Blessing") >= 0) {
                Show(b.UseSkill(Find(me, "Din's Blessing"), -1));
                continue;
            }
            if (larva >= 0 && a == 0 && Find(me, "Spin Attack") >= 0) {
                Show(b.UseSkill(Find(me, "Spin Attack"), -1));
                continue;
            }
        }
        if (a == 0) {
            Show(b.UseSkill(Find(me, "Slingshot"), 0));
        } else {
            int gale = Find(me, "Gale");
            if (gale >= 0) {
                Show(b.UseSkill(gale, 0));
            } else {
                Show(b.UseSkill(0, 0));
            }
        }
    }
    return { b.Phase() == BattlePhase::Victory, b.TimedOut(), turns };
}


} // namespace GohmaPolicy
