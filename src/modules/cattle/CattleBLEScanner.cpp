#include "CattleBLEScanner.h"
#include "CattleTable.h"
#include "main.h"  // For nrf52Bluetooth
#include <Arduino.h>
#include <string.h>

// Check for nRF52 platform - try multiple detection methods
// NRF52840_XXAA and NRF52832_XXAA are defined by the board files
#if defined(ARCH_NRF52) || defined(ARDUINO_NRF52840_XXAA) || defined(ARDUINO_NRF52832_XXAA) || \
    defined(NRF52840_XXAA) || defined(NRF52832_XXAA)
#ifndef ARCH_NRF52
#define ARCH_NRF52  // Define it if not already defined
#endif
#include <bluefruit.h>
#elif __has_include("platform/nrf52/architecture.h")
#include "platform/nrf52/architecture.h"
#include <bluefruit.h>
#endif

// Configuration - adjust these to filter your specific cattle tags
// Set bytes to 0 to allow any value for that byte position
// Example: {0xBC, 0, 0} matches any MAC starting with BC
uint8_t g_cattleTagMacPrefix[3] = {0xBC, 0, 0};  // Filter: MACs starting with BC
uint16_t g_cattleTagManufacturerId = 0;       // Disabled by default - set to your tag's manufacturer ID

#ifdef ARCH_NRF52

// BLE scan state
static bool g_scanning = false;
static bool g_advertisingWasActive = false;
static uint32_t g_lastScanStartMs = 0;
static uint32_t g_lastScanAttemptMs = 0;
static const uint32_t SCAN_INTERVAL_MS = 60000;  // Try to scan every 10 seconds
static const uint32_t SCAN_DURATION_MS = 3000;   // Scan for 3 seconds each time

// Helper to convert MAC address to a short tag ID
// Uses last 2 bytes of MAC address
static uint16_t macToTagId(const uint8_t mac[6]) {
    return (uint16_t)((mac[4] << 8) | mac[5]);
}

// Check if a MAC address matches our filter
static bool matchesFilter(const uint8_t mac[6]) {
    // If MAC prefix filter is set (not all zeros), check it
    // A value of 0 in the filter means "any value" for that byte position
    // All zeros means "no filter" - accept all devices for debugging
    bool filterEnabled = (g_cattleTagMacPrefix[0] != 0 || g_cattleTagMacPrefix[1] != 0 || g_cattleTagMacPrefix[2] != 0);
    
    if (filterEnabled) {
        // Filter is enabled - check each byte (0 means "any value")
        if (g_cattleTagMacPrefix[0] != 0 && mac[0] != g_cattleTagMacPrefix[0]) {
            return false;  // First byte doesn't match
        }
        if (g_cattleTagMacPrefix[1] != 0 && mac[1] != g_cattleTagMacPrefix[1]) {
            return false;  // Second byte doesn't match
        }
        if (g_cattleTagMacPrefix[2] != 0 && mac[2] != g_cattleTagMacPrefix[2]) {
            return false;  // Third byte doesn't match
        }
    }
    // No filter or matches filter - accept it
    return true;
}

// BLE scan callback - Bluefruit library callback signature
static void scan_callback(ble_gap_evt_adv_report_t* report) {
    // Get MAC address (reverse order for BLE)
    uint8_t mac[6];
    // BLE addresses are in reverse order
    for (int i = 0; i < 6; i++) {
        mac[i] = report->peer_addr.addr[5 - i];
    }
    
    // Check if it matches our filter
    bool filterEnabled = (g_cattleTagMacPrefix[0] != 0 || g_cattleTagMacPrefix[1] != 0 || g_cattleTagMacPrefix[2] != 0);
    
    // Check if it matches our filter
    if (!matchesFilter(mac)) {
        // Log filtered-out devices only when filter is disabled (for debugging)
        if (!filterEnabled) {
            Serial.print("BLE: MAC=");
            for (int i = 0; i < 6; i++) {
                if (i > 0) Serial.print(":");
                if (mac[i] < 16) Serial.print("0");
                Serial.print(mac[i], HEX);
            }
            Serial.print(" RSSI=");
            Serial.print((int)report->rssi);
            Serial.println(" (no filter - all devices logged)");
        }
        // Continue scanning
        Bluefruit.Scanner.resume();
        return;
    }
    
    // Convert MAC to tag ID
    uint16_t tagId = macToTagId(mac);
    
    // Log the found cattle tag
    Serial.print("CATTLE TAG: ID=");
    Serial.print(tagId);
    Serial.print(" MAC=");
    for (int i = 0; i < 6; i++) {
        if (i > 0) Serial.print(":");
        if (mac[i] < 16) Serial.print("0");
        Serial.print(mac[i], HEX);
    }
    Serial.print(" RSSI=");
    Serial.print((int)report->rssi);
    Serial.println();
    
    // Update cattle table
    uint8_t status = 0x01;  // Default status - you can parse from advertisement data if needed
    int result = Cattle_UpdateTag(tagId, report->rssi, status);
    if (result == 1) {
        // New tag added - only log occasionally to reduce noise
        // Serial.print("  -> New tag added to table");
    } else if (result == 2) {
        // Existing tag updated - don't log every update
        // Serial.print("  -> Tag updated");
    } else {
        Serial.print("  -> Table full, could not add");
    }
    // Only log on errors to reduce serial spam
    
    // Continue scanning
    Bluefruit.Scanner.resume();
}

void CattleBLEScanner_Init(void) {
    Serial.println("CattleBLEScanner_Init: starting BLE scanner (time-multiplexed mode)");
    
    // Note: Bluefruit should already be initialized by NRF52Bluetooth::setup()
    // On nRF52, we use time-multiplexing: stop advertising, scan briefly, then resume advertising
    
    // Initialize Bluefruit Central (scanner mode)
    // Set callbacks (NULL means we don't want to connect, just scan)
    Bluefruit.Central.setConnectCallback(NULL);
    Bluefruit.Central.setDisconnectCallback(NULL);
    
    // Configure scanner
    Bluefruit.Scanner.setRxCallback(scan_callback);
    Bluefruit.Scanner.useActiveScan(false);  // Passive scan to save power
    Bluefruit.Scanner.setInterval(160, 80);  // Scan interval in units of 0.625ms (100ms, 50ms)
    
    g_scanning = false;
    g_advertisingWasActive = false;
    g_lastScanStartMs = 0;
    g_lastScanAttemptMs = 0;
    
    Serial.println("CattleBLEScanner: Configured for time-multiplexed scanning");
    Serial.println("CattleBLEScanner: Will temporarily stop advertising to scan for BLE devices");
    
    // Show filter status
    if (g_cattleTagMacPrefix[0] != 0 || g_cattleTagMacPrefix[1] != 0 || g_cattleTagMacPrefix[2] != 0) {
        Serial.print("CattleBLEScanner: MAC filter active: ");
        for (int i = 0; i < 3; i++) {
            if (i > 0) Serial.print(":");
            if (g_cattleTagMacPrefix[i] == 0) {
                Serial.print("XX");
            } else {
                if (g_cattleTagMacPrefix[i] < 16) Serial.print("0");
                Serial.print(g_cattleTagMacPrefix[i], HEX);
            }
        }
        Serial.println(" (0 = any value)");
    } else {
        Serial.println("CattleBLEScanner: No MAC filter - logging all BLE devices");
    }
}

void CattleBLEScanner_Poll(void) {
    uint32_t now = millis();
    
    // If we're currently scanning, check if it's time to stop
    if (g_scanning) {
        if (now - g_lastScanStartMs >= SCAN_DURATION_MS) {
            // Time to stop scanning and resume advertising
            Serial.println("CattleBLEScanner: Stopping scan, resuming advertising");
            Bluefruit.Scanner.stop();
            g_scanning = false;
            
            // Resume advertising if it was active before
            if (g_advertisingWasActive && nrf52Bluetooth) {
                // Only resume if not connected (don't interrupt active connections)
                if (!nrf52Bluetooth->isConnected()) {
                    nrf52Bluetooth->resumeAdvertising();
                    Serial.println("CattleBLEScanner: Advertising resumed");
                } else {
                    Serial.println("CattleBLEScanner: BLE connected, skipping advertising resume");
                }
            }
            g_advertisingWasActive = false;
        }
        return;  // Currently scanning, don't start a new scan
    }
    
    // Check if it's time to start a new scan
    if (now - g_lastScanAttemptMs < SCAN_INTERVAL_MS) {
        return;  // Not time yet
    }
    
    // Don't scan if there's an active BLE connection (would interrupt it)
    if (nrf52Bluetooth && nrf52Bluetooth->isConnected()) {
        // Skip this scan cycle, but update timestamp to avoid constant checks
        g_lastScanAttemptMs = now;
        return;
    }
    
    // Try to start scanning by temporarily stopping advertising
    Serial.println("CattleBLEScanner: Starting scan (temporarily stopping advertising)");
    
    // Check if advertising is currently active
    // We'll assume it is (since Meshtastic usually advertises), but we'll try to stop it
    g_advertisingWasActive = true;  // Assume advertising was active
    
    // Stop advertising to allow scanning
    Bluefruit.Advertising.stop();
    
    // Small delay to let advertising fully stop
    delay(50);
    
    // Try to start scanning
    if (Bluefruit.Scanner.start(0)) {
        g_scanning = true;
        g_lastScanStartMs = now;
        g_lastScanAttemptMs = now;
        Serial.println("CattleBLEScanner: Scan started successfully");
    } else {
        // Failed to start scanning - resume advertising
        Serial.println("CattleBLEScanner: Failed to start scan, resuming advertising");
        if (nrf52Bluetooth) {
            nrf52Bluetooth->resumeAdvertising();
        }
        g_advertisingWasActive = false;
        g_lastScanAttemptMs = now;  // Wait before trying again
    }
}

#else
// Non-nRF52 platforms - stub implementation
void CattleBLEScanner_Init(void) {
    Serial.println("CattleBLEScanner_Init: BLE scanning not supported on this platform");
}

void CattleBLEScanner_Poll(void) {
    // No-op on non-nRF52
}
#endif

