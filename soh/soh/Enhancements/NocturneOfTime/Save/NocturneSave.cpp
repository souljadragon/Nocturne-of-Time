#include "NocturneSave.h"
#include "../Party.h"
#include "../Battle/BattleData.h"
#include "../Moon/Moon.h"

#include <algorithm>

using nlohmann::json;

namespace Nocturne {

namespace {

json ItemToJson(const Item& item) {
    json mods = json::array();
    for (const StatMod& m : item.mods) {
        mods.push_back({ { "k", (int)m.kind }, { "v", m.value }, { "e", (int)m.element } });
    }
    return { { "uid", item.uid }, { "base", item.baseId }, { "rarity", (int)item.rarity },
             { "level", item.level }, { "name", item.name }, { "mods", mods } };
}

Item ItemFromJson(const json& j) {
    Item item;
    item.uid = j.value("uid", 0u);
    item.baseId = j.value("base", 0);
    item.rarity = (Rarity)std::clamp(j.value("rarity", 0), 0, (int)Rarity::Count - 1);
    item.level = j.value("level", 1);
    item.name = j.value("name", std::string());
    if (j.contains("mods") && j["mods"].is_array()) {
        for (const json& m : j["mods"]) {
            StatMod mod;
            mod.kind = (StatKind)m.value("k", 0);
            mod.value = m.value("v", 0);
            mod.element = (Element)std::clamp(m.value("e", 0), 0, (int)Element::Count - 1);
            item.mods.push_back(mod);
        }
    }
    return item;
}

// A member's skills, affinities and archetype come from their template; the saved numbers go on top
bool MemberTemplate(const std::string& name, Combatant& out) {
    if (name == "Link") {
        out = MakeLink(1);
        return true;
    }
    if (name == "Saria") {
        out = MakeSaria(1);
        return true;
    }
    return false;
}

json MemberToJson(const PartyMember& m) {
    const Combatant& c = m.base;
    json gear = json::array();
    for (const std::optional<Item>& g : m.gear) {
        gear.push_back(g ? ItemToJson(*g) : json(nullptr));
    }
    return { { "name", c.name },   { "level", c.level }, { "maxHp", c.maxHp }, { "hp", c.hp },
             { "maxMp", c.maxMp }, { "mp", c.mp },       { "st", c.st },       { "ma", c.ma },
             { "vi", c.vi },       { "ag", c.ag },       { "lu", c.lu },       { "ailment", (int)c.ailment },
             { "xp", m.xp },       { "points", m.unspentStatPoints },          { "gear", gear } };
}

bool MemberFromJson(const json& j, PartyMember& m) {
    if (!MemberTemplate(j.value("name", std::string()), m.base)) {
        return false;
    }
    Combatant& c = m.base;
    c.level = std::max(1, j.value("level", 1));
    c.maxHp = std::max(1, j.value("maxHp", c.maxHp));
    // current HP/MP can be above the base maximum (equipment adds to it): clamped with gear after loading
    c.hp = std::max(0, j.value("hp", c.maxHp));
    c.maxMp = std::max(0, j.value("maxMp", c.maxMp));
    c.mp = std::max(0, j.value("mp", c.maxMp));
    c.st = j.value("st", c.st);
    c.ma = j.value("ma", c.ma);
    c.vi = j.value("vi", c.vi);
    c.ag = j.value("ag", c.ag);
    c.lu = j.value("lu", c.lu);
    int ailment = j.value("ailment", 0);
    c.ailment = ailment == (int)Ailment::Poison ? Ailment::Poison : Ailment::None; // burns end with battles
    c.ailmentTurns = 0;
    m.xp = std::max(0, j.value("xp", 0));
    m.unspentStatPoints = std::max(0, j.value("points", 0));
    if (j.contains("gear") && j["gear"].is_array()) {
        for (size_t i = 0; i < m.gear.size() && i < j["gear"].size(); i++) {
            const json& g = j["gear"][i];
            m.gear[i] = g.is_null() ? std::optional<Item>() : std::optional<Item>(ItemFromJson(g));
        }
    }
    return true;
}

} // namespace

json Save_Write() {
    json members = json::array();
    for (const PartyMember& m : Party_Members()) {
        members.push_back(MemberToJson(m));
    }
    json bag = json::array();
    for (const Item& item : Party_Bag()) {
        bag.push_back(ItemToJson(item));
    }
    json consumables = json::array();
    for (int n : Party_Consumables()) {
        consumables.push_back(n);
    }
    return { { "version", kSaveVersion },
             { "party", members },
             { "bag", bag },
             { "consumables", consumables },
             { "moon", { { "cycle", Moon_CyclePos() }, { "steps", Moon_StepsInPhase() } } },
             { "nextUid", Loot_NextUid() } };
}

void Save_Read(const json& data) {
    Party_Reset();
    Moon_Reset();
    if (!data.is_object()) {
        return;
    }
    std::vector<PartyMember>& members = Party_Members();
    uint32_t maxUid = 0;
    auto seeUid = [&](const Item& item) { maxUid = std::max(maxUid, item.uid); };
    if (data.contains("party") && data["party"].is_array()) {
        for (const json& j : data["party"]) {
            PartyMember m;
            if (MemberFromJson(j, m)) {
                for (const std::optional<Item>& g : m.gear) {
                    if (g) {
                        seeUid(*g);
                    }
                }
                members.push_back(m);
            }
        }
    }
    // Link leads the party, always; without him the save is no good to us - start fresh
    if (members.empty() || members[0].base.name != "Link") {
        Party_Reset();
        return;
    }
    if (data.contains("bag") && data["bag"].is_array()) {
        for (const json& j : data["bag"]) {
            Party_Bag().push_back(ItemFromJson(j));
            seeUid(Party_Bag().back());
        }
    }
    auto& counts = Party_Consumables();
    counts.fill(0);
    if (data.contains("consumables") && data["consumables"].is_array()) {
        for (size_t i = 0; i < counts.size() && i < data["consumables"].size(); i++) {
            counts[i] = std::clamp(data["consumables"][i].get<int>(), 0, kMaxCarry);
        }
    }
    if (data.contains("moon") && data["moon"].is_object()) {
        Moon_SetState(data["moon"].value("cycle", 0), data["moon"].value("steps", 0));
    }
    Loot_SetNextUid(std::max<uint32_t>(data.value("nextUid", 1u), maxUid + 1));
    for (PartyMember& m : members) {
        Party_ClampToMax(m);
    }
}

} // namespace Nocturne
