#ifndef LEOPROXY_HANDOVER_LEOLINKCONTROLLER_H
#define LEOPROXY_HANDOVER_LEOLINKCONTROLLER_H

#include <omnetpp.h>

#include "satchannel/LeoSatLinkChannel.h"

namespace leoproxy {

class LeoLinkController : public omnetpp::cSimpleModule, public omnetpp::cListener
{
  protected:
    // channel directions of the chain
    LeoSatLinkChannel *userUp = nullptr;      // terminal -> satellite (rate-limited uplink)
    LeoSatLinkChannel *userDown = nullptr;    // satellite -> terminal
    LeoSatLinkChannel *feederUp = nullptr;    // gateway -> satellite (rate-limited downlink entry)
    LeoSatLinkChannel *feederDown = nullptr;  // satellite -> gateway

    omnetpp::cModule *gatewayQueue = nullptr;   // PPP queue of gateway towards the satellite
    omnetpp::cModule *terminalQueue = nullptr;  // PPP queue of terminal towards the satellite
    uint64_t downlinkQueueDrops = 0, uplinkQueueDrops = 0;

    double linkCapacity = 0;
    double altitude = 0, earthRadius = 0;
    double userMinEl = 0, userMaxEl = 0, feederMinEl = 0, feederMaxEl = 0;
    bool handoverEnabled = true;
    omnetpp::simtime_t passStart, passDuration;
    omnetpp::simtime_t geometryInterval, meterInterval;

    omnetpp::cMessage *geometryTimer = nullptr;
    omnetpp::cMessage *meterTimer = nullptr;
    omnetpp::cMessage *handoverTimer = nullptr;

    omnetpp::simsignal_t dlUtilSignal, ulUtilSignal, dlTputSignal, ulTputSignal, dlLossSignal, ulLossSignal,
            sentSignal, lostSignal, queueDropSignal, delaySignal, handoverSignal, packetDroppedSignal;

  protected:
    virtual int numInitStages() const override { return 2; }
    virtual void initialize(int stage) override;
    virtual void handleMessage(omnetpp::cMessage *msg) override;
    virtual void finish() override;
    using omnetpp::cListener::finish;   // keep cListener::finish(cComponent*, simsignal_t) visible
    virtual ~LeoLinkController();
    virtual void receiveSignal(omnetpp::cComponent *source, omnetpp::simsignal_t signalID, omnetpp::cObject *obj, omnetpp::cObject *details) override;

    void discoverChannels();
    omnetpp::cModule *findQueueTowards(omnetpp::cModule *node, LeoSatLinkChannel *channel);
    static double slantRangeKm(double elevationRad, double altitudeKm, double earthRadiusKm);
    void updateGeometry();
    void doHandover();
    void meter();
};

} // namespace leoproxy

#endif
