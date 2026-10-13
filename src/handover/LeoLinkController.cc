#include "handover/LeoLinkController.h"

#include <cmath>

namespace leoproxy {

using namespace omnetpp;

Define_Module(LeoLinkController);

static const double SPEED_OF_LIGHT_KM_S = 299792.458;

LeoLinkController::~LeoLinkController()
{
    cancelAndDelete(geometryTimer);
    cancelAndDelete(meterTimer);
    cancelAndDelete(handoverTimer);
}

void LeoLinkController::initialize(int stage)
{
    if (stage == 0) {
        linkCapacity = par("linkCapacity").doubleValue();
        altitude = par("altitude").doubleValue();
        earthRadius = par("earthRadius").doubleValue();
        const double deg = M_PI / 180.0;
        userMinEl = par("userMinElevation").doubleValue() * deg;
        userMaxEl = par("userMaxElevation").doubleValue() * deg;
        feederMinEl = par("feederMinElevation").doubleValue() * deg;
        feederMaxEl = par("feederMaxElevation").doubleValue() * deg;
        if (userMinEl <= 0 || feederMinEl <= 0 || userMaxEl < userMinEl || feederMaxEl < feederMinEl || userMaxEl > M_PI / 2 || feederMaxEl > M_PI / 2)
            throw cRuntimeError("Invalid elevation range");
        handoverEnabled = par("handoverEnabled").boolValue();
        geometryInterval = par("geometryUpdateInterval");
        meterInterval = par("meterInterval");

        dlUtilSignal = registerSignal("satDownlinkUtilization");
        ulUtilSignal = registerSignal("satUplinkUtilization");
        dlTputSignal = registerSignal("satDownlinkThroughput");
        ulTputSignal = registerSignal("satUplinkThroughput");
        dlLossSignal = registerSignal("satDownlinkLossRate");
        ulLossSignal = registerSignal("satUplinkLossRate");
        sentSignal = registerSignal("satPacketsSent");
        lostSignal = registerSignal("satPacketsLost");
        queueDropSignal = registerSignal("satQueueDrops");
        delaySignal = registerSignal("satOneWayDelay");
        handoverSignal = registerSignal("handover");
        packetDroppedSignal = registerSignal("packetDropped");

        geometryTimer = new cMessage("geometry");
        meterTimer = new cMessage("meter");
        handoverTimer = new cMessage("handover");
    }
    else if (stage == 1) {
        discoverChannels();

        // start in the middle of a pass so that the three chains are not synchronised
        passDuration = par("handoverInterval");
        double phase = par("initialPassPhase").doubleValue();
        passStart = simTime() - passDuration * phase;
        updateGeometry();
        scheduleAt(simTime() + geometryInterval, geometryTimer);
        scheduleAt(simTime() + meterInterval, meterTimer);
        if (handoverEnabled)
            scheduleAt(passStart + passDuration, handoverTimer);

        WATCH(downlinkQueueDrops);
        WATCH(uplinkQueueDrops);
    }
}

void LeoLinkController::discoverChannels()
{
    cModule *ut = getModuleByPath(par("userTerminalModule").stringValue());
    cModule *sat = getModuleByPath(par("satelliteModule").stringValue());
    cModule *gs = getModuleByPath(par("groundStationModule").stringValue());
    if (!ut || !sat || !gs)
        throw cRuntimeError("Cannot resolve terminal/satellite/gateway modules");

    auto scan = [&](cModule *from, cModule *to) -> LeoSatLinkChannel * {
        int n = from->gateSize("pppg$o");
        for (int i = 0; i < n; i++) {
            cGate *g = from->gate("pppg$o", i);
            auto ch = dynamic_cast<LeoSatLinkChannel *>(g->getChannel());
            if (ch && g->getNextGate() && g->getNextGate()->getOwnerModule() == to)
                return ch;
        }
        return nullptr;
    };
    userUp = scan(ut, sat);
    userDown = scan(sat, ut);
    feederUp = scan(gs, sat);
    feederDown = scan(sat, gs);
    if (!userUp || !userDown || !feederUp || !feederDown)
        throw cRuntimeError("Satellite chain %s - %s - %s is not connected with LeoSatLinkChannel on all segments",
                ut->getFullPath().c_str(), sat->getFullPath().c_str(), gs->getFullPath().c_str());

    gatewayQueue = findQueueTowards(gs, feederUp);
    terminalQueue = findQueueTowards(ut, userUp);
    if (gatewayQueue)
        gatewayQueue->subscribe(packetDroppedSignal, this);
    if (terminalQueue)
        terminalQueue->subscribe(packetDroppedSignal, this);
}

cModule *LeoLinkController::findQueueTowards(cModule *node, LeoSatLinkChannel *channel)
{
    cGate *g = channel->getSourceGate();
    // INET node: pppg[k] is connected to ppp[k].phys
    cModule *ppp = node->getSubmodule("ppp", g->getIndex());
    if (!ppp)
        return nullptr;
    return ppp->getSubmodule("queue");
}

void LeoLinkController::receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj, cObject *details)
{
    if (signalID != packetDroppedSignal)
        return;
    if (source == gatewayQueue)
        downlinkQueueDrops++;
    else if (source == terminalQueue)
        uplinkQueueDrops++;
}

double LeoLinkController::slantRangeKm(double e, double h, double re)
{
    double r = re + h;
    return std::sqrt(r * r - std::pow(re * std::cos(e), 2)) - re * std::sin(e);
}

void LeoLinkController::updateGeometry()
{
    double x = passDuration > SIMTIME_ZERO ? (simTime() - passStart) / passDuration : 0.5;
    x = std::min(std::max(x, 0.0), 1.0);
    double shape = std::sin(M_PI * x);
    double eUser = userMinEl + (userMaxEl - userMinEl) * shape;
    double eFeeder = feederMinEl + (feederMaxEl - feederMinEl) * shape;
    double dUser = slantRangeKm(eUser, altitude, earthRadius) / SPEED_OF_LIGHT_KM_S;
    double dFeeder = slantRangeKm(eFeeder, altitude, earthRadius) / SPEED_OF_LIGHT_KM_S;
    userUp->setPropagationDelay(dUser);
    userDown->setPropagationDelay(dUser);
    feederUp->setPropagationDelay(dFeeder);
    feederDown->setPropagationDelay(dFeeder);
    emit(delaySignal, SimTime(dUser + dFeeder));
}

void LeoLinkController::doHandover()
{
    simtime_t outage = par("handoverOutage");
    simtime_t until = simTime() + outage;
    userUp->startOutage(until);
    userDown->startOutage(until);
    feederUp->startOutage(until);
    feederDown->startOutage(until);
    emit(handoverSignal, outage);
    EV_INFO << "Handover on " << getFullPath() << ", outage " << outage << endl;

    // the next satellite is acquired at minimum elevation when the outage ends
    passStart = until;
    passDuration = par("handoverInterval");
    updateGeometry();
    scheduleAt(passStart + passDuration, handoverTimer);
}

void LeoLinkController::meter()
{
    uint64_t bits, packets, lost, outage;
    double T = meterInterval.dbl();

    feederUp->collect(bits, packets, lost, outage);
    uint64_t dlBits = bits, dlPackets = packets, dlLost = lost + outage;
    userDown->collect(bits, packets, lost, outage);
    dlLost += lost + outage;

    userUp->collect(bits, packets, lost, outage);
    uint64_t ulBits = bits, ulPackets = packets, ulLost = lost + outage;
    feederDown->collect(bits, packets, lost, outage);
    ulLost += lost + outage;

    emit(dlUtilSignal, dlBits / (linkCapacity * T));
    emit(ulUtilSignal, ulBits / (linkCapacity * T));
    emit(dlTputSignal, dlBits / T);
    emit(ulTputSignal, ulBits / T);

    double dlOffered = (double)dlPackets + downlinkQueueDrops;
    double ulOffered = (double)ulPackets + uplinkQueueDrops;
    emit(dlLossSignal, dlOffered > 0 ? (dlLost + downlinkQueueDrops) / dlOffered : 0.0);
    emit(ulLossSignal, ulOffered > 0 ? (ulLost + uplinkQueueDrops) / ulOffered : 0.0);
    emit(sentSignal, (intval_t)(dlPackets + ulPackets));
    emit(lostSignal, (intval_t)(dlLost + ulLost));
    emit(queueDropSignal, (intval_t)(downlinkQueueDrops + uplinkQueueDrops));
    downlinkQueueDrops = uplinkQueueDrops = 0;
}

void LeoLinkController::handleMessage(cMessage *msg)
{
    if (msg == geometryTimer) {
        updateGeometry();
        scheduleAt(simTime() + geometryInterval, geometryTimer);
    }
    else if (msg == meterTimer) {
        meter();
        scheduleAt(simTime() + meterInterval, meterTimer);
    }
    else if (msg == handoverTimer)
        doHandover();
    else
        throw cRuntimeError("Unexpected message %s", msg->getName());
}

void LeoLinkController::finish()
{
    recordScalar("satTotalGilbertElliottLosses", (double)(userUp->getTotalLost() + userDown->getTotalLost() + feederUp->getTotalLost() + feederDown->getTotalLost()));
    recordScalar("satTotalOutageDiscards", (double)(userUp->getTotalOutage() + userDown->getTotalOutage() + feederUp->getTotalOutage() + feederDown->getTotalOutage()));
}

} // namespace leoproxy
