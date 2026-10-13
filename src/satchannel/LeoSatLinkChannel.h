#ifndef LEOPROXY_CHANNEL_LEOSATLINKCHANNEL_H
#define LEOPROXY_CHANNEL_LEOSATLINKCHANNEL_H

#include <omnetpp.h>

namespace leoproxy {

//
// cDatarateChannel with Gilbert-Elliott burst loss, handover outages and
// byte/packet counters read by LeoLinkController.
//
class LeoSatLinkChannel : public omnetpp::cDatarateChannel
{
  protected:
    double meanLossRate = 0;
    double meanBurstLength = 4;
    bool lossEnabled = true;
    double pGoodToBad = 0;     // per-packet transition probabilities
    double pBadToGood = 1;
    bool badState = false;
    omnetpp::simtime_t outageUntil;

    // counters (since last reset by the controller)
    uint64_t txBits = 0;
    uint64_t txPackets = 0;
    uint64_t lostPackets = 0;      // Gilbert-Elliott losses
    uint64_t outagePackets = 0;    // discarded during handover outage
    uint64_t totalLost = 0;
    uint64_t totalOutage = 0;

    omnetpp::simsignal_t frameLostSignal;

  protected:
    virtual void initialize() override;
    virtual void handleParameterChange(const char *parname) override;
    void readLossParams();

  public:
    LeoSatLinkChannel(const char *name = nullptr) : cDatarateChannel(name) {}
    virtual Result processMessage(omnetpp::cMessage *msg, const omnetpp::SendOptions& options, omnetpp::simtime_t t) override;

    void setPropagationDelay(double seconds) { setDelay(seconds); }
    void startOutage(omnetpp::simtime_t until) { outageUntil = until; }
    bool inOutage() const { return omnetpp::simTime() < outageUntil; }

    // returns and clears the window counters
    void collect(uint64_t& bits, uint64_t& packets, uint64_t& lost, uint64_t& outage)
    {
        bits = txBits; packets = txPackets; lost = lostPackets; outage = outagePackets;
        txBits = txPackets = lostPackets = outagePackets = 0;
    }
    uint64_t getTotalLost() const { return totalLost; }
    uint64_t getTotalOutage() const { return totalOutage; }
};

} // namespace leoproxy

#endif
