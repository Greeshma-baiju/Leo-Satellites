//
// Random replacement: the victim is drawn uniformly at
// random from the resident objects. The keys are kept in a dense vector with
// an index map (swap-and-pop removal), so selection and removal are O(1).
// The RNG is the proxy module's OMNeT++ RNG, i.e. it follows the run's seed-set.
//
#ifndef LEOPROXY_CACHE_RANDOMPOLICY_H
#define LEOPROXY_CACHE_RANDOMPOLICY_H

#include <unordered_map>
#include <vector>

#include "cache/CacheReplacementPolicy.h"

namespace leoproxy {
namespace cache {

class RandomPolicy : public CacheReplacementPolicy
{
  protected:
    std::vector<std::string> keys;
    std::unordered_map<std::string, size_t> pos;

  public:
    virtual void onInsert(const std::string& key) override
    {
        pos[key] = keys.size();
        keys.push_back(key);
    }
    virtual void onAccess(const std::string& key) override {}
    virtual std::string selectVictim() override
    {
        if (keys.empty())
            return std::string();
        if (rng == nullptr)
            throw omnetpp::cRuntimeError("RandomPolicy: RNG not set");
        size_t i = (size_t)omnetpp::intuniform(rng, 0, (int)keys.size() - 1);
        return keys[i];
    }
    virtual void onEvict(const std::string& key) override
    {
        auto it = pos.find(key);
        if (it == pos.end())
            return;
        size_t i = it->second;
        size_t last = keys.size() - 1;
        if (i != last) {
            keys[i] = keys[last];
            pos[keys[i]] = i;
        }
        keys.pop_back();
        pos.erase(it);
    }
    virtual const char *getName() const override { return "Random"; }
};

} // namespace cache
} // namespace leoproxy

#endif
