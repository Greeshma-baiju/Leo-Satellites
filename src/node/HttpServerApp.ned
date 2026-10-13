package leoproxy.node;

import inet.applications.contract.IApp;

//
// Origin HTTP/1.1 server.
//
// Every origin hosts an identical replica of the content catalogue. The
// response body length is a deterministic function of the URL (see
// common/HttpUtils), so all five origins return identical objects.
// Responses carry permissive Cache-Control headers:
// static web content "public, max-age=86400", semi-static video segments
// "public, max-age=3600".
//
simple HttpServerApp like IApp
{
    parameters:
        int localPort = default(80);
        volatile double serviceTime @unit(s) = default(1ms);   // origin processing time per request
        double webSizeMedian @unit(B) = default(15kB);
        double webSizeSigma = default(2.45);                    // lognormal sigma (median 15 KB, p95 ~850 KB)
        double webSizeMin @unit(B) = default(200B);
        double webSizeMax @unit(B) = default(10MB);
        string videoSegmentSizes = default("1MB 3MB 6MB 9MB");  // 240p 480p 720p 1080p
        double videoSizeJitter = default(0.1);
        int catalogSeed = default(1);                           // fixed: identical catalogue across runs/servers
        double webMaxAge @unit(s) = default(86400s);
        double videoMaxAge @unit(s) = default(3600s);
        @display("i=block/app");
        @lifecycleSupport;
        double stopOperationExtraTime @unit(s) = default(-1s);
        double stopOperationTimeout @unit(s) = default(2s);

        @signal[requestServed](type=long);
        @signal[bytesServed](type=long);
        @statistic[requestServed](title="requests served by origin"; source=requestServed; record=count; interpolationmode=none);
        @statistic[bytesServed](title="body bytes served by origin"; source=bytesServed; unit=B; record=sum; interpolationmode=none);
    gates:
        input socketIn @labels(TcpCommand/up);
        output socketOut @labels(TcpCommand/down);
}
