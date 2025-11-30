#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize BLE scanner (call once at startup)
void CattleBLEScanner_Init(void);

// Poll BLE scanner (call periodically in main loop)
void CattleBLEScanner_Poll(void);

// Configuration for filtering tags
// You can filter by MAC address prefix, manufacturer ID, or service UUID
// For now, we'll use MAC address prefix filtering

// MAC address prefix to filter (first 3 bytes in little-endian format)
// Example: {0xAA, 0xBB, 0xCC} matches MACs starting with AA:BB:CC
// Set to {0, 0, 0} to disable MAC filtering
extern uint8_t g_cattleTagMacPrefix[3];

// Manufacturer ID filter (0 = disabled)
// Many cattle tags use specific manufacturer IDs
extern uint16_t g_cattleTagManufacturerId;

#ifdef __cplusplus
}
#endif

