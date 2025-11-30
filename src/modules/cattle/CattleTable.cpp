#include "CattleTable.h"
#include <Arduino.h>
#include <string.h>

static CattleTag g_tags[CATTLE_MAX_TAGS];
static size_t g_tagCount = 0;

const CattleTag* Cattle_GetTags(size_t *outCount){
    if(outCount){
        *outCount = g_tagCount;
    }
    return g_tags;
}

int Cattle_UpdateTag(uint16_t tagId, int8_t rssi, uint8_t status){
    uint32_t nowSec = millis() / 1000;
    
    // Try to find existing tag
    for(size_t i = 0; i < g_tagCount; ++i){
        if(g_tags[i].tagId == tagId){
            // Update existing tag - use stronger RSSI if available
            if(rssi > g_tags[i].lastRssi){
                g_tags[i].lastRssi = rssi;
            }
            g_tags[i].status = status;
            g_tags[i].lastSeenSec = nowSec;
            return 2;  // Updated existing tag
        }
    }
    
    // Tag not found, add new one if we have space
    if(g_tagCount >= CATTLE_MAX_TAGS){
        return 0;  // Table full
    }
    
    g_tags[g_tagCount].tagId = tagId;
    g_tags[g_tagCount].lastRssi = rssi;
    g_tags[g_tagCount].status = status;
    g_tags[g_tagCount].lastSeenSec = nowSec;
    g_tagCount++;
    
    return 1;  // Added new tag
}

size_t Cattle_RemoveStaleTags(uint32_t ageThresholdSec){
    uint32_t nowSec = millis() / 1000;
    size_t removed = 0;
    
    // Remove tags that haven't been seen recently
    for(size_t i = g_tagCount; i > 0; --i){
        size_t idx = i - 1;
        uint32_t age = nowSec - g_tags[idx].lastSeenSec;
        
        if(age > ageThresholdSec){
            // Remove this tag by shifting remaining tags down
            for(size_t j = idx; j < g_tagCount - 1; ++j){
                g_tags[j] = g_tags[j + 1];
            }
            g_tagCount--;
            removed++;
        }
    }
    
    return removed;
}

void Cattle_SortByRecency(void){
    // Simple bubble sort by lastSeenSec (newest first)
    for(size_t i = 0; i < g_tagCount - 1; ++i){
        for(size_t j = 0; j < g_tagCount - i - 1; ++j){
            if(g_tags[j].lastSeenSec < g_tags[j + 1].lastSeenSec){
                CattleTag temp = g_tags[j];
                g_tags[j] = g_tags[j + 1];
                g_tags[j + 1] = temp;
            }
        }
    }
}

void Cattle_SortByRssi(void){
    // Simple bubble sort by RSSI (strongest first)
    for(size_t i = 0; i < g_tagCount - 1; ++i){
        for(size_t j = 0; j < g_tagCount - i - 1; ++j){
            if(g_tags[j].lastRssi < g_tags[j + 1].lastRssi){
                CattleTag temp = g_tags[j];
                g_tags[j] = g_tags[j + 1];
                g_tags[j + 1] = temp;
            }
        }
    }
}

// For now, create a few fake tags so we can see packets on the mesh.
// Later, this will be replaced by the BLE scanner updating g_tags.
void Cattle_FillTestData(void){
    g_tagCount = 0;
    memset(g_tags, 0, sizeof(g_tags));

    // Tag 1001
    g_tags[g_tagCount].tagId       = 1001;
    g_tags[g_tagCount].status      = 0x01;     // arbitrary flag
    g_tags[g_tagCount].lastRssi    = -55;
    g_tags[g_tagCount].lastSeenSec = 10;
    g_tagCount++;

    // Tag 1002
    g_tags[g_tagCount].tagId       = 1002;
    g_tags[g_tagCount].status      = 0x02;
    g_tags[g_tagCount].lastRssi    = -60;
    g_tags[g_tagCount].lastSeenSec = 20;
    g_tagCount++;

    // You can add more fake entries here if desired
}
