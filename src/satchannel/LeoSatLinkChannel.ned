package leoproxy.satchannel;

//
// Point-to-point satellite link segment (user link terminal<->satellite or
// feeder link satellite<->gateway) used with INET PPP interfaces.
//
// Model:
//  - bandwidth limitation: the channel datarate is the rate limiter at the
//    satellite link interface (the PPP egress queue in front of it is the
//    token-bucket backlog); datarate = raw capacity * (1 - FEC overhead);
//  - propagation delay: "delay" is updated at run time by LeoLinkController
//    from the 550 km orbit geometry (elevation angle);
//  - packet loss: two-state Gilbert-Elliott model (Good: no loss, Bad: loss),
//    parameterised by its stationary mean loss rate and mean burst length;
//  - handover: LeoLinkController puts the channel into outage (all frames
//    discarded) for the handover duration.
//
// One instance exists per direction of a bidirectional connection, so the
// uplink and downlink have independent loss processes.
//
channel LeoSatLinkChannel extends ned.DatarateChannel
{
    @class(LeoSatLinkChannel);
    double meanLossRate = default(0);     // stationary loss probability (0, 0.001, 0.01)
    double meanBurstLength = default(4);  // mean number of consecutive lost packets (3-5)
    bool lossEnabled = default(true);     // false for feeder links (loss applied once per path)
    @signal[satFrameLost](type=long);
}
