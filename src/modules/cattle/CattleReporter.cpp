#include "CattleReporter.h"
#include "CattlePresence.h"
#include "CattleTable.h"
#include "CattleBLEScanner.h"
#include "target_specific.h"  // For ARCH_NRF52 define

#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>

// Meshtastic mesh sending APIs
#include "Router.h"
#include "MeshService.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "mesh/generated/meshtastic/portnums.pb.h"  // for meshtastic_PortNum enum
#include <string.h>  // for memcpy, memset


// Forward declaration
static void sendCattlePayloadToMesh(const uint8_t *data, size_t len);

// External references to global router and service instances
extern Router *router;
extern MeshService *service;

static uint32_t g_lastSendMs = 0;
static const uint32_t CATTLE_REPORT_INTERVAL_MS = 30000; // 30 seconds for testing (change back to 180000 for production)
static const uint32_t CATTLE_TAG_AGE_THRESHOLD_SEC = 60; // Remove tags not seen in 60 seconds (2-3 scan cycles)
static const size_t CATTLE_MAX_TAGS_PER_PACKET = 12; // Cap at 12 tags per packet to control airtime

void CattleReporter_Init(void){
    g_lastSendMs = 0;

    // Ensure Serial is up for debug
    Serial.begin(115200);
    delay(100);
    Serial.println("CattleReporter_Init: starting");

#if defined(ARCH_NRF52) || defined(NRF52840_XXAA) || defined(NRF52832_XXAA)
    Serial.println("CattleReporter: nRF52 detected, initializing BLE scanner");
    // Initialize BLE scanner for real cattle tags
    CattleBLEScanner_Init();
    
    // Fill with test data as fallback (will be replaced by real BLE scans)
    // Cattle_FillTestData(); // Commented out - using real BLE tags now
#else
    // Non-nRF52: just use test data
    Serial.println("CattleReporter: using test data (BLE scanning not available)");
    Serial.print("CattleReporter: ARCH_NRF52=");
    #ifdef ARCH_NRF52
    Serial.println("defined");
    #else
    Serial.println("NOT defined");
    #endif
    Cattle_FillTestData();
#endif
}

void CattleReporter_Poll(void){
#ifdef ARCH_NRF52
    // Poll BLE scanner (runs in background, but we call it here for consistency)
    CattleBLEScanner_Poll();
#endif

    uint32_t now = millis();

    // Periodically clean up stale tags (every scan cycle)
    static uint32_t g_lastCleanupMs = 0;
    if(now - g_lastCleanupMs > 10000){ // Cleanup every 10 seconds
        size_t removed = Cattle_RemoveStaleTags(CATTLE_TAG_AGE_THRESHOLD_SEC);
        if(removed > 0){
            Serial.print("CattleReporter: removed ");
            Serial.print((int)removed);
            Serial.println(" stale tags");
        }
        g_lastCleanupMs = now;
    }

    // Check if it's time to send a report
    if(g_lastSendMs != 0){
        uint32_t elapsed = now - g_lastSendMs;
        if(elapsed < CATTLE_REPORT_INTERVAL_MS){
            return; // Not time yet
        }
    }

    g_lastSendMs = now;

    // Get tags and sort by recency (most recently seen first)
    // This ensures we prioritize tags that are currently present
    size_t tagCount = 0;
    const CattleTag *tags = Cattle_GetTags(&tagCount);
    if(!tags || tagCount == 0){
        // Serial.println("CattleReporter: no tags to report"); // Reduce log noise
        return; // nothing to report
    }

    // Sort tags by recency (newest first) so we send the most relevant tags
    Cattle_SortByRecency();

    // Re-get tags after sorting (they're already sorted in-place)
    tags = Cattle_GetTags(&tagCount);

    // Build payload with tag limit
    uint8_t buf[200];  // adjust size as needed
    uint16_t nodeId = (uint16_t)nodeDB->getNodeNum();

    size_t written = buildCattlePresencePayload(
        buf,
        sizeof(buf),
        nodeId,
        tags,
        tagCount,
        CATTLE_MAX_TAGS_PER_PACKET  // Cap at reasonable number
    );

    if(written == 0){
        Serial.println("CattleReporter: buildCattlePresencePayload returned 0, nothing to send");
        return;
    }

    // Debug: show how many tags we're sending vs total
    Serial.print("CattleReporter: sending ");
    Serial.print((int)(tagCount > CATTLE_MAX_TAGS_PER_PACKET ? CATTLE_MAX_TAGS_PER_PACKET : tagCount));
    Serial.print(" of ");
    Serial.print((int)tagCount);
    Serial.print(" tags, payload bytes=");
    Serial.println((int)written);

    sendCattlePayloadToMesh(buf, written);
}

// Send cattle payload to mesh using Meshtastic's Router API
static void sendCattlePayloadToMesh(const uint8_t *data, size_t len){
    if(!data || len == 0 || !router || !service){
        Serial.println("CattleReporter: sendCattlePayloadToMesh - invalid params or router/service not available");
        return;
    }

    // Allocate a packet from the router
    meshtastic_MeshPacket *p = router->allocForSending();
    if(!p){
        Serial.println("CattleReporter: failed to allocate packet");
        return;
    }

    // Set packet destination (broadcast)
    p->to = NODENUM_BROADCAST;

    // Set packet properties
    p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
    p->want_ack = false;
    p->hop_limit = 3;

    // Set our custom port number (cast to enum type)
    p->decoded.portnum = (meshtastic_PortNum)CATTLE_MESH_PORTNUM;
    p->decoded.want_response = false;

    // Copy payload data
    if(len > sizeof(p->decoded.payload.bytes)){
        Serial.print("CattleReporter: payload too large (");
        Serial.print((int)len);
        Serial.print(" > ");
        Serial.print((int)sizeof(p->decoded.payload.bytes));
        Serial.println(")");
        return;
    }

    p->decoded.payload.size = (uint32_t)len;
    memcpy(p->decoded.payload.bytes, data, len);

    // Send via MeshService
    service->sendToMesh(p, RX_SRC_LOCAL);
    
    Serial.println("CattleReporter: packet sent to mesh");
}
