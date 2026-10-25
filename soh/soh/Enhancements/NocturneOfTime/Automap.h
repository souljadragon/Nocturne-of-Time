#pragma once

// Nocturne of Time: SMT-style automap for the dungeon crawler.

struct PlayState;
struct Player;

// Record the tile Link stands on (and its walls/doors). Called by the crawler every frame it's active.
void Automap_Update(PlayState* play, Player* player);

// Switch between the corner map and the large centred map.
void Automap_ToggleLarge();
