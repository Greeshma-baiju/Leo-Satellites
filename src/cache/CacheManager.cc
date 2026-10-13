#include "cache/CacheManager.h"

#include "cache/FIFOPolicy.h"
#include "cache/LFUPolicy.h"
#include "cache/LRUPolicy.h"
#include "cache/RandomPolicy.h"

namespace leoproxy {
namespace cache {

using namespace omnetpp;

CacheManager::CacheManager(int64_t capacityBytes, int64_t maxObjectSize, const std::string& policyName,
                           cRNG *rng, const DtParams& dtParams) :
    capacityBytes(capacityBytes), maxObjectSize(maxObjectSize), frequencyWindow(dtParams.frequencyWindow)
{
    if (policyName == "LRU")
        policy.reset(new LRUPolicy());
    else if (policyName == "LFU")
        policy.reset(new LFUPolicy());
    else if (policyName == "FIFO")
        policy.reset(new FIFOPolicy());
    else if (policyName == "Random")
        policy.reset(new RandomPolicy());
    else if (policyName == "DTLRU")
        policy.reset(new DecisionTreeAugmentedLRU(dtParams));
    else
        throw cRuntimeError("Unknown cache replacement policy '%s' (expected LRU, LFU, FIFO, Random or DTLRU)", policyName.c_str());
    policy->setStore(&store);
    policy->setRng(rng);
}

const CacheEntry *CacheManager::lookup(const std::string& key, bool honorExpiry)
{
    auto it = store.find(key);
    if (it == store.end())
        return nullptr;
    simtime_t now = simTime();
    CacheEntry& e = it->second;
    if (honorExpiry && now >= e.expiresAt) {
        usedBytes -= e.sizeBytes;
        policy->onEvict(key);
        store.erase(it);
        return nullptr;
    }
    e.lastAccess = now;
    e.accessCount++;
    e.recentAccesses.push_back(now);
    while (!e.recentAccesses.empty() && (now - e.recentAccesses.front()).dbl() > frequencyWindow)
        e.recentAccesses.pop_front();
    policy->onAccess(key);
    return &e;
}

void CacheManager::evictOne()
{
    std::string victim = policy->selectVictim();
    auto it = store.find(victim);
    if (it == store.end())
        throw cRuntimeError("Cache policy %s selected victim '%s' which is not in the cache", policy->getName(), victim.c_str());
    stats.evictions++;
    stats.evictedBytes += it->second.sizeBytes;
    usedBytes -= it->second.sizeBytes;
    policy->onEvict(victim);   // entry still present so the policy can read its features
    store.erase(it);
}

bool CacheManager::insert(const std::string& key, int64_t sizeBytes, bool video, int qualityLevel,
                          const std::string& cacheControl, double maxAgeSeconds)
{
    if (sizeBytes > maxObjectSize || sizeBytes > capacityBytes) {
        stats.rejectedTooLarge++;
        return false;
    }
    auto existing = store.find(key);
    if (existing != store.end()) {   // replace (e.g. refreshed stale copy)
        usedBytes -= existing->second.sizeBytes;
        policy->onEvict(key);
        store.erase(existing);
    }
    while (usedBytes + sizeBytes > capacityBytes && !store.empty())
        evictOne();

    simtime_t now = simTime();
    CacheEntry e;
    e.key = key;
    e.sizeBytes = sizeBytes;
    e.video = video;
    e.qualityLevel = qualityLevel;
    e.cacheControl = cacheControl;
    e.insertedAt = now;
    e.lastAccess = now;
    e.expiresAt = now + maxAgeSeconds;
    e.accessCount = 1;
    e.recentAccesses.push_back(now);
    store.emplace(key, std::move(e));
    usedBytes += sizeBytes;
    stats.insertions++;
    policy->onInsert(key);
    return true;
}

} // namespace cache
} // namespace leoproxy
