#pragma once
#include <stdlib.h>

class cache_t;

#pragma pack(push, 1)
class slab_t // 32 bytes
{
    //
    slab_t *prev, *next; // 8, 8
    uint8_t *mem;        // 8
    //
    uint8_t active_obj_cnt; // 1
    uint8_t free;           // first free // 1

    struct
    {
        uint8_t perfectly_aligned : 1; // true or false, used for unmap checks // 1
        uint8_t is_mmap_front : 1;     // true or false, this is front of mmap allocation
        uint8_t unused : 6;            // 6 bits
    } flags;



    // FREE LIST IS KEPT JUST HERE [ 0, 1, 2, 3 ... ]
    friend class cache_t;

public:
    slab_t() : prev(this), next(this), mem(nullptr), active_obj_cnt(0), free(0)
    {
        flags = {0, 0, 0};
    }

    slab_t(bool is_aligned, bool is_front) : mem(nullptr), active_obj_cnt(0), free(0), prev(nullptr), next(nullptr)
    {
        flags.is_mmap_front = is_front;
        flags.perfectly_aligned = is_aligned;
        flags.unused = 0;
    }

    inline void unlink() noexcept
    {
        next->prev = prev;
        prev->next = next;
    }

    inline void link_after(slab_t *sentinel) noexcept
    {
        this->next = sentinel->next;
        this->prev = sentinel;
        sentinel->next->prev = this;
        sentinel->next = this;
    }

    inline bool is_empty_list() const
    {
        return next == this;
    }
};
#pragma pack(pop)