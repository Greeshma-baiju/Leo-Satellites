package leoproxy.handover;

//
//   terminal<i> <-> leosat<i> <-> groundStation<i>
//
simple LeoLinkController
{
    parameters:
        string userTerminalModule = default("^.userTerminal[" + string(index) + "]");
        string satelliteModule = default("^.leoSat[" + string(index) + "]");
        string groundStationModule = default("^.groundStation[" + string(index) + "]");

        double linkCapacity @unit(bps) = default(100Mbps);   // raw configured satellite link rate
        double altitude @unit(km) = default(550km);
        double earthRadius @unit(km) = default(6371km);
        double userMinElevation @unit(deg) = default(25deg);
        double userMaxElevation @unit(deg) = default(85deg);
        double feederMinElevation @unit(deg) = default(25deg);
        double feederMaxElevation @unit(deg) = default(85deg);
        volatile double handoverInterval @unit(s) = default(max(60s, normal(360s, 30s)));
        volatile double handoverOutage @unit(s) = default(100ms);
        volatile double initialPassPhase = default(uniform(0, 1));   // fraction of the first pass already elapsed
        bool handoverEnabled = default(true);
        double geometryUpdateInterval @unit(s) = default(1s);
        double meterInterval @unit(s) = default(1s);

        @display("i=block/cogwheel");

        @signal[satDownlinkUtilization](type=double);
        @signal[satUplinkUtilization](type=double);
        @signal[satDownlinkThroughput](type=double);
        @signal[satUplinkThroughput](type=double);
        @signal[satDownlinkLossRate](type=double);
        @signal[satUplinkLossRate](type=double);
        @signal[satPacketsSent](type=long);
        @signal[satPacketsLost](type=long);
        @signal[satQueueDrops](type=long);
        @signal[satOneWayDelay](type=simtime_t);
        @signal[handover](type=simtime_t);

        @statistic[satDownlinkUtilization](title="satellite downlink utilisation (raw capacity)"; source=satDownlinkUtilization; unit=ratio; record=mean,max,vector; interpolationmode=sample-hold);
        @statistic[satUplinkUtilization](title="satellite uplink utilisation (raw capacity)"; source=satUplinkUtilization; unit=ratio; record=mean,max,vector; interpolationmode=sample-hold);
        @statistic[satDownlinkThroughput](title="satellite downlink throughput"; source=satDownlinkThroughput; unit=bps; record=mean,max,vector; interpolationmode=sample-hold);
        @statistic[satUplinkThroughput](title="satellite uplink throughput"; source=satUplinkThroughput; unit=bps; record=mean,vector; interpolationmode=sample-hold);
        @statistic[satDownlinkLossRate](title="satellite downlink packet loss rate"; source=satDownlinkLossRate; unit=ratio; record=mean,vector; interpolationmode=sample-hold);
        @statistic[satUplinkLossRate](title="satellite uplink packet loss rate"; source=satUplinkLossRate; unit=ratio; record=mean,vector; interpolationmode=sample-hold);
        @statistic[satPacketsSent](title="packets transmitted on satellite channels"; source=satPacketsSent; record=sum);
        @statistic[satPacketsLost](title="packets lost on satellite channels (GE + outage)"; source=satPacketsLost; record=sum);
        @statistic[satQueueDrops](title="satellite interface queue overflow drops"; source=satQueueDrops; record=sum);
        @statistic[satOneWayDelay](title="one-way propagation delay terminal-gateway"; source=satOneWayDelay; unit=s; record=mean,min,max,vector; interpolationmode=linear);
        @statistic[handover](title="handover events"; source=handover; record=count,vector; interpolationmode=none);
}
