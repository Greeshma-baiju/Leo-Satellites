//
// Byte-capacity cache with pluggable replacement policy. Objects larger than maxObjectSize (100 MB) are never cached.
// Eviction is triggered when a new object does not fit, and repeats until the
// object fits.
//
#ifndef LEOPROXY_CACHE_CACHEMANAGER_H
#define LEOPROXY_CACHE_CACHEMANAGER_H

#include <memory>
#include <string>

#include "cache/CacheReplacementPolicy.h"
#include "cache/DecisionTreeAugmentedLRU.h"

namespace leoproxy {
namespace cache {

class CacheManager
{
  public:
    struct Stats { uint64_t insertions = 0; uint64_t evictions = 0; uint64_t evictedBytes = 0; uint64_t rejectedTooLarge = 0; };

  protected:
    int64_t capacityBytes;
    int64_t maxObjectSize;
    double frequencyWindow;
    int64_t usedBytes = 0;
    CacheStore store;
    std::unique_ptr<CacheReplacementPolicy> policy;
    Stats stats;

    void evictOne();

  public:
    CacheManager(int64_t capacityBytes, int64_t maxObjectSize, const std::string& policyName,
                 omnetpp::cRNG *rng, const DtParams& dtParams);

    // Called for every request (hit or miss) before lookup; feeds DT+LRU labels.
    void noteRequest(const std::string& key) { policy->noteRequest(key); }

    // Returns nullptr on miss. On hit updates recency/frequency metadata.
    // If honorExpiry is set and the entry is stale, it is removed and nullptr returned.
    const CacheEntry *lookup(const std::string& key, bool honorExpiry);

    // Returns the entry without updating access metadata (nullptr if absent).
    const CacheEntry *peek(const std::string& key) const
    {
        auto it = store.find(key);
        return it == store.end() ? nullptr : &it->second;
    }

    // Stores a response; returns false if not admitted (too large).
    bool insert(const std::string& key, int64_t sizeBytes, bool video, int qualityLevel,
                const std::string& cacheControl, double maxAgeSeconds);

    void periodicMaintenance() { policy->periodicMaintenance(); }
    void retrain() { policy->retrain(); }
    bool collectAccuracy(long& correct, long& total) { return policy->collectAccuracy(correct, total); }

    int64_t getUsedBytes() const { return usedBytes; }
    int64_t getCapacityBytes() const { return capacityBytes; }
    size_t getNumEntries() const { return store.size(); }
    const Stats& getStats() const { return stats; }
    const char *getPolicyName() const { return policy->getName(); }
};

} // namespace cache
} // namespace leoproxy

#endif
