#pragma once

// Nocturne of Time: first-person grid movement in dungeons.

// True while the crawler controls Link and the camera (in an enabled dungeon and Link isn't busy).
bool DungeonCrawler_IsActive();

// The grid direction Link faces: 0 = +Z, 1 = +X, 2 = -Z, 3 = -X.
signed char DungeonCrawler_GetFacing();
