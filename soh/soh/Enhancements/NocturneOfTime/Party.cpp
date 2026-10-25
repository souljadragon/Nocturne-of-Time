#include "Party.h"
#include "Battle/BattleData.h"

#include <algorithm>

namespace Nocturne {

namespace {
std::vector<PartyMember> sMembers;
std::vector<Item> sBag;
std::array<int, (size_t)ConsumableId::Count> sConsumables{};

void StartingConsumables() {
    sConsumables.fill(0);
    sConsumables[(size_t)ConsumableId::RedPotion] = 3;
    sConsumables[(size_t)ConsumableId::HylianHerb] = 1;
}

std::vector<const Item*> GearList(const PartyMember& m) {
    std::vector<const Item*> list;
    for (const std::optional<Item>& g : m.gear) {
        if (g) {
            list.push_back(&*g);
        }
    }
    return list;
}
} // namespace

std::vector<PartyMember>& Party_Members() {
    return sMembers;
}

std::vector<Item>& Party_Bag() {
    return sBag;
}

std::array<int, (size_t)ConsumableId::Count>& Party_Consumables() {
    return sConsumables;
}

int Party_AddConsumable(ConsumableId id, int count) {
    int& have = sConsumables[(size_t)id];
    int added = std::clamp(kMaxCarry - have, 0, count);
    have += added;
    return added;
}

std::vector<std::string> Party_UseConsumable(ConsumableId id, int member) {
    std::vector<std::string> messages;
    const ConsumableDef& def = ConsumableOf(id);
    if (sConsumables[(size_t)id] <= 0 || def.battleOnly) {
        return messages;
    }
    bool used = false;
    for (int i = 0; i < (int)sMembers.size(); i++) {
        if (def.effect.target != TargetType::AllAllies && i != member) {
            continue;
        }
        PartyMember& m = sMembers[i];
        Combatant eff = Party_Effective(m); // maximums include equipment
        if (ApplyItemEffect(def.effect, eff, &messages)) {
            m.base.hp = eff.hp;
            m.base.mp = eff.mp;
            m.base.ailment = eff.ailment;
            m.base.ailmentTurns = eff.ailmentTurns;
            used = true;
        }
    }
    if (used) {
        sConsumables[(size_t)id]--;
    }
    return messages;
}

void Party_EnsureCreated(int startLevel) {
    if (!sMembers.empty()) {
        return;
    }
    PartyMember link;
    link.base = MakeLink(startLevel);
    link.xp = XpForLevel(startLevel, link.base.name);
    link.gear[(size_t)Slot::Weapon] = MakeCommon(BaseId::KokiriSword);
    link.gear[(size_t)Slot::Armor] = MakeCommon(BaseId::KokiriTunic);

    PartyMember saria;
    saria.base = MakeSaria(startLevel);
    saria.xp = XpForLevel(startLevel, saria.base.name);
    saria.gear[(size_t)Slot::Weapon] = MakeCommon(BaseId::ForestStaff);
    saria.gear[(size_t)Slot::Armor] = MakeCommon(BaseId::LeafRobe);

    sMembers = { link, saria };
    StartingConsumables();
    Party_HealAll();
}

void Party_Reset() {
    sMembers.clear();
    sBag.clear();
    sConsumables.fill(0);
}

Combatant Party_Effective(const PartyMember& member) {
    Combatant c = member.base;
    ApplyGear(c, GearList(member));
    c.hp = std::clamp(member.base.hp, 0, c.maxHp);
    c.mp = std::clamp(member.base.mp, 0, c.maxMp);
    return c;
}

void Party_HealAll() {
    for (PartyMember& m : sMembers) {
        Combatant e = Party_Effective(m);
        m.base.hp = e.maxHp;
        m.base.mp = e.maxMp;
        m.base.ailment = Ailment::None;
        m.base.ailmentTurns = 0;
    }
}

void Party_ClampToMax(PartyMember& m) {
    Combatant e = Party_Effective(m);
    m.base.hp = std::min(m.base.hp, e.maxHp);
    m.base.mp = std::min(m.base.mp, e.maxMp);
}

bool Party_HasEffect(UniqueEffect effect) {
    for (const PartyMember& m : sMembers) {
        for (const std::optional<Item>& g : m.gear) {
            if (g && BaseOf(*g).effect == effect) {
                return true;
            }
        }
    }
    return false;
}

bool Party_Equip(int member, int bagIndex) {
    if (member < 0 || member >= (int)sMembers.size() || bagIndex < 0 || bagIndex >= (int)sBag.size()) {
        return false;
    }
    PartyMember& m = sMembers[member];
    Item item = sBag[bagIndex];
    if (!CanEquip(item, m.base.name)) {
        return false;
    }
    size_t slot = (size_t)BaseOf(item).slot;
    sBag.erase(sBag.begin() + bagIndex);
    if (m.gear[slot]) {
        sBag.push_back(*m.gear[slot]);
    }
    m.gear[slot] = item;
    Party_ClampToMax(m);
    return true;
}

void Party_Unequip(int member, Slot slot) {
    if (member < 0 || member >= (int)sMembers.size()) {
        return;
    }
    PartyMember& m = sMembers[member];
    if (m.gear[(size_t)slot]) {
        sBag.push_back(*m.gear[(size_t)slot]);
        m.gear[(size_t)slot].reset();
        Party_ClampToMax(m);
    }
}

} // namespace Nocturne
