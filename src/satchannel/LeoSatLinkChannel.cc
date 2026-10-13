#include "satchannel/LeoSatLinkChannel.h"

namespace leoproxy {

using namespace omnetpp;

Define_Channel(LeoSatLinkChannel);

void LeoSatLinkChannel::initialize()
{
    cDatarateChannel::initialize();
    frameLostSignal = registerSignal("satFrameLost");
    outageUntil = SIMTIME_ZERO;
    readLossParams();
}

void LeoSatLinkChannel::readLossParams()
{
    meanLossRate = par("meanLossRate").doubleValue();
    meanBurstLength = par("meanBurstLength").doubleValue();
    lossEnabled = par("lossEnabled").boolValue();
    if (meanLossRate < 0 || meanLossRate >= 1)
        throw cRuntimeError("meanLossRate must be in [0,1)");
    if (meanBurstLength < 1)
        throw cRuntimeError("meanBurstLength must be >= 1 packet");
    // Gilbert model: loss probability 1 in Bad, 0 in Good.
    // mean burst length L = 1/pBG ; stationary P(Bad) = pGB/(pGB+pBG) = meanLossRate
    pBadToGood = 1.0 / meanBurstLength;
    pGoodToBad = meanLossRate > 0 ? meanLossRate * pBadToGood / (1.0 - meanLossRate) : 0.0;
}

void LeoSatLinkChannel::handleParameterChange(const char *parname)
{
    cDatarateChannel::handleParameterChange(parname);
    if (parname == nullptr || !strcmp(parname, "meanLossRate") || !strcmp(parname, "meanBurstLength") || !strcmp(parname, "lossEnabled"))
        readLossParams();
}

cChannel::Result LeoSatLinkChannel::processMessage(cMessage *msg, const SendOptions& options, simtime_t t)
{
    Result result = cDatarateChannel::processMessage(msg, options, t);
    if (!msg->isPacket() || result.discard)
        return result;
    cPacket *pkt = static_cast<cPacket *>(msg);
    if (pkt->isUpdate())
        return result;

    txPackets++;
    txBits += pkt->getBitLength();

    if (t < outageUntil) {
        outagePackets++;
        totalOutage++;
        result.discard = true;
        emit(frameLostSignal, (intval_t)2);
        return result;
    }
    if (lossEnabled && meanLossRate > 0) {
        // advance the Gilbert-Elliott chain by one packet, then decide loss
        if (badState) {
            if (dblrand() < pBadToGood)
                badState = false;
        }
        else {
            if (dblrand() < pGoodToBad)
                badState = true;
        }
        if (badState) {
            lostPackets++;
            totalLost++;
            result.discard = true;
            emit(frameLostSignal, (intval_t)1);
        }
    }
    return result;
}

} // namespace leoproxy
