
#pragma once
#include <stdlib.h>
#include <unordered_map>
#include "assert.h"
#include <string>
#include <mutex>

#include "cache.hpp"

class slabAllocator
{
    using ctor = void (*)(void *);
    using dtor = void (*)(void *);

    std::unordered_map<std::string, cache_t *> caches;

    std::mutex mtx;

public:
    ~slabAllocator()
    {
        for (auto &pair : caches)
            delete pair.second;
    }

    cache_t *cache_create(const std::string &name, size_t obj_size, ctor ctr, dtor dtr)
    {
        // assert(!caches.contains(name)); // apps responsiblity otherwise UB

        cache_t *cur_cache = new cache_t(obj_size, ctr, dtr);

        std::lock_guard lock(mtx);

        auto ret = caches.try_emplace(name, cur_cache);
        if (ret.second)
            return cur_cache;

        delete cur_cache;
        return nullptr;
    }

    void cache_destroy(const std::string &name)
    {
        std::lock_guard lock(mtx);
        auto it = caches.find(name);

        // assert(it != caches.end()); // apps responsibilty otherwise UB

        delete it->second;
        caches.erase(it);
    }

    void *cache_alloc(cache_t *cur_cache) // callers resonsiblity to send only the thing that was returned by cache_create, otherwise UB
    {
        return cur_cache->cache_alloc();
    }

    void cache_free(cache_t *cur_cache, void *obj) // callers resonsiblity to send only the thing that was returned by cache_alloc, otherwise UB
    {
        cur_cache->cache_free(obj);
    }
    void *thread_safe_cache_alloc(cache_t *cur_cache) // callers resonsiblity to send only the thing that was returned by cache_create, otherwise UB
    {
        return cur_cache->thread_safe_alloc();
    }

    void thread_safe_cache_free(cache_t *cur_cache, void *obj) // callers resonsiblity to send only the thing that was returned by cache_alloc, otherwise UB
    {
        cur_cache->thread_safe_free(obj);
    }
};