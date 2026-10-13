#include "popularity/DynamicZipfGenerator.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace leoproxy {

using namespace omnetpp;

Define_Module(DynamicZipfGenerator);

DynamicZipfGenerator::~DynamicZipfGenerator()
{
    cancelAndDelete(phaseTimer);
}

void DynamicZipfGenerator::initialize()
{
    stationaryAlpha = par("alpha").doubleValue();
    dynamic = strcmp(par("model").stringValue(), "dynamic") == 0;
    webCatalogSize = par("webCatalogSize").intValue();
    videoCatalogSize = par("videoCatalogSize").intValue();
    if (webCatalogSize < 1 || videoCatalogSize < 1)
        throw cRuntimeError("Catalogue sizes must be positive");
    alphaSignal = registerSignal("zipfAlpha");

    currentAlpha = stationaryAlpha;
    if (dynamic) {
        parseSchedule(par("phaseSchedule").stringValue(), par("phaseOffset"));
        // the first phase also covers the warm-up period
        currentAlpha = phases.front().alpha;
        nextPhase = 1;
        if (nextPhase < phases.size()) {
            phaseTimer = new cMessage("zipfPhase");
            scheduleAt(phases[nextPhase].start, phaseTimer);
        }
        // pre-compute all CDFs so that the phase switch is instantaneous
        for (const auto& p : phases) {
            getCdf(webCatalogSize, p.alpha);
            getCdf(videoCatalogSize, p.alpha);
        }
    }
    else {
        getCdf(webCatalogSize, currentAlpha);
        getCdf(videoCatalogSize, currentAlpha);
    }
    emit(alphaSignal, currentAlpha);
    WATCH(currentAlpha);
}

void DynamicZipfGenerator::parseSchedule(const char *text, simtime_t offset)
{
    std::istringstream iss(text);
    std::string token;
    while (iss >> token) {
        auto colon = token.find(':');
        if (colon == std::string::npos)
            throw cRuntimeError("Invalid phaseSchedule entry '%s' (expected <time>:<alpha>)", token.c_str());
        double start = cValue::parseQuantity(token.substr(0, colon).c_str(), "s");
        double alpha = std::stod(token.substr(colon + 1));
        phases.push_back(Phase{ offset + start, alpha });
    }
    if (phases.empty())
        throw cRuntimeError("phaseSchedule is empty");
    std::sort(phases.begin(), phases.end(), [](const Phase& a, const Phase& b) { return a.start < b.start; });
    phases.front().start = SIMTIME_ZERO;
}

void DynamicZipfGenerator::handleMessage(cMessage *msg)
{
    if (msg != phaseTimer)
        throw cRuntimeError("Unexpected message");
    currentAlpha = phases[nextPhase].alpha;
    EV_INFO << "Zipf popularity phase " << nextPhase + 1 << ": alpha = " << currentAlpha << endl;
    emit(alphaSignal, currentAlpha);
    nextPhase++;
    if (nextPhase < phases.size())
        scheduleAt(phases[nextPhase].start, phaseTimer);
}

const std::vector<double>& DynamicZipfGenerator::getCdf(int catalogSize, double alpha)
{
    auto key = std::make_pair(catalogSize, alpha);
    auto it = cdfCache.find(key);
    if (it != cdfCache.end())
        return it->second;
    std::vector<double> cdf(catalogSize);
    double norm = 0;
    for (int k = 1; k <= catalogSize; k++)
        norm += 1.0 / std::pow((double)k, alpha);
    double acc = 0;
    for (int k = 1; k <= catalogSize; k++) {
        acc += (1.0 / std::pow((double)k, alpha)) / norm;
        cdf[k - 1] = acc;
    }
    cdf.back() = 1.0;
    return cdfCache.emplace(key, std::move(cdf)).first->second;
}

int DynamicZipfGenerator::sampleRank(bool video, cRNG *rng)
{
    const auto& cdf = getCdf(video ? videoCatalogSize : webCatalogSize, currentAlpha);
    double u = rng->doubleRand();
    auto it = std::lower_bound(cdf.begin(), cdf.end(), u);
    if (it == cdf.end())
        --it;
    return (int)(it - cdf.begin()) + 1;
}

} // namespace leoproxy
