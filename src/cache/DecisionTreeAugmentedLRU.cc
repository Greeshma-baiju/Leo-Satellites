#include "cache/DecisionTreeAugmentedLRU.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace leoproxy {
namespace cache {

using namespace omnetpp;

DecisionTreeAugmentedLRU::Features DecisionTreeAugmentedLRU::computeFeatures(const CacheEntry& e) const
{
    simtime_t now = simTime();
    double windowStart = now.dbl() - params.frequencyWindow;
    int freq = 0;
    for (auto it = e.recentAccesses.rbegin(); it != e.recentAccesses.rend(); ++it) {
        if (it->dbl() < windowStart)
            break;
        freq++;
    }
    return Features{
        (now - e.lastAccess).dbl(),          // recency
        (double)freq,                        // frequency within sliding window
        (double)e.sizeBytes,                 // object size
        e.video ? 1.0 : 0.0,                 // content type
        (now - e.insertedAt).dbl()           // time since first seen
    };
}

double DecisionTreeAugmentedLRU::predict(const Features& x) const
{
    if (tree.empty())
        return 0.5;
    int n = 0;
    while (tree[n].feature >= 0)
        n = (x[tree[n].feature] <= tree[n].threshold) ? tree[n].left : tree[n].right;
    return tree[n].prob;
}

std::string DecisionTreeAugmentedLRU::selectVictim()
{
    if (order.empty())
        return std::string();
    if (tree.empty() || store == nullptr)
        return order.back();   // plain LRU until the first model exists
    std::string best = order.back();
    double bestProb = std::numeric_limits<double>::infinity();
    int examined = 0;
    for (auto it = order.rbegin(); it != order.rend() && examined < params.candidatePool; ++it, ++examined) {
        auto e = store->find(*it);
        if (e == store->end())
            continue;
        double p = predict(computeFeatures(e->second));
        if (p < bestProb) {   // strict: ties keep the least-recently-used candidate
            bestProb = p;
            best = *it;
        }
    }
    return best;
}

void DecisionTreeAugmentedLRU::onEvict(const std::string& key)
{
    if (store != nullptr) {
        auto e = store->find(key);
        if (e != store->end()) {
            auto old = pending.find(key);
            if (old != pending.end()) {
                addLabelled(old->second);
                pending.erase(old);
            }
            PendingLabel p;
            p.sample.x = computeFeatures(e->second);
            p.evictedAt = simTime();
            p.predicted = tree.empty() ? -1 : predict(p.sample.x);
            pending.emplace(key, p);
        }
    }
    LRUPolicy::onEvict(key);
}

void DecisionTreeAugmentedLRU::noteRequest(const std::string& key)
{
    auto it = pending.find(key);
    if (it == pending.end())
        return;
    if ((simTime() - it->second.evictedAt).dbl() <= params.horizon)
        it->second.reRequested = true;
}

void DecisionTreeAugmentedLRU::addLabelled(const PendingLabel& p)
{
    Sample s = p.sample;
    s.y = p.reRequested ? 1 : 0;
    if (p.predicted >= 0) {
        windowTotal++;
        if ((p.predicted >= 0.5) == (s.y == 1))
            windowCorrect++;
    }
    training.push_back(s);
}

void DecisionTreeAugmentedLRU::matureLabels(bool all)
{
    simtime_t now = simTime();
    for (auto it = pending.begin(); it != pending.end();) {
        if (all || (now - it->second.evictedAt).dbl() >= params.horizon) {
            addLabelled(it->second);
            it = pending.erase(it);
        }
        else
            ++it;
    }
    if (training.size() > params.maxTrainingSamples)
        training.erase(training.begin(), training.begin() + (training.size() - params.maxTrainingSamples));
}

bool DecisionTreeAugmentedLRU::collectAccuracy(long& correct, long& total)
{
    correct = windowCorrect;
    total = windowTotal;
    windowCorrect = windowTotal = 0;
    return true;
}

int DecisionTreeAugmentedLRU::build(std::vector<int>& rows, int depth)
{
    int id = (int)tree.size();
    tree.emplace_back();
    int positives = 0;
    for (int r : rows)
        positives += training[r].y;
    double p = rows.empty() ? 0.5 : (double)positives / rows.size();
    tree[id].prob = p;
    if (depth >= params.maxDepth || (int)rows.size() < 2 * params.minSamplesPerLeaf || positives == 0 || positives == (int)rows.size())
        return id;

    double bestImpurity = std::numeric_limits<double>::infinity();
    int bestFeature = -1;
    double bestThreshold = 0;
    for (int f = 0; f < NUM_FEATURES; f++) {
        double lo = std::numeric_limits<double>::infinity(), hi = -lo;
        for (int r : rows) {
            lo = std::min(lo, training[r].x[f]);
            hi = std::max(hi, training[r].x[f]);
        }
        if (!(hi > lo))
            continue;
        for (int s = 1; s < params.thresholdsPerFeature; s++) {
            double thr = lo + (hi - lo) * s / params.thresholdsPerFeature;
            int nL = 0, nR = 0, pL = 0, pR = 0;
            for (int r : rows) {
                if (training[r].x[f] <= thr) { nL++; pL += training[r].y; }
                else { nR++; pR += training[r].y; }
            }
            if (nL < params.minSamplesPerLeaf || nR < params.minSamplesPerLeaf)
                continue;
            double qL = (double)pL / nL, qR = (double)pR / nR;
            double gini = (nL * 2 * qL * (1 - qL) + nR * 2 * qR * (1 - qR)) / rows.size();
            if (gini < bestImpurity) {
                bestImpurity = gini;
                bestFeature = f;
                bestThreshold = thr;
            }
        }
    }
    if (bestFeature < 0)
        return id;
    std::vector<int> left, right;
    for (int r : rows)
        (training[r].x[bestFeature] <= bestThreshold ? left : right).push_back(r);
    rows.clear();
    rows.shrink_to_fit();
    int l = build(left, depth + 1);
    int rgt = build(right, depth + 1);
    tree[id].feature = bestFeature;
    tree[id].threshold = bestThreshold;
    tree[id].left = l;
    tree[id].right = rgt;
    return id;
}

void DecisionTreeAugmentedLRU::retrain()
{
    matureLabels(false);
    if ((int)training.size() < params.minSamplesToFit)
        return;
    std::vector<Node> oldTree;
    oldTree.swap(tree);
    std::vector<int> rows(training.size());
    for (size_t i = 0; i < rows.size(); i++)
        rows[i] = (int)i;
    build(rows, 0);
    if (tree.empty())
        tree.swap(oldTree);
}

} // namespace cache
} // namespace leoproxy
