#include "node/HttpClientApp.h"

#include <algorithm>

#include "inet/common/ModuleAccess.h"
#include "inet/common/lifecycle/ModuleOperations.h"
#include "inet/networklayer/common/L3AddressResolver.h"

#include "common/HttpUtils.h"
#include "popularity/DynamicZipfGenerator.h"

namespace leoproxy {

using namespace omnetpp;
using namespace inet;

Define_Module(HttpClientApp);

HttpClientApp::~HttpClientApp()
{
    for (auto& u : users)
        cancelAndDelete(u.timer);
    cancelAndDelete(statsTimer);
    socketMap.deleteSockets();
}

void HttpClientApp::initialize(int stage)
{
    ApplicationBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        numUsers = par("numUsers").intValue();
        if (numUsers < 0)
            throw cRuntimeError("numUsers must be >= 0");
        std::string tt = par("trafficType").stdstringValue();
        trafficType = tt == "web" ? WEB : tt == "video" ? VIDEO : MIXED;
        webFraction = trafficType == WEB ? 1.0 : trafficType == VIDEO ? 0.0 : par("mixedWebFraction").doubleValue();
        dashMode = par("videoMode").stdstringValue() == "dash";
        requestRate = par("requestRate").doubleValue();
        maxConnectionsPerUser = par("maxConnectionsPerUser").intValue();
        maxQueuedRequestsPerUser = par("maxQueuedRequestsPerUser").intValue();
        segmentsPerTitle = par("segmentsPerTitle").intValue();
        segmentDuration = par("segmentDuration").doubleValue();
        dashMaxBuffer = par("dashMaxBuffer").doubleValue();
        dashStartupBuffer = par("dashStartupBuffer").doubleValue();
        tracedUser = par("tracedUser").intValue();
        statsInterval = par("statsInterval");
        connectPort = par("connectPort").intValue();
        if (maxConnectionsPerUser < 1)
            throw cRuntimeError("maxConnectionsPerUser must be >= 1");
        if (!dashMode && requestRate <= 0 && numUsers > 0)
            throw cRuntimeError("requestRate must be positive");

        AbrController::Params ap;
        for (double size : httputil::parseQuantityList(par("videoSegmentSizes").stringValue(), "B"))
            ap.bitrates.push_back(size * 8.0 / segmentDuration);
        ap.windowSize = par("abrWindow").intValue();
        ap.safetyFactor = par("abrSafetyFactor").doubleValue();
        ap.panicBuffer = par("abrPanicBuffer").doubleValue();
        ap.cacheAware = par("cacheAwareABR").boolValue();
        abr.setParams(ap);

        responseTimeSignal = registerSignal("responseTime");
        webResponseTimeSignal = registerSignal("webResponseTime");
        videoResponseTimeSignal = registerSignal("videoResponseTime");
        hitResponseTimeSignal = registerSignal("hitResponseTime");
        missResponseTimeSignal = registerSignal("missResponseTime");
        requestTotalTimeSignal = registerSignal("requestTotalTime");
        requestSentSignal = registerSignal("requestSent");
        responseReceivedSignal = registerSignal("responseReceived");
        requestDroppedSignal = registerSignal("requestDropped");
        requestFailedSignal = registerSignal("requestFailed");
        bodyBytesSignal = registerSignal("bodyBytesReceived");
        groupThroughputSignal = registerSignal("groupThroughput");
        responseTimeWindowSignal = registerSignal("meanResponseTimeWindow");
        throughputPerUserSignal = registerSignal("throughputPerUser");
        qualitySignal = registerSignal("dashQualityLevel");
        qualitySwitchSignal = registerSignal("dashQualitySwitch");
        oscillationSignal = registerSignal("dashOscillation");
        stallSignal = registerSignal("dashStallDuration");
        bufferSignal = registerSignal("dashBufferLevel");
        tracedBufferSignal = registerSignal("tracedBufferLevel");
        tracedQualitySignal = registerSignal("tracedQualityLevel");

        users.resize(numUsers);
        for (int u = 0; u < numUsers; u++) {
            users[u].timer = new cMessage("userTimer", u);
            users[u].timer->setKind(u);
        }
        statsTimer = new cMessage("stats");
        WATCH(numSent);
        WATCH(numReceived);
        WATCH(numDropped);
    }
    else if (stage == INITSTAGE_LAST) {
        cModule *mod = getSimulation()->getSystemModule()->getModuleByPath(par("popularityModule").stringValue());
        if (mod == nullptr)
            throw cRuntimeError("Popularity module '%s' not found", par("popularityModule").stringValue());
        popularity = check_and_cast<DynamicZipfGenerator *>(mod);
    }
}

void HttpClientApp::handleStartOperation(LifecycleOperation *operation)
{
    connectAddress = L3AddressResolver().resolve(par("connectAddress").stringValue());
    simtime_t start = std::max(simTime(), (simtime_t)par("startTime"));
    for (int u = 0; u < numUsers; u++) {
        // de-synchronise the users
        simtime_t offset = dashMode ? uniform(0, segmentDuration) : exponential(1.0 / requestRate);
        scheduleAt(start + offset, users[u].timer);
    }
    scheduleAt(start + statsInterval, statsTimer);
}

void HttpClientApp::handleStopOperation(LifecycleOperation *operation)
{
    for (auto& u : users)
        cancelEvent(u.timer);
    cancelEvent(statsTimer);
    for (auto& kv : socketMap.getMap())
        static_cast<TcpSocket *>(kv.second)->close();
    delayActiveOperationFinish(par("stopOperationTimeout"));
}

void HttpClientApp::handleCrashOperation(LifecycleOperation *operation)
{
    for (auto& u : users)
        cancelEvent(u.timer);
    cancelEvent(statsTimer);
    for (auto& kv : socketMap.getMap())
        static_cast<TcpSocket *>(kv.second)->destroy();
    socketMap.deleteSockets();
    conns.clear();
    for (auto& u : users) {
        u.conns.clear();
        u.queue.clear();
    }
}

void HttpClientApp::handleMessageWhenUp(cMessage *msg)
{
    if (msg == statsTimer) {
        double T = statsInterval.dbl();
        emit(groupThroughputSignal, windowBytes * 8.0 / T);
        if (numUsers > 0)
            emit(throughputPerUserSignal, windowBytes * 8.0 / T / numUsers);
        windowBytes = 0;
        if (windowRtCount > 0)
            emit(responseTimeWindowSignal, windowRtSum / windowRtCount);
        windowRtSum = 0;
        windowRtCount = 0;
        if (dashMode && tracedUser >= 0 && tracedUser < numUsers) {
            drainBuffer(users[tracedUser]);
            emit(tracedBufferSignal, users[tracedUser].buffer);
        }
        scheduleAfter(statsInterval, statsTimer);
    }
    else if (msg->isSelfMessage())
        userTimerExpired(msg->getKind());
    else if (auto socket = check_and_cast_nullable<TcpSocket *>(socketMap.findSocketFor(msg)))
        socket->processMessage(msg);
    else {
        EV_WARN << "Message for unknown socket, discarding: " << msg->getName() << endl;
        delete msg;
    }
}

void HttpClientApp::userTimerExpired(int u)
{
    if (dashMode)
        requestNextSegment(u);
    else {
        generateIrmRequest(u);
        scheduleAfter(exponential(1.0 / requestRate), users[u].timer);
    }
}

void HttpClientApp::noteQualityDecision(int u, int q)
{
    User& user = users[u];
    int last = user.abr.lastQuality;
    if (last >= 0) {
        if (q != last)
            emit(qualitySwitchSignal, 1L);
        if (q > last)
            user.sinceUpSwitch = 0;
        else if (user.sinceUpSwitch >= 0) {
            user.sinceUpSwitch++;
            if (q < last && user.sinceUpSwitch <= 3) {
                // an upward transition followed by a downgrade within the next three segment requests
                emit(oscillationSignal, 1L);
                user.sinceUpSwitch = -1;
            }
            else if (user.sinceUpSwitch >= 3)
                user.sinceUpSwitch = -1;
        }
    }
    user.abr.lastQuality = q;
    emit(qualitySignal, (intval_t)q);
    if (u == tracedUser)
        emit(tracedQualitySignal, (intval_t)q);
}

HttpClientApp::Request HttpClientApp::makeVideoRequest(int u, double bufferForAbr)
{
    User& user = users[u];
    if (user.title < 0 || user.nextSegment >= segmentsPerTitle) {
        user.title = popularity->sampleRank(true, getRNG(0));
        user.nextSegment = 0;
    }
    int q = abr.selectQuality(user.abr, bufferForAbr);
    noteQualityDecision(u, q);
    Request r;
    r.id = nextRequestId++;
    r.user = u;
    r.video = true;
    r.quality = q;
    r.url = httputil::makeVideoUrl(user.title, q, user.nextSegment++);
    r.created = simTime();
    return r;
}

void HttpClientApp::generateIrmRequest(int u)
{
    bool web = webFraction >= 1.0 || (webFraction > 0.0 && uniform(0, 1) < webFraction);
    Request r;
    if (web) {
        r.id = nextRequestId++;
        r.user = u;
        r.video = false;
        r.url = httputil::makeWebUrl(popularity->sampleRank(false, getRNG(0)));
        r.created = simTime();
    }
    else
        r = makeVideoRequest(u, -1);
    submit(u, r);
}

void HttpClientApp::requestNextSegment(int u)
{
    User& user = users[u];
    if (user.segmentOutstanding)
        return;
    drainBuffer(user);
    Request r = makeVideoRequest(u, user.playing || user.buffer > 0 ? user.buffer : -1);
    user.segmentOutstanding = true;
    submit(u, r);
}

void HttpClientApp::submit(int u, const Request& req)
{
    User& user = users[u];
    for (int id : user.conns) {
        Conn& c = conns.at(id);
        if (c.established && !c.busy) {
            sendOnConnection(c, req);
            return;
        }
    }
    if ((int)user.conns.size() < maxConnectionsPerUser) {
        openConnection(u, req);
        return;
    }
    if ((int)user.queue.size() < maxQueuedRequestsPerUser) {
        user.queue.push_back(req);
        return;
    }
    numDropped++;
    emit(requestDroppedSignal, 1L);
    if (dashMode && req.video) {
        user.segmentOutstanding = false;
        scheduleNextSegment(u);
    }
}

void HttpClientApp::openConnection(int u, const Request& req)
{
    auto *socket = new TcpSocket();
    socket->setOutputGate(gate("socketOut"));
    socket->setCallback(this);
    socketMap.addSocket(socket);
    Conn& c = conns[socket->getSocketId()];
    c.socket = socket;
    c.user = u;
    c.busy = true;            // reserved for the request that triggered the connection
    c.request = req;
    users[u].conns.push_back(socket->getSocketId());
    socket->connect(connectAddress, connectPort);
}

void HttpClientApp::sendOnConnection(Conn& c, const Request& req)
{
    c.busy = true;
    c.request = req;
    c.request.sent = simTime();
    auto hdr = makeShared<HttpRequestHeader>();
    hdr->setRequestId(req.id);
    hdr->setUserId(req.user);
    hdr->setMethod("GET");
    hdr->setUrl(req.url.c_str());
    hdr->setHost("origin.example");
    hdr->setVideo(req.video);
    hdr->setQualityLevel(req.quality);
    auto packet = new Packet("HttpRequest");
    packet->insertAtBack(hdr);
    c.socket->send(packet);
    numSent++;
    emit(requestSentSignal, 1L);
}

void HttpClientApp::socketEstablished(TcpSocket *socket)
{
    auto it = conns.find(socket->getSocketId());
    if (it == conns.end())
        return;
    Conn& c = it->second;
    c.established = true;
    if (c.busy)
        sendOnConnection(c, c.request);
    else
        connectionFree(c);
}

void HttpClientApp::socketDataArrived(TcpSocket *socket, Packet *packet, bool urgent)
{
    auto it = conns.find(socket->getSocketId());
    if (it == conns.end()) {
        delete packet;
        return;
    }
    Conn& c = it->second;
    c.rx.push(packet->peekData());
    delete packet;
    while (c.rx.has<HttpResponseHeader>()) {
        auto hdr = c.rx.peek<HttpResponseHeader>();
        B total = hdr->getChunkLength() + B(hdr->getContentLength());
        if (c.rx.getLength() < total)
            break;
        c.rx.pop<HttpResponseHeader>();
        if (hdr->getContentLength() > 0)
            c.rx.pop(B(hdr->getContentLength()));
        handleResponse(c, hdr);
    }
}

void HttpClientApp::handleResponse(Conn& c, const Ptr<const HttpResponseHeader>& hdr)
{
    if (!c.busy || hdr->getRequestId() != c.request.id) {
        EV_WARN << "Unexpected response " << hdr->getRequestId() << endl;
        return;
    }
    Request req = c.request;
    c.busy = false;
    int u = c.user;
    simtime_t now = simTime();
    simtime_t rt = now - req.sent;
    int64_t bytes = hdr->getContentLength();
    bool ok = hdr->getStatusCode() == 200;

    if (ok) {
        numReceived++;
        emit(responseReceivedSignal, 1L);
        emit(responseTimeSignal, rt);
        emit(req.video ? videoResponseTimeSignal : webResponseTimeSignal, rt);
        if (hdr->getCacheStatus() == HTTP_CACHE_HIT)
            emit(hitResponseTimeSignal, rt);
        else if (hdr->getCacheStatus() == HTTP_CACHE_MISS)
            emit(missResponseTimeSignal, rt);
        emit(requestTotalTimeSignal, now - req.created);
        emit(bodyBytesSignal, (intval_t)bytes);
        windowBytes += bytes;
        windowRtSum += rt.dbl();
        windowRtCount++;
    }
    else {
        emit(requestFailedSignal, 1L);
    }

    User& user = users[u];
    if (req.video && ok && rt > SIMTIME_ZERO)
        abr.addSample(user.abr, bytes * 8.0 / rt.dbl(), hdr->getCacheStatus() == HTTP_CACHE_HIT);

    if (dashMode && req.video) {
        drainBuffer(user);
        if (ok)
            user.buffer += segmentDuration;
        if (!user.playing && user.buffer >= dashStartupBuffer) {
            user.playing = true;
            if (user.stalled) {
                emit(stallSignal, now - user.stallStart);
                user.stalled = false;
            }
        }
        emit(bufferSignal, user.buffer);
        user.segmentOutstanding = false;
        scheduleNextSegment(u);
    }
    connectionFree(c);
}

void HttpClientApp::scheduleNextSegment(int u)
{
    User& user = users[u];
    cancelEvent(user.timer);
    double wait = user.playing ? user.buffer + segmentDuration - dashMaxBuffer : 0;
    scheduleAfter(std::max(0.0, wait), user.timer);
}

void HttpClientApp::connectionFree(Conn& c)
{
    User& user = users[c.user];
    if (!user.queue.empty()) {
        Request next = user.queue.front();
        user.queue.pop_front();
        sendOnConnection(c, next);
    }
}

void HttpClientApp::drainBuffer(User& user)
{
    simtime_t now = simTime();
    if (user.playing) {
        double elapsed = (now - user.lastBufferUpdate).dbl();
        if (elapsed >= user.buffer) {
            user.stallStart = user.lastBufferUpdate + user.buffer;
            user.buffer = 0;
            user.playing = false;
            user.stalled = true;
        }
        else
            user.buffer -= elapsed;
    }
    user.lastBufferUpdate = now;
}

void HttpClientApp::connectionLost(TcpSocket *socket)
{
    auto it = conns.find(socket->getSocketId());
    if (it == conns.end())
        return;
    int u = it->second.user;
    bool busy = it->second.busy;
    Request req = it->second.request;
    conns.erase(it);
    User& user = users[u];
    user.conns.erase(std::remove(user.conns.begin(), user.conns.end(), socket->getSocketId()), user.conns.end());
    socketMap.removeSocket(socket);
    delete socket;
    if (busy) {
        if (req.retries++ < 2)
            submit(u, req);
        else {
            emit(requestFailedSignal, 1L);
            if (dashMode && req.video) {
                user.segmentOutstanding = false;
                scheduleNextSegment(u);
            }
        }
    }
    else if (!user.queue.empty() && (int)user.conns.size() < maxConnectionsPerUser) {
        Request next = user.queue.front();
        user.queue.pop_front();
        openConnection(u, next);
    }
}

void HttpClientApp::socketPeerClosed(TcpSocket *socket)
{
    socket->close();
}

void HttpClientApp::socketClosed(TcpSocket *socket)
{
    connectionLost(socket);
}

void HttpClientApp::socketFailure(TcpSocket *socket, int code)
{
    EV_WARN << "TCP failure " << code << " on client socket " << socket->getSocketId() << endl;
    connectionLost(socket);
}

void HttpClientApp::finish()
{
    recordScalar("numUsers", numUsers);
    recordScalar("requestsSentTotal", numSent);
    recordScalar("responsesReceivedTotal", numReceived);
    recordScalar("requestsDroppedTotal", numDropped);
}

} // namespace leoproxy
