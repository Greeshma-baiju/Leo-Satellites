//
// Least Recently Used: doubly linked recency list with
// the list iterator stored in a hash map, giving O(1) access/update/evict.
//
#ifndef LEOPROXY_CACHE_LRUPOLICY_H
#define LEOPROXY_CACHE_LRUPOLICY_H

#include <list>
#include <unordered_map>

#include "cache/CacheReplacementPolicy.h"

namespace leoproxy {
namespace cache {

class LRUPolicy : public CacheReplacementPolicy
{
  protected:
    std::list<std::string> order;    // front = most recently used
    std::unordered_map<std::string, std::list<std::string>::iterator> pos;

  public:
    virtual void onInsert(const std::string& key) override
    {
        order.push_front(key);
        pos[key] = order.begin();
    }
    virtual void onAccess(const std::string& key) override
    {
        auto it = pos.find(key);
        if (it == pos.end())
            return;
        order.splice(order.begin(), order, it->second);
    }
    virtual std::string selectVictim() override
    {
        return order.empty() ? std::string() : order.back();
    }
    virtual void onEvict(const std::string& key) override
    {
        auto it = pos.find(key);
        if (it == pos.end())
            return;
        order.erase(it->second);
        pos.erase(it);
    }
    virtual const char *getName() const override { return "LRU"; }
};

} // namespace cache
} // namespace leoproxy

#endif
