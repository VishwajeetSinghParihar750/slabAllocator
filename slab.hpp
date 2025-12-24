#pragma once
#include <atomic>
#include <stdint.h>

struct ThreadContext;

class slab_t {
public:
    slab_t *prev, *next;

    // --- NEW: Thread Safety Features ---
    // Remote Inbox: Other threads atomically push freed objects here
    std::atomic<void*> atomic_head;
    
    // Local Freelist: Owner thread uses this (replaces 'uint8_t free' index)
    void* local_head;
    
    // Identity: Who owns this slab?
    ThreadContext* owner;

    // --- Original Metadata ---
    void* mem;
    std::atomic<uint32_t> active_obj_cnt; // Atomic for remote decrements

    struct {
        uint8_t perfectly_aligned : 1;
        uint8_t is_mmap_front : 1;
        uint8_t unused : 6;
    } flags;

    slab_t() : prev(this), next(this), atomic_head(nullptr), 
               local_head(nullptr), owner(nullptr), mem(nullptr), active_obj_cnt(0) {
        flags = {0, 0, 0};
    }

    // Helper for sentinel initialization
    slab_t(bool is_aligned, bool is_front) : slab_t() {
        flags.perfectly_aligned = is_aligned;
        flags.is_mmap_front = is_front;
    }

    inline void unlink() noexcept {
        next->prev = prev;
        prev->next = next;
        next = prev = this;
    }

    inline void link_after(slab_t *sentinel) noexcept {
        this->next = sentinel->next;
        this->prev = sentinel;
        sentinel->next->prev = this;
        sentinel->next = this;
    }

    inline bool is_empty_list() const { return next == this; }
};