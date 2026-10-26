#pragma once

// Nocturne of Time: first-person grid movement in dungeons.

// True while the crawler controls Link and the camera (in an enabled dungeon and Link isn't busy).
bool DungeonCrawler_IsActive();

// The grid direction Link faces: 0 = +Z, 1 = +X, 2 = -Z, 3 = -X.
signed char DungeonCrawler_GetFacing();

// Turn to face grid direction `facing` (0..3), e.g. so a battle has room in front.
void DungeonCrawler_SetFacing(signed char facing);

// Tilt the first-person view up (positive) or down, in binary angle units; during battles it stays where it's
// put (boss cutscenes look up at the ceiling), otherwise the right stick takes over again.
void DungeonCrawler_SetPitch(short pitch);

// Turn to face any yaw (not only a grid direction), e.g. a boss room's centre.
void DungeonCrawler_FaceYaw(short yaw);
