#include "CattlePresence.h"
#include <string.h>  // for memset

// You can tweak these defaults later
static const uint8_t CATTLE_PROTOCOL_VERSION = 0x01;
static const uint8_t CATTLE_MSG_TYPE_PRESENCE = 0x42;

size_t buildCattlePresencePayload(uint8_t *buf,
                                  size_t maxLen,
                                  uint16_t nodeId,
                                  const CattleTag *tags,
                                  size_t tagCount,
                                  size_t maxTagsToInclude)
{
    if(!buf || !tags || maxLen < sizeof(CattlePresenceHeader)){
        return 0;
    }

    // How many tags can we actually fit?
    size_t maxBySize;
    if(maxLen <= sizeof(CattlePresenceHeader)){
        maxBySize = 0;
    }else{
        maxBySize = (maxLen - sizeof(CattlePresenceHeader)) / sizeof(CattleTagEntry);
    }

    size_t count = tagCount;
    if(count > maxTagsToInclude){
        count = maxTagsToInclude;
    }
    if(count > maxBySize){
        count = maxBySize;
    }

    if(count == 0){
        // Nothing to send
        return 0;
    }

    uint8_t *p = buf;

    CattlePresenceHeader header;
    header.version   = CATTLE_PROTOCOL_VERSION;
    header.msgType   = CATTLE_MSG_TYPE_PRESENCE;
    header.nodeId    = nodeId;
    header.tagCount  = (uint16_t)count;

    memcpy(p, &header, sizeof(header));
    p += sizeof(header);

    for(size_t i = 0; i < count; ++i){
        const CattleTag &ct = tags[i];

        CattleTagEntry entry;
        entry.tagId = ct.tagId;
        entry.rssi  = ct.lastRssi;
        entry.status = ct.status;

        memcpy(p, &entry, sizeof(entry));
        p += sizeof(entry);
    }

    return (size_t)(p - buf);
}
