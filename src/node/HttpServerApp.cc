#include "node/HttpServerApp.h"

#include "inet/common/ModuleAccess.h"
#include "inet/common/lifecycle/ModuleOperations.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"

namespace leoproxy {

using namespace omnetpp;
using namespace inet;

Define_Module(HttpServerApp);

namespace {
struct DelayedResponse
{
    int socketId;
    Ptr<const HttpRequestHeader> request;
};
}

HttpServerApp::~HttpServerApp()
{
    socketMap.deleteSockets();
}

void HttpServerApp::initialize(int stage)
{
    ApplicationBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        catalog.webSizeMedian = par("webSizeMedian").doubleValue();
        catalog.webSizeSigma = par("webSizeSigma").doubleValue();
        catalog.webSizeMin = par("webSizeMin").doubleValue();
        catalog.webSizeMax = par("webSizeMax").doubleValue();
        catalog.videoSegmentSizes = httputil::parseQuantityList(par("videoSegmentSizes").stringValue(), "B");
        catalog.videoSizeJitter = par("videoSizeJitter").doubleValue();
        catalog.seed = (uint64_t)par("catalogSeed").intValue();
        webMaxAge = par("webMaxAge").doubleValue();
        videoMaxAge = par("videoMaxAge").doubleValue();
        requestServedSignal = registerSignal("requestServed");
        bytesServedSignal = registerSignal("bytesServed");
        WATCH(numServed);
    }
}

void HttpServerApp::handleStartOperation(LifecycleOperation *operation)
{
    listenSocket.setOutputGate(gate("socketOut"));
    listenSocket.setCallback(this);
    listenSocket.bind(par("localPort").intValue());
    listenSocket.listen();
}

void HttpServerApp::handleStopOperation(LifecycleOperation *operation)
{
    for (auto& kv : socketMap.getMap())
        static_cast<TcpSocket *>(kv.second)->close();
    listenSocket.close();
    delayActiveOperationFinish(par("stopOperationTimeout"));
}

void HttpServerApp::handleCrashOperation(LifecycleOperation *operation)
{
    for (auto& kv : socketMap.getMap())
        static_cast<TcpSocket *>(kv.second)->destroy();
    socketMap.deleteSockets();
    rxQueues.clear();
    if (operation->getRootModule() != getContainingNode(this))
        listenSocket.destroy();
}

void HttpServerApp::handleMessageWhenUp(cMessage *msg)
{
    if (msg->isSelfMessage()) {
        auto *d = static_cast<DelayedResponse *>(msg->getContextPointer());
        sendResponse(d->socketId, d->request);
        delete d;
        delete msg;
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

void HttpServerApp::socketAvailable(TcpSocket *socket, TcpAvailableInfo *availableInfo)
{
    auto *newSocket = new TcpSocket(availableInfo);
    newSocket->setOutputGate(gate("socketOut"));
    newSocket->setCallback(this);
    socketMap.addSocket(newSocket);
    rxQueues[newSocket->getSocketId()];
    socket->accept(availableInfo->getNewSocketId());
}

void HttpServerApp::socketDataArrived(TcpSocket *socket, Packet *packet, bool urgent)
{
    auto& queue = rxQueues[socket->getSocketId()];
    queue.push(packet->peekData());
    delete packet;
    while (queue.has<HttpRequestHeader>()) {
        auto req = queue.pop<HttpRequestHeader>();
        simtime_t service = par("serviceTime");
        auto *msg = new cMessage("service");
        msg->setContextPointer(new DelayedResponse{ socket->getSocketId(), req });
        scheduleAfter(service, msg);
    }
}

void HttpServerApp::sendResponse(int socketId, const Ptr<const HttpRequestHeader>& req)
{
    auto it = socketMap.getMap().find(socketId);
    if (it == socketMap.getMap().end())
        return;   // client went away meanwhile
    auto socket = static_cast<TcpSocket *>(it->second);
    if (socket->getState() != TcpSocket::CONNECTED && socket->getState() != TcpSocket::PEER_CLOSED)
        return;

    httputil::ObjectId obj;
    auto resp = makeShared<HttpResponseHeader>();
    resp->setRequestId(req->getRequestId());
    resp->setUserId(req->getUserId());
    resp->setUrl(req->getUrl());
    int64_t size = 0;
    if (!httputil::parseUrl(req->getUrl(), obj)) {
        resp->setStatusCode(404);
        resp->setCacheControl("no-store");
        resp->setMaxAge(0);
    }
    else {
        size = httputil::objectSize(obj, catalog);
        double maxAge = obj.video ? videoMaxAge : webMaxAge;
        resp->setStatusCode(200);
        resp->setCacheControl(httputil::makeCacheControl(maxAge).c_str());
        resp->setMaxAge(maxAge);
        resp->setVideo(obj.video);
        resp->setQualityLevel(obj.quality);
    }
    resp->setContentLength(size);
    resp->setCacheStatus(HTTP_CACHE_NONE);

    auto packet = new Packet("HttpResponse");
    packet->insertAtBack(resp);
    if (size > 0)
        packet->insertAtBack(makeShared<ByteCountChunk>(B(size)));
    socket->send(packet);
    numServed++;
    emit(requestServedSignal, 1L);
    emit(bytesServedSignal, (intval_t)size);
}

void HttpServerApp::removeSocket(TcpSocket *socket)
{
    rxQueues.erase(socket->getSocketId());
    socketMap.removeSocket(socket);
    delete socket;
}

void HttpServerApp::socketPeerClosed(TcpSocket *socket)
{
    socket->close();
}

void HttpServerApp::socketClosed(TcpSocket *socket)
{
    if (socket != &listenSocket)
        removeSocket(socket);
}

void HttpServerApp::socketFailure(TcpSocket *socket, int code)
{
    if (socket != &listenSocket)
        removeSocket(socket);
}

void HttpServerApp::finish()
{
    recordScalar("requestsServedTotal", numServed);
}

} // namespace leoproxy
