#pragma once
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cassert>
#include <new>
#include "slab.hpp"

namespace {
    static constexpr uint32_t MIN_OBJECTS_PER_SLAB = 8;
    static constexpr uint32_t MIN_OBJECT_SIZE = 16;
    static constexpr uint32_t DEFAULT_PAGE_ALLOCATION_COUNT = 64;
    static const uint32_t CACHE_LINE_SIZE = sysconf(_SC_LEVEL1_DCACHE_LINESIZE) > 0 ? sysconf(_SC_LEVEL1_DCACHE_LINESIZE) : 64;
    
    static constexpr uint32_t MAX_LOCAL_EMPTY_SLABS = 16; 

    template <typename T>
    constexpr T align_up(T n, size_t align = alignof(std::max_align_t)) noexcept {
        return (n + align - 1) & ~(align - 1);
    }
}

struct ThreadContext {
    slab_t list_partial;
    slab_t list_full;
    slab_t list_empty;
    slab_t* active_slab = nullptr;
    uint32_t empty_slab_count = 0;
};

class cache_t {
    using ctor = void (*)(void *);
    using dtor = void (*)(void *);

    uint32_t obj_size, obj_cnt;
    uint64_t PAGE_SIZE;

    // for cache coloring
    uint32_t color, color_offset;
    std::atomic<uint32_t> color_next; 

    slab_t global_empty; 
    std::mutex global_mtx;
    
    std::vector<std::pair<void*, size_t>> mapped_pages;

    ctor cons;
    dtor dest;

    static inline thread_local ThreadContext* my_ctx = nullptr;

public:
    cache_t(uint32_t obj_size_, ctor ctr = nullptr, dtor dtr = nullptr) 
        : obj_size(obj_size_), cons(ctr), dest(dtr) 
    {
        assert(obj_size > 0);

        obj_size = std::max(obj_size, MIN_OBJECT_SIZE);
        if (obj_size < sizeof(void*)) obj_size = sizeof(void*);
        obj_size = (1 << (32 - __builtin_clz(obj_size - 1)));

        uint32_t metadata_min_requirement = CACHE_LINE_SIZE;
        uint32_t required = obj_size * MIN_OBJECTS_PER_SLAB + metadata_min_requirement;

        PAGE_SIZE = std::max(4096u, required);
        PAGE_SIZE = (1 << (32 - __builtin_clz(PAGE_SIZE - 1)));

        obj_cnt = PAGE_SIZE / obj_size;

        size_t size_left = 0;
        while (true) {
            uint32_t metadata_req = (sizeof(slab_t) + CACHE_LINE_SIZE - 1) & (~(CACHE_LINE_SIZE - 1));
            uint32_t total_used = metadata_req + obj_cnt * obj_size;

            if (total_used <= PAGE_SIZE) {
                size_left = PAGE_SIZE - total_used;
                break;
            }
            obj_cnt--;
        }

        // set up coloring
        color = (size_left / CACHE_LINE_SIZE) + 1;
        color_next = 0;
        color_offset = CACHE_LINE_SIZE;
    }

    ~cache_t() {
        std::lock_guard<std::mutex> lock(global_mtx);
        
        for (auto& [ptr, size] : mapped_pages) {
            munmap(ptr, size);
        }
        mapped_pages.clear();
    }

    void* thread_safe_alloc() {
        if (__builtin_expect(my_ctx == nullptr, 0)) init_thread();

        if (my_ctx->active_slab) {
            if (my_ctx->active_slab->local_head) return pop_local();
            if (reclaim_remote(my_ctx->active_slab)) return pop_local();
            
            my_ctx->active_slab->unlink();
            my_ctx->active_slab->link_after(&my_ctx->list_full);
            my_ctx->active_slab = nullptr;
        }

        if (!my_ctx->list_partial.is_empty_list()) {
            my_ctx->active_slab = my_ctx->list_partial.next;
            my_ctx->active_slab->unlink();
            
            if (!my_ctx->active_slab->local_head) {
                 reclaim_remote(my_ctx->active_slab);
            }
            return pop_local();
        }

        if (!my_ctx->list_empty.is_empty_list()) {
            my_ctx->active_slab = my_ctx->list_empty.next;
            my_ctx->active_slab->unlink();
            my_ctx->empty_slab_count--;
            return pop_local();
        }

        return fetch_global_slab();
    }

    void thread_safe_free(void* obj) {
        if (__builtin_expect(my_ctx == nullptr, 0)) init_thread();

        uintptr_t addr = (uintptr_t)obj;
        slab_t* slab = (slab_t*)(addr & ~(PAGE_SIZE - 1));

        if (__builtin_expect(slab->owner == my_ctx, 1)) {
            *((void**)obj) = slab->local_head;
            slab->local_head = obj;
            
            uint32_t active = slab->active_obj_cnt.fetch_sub(1, std::memory_order_relaxed);
            
            if (dest) dest(obj);

            if (slab == my_ctx->active_slab) return;

            if (active == obj_cnt) { 
                slab->unlink();
                slab->link_after(&my_ctx->list_partial);
            }
            else if (active == 1) { 
                slab->unlink();
                slab->link_after(&my_ctx->list_empty);
                my_ctx->empty_slab_count++;
                
                if (my_ctx->empty_slab_count > MAX_LOCAL_EMPTY_SLABS) {
                    return_slabs_to_global();
                }
            }
            return;
        }

        void* old_head = slab->atomic_head.load(std::memory_order_relaxed);
        do {
            *((void**)obj) = old_head;
        } while (!slab->atomic_head.compare_exchange_weak(old_head, obj, 
                 std::memory_order_release, std::memory_order_relaxed));
        
        slab->active_obj_cnt.fetch_sub(1, std::memory_order_acq_rel);
    }

private:
    void init_thread() {
        my_ctx = new ThreadContext();
    }

    inline void* pop_local() {
        void* obj = my_ctx->active_slab->local_head;
        my_ctx->active_slab->local_head = *((void**)obj);
        my_ctx->active_slab->active_obj_cnt.fetch_add(1, std::memory_order_relaxed);
        if (cons) cons(obj); 
        return obj;
    }

    bool reclaim_remote(slab_t* s) {
        void* remote = s->atomic_head.exchange(nullptr, std::memory_order_acquire);
        if (!remote) return false;
        
        if (s->local_head == nullptr) {
            s->local_head = remote;
        } else {
            void* cur = remote;
            while (*((void**)cur) != nullptr) {
                cur = *((void**)cur);
            }
            *((void**)cur) = s->local_head;
            s->local_head = remote;
        }
        return true;
    }

    void* fetch_global_slab() {
        std::lock_guard<std::mutex> lock(global_mtx);
        
        if (global_empty.is_empty_list()) {
            allocate_free_slab(); 
            if (global_empty.is_empty_list()) std::abort(); 
        }

        slab_t* fresh = global_empty.next;
        fresh->unlink();
        
        fresh->owner = my_ctx; 
        my_ctx->active_slab = fresh;
        
        return pop_local();
    }

    void return_slabs_to_global() {
        std::lock_guard<std::mutex> lock(global_mtx);
        int to_move = my_ctx->empty_slab_count / 2;
        
        while (to_move > 0 && !my_ctx->list_empty.is_empty_list()) {
            slab_t* s = my_ctx->list_empty.next;
            s->unlink();
            s->owner = nullptr; 
            s->link_after(&global_empty);
            my_ctx->empty_slab_count--;
            to_move--;
        }
    }

    void initialize_slab(void *mem, bool is_aligned, bool is_front) {
        slab_t *slab_obj = new (mem) slab_t(is_aligned, is_front);
        uint8_t *memptr = reinterpret_cast<uint8_t *>(mem);

        uint32_t current_color_idx = color_next.fetch_add(1, std::memory_order_relaxed) % color;
        
        uintptr_t metadata_end = (uintptr_t)memptr + sizeof(slab_t);
        metadata_end = align_up(metadata_end, CACHE_LINE_SIZE);

        slab_obj->mem = (void*)(metadata_end + (current_color_idx * color_offset));

        uint8_t *start = (uint8_t *)slab_obj->mem;
        for (uint32_t i = 0; i < obj_cnt - 1; i++) {
            void* curr = start + (i * obj_size);
            void* next = start + ((i + 1) * obj_size);
            *((void**)curr) = next;
        }
        *((void**)(start + (obj_cnt - 1) * obj_size)) = nullptr;
        
        slab_obj->local_head = start;
        slab_obj->link_after(&global_empty);
    }

    void allocate_free_slab() {
        void *mem;
        bool got_aligned = true;
        size_t alloc_size = PAGE_SIZE * DEFAULT_PAGE_ALLOCATION_COUNT;

        void *pos_mem = mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (pos_mem == MAP_FAILED) return;

        mapped_pages.emplace_back(pos_mem, alloc_size);

        uintptr_t mem_location = reinterpret_cast<uintptr_t>(pos_mem);
        uintptr_t og_mem_location = mem_location;
        
        if (mem_location % PAGE_SIZE) {
            got_aligned = false;
            mem_location = (mem_location + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1));
            uintptr_t *metadata_space = (uintptr_t *)mem_location;
            metadata_space[-1] = og_mem_location;
        }

        mem = reinterpret_cast<void *>(mem_location);

        for (int i = 0; i < DEFAULT_PAGE_ALLOCATION_COUNT - (!got_aligned); i++) {
            initialize_slab((uint8_t *)mem + PAGE_SIZE * i, got_aligned, i == 0);
        }
    }
};