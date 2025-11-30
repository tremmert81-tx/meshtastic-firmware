#pragma once
#include "SinglePortModule.h"
#include "modules/cattle/CattlePresence.h"

/**
 * Cattle presence module - receives and processes cattle tag presence reports from the mesh
 */
class CattleModule : public SinglePortModule
{
  public:
    /** Constructor */
    CattleModule() : SinglePortModule("cattle", (meshtastic_PortNum)CATTLE_MESH_PORTNUM)
    {
        loopbackOk = true; // Allow locally generated messages to loop back for debugging
    }

  protected:
    /** Called to handle a particular incoming message */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};

extern CattleModule *cattleModule;

