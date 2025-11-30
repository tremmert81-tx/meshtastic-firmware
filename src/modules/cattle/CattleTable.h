#pragma once

#include "CattlePresence.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CATTLE_MAX_TAGS  32  // we can raise this later

// Global tag table accessors
const CattleTag* Cattle_GetTags(size_t *outCount);

// Update or add a tag to the table
// Returns: 1 if new tag was added, 2 if existing tag was updated, 0 if table is full
// Use this to distinguish between new additions and updates
int Cattle_UpdateTag(uint16_t tagId, int8_t rssi, uint8_t status);

// Remove tags that haven't been seen in X seconds
// Returns number of tags removed
size_t Cattle_RemoveStaleTags(uint32_t ageThresholdSec);

// Sort tags by most recently seen (newest first)
void Cattle_SortByRecency(void);

// Sort tags by strongest RSSI (strongest first)
void Cattle_SortByRssi(void);

// Temporary test helper: fill table with fake data
void Cattle_FillTestData(void);

#ifdef __cplusplus
}
#endif
