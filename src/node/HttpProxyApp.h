#ifndef LEOPROXY_NODE_HTTPPROXYAPP_H
#define LEOPROXY_NODE_HTTPPROXYAPP_H

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "inet/applications/base/ApplicationBase.h"
#include "inet/common/packet/ChunkQueue.h"
#include "inet/common/socket/SocketMap.h"
#include "inet/networklayer/common/L3Address.h"
#include "inet/transportlayer/contract/tcp/TcpSocket.h"

#include "cache/CacheManager.h"
#include "common/HttpMessages_m.h"

namespace leoproxy {

class HttpProxyApp : public inet::ApplicationBase, public inet::TcpSocket::ICallback
{
  protected:
    struct Transaction
    {
        long id = 0;
        int downSocketId = -1;
        inet::Ptr<const HttpRequestHeader> request;
        std::string key;
        omnetpp::simtime_t arrival;
        int upstream = -1;
        int retries = 0;
    };

    struct DownConn
    {
        inet::TcpSocket *socket = nullptr;
        inet::ChunkQueue rx;
    };

    struct UpConn
    {
        inet::TcpSocket *socket = nullptr;
        int upstream = -1;
        bool established = false;
        Transaction *txn = nullptr;   // in-flight transaction (non-pipelined)
        inet::ChunkQueue rx;
    };

    // configuration
    std::vector<std::string> upstreamNames;
    std::vector<inet::L3Address> upstreamAddrs;
    int upstreamPort = 80;
    int maxUpstreamConnections = 0;
    int maxRetries = 2;
    bool honorExpiry = false;
    bool recordCacheStats = true;
    omnetpp::simtime_t statsInterval;
    omnetpp::simtime_t dtRetrainInterval, dtAccuracyWindow;

    // state
    inet::TcpSocket listenSocket;
    inet::SocketMap socketMap;
    std::map<int, DownConn> downConns;
    std::map<int, UpConn> upConns;
    std::vector<std::deque<Transaction *>> waiting;   // per upstream, when pool is exhausted
    std::vector<int> poolSize;                        // open connections per upstream
    std::vector<std::set<int>> idleConns;             // per upstream: established, idle connection ids
    std::set<Transaction *> activeTxns;
    std::unique_ptr<cache::CacheManager> cache;
    long nextTxnId = 0;
    unsigned long roundRobin = 0;

    // measurement-window bookkeeping
    bool measuring = false;
    std::unordered_set<std::string> fetchedKeys;
    std::unordered_set<std::string> deliveredKeys;
    long winRequests = 0, winHits = 0;
    int64_t winBytes = 0, winHitBytes = 0, winOriginBytes = 0;
    uint64_t lastEvictions = 0;
    long totalRequests = 0, totalHits = 0;

    omnetpp::cMessage *statsTimer = nullptr;
    omnetpp::cMessage *retrainTimer = nullptr;
    omnetpp::cMessage *accuracyTimer = nullptr;

    omnetpp::simsignal_t cacheHitSignal, bytesServedSignal, bytesFromCacheSignal, bytesFromOriginSignal,
            redundantOriginSignal, redundantDeliveredSignal, hitDelaySignal, missDelaySignal, originRequestSignal,
            hitRatioWindowSignal, byteHitRatioWindowSignal, originThroughputSignal, occupancySignal,
            evictionsSignal, dtAccuracySignal, upstreamConnSignal;

  protected:
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessageWhenUp(omnetpp::cMessage *msg) override;
    virtual void finish() override;
    virtual ~HttpProxyApp();

    virtual void handleStartOperation(inet::LifecycleOperation *operation) override;
    virtual void handleStopOperation(inet::LifecycleOperation *operation) override;
    virtual void handleCrashOperation(inet::LifecycleOperation *operation) override;

    virtual void socketDataArrived(inet::TcpSocket *socket, inet::Packet *packet, bool urgent) override;
    virtual void socketAvailable(inet::TcpSocket *socket, inet::TcpAvailableInfo *availableInfo) override;
    virtual void socketEstablished(inet::TcpSocket *socket) override;
    virtual void socketPeerClosed(inet::TcpSocket *socket) override;
    virtual void socketClosed(inet::TcpSocket *socket) override;
    virtual void socketFailure(inet::TcpSocket *socket, int code) override;
    virtual void socketStatusArrived(inet::TcpSocket *socket, inet::TcpStatusInfo *status) override {}
    virtual void socketDeleted(inet::TcpSocket *socket) override {}

    void checkMeasurementStart();
    void resolveUpstreams();
    void handleRequest(int downSocketId, const inet::Ptr<const HttpRequestHeader>& req);
    void serveFromCache(Transaction *txn);
    void dispatch(Transaction *txn);
    void sendUpstream(UpConn& conn, Transaction *txn);
    void handleUpstreamResponse(UpConn& conn, const inet::Ptr<const HttpResponseHeader>& hdr);
    void connectionIdle(UpConn& conn);
    void upstreamConnectionLost(inet::TcpSocket *socket);
    void sendDownstream(Transaction *txn, int statusCode, int64_t contentLength, int cacheStatus,
                        const std::string& cacheControl, double maxAge, bool video, int quality);
    void finishTransaction(Transaction *txn);
    void collectWindowStats();
};

} // namespace leoproxy

#endif
