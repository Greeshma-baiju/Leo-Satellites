package leoproxy.node;

import inet.applications.contract.IApp;

//
// HTTP/1.1 proxy application used both as
//  - the gateway-side REVERSE PROXY CACHE: shared cache, LRU/LFU/FIFO/Random/DTLRU
//    replacement, byte capacity 0/1/5/10 GB (0 = no-cache baseline, pass-through),
//    objects > 100 MB not cached, strict round-robin over the 5 origin servers;
//  - the user-side FORWARD PROXIES: transparent pass-through,
//    no caching (cacheSize = 0B), persistent-connection management and
//    multiplexing of client requests onto a pool of upstream connections.
//
// Each downstream request is answered on the connection it arrived on
// (no pipelining). Responses carry the cache status (HIT/MISS) set by the
// caching proxy (needed by the cache-aware ABR); a non-caching
// proxy preserves the status set further upstream.
//
simple HttpProxyApp like IApp
{
    parameters:
        int localPort = default(80);
        string upstreamAddresses;                 // space-separated names/addresses; "server[0..4]" expands to server[0] ... server[4]
        int upstreamPort = default(80);
        int maxUpstreamConnections = default(0);  // per upstream address; 0 = unlimited (new connection if none idle)
        int maxRetries = default(2);              // re-dispatch attempts if an upstream connection fails

        double cacheSize @unit(B) = default(0B);  // 0B = caching disabled
        string cachePolicy @enum("LRU","LFU","FIFO","Random","DTLRU") = default("LRU");
        double maxCacheableObjectSize @unit(B) = default(100MB);
        bool honorExpiry = default(false);        // no TTL expiry/invalidation during the run
        volatile double cacheLookupDelay @unit(s) = default(0.5ms);  // processing time of a cache hit
        volatile double forwardDelay @unit(s) = default(0.2ms);      // processing time before forwarding upstream
        bool recordCacheStats = default(true);    // false for the non-caching forward proxies
        double statsInterval @unit(s) = default(10s);

        // Decision-Tree-Augmented LRU; used when cachePolicy == "DTLRU"
        int dtCandidatePool = default(8);
        double dtRetrainInterval @unit(s) = default(300s);
        double dtHorizon @unit(s) = default(60s);
        double dtFrequencyWindow @unit(s) = default(300s);
        int dtMaxDepth = default(8);
        double dtAccuracyWindow @unit(s) = default(120s);

        @display("i=block/cogwheel");
        @lifecycleSupport;
        double stopOperationExtraTime @unit(s) = default(-1s);
        double stopOperationTimeout @unit(s) = default(2s);

        @signal[cacheHit](type=long);               // 1 = hit, 0 = miss, per request
        @signal[bytesServed](type=long);
        @signal[bytesFromCache](type=long);
        @signal[bytesFromOrigin](type=long);
        @signal[redundantOriginBytes](type=long);
        @signal[redundantDeliveredBytes](type=long);
        @signal[proxyHitDelay](type=simtime_t);
        @signal[proxyMissDelay](type=simtime_t);
        @signal[originRequest](type=long);          // upstream index
        @signal[cacheHitRatioWindow](type=double);
        @signal[byteHitRatioWindow](type=double);
        @signal[originThroughput](type=double);
        @signal[cacheOccupancy](type=long);
        @signal[cacheEvictions](type=long);
        @signal[dtPredictionAccuracy](type=double);
        @signal[upstreamConnections](type=long);

        @statistic[cacheHit](title="cache hit (request hit ratio = mean)"; source=cacheHit; record=mean,count,sum; interpolationmode=none);
        @statistic[bytesServed](title="response body bytes served downstream"; source=bytesServed; unit=B; record=sum; interpolationmode=none);
        @statistic[bytesFromCache](title="response body bytes served from cache"; source=bytesFromCache; unit=B; record=sum; interpolationmode=none);
        @statistic[bytesFromOrigin](title="response body bytes fetched upstream"; source=bytesFromOrigin; unit=B; record=sum; interpolationmode=none);
        @statistic[redundantOriginBytes](title="upstream bytes for objects already fetched in the window"; source=redundantOriginBytes; unit=B; record=sum; interpolationmode=none);
        @statistic[redundantDeliveredBytes](title="delivered bytes for objects already delivered in the window"; source=redundantDeliveredBytes; unit=B; record=sum; interpolationmode=none);
        @statistic[proxyHitDelay](title="proxy residence time, cache hits"; source=proxyHitDelay; unit=s; record=mean,histogram; interpolationmode=none);
        @statistic[proxyMissDelay](title="proxy residence time, cache misses"; source=proxyMissDelay; unit=s; record=mean,histogram; interpolationmode=none);
        @statistic[originRequests](title="requests forwarded upstream"; source=originRequest; record=count; interpolationmode=none);
        @statistic[cacheHitRatioWindow](title="request hit ratio per stats interval"; source=cacheHitRatioWindow; unit=ratio; record=vector; interpolationmode=sample-hold);
        @statistic[byteHitRatioWindow](title="byte hit ratio per stats interval"; source=byteHitRatioWindow; unit=ratio; record=vector; interpolationmode=sample-hold);
        @statistic[originThroughput](title="upstream (origin) throughput"; source=originThroughput; unit=bps; record=mean,vector; interpolationmode=sample-hold);
        @statistic[cacheOccupancy](title="cache occupancy"; source=cacheOccupancy; unit=B; record=last,timeavg,vector; interpolationmode=sample-hold);
        @statistic[cacheEvictions](title="cache evictions"; source=cacheEvictions; record=sum; interpolationmode=none);
        @statistic[dtPredictionAccuracy](title="DT+LRU prediction accuracy per window"; source=dtPredictionAccuracy; unit=ratio; record=mean,vector; interpolationmode=none);
        @statistic[upstreamConnections](title="open upstream connections"; source=upstreamConnections; record=max,timeavg,vector; interpolationmode=sample-hold; autoWarmupFilter=false);
    gates:
        input socketIn @labels(TcpCommand/up);
        output socketOut @labels(TcpCommand/down);
}
