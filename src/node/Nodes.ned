package leoproxy.node;

import inet.node.inet.Router;
import inet.node.inet.StandardHost;

//
// Compound nodes of the six-tier testbed.
// All nodes are INET 4.4 StandardHost/Router derivatives. The proxy nodes
// (ForwardProxyRouter, ReverseProxyRouter) are defined next to the network
// in networks/FloraSatNetwork.ned.
//

//
// Client tier: one host per client group, running a population of users.
//
module ClientGroup extends StandardHost
{
    parameters:
        int numUsers;
        string forwardProxyAddress;
        numApps = 1;
        app[0].typename = "HttpClientApp";
        app[0].numUsers = this.numUsers;
        app[0].connectAddress = this.forwardProxyAddress;
        @display("i=device/laptop");
}

//
// User-side ground terminal (dish) - IP router towards the satellite.
//
module LeoUserTerminal extends Router
{
    parameters:
        @display("i=device/antennatower");
}

//
// LEO satellite, bent-pipe relay: forwards between the user
// link and the feeder link without application processing.
//
module LeoSatellite extends Router
{
    parameters:
        @display("i=device/satellite");
}

//
// Gateway ground station on the server-facing side of the satellite link.
//
module LeoGroundStation extends Router
{
    parameters:
        @display("i=device/receiverdish");
}

//
// Origin server (5 replicas, round-robin load balanced by the reverse proxy).
//
module OriginServer extends StandardHost
{
    parameters:
        numApps = 1;
        app[0].typename = "HttpServerApp";
        @display("i=device/server");
}
