#ifndef LEOPROXY_NODE_HTTPCLIENTAPP_H
#define LEOPROXY_NODE_HTTPCLIENTAPP_H

#include <deque>
#include <map>
#include <string>
#include <vector>

#include "inet/applications/base/ApplicationBase.h"
#include "inet/common/packet/ChunkQueue.h"
#include "inet/common/socket/SocketMap.h"
#include "inet/networklayer/common/L3Address.h"
#include "inet/transportlayer/contract/tcp/TcpSocket.h"

#include "common/HttpMessages_m.h"
#include "dash/AbrController.h"

namespace leoproxy {

class DynamicZipfGenerator;

class HttpClientApp : public inet::ApplicationBase, public inet::TcpSocket::ICallback
{
  protected:
    enum TrafficType { WEB, VIDEO, MIXED };

    struct Request
    {
        long id = 0;
        int user = 0;
        std::string url;
        bool video = false;
        int quality = 0;
        omnetpp::simtime_t created;
        omnetpp::simtime_t sent;
        int retries = 0;
    };

    struct Conn
    {
        inet::TcpSocket *socket = nullptr;
        int user = 0;
        bool established = false;
        bool busy = false;
        Request request;
        inet::ChunkQueue rx;
    };

    struct User
    {
        std::vector<int> conns;           // socket ids
        std::deque<Request> queue;        // waiting for a free connection
        omnetpp::cMessage *timer = nullptr;
        // video playback position
        int title = -1;
        int nextSegment = 0;
        AbrController::State abr;
        int sinceUpSwitch = -1;           // segments since last up-switch (-1: none pending)
        // DASH player
        double buffer = 0;                // s
        omnetpp::simtime_t lastBufferUpdate;
        bool playing = false;
        bool stalled = false;
        omnetpp::simtime_t stallStart;
        bool segmentOutstanding = false;
    };

    // parameters
    int numUsers = 0;
    TrafficType trafficType = MIXED;
    double webFraction = 0.7;
    bool dashMode = false;
    double requestRate = 5;
    int maxConnectionsPerUser = 6;
    int maxQueuedRequestsPerUser = 32;
    int segmentsPerTitle = 300;
    double segmentDuration = 4;
    double dashMaxBuffer = 60, dashStartupBuffer = 8;
    int tracedUser = 0;
    omnetpp::simtime_t statsInterval;
    inet::L3Address connectAddress;
    int connectPort = 8080;

    // state
    std::vector<User> users;
    std::map<int, Conn> conns;          // socketId -> connection
    inet::SocketMap socketMap;
    AbrController abr;
    DynamicZipfGenerator *popularity = nullptr;
    long nextRequestId = 0;
    int64_t windowBytes = 0;
    double windowRtSum = 0;
    long windowRtCount = 0;
    long numSent = 0, numReceived = 0, numDropped = 0;
    omnetpp::cMessage *statsTimer = nullptr;

    omnetpp::simsignal_t responseTimeSignal, webResponseTimeSignal, videoResponseTimeSignal, hitResponseTimeSignal,
            missResponseTimeSignal, requestTotalTimeSignal, requestSentSignal, responseReceivedSignal, requestDroppedSignal,
            requestFailedSignal, bodyBytesSignal, groupThroughputSignal, responseTimeWindowSignal, throughputPerUserSignal, qualitySignal,
            qualitySwitchSignal, oscillationSignal, stallSignal, bufferSignal, tracedBufferSignal, tracedQualitySignal;

  protected:
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessageWhenUp(omnetpp::cMessage *msg) override;
    virtual void finish() override;
    virtual ~HttpClientApp();

    virtual void handleStartOperation(inet::LifecycleOperation *operation) override;
    virtual void handleStopOperation(inet::LifecycleOperation *operation) override;
    virtual void handleCrashOperation(inet::LifecycleOperation *operation) override;

    virtual void socketDataArrived(inet::TcpSocket *socket, inet::Packet *packet, bool urgent) override;
    virtual void socketAvailable(inet::TcpSocket *socket, inet::TcpAvailableInfo *availableInfo) override {}
    virtual void socketEstablished(inet::TcpSocket *socket) override;
    virtual void socketPeerClosed(inet::TcpSocket *socket) override;
    virtual void socketClosed(inet::TcpSocket *socket) override;
    virtual void socketFailure(inet::TcpSocket *socket, int code) override;
    virtual void socketStatusArrived(inet::TcpSocket *socket, inet::TcpStatusInfo *status) override {}
    virtual void socketDeleted(inet::TcpSocket *socket) override {}

    void userTimerExpired(int u);
    void generateIrmRequest(int u);
    void requestNextSegment(int u);
    Request makeVideoRequest(int u, double bufferForAbr);
    void submit(int u, const Request& req);
    void sendOnConnection(Conn& conn, const Request& req);
    void openConnection(int u, const Request& req);
    void handleResponse(Conn& conn, const inet::Ptr<const HttpResponseHeader>& hdr);
    void connectionLost(inet::TcpSocket *socket);
    void connectionFree(Conn& conn);
    void drainBuffer(User& user);
    void scheduleNextSegment(int u);
    void noteQualityDecision(int u, int quality);
};

} // namespace leoproxy

#endif
