#include "BattleEngine.h"

#include <algorithm>
#include <cmath>

namespace Nocturne {

const char* ElementName(Element e) {
    switch (e) {
        case Element::Slash:
            return "Slash";
        case Element::Strike:
            return "Strike";
        case Element::Pierce:
            return "Pierce";
        case Element::Fire:
            return "Fire";
        case Element::Ice:
            return "Ice";
        case Element::Wind:
            return "Wind";
        case Element::Shock:
            return "Shock";
        case Element::Light:
            return "Light";
        case Element::Shadow:
            return "Shadow";
        case Element::Almighty:
            return "Almighty";
        default:
            return "?";
    }
}

const char* AffinityName(Affinity a) {
    switch (a) {
        case Affinity::Normal:
            return "-";
        case Affinity::Weak:
            return "Wk";
        case Affinity::Resist:
            return "Rs";
        case Affinity::Null:
            return "Nu";
        case Affinity::Repel:
            return "Rp";
        case Affinity::Drain:
            return "Dr";
        default:
            return "?";
    }
}

// ---- Press Turn icons -----------------------------------------------------------------------------------

void PressTurns::UseFull() {
    if (blinking > 0) {
        blinking--;
    } else if (solid > 0) {
        solid--;
    }
}

void PressTurns::UseHalf() {
    if (solid > 0) {
        solid--;
        blinking++;
    } else if (blinking > 0) {
        blinking--;
    }
}

void PressTurns::UseTwo() {
    UseFull();
    UseFull();
}

void PressTurns::UseAll() {
    solid = 0;
    blinking = 0;
}

// ---- Battle ---------------------------------------------------------------------------------------------

namespace {

BattleEvent Ev(BattleEvent::Type type, bool actorIsEnemy, int actor, bool targetIsEnemy, int target, int amount,
               std::string text) {
    BattleEvent e;
    e.type = type;
    e.actorIsEnemy = actorIsEnemy;
    e.actor = actor;
    e.targetIsEnemy = targetIsEnemy;
    e.target = target;
    e.amount = amount;
    e.text = std::move(text);
    return e;
}

BattleEvent Msg(std::string text) {
    return Ev(BattleEvent::Type::Message, false, -1, false, -1, 0, std::move(text));
}

} // namespace

const char* AilmentName(Ailment a) {
    switch (a) {
        case Ailment::Poison:
            return "Poison";
        case Ailment::Burn:
            return "Burn";
        default:
            return "";
    }
}

const char* AilmentTag(Ailment a) {
    switch (a) {
        case Ailment::Poison:
            return "PSN";
        case Ailment::Burn:
            return "BRN";
        default:
            return "";
    }
}

int AilmentDamage(Ailment a, int maxHp) {
    switch (a) {
        case Ailment::Poison:
            return std::max(1, maxHp / 12);
        case Ailment::Burn:
            return std::max(1, maxHp / 10);
        default:
            return 0;
    }
}

bool ItemHelps(const ItemEffect& e, const Combatant& c) {
    if (!c.Alive()) {
        return e.revive > 0.0f;
    }
    if ((e.hp > 0 || e.fullHp) && c.hp < c.maxHp) {
        return true;
    }
    if ((e.mp > 0 || e.fullMp) && c.mp < c.maxMp) {
        return true;
    }
    if (c.ailment != Ailment::None && (e.curesAll || e.cures == c.ailment)) {
        return true;
    }
    return false;
}

bool ApplyItemEffect(const ItemEffect& e, Combatant& c, std::vector<std::string>* messages) {
    if (!ItemHelps(e, c)) {
        return false;
    }
    auto say = [&](const std::string& m) {
        if (messages != nullptr) {
            messages->push_back(m);
        }
    };
    if (!c.Alive()) {
        c.hp = std::max(1, (int)std::lround(c.maxHp * e.revive));
        c.ailment = Ailment::None;
        c.ailmentTurns = 0;
        say(c.name + " is revived!");
        return true;
    }
    if (e.hp > 0 || e.fullHp) {
        int before = c.hp;
        c.hp = e.fullHp ? c.maxHp : std::min(c.maxHp, c.hp + e.hp);
        if (c.hp > before) {
            say(c.name + " recovers " + std::to_string(c.hp - before) + " HP");
        }
    }
    if (e.mp > 0 || e.fullMp) {
        int before = c.mp;
        c.mp = e.fullMp ? c.maxMp : std::min(c.maxMp, c.mp + e.mp);
        if (c.mp > before) {
            say(c.name + " recovers " + std::to_string(c.mp - before) + " MP");
        }
    }
    if (c.ailment != Ailment::None && (e.curesAll || e.cures == c.ailment)) {
        say(c.name + " is cured of " + std::string(AilmentName(c.ailment)) + "!");
        c.ailment = Ailment::None;
        c.ailmentTurns = 0;
    }
    return true;
}

Battle::Battle(std::vector<Combatant> party, std::vector<Combatant> enemies, std::function<float()> rand01)
    : mParty(std::move(party)), mEnemies(std::move(enemies)), mRand(std::move(rand01)) {
    Events ignored;
    StartPartyTurn(ignored);
}

int Battle::CountAlive(const std::vector<Combatant>& side) const {
    int n = 0;
    for (const Combatant& c : side) {
        n += c.Alive() ? 1 : 0;
    }
    return n;
}

void Battle::StartPartyTurn(Events& out) {
    mPhase = BattlePhase::PartyCommand;
    mIcons = { CountAlive(mParty), 0 };
    for (Combatant& c : mParty) {
        c.guarding = false;
    }
    mActive = -1;
    AdvanceActive();
    out.push_back(Ev(BattleEvent::Type::PhaseChange, false, -1, false, -1, 0, "Your turn"));
    TickAilments(mParty, false, out);
}

void Battle::TickAilments(std::vector<Combatant>& side, bool sideIsEnemy, Events& out) {
    for (int i = 0; i < (int)side.size(); i++) {
        Combatant& c = side[i];
        if (!c.Alive() || c.ailment == Ailment::None) {
            continue;
        }
        int dmg = std::min(AilmentDamage(c.ailment, c.maxHp), c.hp - 1); // never the last HP
        if (dmg > 0) {
            c.hp -= dmg;
            out.push_back(Ev(BattleEvent::Type::Hit, sideIsEnemy, i, sideIsEnemy, i, dmg,
                             c.name + " takes " + std::to_string(dmg) + " " +
                                 (c.ailment == Ailment::Poison ? "poison" : "burn") + " damage"));
        }
        if (c.ailment == Ailment::Burn && --c.ailmentTurns <= 0) {
            c.ailment = Ailment::None;
            c.ailmentTurns = 0;
            out.push_back(Msg(c.name + "'s burn fades."));
        }
    }
}

void Battle::StartEnemyTurn(Events& out) {
    mPhase = BattlePhase::EnemyTurn;
    mIcons = { CountAlive(mEnemies), 0 };
    for (Combatant& c : mEnemies) {
        c.guarding = false;
    }
    mEnemyCursor = 0;
    while (mEnemyCursor < (int)mEnemies.size() && !mEnemies[mEnemyCursor].Alive()) {
        mEnemyCursor++;
    }
    out.push_back(Ev(BattleEvent::Type::PhaseChange, true, -1, false, -1, 0, "Enemy turn"));
    TickAilments(mEnemies, true, out);
}

bool Battle::AdvanceActive() {
    int n = (int)mParty.size();
    for (int step = 1; step <= n; step++) {
        int i = ((mActive < 0 ? -1 : mActive) + step) % n;
        if (mParty[i].Alive()) {
            mActive = i;
            return true;
        }
    }
    return false;
}

bool Battle::CheckEnd(Events& out) {
    if (CountAlive(mEnemies) == 0) {
        mPhase = BattlePhase::Victory;
        out.push_back(Ev(BattleEvent::Type::Victory, false, -1, false, -1, 0, "Victory!"));
        return true;
    }
    // Link (party slot 0) falling is a game over, as with the SMT protagonist
    if (mParty.empty() || !mParty[0].Alive() || CountAlive(mParty) == 0) {
        mPhase = BattlePhase::Defeat;
        out.push_back(Ev(BattleEvent::Type::Defeat, false, -1, false, -1, 0, "Link has fallen..."));
        return true;
    }
    return false;
}

void Battle::SpendIcons(Outcome outcome) {
    switch (outcome) {
        case Outcome::Normal:
            mIcons.UseFull();
            break;
        case Outcome::WeakOrCrit:
            mIcons.UseHalf();
            break;
        case Outcome::MissOrNull:
            mIcons.UseTwo();
            break;
        case Outcome::RepelOrDrain:
            mIcons.UseAll();
            break;
    }
}

void Battle::AfterAction(Events& out) {
    if (CheckEnd(out)) {
        return;
    }
    bool partyTurn = mPhase == BattlePhase::PartyCommand;
    if (mIcons.Empty()) {
        if (partyTurn) {
            StartEnemyTurn(out);
        } else {
            StartPartyTurn(out);
        }
        return;
    }
    if (partyTurn) {
        AdvanceActive();
    } else {
        int n = (int)mEnemies.size();
        for (int step = 1; step <= n; step++) {
            int i = (mEnemyCursor + step) % n;
            if (mEnemies[i].Alive()) {
                mEnemyCursor = i;
                break;
            }
        }
    }
}

int Battle::BaseDamage(const Combatant& attacker, const Combatant& defender, const Skill& skill) const {
    bool physical = IsPhysical(skill.element);
    int atk = physical ? attacker.st : attacker.ma;
    int power = skill.power + (physical ? attacker.weaponPower : attacker.magicPower);
    float ratio = (float)(atk + attacker.level + 10) / (float)(defender.vi + defender.level + 10);
    return std::max(1, (int)std::lround(power * ratio));
}

Battle::Outcome Battle::ApplySkill(Combatant& user, bool userIsEnemy, int userIndex, const Skill& skill,
                                   bool targetIsEnemy, int target, Events& out) {
    std::vector<Combatant>& side = targetIsEnemy ? mEnemies : mParty;
    Combatant& def = side[target];

    if (skill.heal) {
        if (!def.Alive()) {
            return Outcome::Normal;
        }
        int before = def.hp;
        int amount = (int)std::lround((skill.power + user.magicPower) * (1.0f + user.healBonus));
        def.hp = std::min(def.maxHp, def.hp + amount);
        out.push_back(Ev(BattleEvent::Type::Heal, userIsEnemy, userIndex, targetIsEnemy, target, def.hp - before,
                         def.name + " recovers " + std::to_string(def.hp - before) + " HP"));
        return Outcome::Normal;
    }

    Affinity aff = skill.element == Element::Almighty ? Affinity::Normal : def.AffinityTo(skill.element);
    if (def.guarding && aff == Affinity::Weak) {
        aff = Affinity::Normal; // guarding covers a weakness
    }

    // Accuracy: only attacks that would connect can miss
    if (aff == Affinity::Normal || aff == Affinity::Weak || aff == Affinity::Resist) {
        float hitChance = std::clamp(skill.accuracy + (user.ag - def.ag) * 0.01f, 0.5f, 0.99f);
        if (mRand() >= hitChance) {
            out.push_back(Ev(BattleEvent::Type::Miss, userIsEnemy, userIndex, targetIsEnemy, target, 0, "Miss!"));
            return Outcome::MissOrNull;
        }
    }

    float variance = 0.9f + mRand() * 0.2f;
    int dmg = std::max(1, (int)std::lround(BaseDamage(user, def, skill) * variance));

    if (aff == Affinity::Null) {
        out.push_back(Ev(BattleEvent::Type::Null, userIsEnemy, userIndex, targetIsEnemy, target, 0,
                         def.name + " is unaffected!"));
        return Outcome::MissOrNull;
    }

    if (aff == Affinity::Repel) {
        out.push_back(Ev(BattleEvent::Type::Repel, targetIsEnemy, target, userIsEnemy, userIndex, dmg,
                         def.name + " repels the attack!"));
        user.hp = std::max(0, user.hp - dmg);
        out.push_back(Ev(BattleEvent::Type::Hit, targetIsEnemy, target, userIsEnemy, userIndex, dmg,
                         user.name + " takes " + std::to_string(dmg) + " damage"));
        if (!user.Alive()) {
            out.push_back(Ev(BattleEvent::Type::Defeated, targetIsEnemy, target, userIsEnemy, userIndex, 0,
                             user.name + " is down!"));
        }
        return Outcome::RepelOrDrain;
    }

    if (aff == Affinity::Drain) {
        int before = def.hp;
        def.hp = std::min(def.maxHp, def.hp + dmg);
        out.push_back(Ev(BattleEvent::Type::Drain, userIsEnemy, userIndex, targetIsEnemy, target, def.hp - before,
                         def.name + " absorbs the attack!"));
        return Outcome::RepelOrDrain;
    }

    bool weak = aff == Affinity::Weak;
    bool crit = false;
    if (IsPhysical(skill.element) && !def.guarding) {
        crit = mRand() < 0.05f + user.lu * 0.005f + user.critBonus;
    }

    float mult = 1.0f;
    if (weak) {
        mult *= 1.5f;
        out.push_back(Ev(BattleEvent::Type::Weak, userIsEnemy, userIndex, targetIsEnemy, target, 0, "WEAK!"));
    } else if (aff == Affinity::Resist) {
        mult *= 0.5f;
        out.push_back(Ev(BattleEvent::Type::Resist, userIsEnemy, userIndex, targetIsEnemy, target, 0, "Resist"));
    }
    if (crit) {
        mult *= 1.5f;
        out.push_back(
            Ev(BattleEvent::Type::Critical, userIsEnemy, userIndex, targetIsEnemy, target, 0, "CRITICAL!"));
    }
    if (def.guarding) {
        mult *= 0.5f;
    }
    dmg = std::max(1, (int)std::lround(dmg * mult));

    def.hp = std::max(0, def.hp - dmg);
    out.push_back(Ev(BattleEvent::Type::Hit, userIsEnemy, userIndex, targetIsEnemy, target, dmg,
                     def.name + " takes " + std::to_string(dmg) + " damage"));
    if (!def.Alive()) {
        out.push_back(Ev(BattleEvent::Type::Defeated, userIsEnemy, userIndex, targetIsEnemy, target, 0,
                         def.name + (targetIsEnemy ? " is defeated!" : " is down!")));
    } else if (skill.inflict != Ailment::None && def.ailment == Ailment::None) {
        float chance = std::clamp(skill.inflictChance + (user.lu - def.lu) * 0.01f, 0.05f, 0.9f);
        if (mRand() < chance) {
            def.ailment = skill.inflict;
            def.ailmentTurns = skill.inflict == Ailment::Burn ? kBurnTurns : 0;
            BattleEvent e = Ev(BattleEvent::Type::Message, userIsEnemy, userIndex, targetIsEnemy, target, 0,
                               def.name + (skill.inflict == Ailment::Poison ? " is poisoned!" : " is burned!"));
            e.color = skill.inflict == Ailment::Poison ? 0xC080FF00u : 0xFF904000u;
            out.push_back(e);
        }
    }
    return (weak || crit) ? Outcome::WeakOrCrit : Outcome::Normal;
}

bool Battle::CanUseSkill(int skill) const {
    if (mPhase != BattlePhase::PartyCommand || mActive < 0) {
        return false;
    }
    const Combatant& user = mParty[mActive];
    if (skill < 0 || skill >= (int)user.skills.size()) {
        return false;
    }
    const Skill& s = user.skills[skill];
    return user.mp >= s.mpCost && user.hp > s.hpCost;
}

Events Battle::UseSkill(int skill, int target) {
    Events out;
    if (!CanUseSkill(skill)) {
        return out;
    }
    Combatant& user = mParty[mActive];
    const Skill s = user.skills[skill]; // copy: the user's skill list must not be referenced across HP changes
    user.mp -= s.mpCost;
    user.hp -= s.hpCost;

    out.push_back(Msg(skill == 0 ? user.name + " attacks!" : user.name + " uses " + s.name + "!"));

    bool targetsEnemies = s.target == TargetType::OneEnemy || s.target == TargetType::AllEnemies;
    bool all = s.target == TargetType::AllEnemies || s.target == TargetType::AllAllies;
    std::vector<Combatant>& side = targetsEnemies ? mEnemies : mParty;

    std::vector<int> targets;
    if (all) {
        for (int i = 0; i < (int)side.size(); i++) {
            if (side[i].Alive()) {
                targets.push_back(i);
            }
        }
    } else {
        // Retarget if the chosen one is already down (heals may only target the living)
        if (target < 0 || target >= (int)side.size() || !side[target].Alive()) {
            target = -1;
            for (int i = 0; i < (int)side.size(); i++) {
                if (side[i].Alive()) {
                    target = i;
                    break;
                }
            }
        }
        if (target >= 0) {
            targets.push_back(target);
        }
    }

    Outcome worst = Outcome::Normal;
    for (int t : targets) {
        Outcome o = ApplySkill(user, false, mActive, s, targetsEnemies, t, out);
        worst = std::max(worst, o);
        if (!user.Alive()) {
            break; // repelled to death
        }
    }

    SpendIcons(worst);
    if (worst == Outcome::RepelOrDrain || worst == Outcome::MissOrNull) {
        out.push_back(Ev(BattleEvent::Type::TurnLost, false, mActive, false, -1, 0,
                         worst == Outcome::RepelOrDrain ? "The turn is lost!" : "Lost extra turns!"));
    }
    AfterAction(out);
    return out;
}

bool Battle::CanUseItem(const ItemEffect& effect, int target) const {
    if (mPhase != BattlePhase::PartyCommand || mActive < 0) {
        return false;
    }
    if (effect.target == TargetType::AllAllies) {
        for (const Combatant& c : mParty) {
            if (ItemHelps(effect, c)) {
                return true;
            }
        }
        return false;
    }
    return target >= 0 && target < (int)mParty.size() && ItemHelps(effect, mParty[target]);
}

Events Battle::UseItem(const std::string& itemName, const ItemEffect& effect, int target) {
    Events out;
    if (!CanUseItem(effect, target)) {
        return out;
    }
    out.push_back(Msg(mParty[mActive].name + " uses " + itemName + "!"));
    for (int i = 0; i < (int)mParty.size(); i++) {
        if (effect.target != TargetType::AllAllies && i != target) {
            continue;
        }
        Combatant& c = mParty[i];
        bool wasDown = !c.Alive();
        int before = c.hp;
        std::vector<std::string> messages;
        if (!ApplyItemEffect(effect, c, &messages)) {
            continue;
        }
        for (const std::string& m : messages) {
            BattleEvent e = Ev(c.hp > before || wasDown ? BattleEvent::Type::Heal : BattleEvent::Type::Message, false,
                               mActive, false, i, c.hp - before, m);
            out.push_back(e);
        }
    }
    mIcons.UseFull();
    AfterAction(out);
    return out;
}

Events Battle::Guard() {
    Events out;
    if (mPhase != BattlePhase::PartyCommand) {
        return out;
    }
    mParty[mActive].guarding = true;
    out.push_back(Msg(mParty[mActive].name + " guards."));
    mIcons.UseFull();
    AfterAction(out);
    return out;
}

Events Battle::Pass() {
    Events out;
    if (mPhase != BattlePhase::PartyCommand) {
        return out;
    }
    out.push_back(Msg(mParty[mActive].name + " passes."));
    mIcons.UseHalf();
    AfterAction(out);
    return out;
}

Events Battle::Analyze(int enemy) {
    Events out;
    if (mPhase != BattlePhase::PartyCommand || enemy < 0 || enemy >= (int)mEnemies.size()) {
        return out;
    }
    out.push_back(Ev(BattleEvent::Type::Analyze, false, mActive, true, enemy, 0,
                     "Navi analyzes " + mEnemies[enemy].name + "."));
    mIcons.UseHalf();
    AfterAction(out);
    return out;
}

Events Battle::Run() {
    Events out;
    if (mPhase != BattlePhase::PartyCommand) {
        return out;
    }
    float partyAg = 0, enemyAg = 0;
    int pn = 0, en = 0;
    for (const Combatant& c : mParty) {
        if (c.Alive()) {
            partyAg += c.ag;
            pn++;
        }
    }
    for (const Combatant& c : mEnemies) {
        if (c.Alive()) {
            enemyAg += c.ag;
            en++;
        }
    }
    float chance = 0.5f + ((pn ? partyAg / pn : 0) - (en ? enemyAg / en : 0)) * 0.03f;
    if (mRand() < std::clamp(chance, 0.1f, 0.95f)) {
        mPhase = BattlePhase::Escaped;
        out.push_back(Ev(BattleEvent::Type::Escaped, false, mActive, false, -1, 0, "Escaped!"));
        return out;
    }
    out.push_back(Msg("Couldn't escape!"));
    mIcons.UseAll();
    out.push_back(Ev(BattleEvent::Type::TurnLost, false, mActive, false, -1, 0, "The turn is lost!"));
    AfterAction(out);
    return out;
}

Events Battle::EnemyAct() {
    Events out;
    if (mPhase != BattlePhase::EnemyTurn || mEnemyCursor >= (int)mEnemies.size()) {
        return out;
    }
    int ei = mEnemyCursor;
    Combatant& enemy = mEnemies[ei];

    // Pick an affordable skill at random
    std::vector<int> usable;
    for (int i = 0; i < (int)enemy.skills.size(); i++) {
        if (enemy.mp >= enemy.skills[i].mpCost && enemy.hp > enemy.skills[i].hpCost) {
            usable.push_back(i);
        }
    }
    int si = usable.empty() ? 0 : usable[std::min((int)usable.size() - 1, (int)(mRand() * usable.size()))];
    const Skill s = enemy.skills[si];
    enemy.mp -= s.mpCost;
    enemy.hp -= s.hpCost;

    out.push_back(Msg(enemy.name + (si == 0 ? " attacks!" : " uses " + s.name + "!")));

    std::vector<int> targets;
    if (s.target == TargetType::AllEnemies) {
        for (int i = 0; i < (int)mParty.size(); i++) {
            if (mParty[i].Alive()) {
                targets.push_back(i);
            }
        }
    } else {
        // Enemies know weaknesses: usually go for a party member weak to this attack
        std::vector<int> alive, weak;
        for (int i = 0; i < (int)mParty.size(); i++) {
            if (mParty[i].Alive()) {
                alive.push_back(i);
                if (mParty[i].AffinityTo(s.element) == Affinity::Weak) {
                    weak.push_back(i);
                }
            }
        }
        const std::vector<int>& pool = (!weak.empty() && mRand() < 0.6f) ? weak : alive;
        if (!pool.empty()) {
            targets.push_back(pool[std::min((int)pool.size() - 1, (int)(mRand() * pool.size()))]);
        }
    }

    Outcome worst = Outcome::Normal;
    for (int t : targets) {
        worst = std::max(worst, ApplySkill(enemy, true, ei, s, false, t, out));
        if (!enemy.Alive()) {
            break;
        }
    }
    SpendIcons(worst);
    if (worst == Outcome::RepelOrDrain || worst == Outcome::MissOrNull) {
        out.push_back(Ev(BattleEvent::Type::TurnLost, true, ei, false, -1, 0,
                         worst == Outcome::RepelOrDrain ? "The enemy loses its turn!" : "The enemy loses turns!"));
    }
    AfterAction(out);
    return out;
}

int Battle::XpEarned() const {
    int xp = 0;
    for (const Combatant& e : mEnemies) {
        xp += e.xpReward;
    }
    return xp;
}

int Battle::RupeesEarned() {
    if (mRupees < 0) {
        mRupees = 0;
        for (const Combatant& e : mEnemies) {
            int span = std::max(0, e.rupeeMax - e.rupeeMin);
            mRupees += e.rupeeMin + std::min(span, (int)(mRand() * (span + 1)));
        }
    }
    return mRupees;
}

} // namespace Nocturne
