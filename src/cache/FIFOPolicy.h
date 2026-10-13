//
// First-In-First-Out: eviction in insertion
// order, independent of accesses. Implemented as an insertion-ordered queue
// with an index for O(1) removal (functionally identical to a circular
// buffer, but without a fixed capacity).
//
#ifndef LEOPROXY_CACHE_FIFOPOLICY_H
#define LEOPROXY_CACHE_FIFOPOLICY_H

#include <list>
#include <unordered_map>

#include "cache/CacheReplacementPolicy.h"

namespace leoproxy {
namespace cache {

class FIFOPolicy : public CacheReplacementPolicy
{
  protected:
    std::list<std::string> queue;    // front = oldest
    std::unordered_map<std::string, std::list<std::string>::iterator> pos;

  public:
    virtual void onInsert(const std::string& key) override
    {
        queue.push_back(key);
        pos[key] = std::prev(queue.end());
    }
    virtual void onAccess(const std::string& key) override {}
    virtual std::string selectVictim() override
    {
        return queue.empty() ? std::string() : queue.front();
    }
    virtual void onEvict(const std::string& key) override
    {
        auto it = pos.find(key);
        if (it == pos.end())
            return;
        queue.erase(it->second);
        pos.erase(it);
    }
    virtual const char *getName() const override { return "FIFO"; }
};

} // namespace cache
} // namespace leoproxy

#endif
