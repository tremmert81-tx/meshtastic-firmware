#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Call once at startup (init)
void CattleReporter_Init(void);

// Call periodically (e.g. every few minutes)
void CattleReporter_Poll(void);

#ifdef __cplusplus
}
#endif
