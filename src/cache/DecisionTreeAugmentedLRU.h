//
// Decision-Tree-Augmented LRU (DT+LRU).
//
//  - Keeps the plain LRU recency list.
//  - On eviction, takes the k least-recently-used objects as candidate pool
//    and evicts the one with the lowest predicted re-request probability.
//  - Classifier features: recency, frequency within a sliding window, object
//    size, content type (web/video), time since first seen (insertion).
//  - Training labels: an evicted object is positive if it is re-requested
//    within a fixed horizon after its eviction, negative otherwise.
//  - The tree (CART, Gini impurity) is refit periodically (default every
//    300 s of simulated time) on the accumulated labelled samples.
//  - Before the first successful fit the policy is exactly LRU (graceful
//    degradation).
//
#ifndef LEOPROXY_CACHE_DECISIONTREEAUGMENTEDLRU_H
#define LEOPROXY_CACHE_DECISIONTREEAUGMENTEDLRU_H

#include <array>
#include <unordered_map>
#include <vector>

#include "cache/LRUPolicy.h"

namespace leoproxy {
namespace cache {

struct DtParams
{
    int candidatePool = 8;                // k
    double horizon = 60;                  // s, re-request labelling horizon
    double frequencyWindow = 300;         // s, sliding window for the frequency feature
    int maxDepth = 8;
    int minSamplesToFit = 30;
    int minSamplesPerLeaf = 10;
    size_t maxTrainingSamples = 20000;    // most recent samples kept
    int thresholdsPerFeature = 16;
};

class DecisionTreeAugmentedLRU : public LRUPolicy
{
  public:
    static constexpr int NUM_FEATURES = 5;
    using Features = std::array<double, NUM_FEATURES>;

  protected:
    struct Sample { Features x; int y = 0; };
    struct PendingLabel
    {
        Sample sample;
        omnetpp::simtime_t evictedAt;
        double predicted = -1;            // predicted re-request probability at eviction (-1: no model)
        bool reRequested = false;
    };
    struct Node { int feature = -1; double threshold = 0; int left = -1; int right = -1; double prob = 0.5; };

    DtParams params;
    std::unordered_map<std::string, PendingLabel> pending;
    std::vector<Sample> training;
    std::vector<Node> tree;
    long windowCorrect = 0, windowTotal = 0;

  protected:
    Features computeFeatures(const CacheEntry& e) const;
    double predict(const Features& x) const;
    void matureLabels(bool all);
    void addLabelled(const PendingLabel& p);
    int build(std::vector<int>& rows, int depth);

  public:
    explicit DecisionTreeAugmentedLRU(const DtParams& p) : params(p) {}

    virtual std::string selectVictim() override;
    virtual void onEvict(const std::string& key) override;
    virtual void noteRequest(const std::string& key) override;
    virtual void periodicMaintenance() override { matureLabels(false); }
    virtual void retrain() override;
    virtual bool collectAccuracy(long& correct, long& total) override;
    virtual const char *getName() const override { return "DTLRU"; }

    bool hasModel() const { return !tree.empty(); }
    size_t getTrainingSetSize() const { return training.size(); }
};

} // namespace cache
} // namespace leoproxy

#endif
