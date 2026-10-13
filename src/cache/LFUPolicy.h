//
// Least Frequently Used: objects are grouped in
// frequency buckets; an access moves the object to the next-higher bucket and
// eviction removes an element from the minimum-frequency bucket (ties broken
// by least-recent use within the bucket).
//
#ifndef LEOPROXY_CACHE_LFUPOLICY_H
#define LEOPROXY_CACHE_LFUPOLICY_H

#include <list>
#include <map>
#include <unordered_map>
#include <utility>

#include "cache/CacheReplacementPolicy.h"

namespace leoproxy {
namespace cache {

class LFUPolicy : public CacheReplacementPolicy
{
  protected:
    std::map<uint64_t, std::list<std::string>> buckets;   // frequency -> keys (front = most recent)
    std::unordered_map<std::string, std::pair<uint64_t, std::list<std::string>::iterator>> index;

    void removeFromBucket(uint64_t freq, std::list<std::string>::iterator it)
    {
        auto b = buckets.find(freq);
        b->second.erase(it);
        if (b->second.empty())
            buckets.erase(b);
    }

  public:
    virtual void onInsert(const std::string& key) override
    {
        auto& lst = buckets[1];
        lst.push_front(key);
        index[key] = { 1, lst.begin() };
    }
    virtual void onAccess(const std::string& key) override
    {
        auto it = index.find(key);
        if (it == index.end())
            return;
        uint64_t freq = it->second.first;
        removeFromBucket(freq, it->second.second);
        auto& lst = buckets[freq + 1];
        lst.push_front(key);
        it->second = { freq + 1, lst.begin() };
    }
    virtual std::string selectVictim() override
    {
        if (buckets.empty())
            return std::string();
        return buckets.begin()->second.back();
    }
    virtual void onEvict(const std::string& key) override
    {
        auto it = index.find(key);
        if (it == index.end())
            return;
        removeFromBucket(it->second.first, it->second.second);
        index.erase(it);
    }
    virtual const char *getName() const override { return "LFU"; }
};

} // namespace cache
} // namespace leoproxy

#endif
