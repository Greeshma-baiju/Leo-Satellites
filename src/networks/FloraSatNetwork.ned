package leoproxy.networks;

import inet.common.misc.ThruputMeteringChannel;
import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;
import inet.node.inet.Router;
import leoproxy.handover.LeoLinkController;
import leoproxy.node.ClientGroup;
import leoproxy.node.LeoGroundStation;
import leoproxy.node.LeoSatellite;
import leoproxy.node.LeoUserTerminal;
import leoproxy.node.OriginServer;
import leoproxy.popularity.DynamicZipfGenerator;
import leoproxy.satchannel.LeoSatLinkChannel;

//
// Forward proxy near the clients.
// INET Router (it forwards IP traffic) that also runs the HTTP proxy
// application: transparent pass-through, no caching, persistent-connection
// management and multiplexing of the client requests towards the reverse proxy.
//
module ForwardProxyRouter extends Router
{
    parameters:
        string reverseProxyAddress = default("reverseProxy");
        double processingDelay @unit(s) = default(0.1ms);   // relay processing per request
        bool enableCaching = default(false);                // default: no cache at the forward proxies
        double cacheSize @unit(B) = default(0B);
        string cachePolicy = default("LRU");
        hasTcp = true;
        numApps = 1;
        app[0].typename = "HttpProxyApp";
        app[0].localPort = 8080;
        app[0].upstreamAddresses = this.reverseProxyAddress;
        app[0].upstreamPort = 80;
        app[0].forwardDelay = this.processingDelay;
        app[0].cacheSize = this.enableCaching ? this.cacheSize : 0B;
        app[0].cachePolicy = this.cachePolicy;
        app[0].recordCacheStats = this.enableCaching;
        app[0].cacheLookupDelay = default(0.5ms);
        @display("i=device/modem");
}

//
// Reverse proxy with caching capability.
// Single shared instance between the gateway tier and the five origin servers;
// enableCaching = false or cacheSize = 0B is the no-cache baseline.
// Misses are forwarded to the origins in strict round-robin order.
//
module ReverseProxyRouter extends Router
{
    parameters:
        bool enableCaching = default(true);
        double cacheSize @unit(B) = default(10GB);          // 0B (baseline), 1GB, 5GB, 10GB
        string cachePolicy = default("LRU");                // LRU / LFU / FIFO / Random / DTLRU
        int connectionPoolSize = default(0);                // max connections per origin, 0 = unlimited
        double processingDelay @unit(s) = default(0.2ms);   // forwarding time of a miss
        double cacheLookupDelay @unit(s) = default(0.5ms);  // service time of a hit
        string originServers = default("server1 server2 server3 server4 server5");
        hasTcp = true;
        numApps = 1;
        app[0].typename = "HttpProxyApp";
        app[0].localPort = 80;
        app[0].upstreamAddresses = this.originServers;
        app[0].upstreamPort = 80;
        app[0].maxUpstreamConnections = this.connectionPoolSize;
        app[0].forwardDelay = this.processingDelay;
        app[0].cacheLookupDelay = this.cacheLookupDelay;
        app[0].cacheSize = this.enableCaching ? this.cacheSize : 0B;
        app[0].cachePolicy = this.cachePolicy;
        app[0].recordCacheStats = true;
        @display("i=device/router;is=l");
}

//
// Full satellite + ground + proxy + server network.
//
//  clientGroup1..4 --LAN-- forwardProxy1..3 --LAN-- terminal1..3
//     ==user link== leosat1..3 ==feeder link== groundStation1..3
//     --terrestrial 10 Gbps-- internetCloud --10 Gbps-- reverseProxy --10 Gbps-- server1..5
//
// numClientGroups = 4: groups 1 and 2 share forwardProxy1;
// numClientGroups = 3: one group per forward proxy.
// The satellites are bent-pipe relays; the inter-satellite links are only
// created when enableIsl = true.
//
network FloraSatNetwork
{
    parameters:
        // ---- scenario and load ----
        string scenario @enum("land","remote") = default("land");
        int numClients = default(500);                    // 10, 50, 100, 200, 500 concurrent clients
        int numClientGroups = default(4);                 // 4 or 3
        bool enableIsl = default(false);                  // bent pipe, no ISL

        // ---- cache placement and configuration ----
        string cacheLocation @enum("reverseProxy","forwardProxy","none") = default("reverseProxy");
        double cacheSize @unit(B) = default(10GB);        // 0B, 1GB, 5GB, 10GB
        string cachePolicy = default("LRU");              // LRU / LFU / FIFO / Random / DTLRU

        // ---- satellite link ----
        double satLinkCapacity @unit(bps) = default(100Mbps);   // raw: 50 / 100 / 200 Mbps
        double fecOverhead = default(0.10);
        double satLossRate = default(0.001);                    // Gilbert-Elliott mean loss: 0 / 0.001 / 0.01
        double satBurstLength = default(4);                     // mean burst length (3-5 packets)
        int satQueuePackets = default(1000);                    // satellite interface queue (assumption)

        // ---- geometry and terrestrial delays per scenario (assumptions, README 5.1) ----
        double userMinElevation @unit(deg) = default(scenario == "remote" ? 10deg : 25deg);
        double feederMinElevation @unit(deg) = default(scenario == "remote" ? 10deg : 25deg);
        double terrestrialDelay @unit(s) = default(scenario == "remote" ? 25ms : 10ms);  // gateway -> internet core
        double originDelay @unit(s) = default(2ms);             // reverse proxy -> origin data centre
        double lanDelay @unit(s) = default(0.1ms);

        // interface queues in front of the rate-limited satellite channels
        terminal*.ppp[*].queue.packetCapacity = default(satQueuePackets);
        groundStation*.ppp[*].queue.packetCapacity = default(satQueuePackets);
        leosat*.ppp[*].queue.packetCapacity = default(satQueuePackets);
        **.ppp[*].ppp.mtu = default(1500B);

        @display("bgb=1980,1180");

    types:
        // client/proxy LAN
        channel LandLink extends ThruputMeteringChannel
        {
            delay = default(0.1ms);
            datarate = default(1Gbps);
        }
        // terrestrial internet: 10 Gbps, "effectively unlimited"
        channel TerrestrialLink extends ThruputMeteringChannel
        {
            delay = default(0.1ms);
            datarate = default(10Gbps);
        }
        // user link (terminal <-> satellite): rate limit + Gilbert-Elliott loss,
        // delay overwritten at run time from the orbit geometry
        channel SatLink_User extends LeoSatLinkChannel
        {
            delay = default(2ms);
            lossEnabled = true;
        }
        // feeder link (satellite <-> gateway): same rate (bent pipe), loss applied once on the user link
        channel SatLink_Feeder extends LeoSatLinkChannel
        {
            delay = default(2ms);
            meanLossRate = 0;
            lossEnabled = false;
        }
        // optional inter-satellite link
        channel SatLink_ISL extends ThruputMeteringChannel
        {
            delay = default(5ms);
            datarate = default(100Mbps);
        }

    submodules:
        configurator: Ipv4NetworkConfigurator {
            @display("p=750,20");
        }
        popularity: DynamicZipfGenerator {
            @display("p=1750,20");
        }
        satLinkController[3]: LeoLinkController {
            userTerminalModule = "^.terminal" + string(index + 1);
            satelliteModule = "^.leosat" + string(index + 1);
            groundStationModule = "^.groundStation" + string(index + 1);
            linkCapacity = parent.satLinkCapacity;
            userMinElevation = parent.userMinElevation;
            feederMinElevation = parent.feederMinElevation;
            @display("p=1750,120,column,80");
        }

        // ---- client groups (users split evenly over the groups) ----
        clientGroup1: ClientGroup {
            numUsers = int(parent.numClients / parent.numClientGroups) + (0 < parent.numClients % parent.numClientGroups ? 1 : 0);
            forwardProxyAddress = "forwardProxy1";
            @display("p=150,150;i=device/pc");
        }
        clientGroup2: ClientGroup {
            numUsers = int(parent.numClients / parent.numClientGroups) + (1 < parent.numClients % parent.numClientGroups ? 1 : 0);
            forwardProxyAddress = parent.numClientGroups == 4 ? "forwardProxy1" : "forwardProxy2";
            @display("p=530,120;i=device/pc");
        }
        clientGroup3: ClientGroup {
            numUsers = int(parent.numClients / parent.numClientGroups) + (2 < parent.numClients % parent.numClientGroups ? 1 : 0);
            forwardProxyAddress = parent.numClientGroups == 4 ? "forwardProxy2" : "forwardProxy3";
            @display("p=960,70;i=device/pc");
        }
        clientGroup4: ClientGroup if numClientGroups == 4 {
            numUsers = int(parent.numClients / parent.numClientGroups);
            forwardProxyAddress = "forwardProxy3";
            @display("p=1640,150;i=device/pc");
        }

        // ---- access tier ----
        forwardProxy1: ForwardProxyRouter {
            enableCaching = parent.cacheLocation == "forwardProxy";
            cacheSize = parent.cacheSize;
            cachePolicy = parent.cachePolicy;
            @display("p=320,350");
        }
        forwardProxy2: ForwardProxyRouter {
            enableCaching = parent.cacheLocation == "forwardProxy";
            cacheSize = parent.cacheSize;
            cachePolicy = parent.cachePolicy;
            @display("p=880,270");
        }
        forwardProxy3: ForwardProxyRouter {
            enableCaching = parent.cacheLocation == "forwardProxy";
            cacheSize = parent.cacheSize;
            cachePolicy = parent.cachePolicy;
            @display("p=1500,230");
        }

        // ---- user terminals ----
        terminal1: LeoUserTerminal {
            @display("p=540,350;i=device/antennatower");
        }
        terminal2: LeoUserTerminal {
            @display("p=900,380;i=device/antennatower");
        }
        terminal3: LeoUserTerminal {
            @display("p=1300,350;i=device/antennatower");
        }

        // ---- space segment (550 km,) ----
        leosat1: LeoSatellite {
            @display("p=550,485;i=device/satellite");
        }
        leosat2: LeoSatellite {
            @display("p=900,525;i=device/satellite");
        }
        leosat3: LeoSatellite {
            @display("p=1300,485;i=device/satellite");
        }

        // ---- gateway tier ----
        groundStation1: LeoGroundStation {
            @display("p=600,640;i=device/antennatower");
        }
        groundStation2: LeoGroundStation {
            @display("p=880,610;i=device/antennatower");
        }
        groundStation3: LeoGroundStation {
            @display("p=1290,640;i=device/antennatower");
        }

        // terrestrial internet core between the gateways and the reverse proxy
        internetCloud: Router {
            @display("p=880,780;i=misc/cloud");
        }

        // ---- reverse proxy cache ----
        reverseProxy: ReverseProxyRouter {
            enableCaching = parent.cacheLocation == "reverseProxy";
            cacheSize = parent.cacheSize;
            cachePolicy = parent.cachePolicy;
            @display("p=880,900;i=device/router");
        }

        // ---- origin servers ----
        server1: OriginServer {
            @display("p=240,1120;i=device/server");
        }
        server2: OriginServer {
            @display("p=635,1120;i=device/server");
        }
        server3: OriginServer {
            @display("p=900,1120;i=device/server");
        }
        server4: OriginServer {
            @display("p=1200,1120;i=device/server");
        }
        server5: OriginServer {
            @display("p=1640,1120;i=device/server");
        }

    connections allowunconnected:
        // clients -> forward proxies (4 groups: 1+2 on forwardProxy1; 3 groups: 1:1)
        clientGroup1.ethg++ <--> LandLink { delay = parent.lanDelay; } <--> forwardProxy1.ethg++;
        clientGroup2.ethg++ <--> LandLink { delay = parent.lanDelay; } <--> forwardProxy1.ethg++ if numClientGroups == 4;
        clientGroup2.ethg++ <--> LandLink { delay = parent.lanDelay; } <--> forwardProxy2.ethg++ if numClientGroups == 3;
        clientGroup3.ethg++ <--> LandLink { delay = parent.lanDelay; } <--> forwardProxy2.ethg++ if numClientGroups == 4;
        clientGroup3.ethg++ <--> LandLink { delay = parent.lanDelay; } <--> forwardProxy3.ethg++ if numClientGroups == 3;
        clientGroup4.ethg++ <--> LandLink { delay = parent.lanDelay; } <--> forwardProxy3.ethg++ if numClientGroups == 4;

        // forward proxies -> user terminals
        forwardProxy1.ethg++ <--> LandLink { delay = parent.lanDelay; } <--> terminal1.ethg++;
        forwardProxy2.ethg++ <--> LandLink { delay = parent.lanDelay; } <--> terminal2.ethg++;
        forwardProxy3.ethg++ <--> LandLink { delay = parent.lanDelay; } <--> terminal3.ethg++;

        // user links (terminal <-> satellite)
        terminal1.pppg++ <--> SatLink_User { datarate = parent.satLinkCapacity * (1 - parent.fecOverhead); meanLossRate = parent.satLossRate; meanBurstLength = parent.satBurstLength; } <--> leosat1.pppg++;
        terminal2.pppg++ <--> SatLink_User { datarate = parent.satLinkCapacity * (1 - parent.fecOverhead); meanLossRate = parent.satLossRate; meanBurstLength = parent.satBurstLength; } <--> leosat2.pppg++;
        terminal3.pppg++ <--> SatLink_User { datarate = parent.satLinkCapacity * (1 - parent.fecOverhead); meanLossRate = parent.satLossRate; meanBurstLength = parent.satBurstLength; } <--> leosat3.pppg++;

        // feeder links (satellite <-> gateway)
        leosat1.pppg++ <--> SatLink_Feeder { datarate = parent.satLinkCapacity * (1 - parent.fecOverhead); } <--> groundStation1.pppg++;
        leosat2.pppg++ <--> SatLink_Feeder { datarate = parent.satLinkCapacity * (1 - parent.fecOverhead); } <--> groundStation2.pppg++;
        leosat3.pppg++ <--> SatLink_Feeder { datarate = parent.satLinkCapacity * (1 - parent.fecOverhead); } <--> groundStation3.pppg++;

        // gateways -> terrestrial internet -> reverse proxy (10 Gbps)
        groundStation1.ethg++ <--> TerrestrialLink { delay = parent.terrestrialDelay; } <--> internetCloud.ethg++;
        groundStation2.ethg++ <--> TerrestrialLink { delay = parent.terrestrialDelay; } <--> internetCloud.ethg++;
        groundStation3.ethg++ <--> TerrestrialLink { delay = parent.terrestrialDelay; } <--> internetCloud.ethg++;
        internetCloud.ethg++ <--> TerrestrialLink { delay = parent.lanDelay; } <--> reverseProxy.ethg++;

        // reverse proxy -> origin servers (10 Gbps)
        reverseProxy.ethg++ <--> TerrestrialLink { delay = parent.originDelay; } <--> server1.ethg++;
        reverseProxy.ethg++ <--> TerrestrialLink { delay = parent.originDelay; } <--> server2.ethg++;
        reverseProxy.ethg++ <--> TerrestrialLink { delay = parent.originDelay; } <--> server3.ethg++;
        reverseProxy.ethg++ <--> TerrestrialLink { delay = parent.originDelay; } <--> server4.ethg++;
        reverseProxy.ethg++ <--> TerrestrialLink { delay = parent.originDelay; } <--> server5.ethg++;

        // optional inter-satellite links (off by default: bent pipe)
        leosat1.pppg++ <--> SatLink_ISL <--> leosat2.pppg++ if enableIsl;
        leosat2.pppg++ <--> SatLink_ISL <--> leosat3.pppg++ if enableIsl;
}
