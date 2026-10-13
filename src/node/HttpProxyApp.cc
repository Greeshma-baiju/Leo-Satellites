#include "node/HttpProxyApp.h"

#include <cstdio>
#include <sstream>

#include "inet/common/ModuleAccess.h"
#include "inet/common/lifecycle/ModuleOperations.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "inet/networklayer/common/L3AddressResolver.h"

namespace leoproxy {

using namespace omnetpp;
using namespace inet;

Define_Module(HttpProxyApp);

HttpProxyApp::~HttpProxyApp()
{
    cancelAndDelete(statsTimer);
    cancelAndDelete(retrainTimer);
    cancelAndDelete(accuracyTimer);
    for (auto t : activeTxns)
        delete t;
    socketMap.deleteSockets();
}

void HttpProxyApp::initialize(int stage)
{
    ApplicationBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        std::istringstream iss(par("upstreamAddresses").stringValue());
        std::string name;
        while (iss >> name) {
            // "server[0..4]" expands to server[0] ... server[4]
            int lo, hi;
            auto lb = name.find('[');
            if (lb != std::string::npos && sscanf(name.c_str() + lb, "[%d..%d]", &lo, &hi) == 2) {
                for (int i = lo; i <= hi; i++)
                    upstreamNames.push_back(name.substr(0, lb) + "[" + std::to_string(i) + "]");
            }
            else
                upstreamNames.push_back(name);
        }
        if (upstreamNames.empty())
            throw cRuntimeError("upstreamAddresses is empty");
        waiting.resize(upstreamNames.size());
        poolSize.assign(upstreamNames.size(), 0);
        idleConns.resize(upstreamNames.size());
        upstreamPort = par("upstreamPort").intValue();
        maxUpstreamConnections = par("maxUpstreamConnections").intValue();
        maxRetries = par("maxRetries").intValue();
        honorExpiry = par("honorExpiry").boolValue();
        recordCacheStats = par("recordCacheStats").boolValue();
        statsInterval = par("statsInterval");
        dtRetrainInterval = par("dtRetrainInterval");
        dtAccuracyWindow = par("dtAccuracyWindow");

        double cacheSize = par("cacheSize").doubleValue();
        std::string policy = par("cachePolicy").stdstringValue();
        if (cacheSize > 0) {
            cache::DtParams dt;
            dt.candidatePool = par("dtCandidatePool").intValue();
            dt.horizon = par("dtHorizon").doubleValue();
            dt.frequencyWindow = par("dtFrequencyWindow").doubleValue();
            dt.maxDepth = par("dtMaxDepth").intValue();
            cache.reset(new cache::CacheManager((int64_t)cacheSize, (int64_t)par("maxCacheableObjectSize").doubleValue(),
                                                policy, getRNG(0), dt));
        }

        cacheHitSignal = registerSignal("cacheHit");
        bytesServedSignal = registerSignal("bytesServed");
        bytesFromCacheSignal = registerSignal("bytesFromCache");
        bytesFromOriginSignal = registerSignal("bytesFromOrigin");
        redundantOriginSignal = registerSignal("redundantOriginBytes");
        redundantDeliveredSignal = registerSignal("redundantDeliveredBytes");
        hitDelaySignal = registerSignal("proxyHitDelay");
        missDelaySignal = registerSignal("proxyMissDelay");
        originRequestSignal = registerSignal("originRequest");
        hitRatioWindowSignal = registerSignal("cacheHitRatioWindow");
        byteHitRatioWindowSignal = registerSignal("byteHitRatioWindow");
        originThroughputSignal = registerSignal("originThroughput");
        occupancySignal = registerSignal("cacheOccupancy");
        evictionsSignal = registerSignal("cacheEvictions");
        dtAccuracySignal = registerSignal("dtPredictionAccuracy");
        upstreamConnSignal = registerSignal("upstreamConnections");

        statsTimer = new cMessage("stats");
        if (cache && policy == "DTLRU") {
            retrainTimer = new cMessage("dtRetrain");
            accuracyTimer = new cMessage("dtAccuracy");
        }
        WATCH(totalRequests);
        WATCH(totalHits);
        WATCH_VECTOR(poolSize);
    }
}

void HttpProxyApp::handleStartOperation(LifecycleOperation *operation)
{
    listenSocket.setOutputGate(gate("socketOut"));
    listenSocket.setCallback(this);
    listenSocket.bind(par("localPort").intValue());
    listenSocket.listen();
    if (recordCacheStats)
        scheduleAfter(statsInterval, statsTimer);
    if (retrainTimer) {
        scheduleAfter(dtRetrainInterval, retrainTimer);
        scheduleAfter(dtAccuracyWindow, accuracyTimer);
    }
}

void HttpProxyApp::handleStopOperation(LifecycleOperation *operation)
{
    for (auto& kv : socketMap.getMap())
        static_cast<TcpSocket *>(kv.second)->close();
    listenSocket.close();
    cancelEvent(statsTimer);
    if (retrainTimer) {
        cancelEvent(retrainTimer);
        cancelEvent(accuracyTimer);
    }
    delayActiveOperationFinish(par("stopOperationTimeout"));
}

void HttpProxyApp::handleCrashOperation(LifecycleOperation *operation)
{
    for (auto& kv : socketMap.getMap())
        static_cast<TcpSocket *>(kv.second)->destroy();
    socketMap.deleteSockets();
    downConns.clear();
    upConns.clear();
    if (operation->getRootModule() != getContainingNode(this))
        listenSocket.destroy();
}

void HttpProxyApp::resolveUpstreams()
{
    if (!upstreamAddrs.empty())
        return;
    for (auto& n : upstreamNames)
        upstreamAddrs.push_back(L3AddressResolver().resolve(n.c_str()));
}

void HttpProxyApp::checkMeasurementStart()
{
    if (!measuring && simTime() >= getSimulation()->getWarmupPeriod()) {
        measuring = true;
        fetchedKeys.clear();
        deliveredKeys.clear();
    }
}

void HttpProxyApp::handleMessageWhenUp(cMessage *msg)
{
    if (msg->isSelfMessage()) {
        if (msg == statsTimer) {
            collectWindowStats();
            scheduleAfter(statsInterval, statsTimer);
        }
        else if (msg == retrainTimer) {
            cache->retrain();
            scheduleAfter(dtRetrainInterval, retrainTimer);
        }
        else if (msg == accuracyTimer) {
            cache->periodicMaintenance();
            long correct = 0, total = 0;
            if (cache->collectAccuracy(correct, total) && total > 0)
                emit(dtAccuracySignal, (double)correct / total);
            scheduleAfter(dtAccuracyWindow, accuracyTimer);
        }
        else if (!strcmp(msg->getName(), "cacheHitServe")) {
            auto txn = static_cast<Transaction *>(msg->getContextPointer());
            delete msg;
            serveFromCache(txn);
        }
        else if (!strcmp(msg->getName(), "forward")) {
            auto txn = static_cast<Transaction *>(msg->getContextPointer());
            delete msg;
            dispatch(txn);
        }
        else
            throw cRuntimeError("Unknown self message %s", msg->getName());
        return;
    }
    if (auto socket = check_and_cast_nullable<TcpSocket *>(socketMap.findSocketFor(msg)))
        socket->processMessage(msg);
    else if (listenSocket.belongsToSocket(msg))
        listenSocket.processMessage(msg);
    else {
        EV_WARN << "Message for unknown socket, discarding: " << msg->getName() << endl;
        delete msg;
    }
}

void HttpProxyApp::socketAvailable(TcpSocket *socket, TcpAvailableInfo *availableInfo)
{
    auto *newSocket = new TcpSocket(availableInfo);
    newSocket->setOutputGate(gate("socketOut"));
    newSocket->setCallback(this);
    socketMap.addSocket(newSocket);
    downConns[newSocket->getSocketId()].socket = newSocket;
    socket->accept(availableInfo->getNewSocketId());
}

void HttpProxyApp::socketEstablished(TcpSocket *socket)
{
    auto it = upConns.find(socket->getSocketId());
    if (it == upConns.end())
        return;
    UpConn& conn = it->second;
    conn.established = true;
    if (conn.txn)
        sendUpstream(conn, conn.txn);
    else
        connectionIdle(conn);
}

void HttpProxyApp::socketDataArrived(TcpSocket *socket, Packet *packet, bool urgent)
{
    int id = socket->getSocketId();
    auto d = downConns.find(id);
    if (d != downConns.end()) {
        d->second.rx.push(packet->peekData());
        delete packet;
        while (d->second.rx.has<HttpRequestHeader>()) {
            auto req = d->second.rx.pop<HttpRequestHeader>();
            handleRequest(id, req);
        }
        return;
    }
    auto u = upConns.find(id);
    if (u != upConns.end()) {
        UpConn& conn = u->second;
        conn.rx.push(packet->peekData());
        delete packet;
        while (conn.rx.has<HttpResponseHeader>()) {
            auto hdr = conn.rx.peek<HttpResponseHeader>();
            B total = hdr->getChunkLength() + B(hdr->getContentLength());
            if (conn.rx.getLength() < total)
                break;
            conn.rx.pop<HttpResponseHeader>();
            if (hdr->getContentLength() > 0)
                conn.rx.pop(B(hdr->getContentLength()));
            handleUpstreamResponse(conn, hdr);
        }
        return;
    }
    delete packet;
}

void HttpProxyApp::handleRequest(int downSocketId, const Ptr<const HttpRequestHeader>& req)
{
    checkMeasurementStart();
    auto txn = new Transaction();
    txn->id = nextTxnId++;
    txn->downSocketId = downSocketId;
    txn->request = req;
    txn->key = req->getUrl();
    txn->arrival = simTime();
    activeTxns.insert(txn);
    totalRequests++;

    if (cache) {
        cache->noteRequest(txn->key);
        if (cache->lookup(txn->key, honorExpiry) != nullptr) {
            auto msg = new cMessage("cacheHitServe");
            msg->setContextPointer(txn);
            scheduleAfter(par("cacheLookupDelay").doubleValue(), msg);
            return;
        }
    }
    simtime_t fwd = par("forwardDelay");
    if (fwd > SIMTIME_ZERO) {
        auto msg = new cMessage("forward");
        msg->setContextPointer(txn);
        scheduleAfter(fwd, msg);
    }
    else
        dispatch(txn);
}

void HttpProxyApp::serveFromCache(Transaction *txn)
{
    // the entry may have been evicted during the lookup delay; re-check (without counting a second access)
    const cache::CacheEntry *e = cache->peek(txn->key);
    if (e == nullptr) {
        dispatch(txn);
        return;
    }
    totalHits++;
    int64_t size = e->sizeBytes;
    if (recordCacheStats) {
        emit(cacheHitSignal, 1L);
        emit(bytesFromCacheSignal, (intval_t)size);
        emit(hitDelaySignal, simTime() - txn->arrival);
        winRequests++;
        winHits++;
        winBytes += size;
        winHitBytes += size;
    }
    double maxAge = std::max(0.0, (e->expiresAt - e->insertedAt).dbl());
    sendDownstream(txn, 200, size, HTTP_CACHE_HIT, e->cacheControl, maxAge, e->video, e->qualityLevel);
    finishTransaction(txn);
}

void HttpProxyApp::dispatch(Transaction *txn)
{
    resolveUpstreams();
    if (txn->upstream < 0)
        txn->upstream = (int)(roundRobin++ % upstreamNames.size());   // strict round-robin
    int u = txn->upstream;
    // reuse an idle established connection to this upstream
    if (!idleConns[u].empty()) {
        int id = *idleConns[u].begin();
        sendUpstream(upConns.at(id), txn);
        return;
    }
    if (maxUpstreamConnections > 0 && poolSize[u] >= maxUpstreamConnections) {
        waiting[u].push_back(txn);
        return;
    }
    auto *socket = new TcpSocket();
    socket->setOutputGate(gate("socketOut"));
    socket->setCallback(this);
    socketMap.addSocket(socket);
    UpConn& c = upConns[socket->getSocketId()];
    c.socket = socket;
    c.upstream = u;
    c.txn = txn;
    poolSize[u]++;
    emit(upstreamConnSignal, (intval_t)upConns.size());
    socket->connect(upstreamAddrs[u], upstreamPort);
}

void HttpProxyApp::sendUpstream(UpConn& conn, Transaction *txn)
{
    idleConns[conn.upstream].erase(conn.socket->getSocketId());
    conn.txn = txn;
    auto hdr = makeShared<HttpRequestHeader>(*txn->request);
    auto packet = new Packet("HttpRequest");
    packet->insertAtBack(hdr);
    conn.socket->send(packet);
    emit(originRequestSignal, (intval_t)conn.upstream);
}

void HttpProxyApp::handleUpstreamResponse(UpConn& conn, const Ptr<const HttpResponseHeader>& hdr)
{
    Transaction *txn = conn.txn;
    conn.txn = nullptr;
    if (txn == nullptr) {
        connectionIdle(conn);
        return;
    }
    int64_t size = hdr->getContentLength();
    if (recordCacheStats) {
        emit(cacheHitSignal, 0L);
        emit(bytesFromOriginSignal, (intval_t)size);
        emit(missDelaySignal, simTime() - txn->arrival);
        winRequests++;
        winBytes += size;
        winOriginBytes += size;
        if (measuring) {
            if (!fetchedKeys.insert(txn->key).second)
                emit(redundantOriginSignal, (intval_t)size);
        }
    }
    if (cache && hdr->getStatusCode() == 200)
        cache->insert(txn->key, size, hdr->getVideo(), hdr->getQualityLevel(), hdr->getCacheControl(), hdr->getMaxAge());

    int status = recordCacheStats ? HTTP_CACHE_MISS : hdr->getCacheStatus();
    sendDownstream(txn, hdr->getStatusCode(), size, status, hdr->getCacheControl(), hdr->getMaxAge(), hdr->getVideo(), hdr->getQualityLevel());
    finishTransaction(txn);
    connectionIdle(conn);
}

void HttpProxyApp::connectionIdle(UpConn& conn)
{
    auto& q = waiting[conn.upstream];
    if (!q.empty()) {
        Transaction *next = q.front();
        q.pop_front();
        sendUpstream(conn, next);
    }
    else
        idleConns[conn.upstream].insert(conn.socket->getSocketId());
}

void HttpProxyApp::sendDownstream(Transaction *txn, int statusCode, int64_t contentLength, int cacheStatus,
                                  const std::string& cacheControl, double maxAge, bool video, int quality)
{
    if (recordCacheStats) {
        emit(bytesServedSignal, (intval_t)contentLength);
        if (measuring && !deliveredKeys.insert(txn->key).second)
            emit(redundantDeliveredSignal, (intval_t)contentLength);
    }
    auto d = downConns.find(txn->downSocketId);
    if (d == downConns.end())
        return;   // client connection closed meanwhile
    TcpSocket *socket = d->second.socket;
    if (socket->getState() != TcpSocket::CONNECTED && socket->getState() != TcpSocket::PEER_CLOSED)
        return;
    auto resp = makeShared<HttpResponseHeader>();
    resp->setRequestId(txn->request->getRequestId());
    resp->setUserId(txn->request->getUserId());
    resp->setUrl(txn->request->getUrl());
    resp->setStatusCode(statusCode);
    resp->setContentLength(contentLength);
    resp->setCacheStatus(cacheStatus);
    resp->setCacheControl(cacheControl.c_str());
    resp->setMaxAge(maxAge);
    resp->setVideo(video);
    resp->setQualityLevel(quality);
    auto packet = new Packet("HttpResponse");
    packet->insertAtBack(resp);
    if (contentLength > 0)
        packet->insertAtBack(makeShared<ByteCountChunk>(B(contentLength)));
    socket->send(packet);
}

void HttpProxyApp::finishTransaction(Transaction *txn)
{
    activeTxns.erase(txn);
    delete txn;
}

void HttpProxyApp::upstreamConnectionLost(TcpSocket *socket)
{
    auto it = upConns.find(socket->getSocketId());
    if (it == upConns.end())
        return;
    Transaction *txn = it->second.txn;
    int u = it->second.upstream;
    idleConns[u].erase(it->first);
    poolSize[u]--;
    upConns.erase(it);
    socketMap.removeSocket(socket);
    delete socket;
    emit(upstreamConnSignal, (intval_t)upConns.size());
    if (txn) {
        if (txn->retries++ < maxRetries)
            dispatch(txn);
        else {
            sendDownstream(txn, 502, 0, recordCacheStats ? HTTP_CACHE_MISS : HTTP_CACHE_NONE, "no-store", 0, false, 0);
            finishTransaction(txn);
        }
    }
    else if (!waiting[u].empty()) {
        Transaction *next = waiting[u].front();
        waiting[u].pop_front();
        dispatch(next);
    }
}

void HttpProxyApp::socketPeerClosed(TcpSocket *socket)
{
    socket->close();
}

void HttpProxyApp::socketClosed(TcpSocket *socket)
{
    if (socket == &listenSocket)
        return;
    int id = socket->getSocketId();
    if (downConns.erase(id)) {
        socketMap.removeSocket(socket);
        delete socket;
        return;
    }
    upstreamConnectionLost(socket);
}

void HttpProxyApp::socketFailure(TcpSocket *socket, int code)
{
    EV_WARN << "TCP failure " << code << " on socket " << socket->getSocketId() << endl;
    socketClosed(socket);
}

void HttpProxyApp::collectWindowStats()
{
    double T = statsInterval.dbl();
    if (winRequests > 0)
        emit(hitRatioWindowSignal, (double)winHits / winRequests);
    if (winBytes > 0)
        emit(byteHitRatioWindowSignal, (double)winHitBytes / winBytes);
    emit(originThroughputSignal, winOriginBytes * 8.0 / T);
    winRequests = winHits = 0;
    winBytes = winHitBytes = winOriginBytes = 0;
    if (cache) {
        emit(occupancySignal, (intval_t)cache->getUsedBytes());
        uint64_t ev = cache->getStats().evictions;
        emit(evictionsSignal, (intval_t)(ev - lastEvictions));
        lastEvictions = ev;
    }
}

void HttpProxyApp::finish()
{
    if (recordCacheStats) {
        recordScalar("requestsTotal", totalRequests);
        recordScalar("hitsTotal", totalHits);
        if (cache) {
            recordScalar("cacheCapacity", (double)cache->getCapacityBytes(), "B");
            recordScalar("cacheEntriesAtEnd", (double)cache->getNumEntries());
            recordScalar("cacheInsertions", (double)cache->getStats().insertions);
            recordScalar("cacheRejectedTooLarge", (double)cache->getStats().rejectedTooLarge);
        }
    }
}

} // namespace leoproxy
