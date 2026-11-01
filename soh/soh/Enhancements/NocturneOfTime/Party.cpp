#include "Party.h"
#include "Battle/BattleData.h"

#include <algorithm>

namespace Nocturne {

namespace {
std::vector<PartyMember> sMembers;
std::vector<Item> sBag;
std::array<int, (size_t)ConsumableId::Count> sConsumables{};
std::array<RelicState, (size_t)RelicId::Count> sRelics{};
int sEquippedRelic = -1;

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
    sRelics = {};
    sEquippedRelic = -1;
    Party_GiveRelic(RelicId::FairySlingshot); // Link's from the start
    Party_RefreshAllSkills();
    Party_HealAll();
}

void Party_Reset() {
    sMembers.clear();
    sBag.clear();
    sConsumables.fill(0);
    sRelics = {};
    sEquippedRelic = -1;
}

// ---- Skills and Relics ---------------------------------------------------------------------------------

std::vector<PoolSkill> Party_SkillPool(const PartyMember& member) {
    std::vector<PoolSkill> pool;
    const Combatant& c = member.base;
    for (size_t i = 1; i < c.skills.size(); i++) {
        if (c.skills[i].learnLevel <= c.level) {
            pool.push_back({ c.skills[i], false, true });
        }
    }
    if (c.name == "Link") {
        for (int r = 0; r < (int)RelicId::Count; r++) {
            const RelicState& st = sRelics[r];
            if (!st.owned) {
                continue;
            }
            const RelicDef& def = RelicOf((RelicId)r);
            int mastery = RelicMastery(def, st.points);
            bool equipped = sEquippedRelic == r;
            for (const RelicSkill& rs : def.skills) {
                bool permanent = mastery > rs.mastery;
                bool usable = permanent || (equipped && mastery >= rs.mastery);
                if (!usable) {
                    continue;
                }
                bool dup = false;
                for (PoolSkill& p : pool) {
                    if (p.skill.name == rs.skill.name) {
                        dup = true;
                        p.permanent = p.permanent || permanent;
                    }
                }
                if (!dup) {
                    pool.push_back({ rs.skill, true, permanent });
                }
            }
        }
    }
    return pool;
}

bool Party_IsActive(const PartyMember& member, const std::string& name) {
    return std::find(member.activeSkills.begin(), member.activeSkills.end(), name) != member.activeSkills.end();
}

void Party_RefreshSkills(PartyMember& m) {
    std::vector<PoolSkill> pool = Party_SkillPool(m);
    auto inPool = [&](const std::string& n) {
        for (const PoolSkill& p : pool) {
            if (p.skill.name == n) {
                return true;
            }
        }
        return false;
    };
    // Drop what's no longer available (an unmastered skill whose Relic came off). It's offered again when it
    // comes back, so wearing the Relic again brings it straight back into the list.
    m.activeSkills.erase(std::remove_if(m.activeSkills.begin(), m.activeSkills.end(),
                                        [&](const std::string& n) { return !inPool(n); }),
                         m.activeSkills.end());
    m.seenSkills.erase(std::remove_if(m.seenSkills.begin(), m.seenSkills.end(),
                                      [&](const std::string& n) { return !inPool(n); }),
                       m.seenSkills.end());
    // Offer what's new
    for (const PoolSkill& p : pool) {
        const std::string& n = p.skill.name;
        bool seen = std::find(m.seenSkills.begin(), m.seenSkills.end(), n) != m.seenSkills.end();
        if (!seen) {
            m.seenSkills.push_back(n);
            if ((int)m.activeSkills.size() < kMaxActiveSkills && !Party_IsActive(m, n)) {
                m.activeSkills.push_back(n);
            }
        }
    }
}

void Party_RefreshAllSkills() {
    for (PartyMember& m : sMembers) {
        Party_RefreshSkills(m);
    }
}

bool Party_ActivateSkill(PartyMember& m, const std::string& name) {
    if (Party_IsActive(m, name) || (int)m.activeSkills.size() >= kMaxActiveSkills) {
        return false;
    }
    for (const PoolSkill& p : Party_SkillPool(m)) {
        if (p.skill.name == name) {
            m.activeSkills.push_back(name);
            return true;
        }
    }
    return false;
}

void Party_DeactivateSkill(PartyMember& m, const std::string& name) {
    m.activeSkills.erase(std::remove(m.activeSkills.begin(), m.activeSkills.end(), name), m.activeSkills.end());
}

std::array<RelicState, (size_t)RelicId::Count>& Party_Relics() {
    return sRelics;
}

int Party_EquippedRelic() {
    return sEquippedRelic;
}

bool Party_GiveRelic(RelicId id) {
    RelicState& st = sRelics[(size_t)id];
    if (st.owned) {
        return false;
    }
    st.owned = true;
    if (sEquippedRelic < 0) {
        sEquippedRelic = (int)id;
    }
    Party_RefreshAllSkills();
    return true;
}

bool Party_EquipRelic(RelicId id) {
    if (!sRelics[(size_t)id].owned) {
        return false;
    }
    sEquippedRelic = (int)id;
    Party_RefreshAllSkills();
    return true;
}

void Party_UnequipRelic() {
    sEquippedRelic = -1;
    Party_RefreshAllSkills();
}

std::vector<std::string> Party_AddRelicMastery(int points) {
    std::vector<std::string> out;
    if (sEquippedRelic < 0 || points <= 0) {
        return out;
    }
    RelicState& st = sRelics[sEquippedRelic];
    const RelicDef& def = RelicOf((RelicId)sEquippedRelic);
    int before = RelicMastery(def, st.points);
    st.points += points;
    int after = RelicMastery(def, st.points);
    // One line per level: "Fairy Slingshot reached Mastery level 1! Slingshot is now learned."
    for (int level = before + 1; level <= after; level++) {
        // (two lines of the battle's message window, 35 characters each)
        std::string line = def.name + " reached Mastery level " + std::to_string(level) + "!";
        if (line.size() > 35) {
            line = def.name + " reached Mastery " + std::to_string(level) + "!";
        }
        if (level >= def.MaxMastery()) {
            line = def.name + " is mastered!";
        }
        for (const RelicSkill& rs : def.skills) {
            if (rs.mastery == level - 1) {
                line += "\n" + rs.skill.name + " is now learned.";
            }
        }
        out.push_back(line);
    }
    if (after > before) {
        Party_RefreshAllSkills();
    }
    return out;
}

Combatant Party_Effective(const PartyMember& member) {
    Combatant c = member.base;
    // Battle skills: the basic Attack, then the active ones (in the order the player set) that are available
    std::vector<Skill> known;
    if (!c.skills.empty()) {
        known.push_back(c.skills[0]);
    }
    std::vector<PoolSkill> pool = Party_SkillPool(member);
    for (const std::string& name : member.activeSkills) {
        for (const PoolSkill& p : pool) {
            if (p.skill.name == name) {
                known.push_back(p.skill);
                break;
            }
        }
    }
    c.skills = known;
    // The equipped Relic sets Link's affinities (gear resists apply on top)
    if (c.name == "Link" && sEquippedRelic >= 0) {
        for (const auto& [element, affinity] : RelicOf((RelicId)sEquippedRelic).affinities) {
            c.affinity[(size_t)element] = affinity;
        }
    }
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
