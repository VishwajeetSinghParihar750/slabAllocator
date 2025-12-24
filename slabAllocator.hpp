#pragma once
#include <unordered_map>
#include <string>
#include <mutex>
#include "cache.hpp"

class slabAllocator {
    using ctor = void (*)(void *);
    using dtor = void (*)(void *);

    std::unordered_map<std::string, cache_t*> caches;
    std::mutex mtx;

public:
    ~slabAllocator() {
        std::lock_guard<std::mutex> lock(mtx);
        for(auto& p : caches) delete p.second;
    }

    cache_t* cache_create(const std::string& name, size_t size, ctor ctr = nullptr, dtor dtr = nullptr) {
        std::lock_guard<std::mutex> lock(mtx);
        if(caches.count(name)) return caches[name];
        
        cache_t* c = new cache_t(static_cast<uint32_t>(size), ctr, dtr);
        caches[name] = c;
        return c;
    }

    inline void* thread_safe_cache_alloc(cache_t* c) { return c->thread_safe_alloc(); }
    inline void thread_safe_cache_free(cache_t* c, void* obj) { c->thread_safe_free(obj); }
    
    // Compatibility aliases
    inline void* cache_alloc(cache_t* c) { return thread_safe_cache_alloc(c); }
    inline void cache_free(cache_t* c, void* obj) { thread_safe_cache_free(c, obj); }
};