#pragma once

// Nocturne of Time is set in an alternate adult timeline: while the mod is on, Link is always adult.
#ifdef __cplusplus
extern "C" {
#endif

// Called at the start of Play_Init, before the scene layer and Link's object are chosen.
void Nocturne_BeforePlayInit(void);

#ifdef __cplusplus
}
#endif
