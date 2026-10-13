//
// Cache entry and replacement-policy interface.
//
// The cache stores complete HTTP responses as atomic, object-granularity
// entries keyed on the full request URL. Only metadata is kept: the simulated
// response body is a byte count, so no payload bytes are held in memory.
//
#ifndef LEOPROXY_CACHE_CACHEREPLACEMENTPOLICY_H
#define LEOPROXY_CACHE_CACHEREPLACEMENTPOLICY_H

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>

#include <omnetpp.h>

namespace leoproxy {
namespace cache {

struct CacheEntry
{
    std::string key;                       // full request URL
    int64_t sizeBytes = 0;                 // body length (Content-Length)
    bool video = false;                    // content type feature (web/video)
    int qualityLevel = 0;
    std::string cacheControl;              // response Cache-Control header
    omnetpp::simtime_t insertedAt;         // insertion timestamp
    omnetpp::simtime_t lastAccess;         // last access timestamp
    omnetpp::simtime_t expiresAt;          // insertedAt + max-age
    uint64_t accessCount = 0;              // access frequency count
    std::deque<omnetpp::simtime_t> recentAccesses; // sliding-window accesses (DT+LRU feature)
};

using CacheStore = std::unordered_map<std::string, CacheEntry>;

class CacheReplacementPolicy
{
  protected:
    const CacheStore *store = nullptr;
    omnetpp::cRNG *rng = nullptr;

  public:
    virtual ~CacheReplacementPolicy() = default;
    void setStore(const CacheStore *s) { store = s; }
    void setRng(omnetpp::cRNG *r) { rng = r; }

    virtual void onInsert(const std::string& key) = 0;
    virtual void onAccess(const std::string& key) = 0;
    // Must return the key of an entry currently in the cache (store not empty).
    virtual std::string selectVictim() = 0;
    virtual void onEvict(const std::string& key) = 0;

    // Hooks used by the learned policy (no-ops for the classic ones).
    virtual void noteRequest(const std::string& key) {}
    virtual void periodicMaintenance() {}
    virtual void retrain() {}
    virtual bool collectAccuracy(long& correct, long& total) { return false; }

    virtual const char *getName() const = 0;
};

} // namespace cache
} // namespace leoproxy

#endif
