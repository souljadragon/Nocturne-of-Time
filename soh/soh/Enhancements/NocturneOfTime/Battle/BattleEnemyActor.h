#pragma once

// Nocturne of Time: a display-only enemy for battles. It draws an OoT enemy model with its idle animation and
// has no AI or collision; the battle scene moves, flashes and removes it.

struct Actor;
struct PlayState;

// Spawn a battle enemy of the given Nocturne::EnemyKind standing on `groundY`, facing `yaw`.
Actor* BattleEnemyActor_Spawn(PlayState* play, int kind, float x, float groundY, float z, short yaw);

// Start the defeat effect (shrinks away, then stops drawing; the battle removes it when it ends).
void BattleEnemyActor_Defeat(Actor* actor);

// Is this one of ours?
bool BattleEnemyActor_Is(Actor* actor);

// Auto-battle: animations and the defeat effect run at double speed.
void BattleEnemyActor_SetFast(bool fast);

// Shrink (or grow) an enemy, e.g. to fit a narrow corridor. Deku Babas keep their size.
void BattleEnemyActor_SetSize(Actor* actor, float factor);
