//
// Throughput-based DASH adaptive-bitrate controller.
//
// Baseline ABR: throughput estimate = harmonic mean of the last N segment
// download throughputs (segment size / download time), all segments treated
// equally. The highest representation whose bitrate does not exceed
// safetyFactor * estimate is selected; a low-buffer rule steps down one level
// when the buffer falls below the panic threshold.
//
// Cache-aware ABR: the proxy annotates every segment
// response as cache HIT or MISS; samples from cache hits are excluded from the
// satellite-path throughput estimator (hits still refill the playback buffer).
//
#ifndef LEOPROXY_DASH_ABRCONTROLLER_H
#define LEOPROXY_DASH_ABRCONTROLLER_H

#include <deque>
#include <vector>

namespace leoproxy {

class AbrController
{
  public:
    struct Params
    {
        std::vector<double> bitrates;     // bps per representation (ascending)
        int windowSize = 5;               // samples in the harmonic mean
        double safetyFactor = 0.8;
        double panicBuffer = 5.0;         // s
        bool cacheAware = false;
    };

    struct State
    {
        std::deque<double> samples;       // bps
        int lastQuality = -1;
    };

  protected:
    Params params;

  public:
    AbrController() {}
    explicit AbrController(const Params& p) : params(p) {}
    void setParams(const Params& p) { params = p; }
    const Params& getParams() const { return params; }

    // Records a completed segment download.
    void addSample(State& s, double throughputBps, bool cacheHit) const;
    // Current throughput estimate (bps), or a negative value if no sample yet.
    double estimate(const State& s) const;
    // Selects the representation index for the next segment.
    // bufferLevel < 0 means "no playback buffer model" (IRM request mode).
    int selectQuality(const State& s, double bufferLevel) const;
};

} // namespace leoproxy

#endif
