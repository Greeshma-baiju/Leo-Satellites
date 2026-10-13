#ifndef LEOPROXY_POPULARITY_DYNAMICZIPFGENERATOR_H
#define LEOPROXY_POPULARITY_DYNAMICZIPFGENERATOR_H

#include <map>
#include <utility>
#include <vector>

#include <omnetpp.h>

namespace leoproxy {

class DynamicZipfGenerator : public omnetpp::cSimpleModule
{
  protected:
    struct Phase { omnetpp::simtime_t start; double alpha; };

    bool dynamic = false;
    double stationaryAlpha = 1.0;
    double currentAlpha = 1.0;
    int webCatalogSize = 10000;
    int videoCatalogSize = 1000;
    std::vector<Phase> phases;
    size_t nextPhase = 0;
    omnetpp::cMessage *phaseTimer = nullptr;
    omnetpp::simsignal_t alphaSignal;

    // cumulative distribution functions keyed by (catalogSize, alpha)
    std::map<std::pair<int, double>, std::vector<double>> cdfCache;

  protected:
    virtual void initialize() override;
    virtual void handleMessage(omnetpp::cMessage *msg) override;
    virtual ~DynamicZipfGenerator();
    const std::vector<double>& getCdf(int catalogSize, double alpha);
    void parseSchedule(const char *text, omnetpp::simtime_t offset);

  public:
    // Returns a 1-based rank drawn from Zipf(alpha) over the web or video catalogue.
    int sampleRank(bool video, omnetpp::cRNG *rng);
    double getCurrentAlpha() const { return currentAlpha; }
    int getWebCatalogSize() const { return webCatalogSize; }
    int getVideoCatalogSize() const { return videoCatalogSize; }
};

} // namespace leoproxy

#endif
