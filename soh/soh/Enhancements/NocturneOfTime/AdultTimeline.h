#pragma once

// Nocturne of Time is set in an alternate adult timeline: while the mod is on, Link is always adult.
#ifdef __cplusplus
extern "C" {
#endif

// Called at the start of Play_Init, before the scene layer and Link's object are chosen.
void Nocturne_BeforePlayInit(void);

// True while the mod (and its adult timeline) is on. Loading a save then returns Link to where he saved
// rather than the Temple of Time.
int Nocturne_AdultTimelineOn(void);

// The wallet: no vanilla 99/200/500 limit while the mod is on (9999, shown with four digits).
#define NOCTURNE_WALLET_MAX 9999
int Nocturne_WalletCapacity(int vanillaCapacity);

#ifdef __cplusplus
}
#endif
