#include "dash/AbrController.h"

#include <algorithm>

namespace leoproxy {

void AbrController::addSample(State& s, double throughputBps, bool cacheHit) const
{
    if (params.cacheAware && cacheHit)
        return;   // hit samples do not represent the satellite-path throughput
    if (throughputBps <= 0)
        return;
    s.samples.push_back(throughputBps);
    while ((int)s.samples.size() > params.windowSize)
        s.samples.pop_front();
}

double AbrController::estimate(const State& s) const
{
    if (s.samples.empty())
        return -1;
    double inv = 0;
    for (double v : s.samples)
        inv += 1.0 / v;
    return s.samples.size() / inv;   // harmonic mean
}

int AbrController::selectQuality(const State& s, double bufferLevel) const
{
    if (params.bitrates.empty())
        return 0;
    double est = estimate(s);
    if (est < 0)
        return 0;   // start-up: lowest representation
    int best = 0;
    for (int i = 0; i < (int)params.bitrates.size(); i++)
        if (params.bitrates[i] <= params.safetyFactor * est)
            best = i;
    if (bufferLevel >= 0 && bufferLevel < params.panicBuffer && best > 0)
        best = std::max(0, std::min(best, s.lastQuality < 0 ? 0 : s.lastQuality) - 1);
    return best;
}

} // namespace leoproxy
