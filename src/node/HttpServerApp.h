#ifndef LEOPROXY_NODE_HTTPSERVERAPP_H
#define LEOPROXY_NODE_HTTPSERVERAPP_H

#include <map>

#include "inet/applications/base/ApplicationBase.h"
#include "inet/common/packet/ChunkQueue.h"
#include "inet/common/socket/SocketMap.h"
#include "inet/transportlayer/contract/tcp/TcpSocket.h"

#include "common/HttpMessages_m.h"
#include "common/HttpUtils.h"

namespace leoproxy {

class HttpServerApp : public inet::ApplicationBase, public inet::TcpSocket::ICallback
{
  protected:
    inet::TcpSocket listenSocket;
    inet::SocketMap socketMap;
    std::map<int, inet::ChunkQueue> rxQueues;   // socketId -> receive buffer
    httputil::CatalogParams catalog;
    double webMaxAge = 86400, videoMaxAge = 3600;
    omnetpp::simsignal_t requestServedSignal, bytesServedSignal;
    long numServed = 0;

  protected:
    virtual int numInitStages() const override { return inet::NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessageWhenUp(omnetpp::cMessage *msg) override;
    virtual void finish() override;
    virtual ~HttpServerApp();

    virtual void handleStartOperation(inet::LifecycleOperation *operation) override;
    virtual void handleStopOperation(inet::LifecycleOperation *operation) override;
    virtual void handleCrashOperation(inet::LifecycleOperation *operation) override;

    virtual void socketDataArrived(inet::TcpSocket *socket, inet::Packet *packet, bool urgent) override;
    virtual void socketAvailable(inet::TcpSocket *socket, inet::TcpAvailableInfo *availableInfo) override;
    virtual void socketEstablished(inet::TcpSocket *socket) override {}
    virtual void socketPeerClosed(inet::TcpSocket *socket) override;
    virtual void socketClosed(inet::TcpSocket *socket) override;
    virtual void socketFailure(inet::TcpSocket *socket, int code) override;
    virtual void socketStatusArrived(inet::TcpSocket *socket, inet::TcpStatusInfo *status) override {}
    virtual void socketDeleted(inet::TcpSocket *socket) override {}

    void removeSocket(inet::TcpSocket *socket);
    void sendResponse(int socketId, const inet::Ptr<const HttpRequestHeader>& req);
};

} // namespace leoproxy

#endif
