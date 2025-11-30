#pragma once

#include <stdint.h>
#include <stddef.h>

// Mesh port number for cattle presence messages
#define CATTLE_MESH_PORTNUM  80  // arbitrary custom app port

#ifdef __cplusplus
extern "C" {
#endif

// In-memory representation of a single tag the node has seen
typedef struct{
    uint16_t tagId;        // Short ID mapped from BLE MAC
    uint8_t  status;       // Bit-packed status flags
    int8_t   lastRssi;     // dBm
    uint32_t lastSeenSec;  // Seconds since boot or epoch
} CattleTag;

// On-air packet layout: header, followed by N tag entries

#pragma pack(push, 1)

typedef struct{
    uint8_t  version;    // e.g. 0x01
    uint8_t  msgType;    // e.g. 0x42 = CATTLE_PRESENCE
    uint16_t nodeId;     // Short ID for this node
    uint16_t tagCount;   // Number of CattleTagEntry records that follow
} CattlePresenceHeader;

typedef struct{
    uint16_t tagId;
    int8_t   rssi;
    uint8_t  status;
} CattleTagEntry;

#pragma pack(pop)

// Build a cattle presence payload into buf.
// Returns number of bytes written, or 0 on error.
size_t buildCattlePresencePayload(uint8_t *buf,
                                  size_t maxLen,
                                  uint16_t nodeId,
                                  const CattleTag *tags,
                                  size_t tagCount,
                                  size_t maxTagsToInclude);

#ifdef __cplusplus
}
#endif
