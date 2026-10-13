package leoproxy.popularity;

//
// Content popularity generator.
//
// Draws object ranks from a Zipf distribution with skew alpha over the web
// catalogue (10,000 objects) or the video catalogue (1,000 titles) under the
// Independent Reference Model.
//
// model = "stationary": alpha is constant for the whole run.
// model = "dynamic"   : alpha follows the four-phase schedule in phaseSchedule.
//                       Phase times are measured from phaseOffset, which by
//                       default equals the 600 s warm-up, so that the
//                       0/900/1750/2650 s checkpoints refer to the 3,600 s
//                       measurement window.
//
// A single instance at network level is shared by all client groups; each
// caller passes its own RNG, so the draws are controlled by the run's seed-set.
//
simple DynamicZipfGenerator
{
    parameters:
        double alpha = default(1.0);                 // stationary Zipf skew (0.3 / 1.0 / 1.2)
        string model @enum("stationary","dynamic") = default("stationary");
        int webCatalogSize = default(10000);
        int videoCatalogSize = default(1000);
        // "<start>:<alpha>" pairs, start relative to phaseOffset
        string phaseSchedule = default("0s:1.0 900s:0.3 1750s:1.2 2650s:1.0");
        double phaseOffset @unit(s) = default(600s);
        @display("i=block/control");
        @signal[zipfAlpha](type=double);
        @statistic[zipfAlpha](title="Zipf alpha in effect"; source=zipfAlpha; record=vector,last; interpolationmode=sample-hold; autoWarmupFilter=false);
}
