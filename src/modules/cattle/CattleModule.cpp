#include "CattleModule.h"
#include "NodeDB.h"
#include <Arduino.h>
#include <string.h>

CattleModule *cattleModule;

ProcessMessage CattleModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    auto &p = mp.decoded;
    
    // Check minimum payload size (header only)
    if (p.payload.size < sizeof(CattlePresenceHeader)) {
        LOG_WARN("Cattle: packet too small (%d bytes)", p.payload.size);
        return ProcessMessage::CONTINUE;
    }

    // Decode header
    CattlePresenceHeader header;
    memcpy(&header, p.payload.bytes, sizeof(header));

    // Validate header
    if (header.version != 0x01) {
        LOG_WARN("Cattle: unsupported version %d", header.version);
        return ProcessMessage::CONTINUE;
    }

    if (header.msgType != 0x42) {  // CATTLE_MSG_TYPE_PRESENCE
        LOG_WARN("Cattle: unknown msgType 0x%02x", header.msgType);
        return ProcessMessage::CONTINUE;
    }

    // Check payload size matches header
    size_t expectedSize = sizeof(CattlePresenceHeader) + (header.tagCount * sizeof(CattleTagEntry));
    if (p.payload.size < expectedSize) {
        LOG_WARN("Cattle: payload size mismatch (got %d, expected %d)", p.payload.size, expectedSize);
        return ProcessMessage::CONTINUE;
    }

    // Decode tags
    Serial.print("Cattle: from 0x");
    Serial.print(mp.from, HEX);
    Serial.print(", nodeId=");
    Serial.print(header.nodeId);
    Serial.print(", tags=[");

    const uint8_t *tagData = p.payload.bytes + sizeof(CattlePresenceHeader);
    for (uint16_t i = 0; i < header.tagCount; ++i) {
        CattleTagEntry entry;
        memcpy(&entry, tagData + (i * sizeof(CattleTagEntry)), sizeof(entry));

        if (i > 0) Serial.print(",");
        Serial.print("{");
        Serial.print(entry.tagId);
        Serial.print(",");
        Serial.print((int)entry.rssi);
        Serial.print(",0x");
        if (entry.status < 16) Serial.print('0');
        Serial.print(entry.status, HEX);
        Serial.print("}");
    }

    Serial.println("]");

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

