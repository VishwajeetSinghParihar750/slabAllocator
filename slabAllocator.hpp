
#pragma once
#include <stdlib.h>
#include <unordered_map>
#include "assert.h"
#include "cache.hpp"

//
using ctor = void (*)(void *);
using dtor = void (*)(void *);

//
class slabAllocator
{
    std::unordered_map<const char *, cache_t> caches;

public:
    void cache_create(const char *name, size_t obj_size, ctor ctr, dtor dtr)
    {
        assert(!caches.contains(name));
        caches.try_emplace(name, name, obj_size, ctr, dtr);
    }
    void cache_destroy(const char *name)
    {
        caches.erase(name);
    }

    void *cache_alloc(const char *name)
    {
        auto it = caches.find(name);
        assert(it->first);
        return it->second.cache_alloc();
    }
    void cache_free(const char *name, void *obj)
    {
        auto it = caches.find(name);
        assert(it->first);

        it->second.cache_free(obj);
    }
};