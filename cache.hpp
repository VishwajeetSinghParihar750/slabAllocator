#pragma once
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdexcept>
#include <vector>

#include "slab.hpp"

namespace
{
    // constexpr uint32_t PAGE_COUNT = 1; this will be one only
    static constexpr uint32_t MIN_OBJECTS_PER_SLAB = 8;
    static constexpr uint32_t MIN_OBJECT_SIZE = 16;

    static constexpr uint32_t DEFAULT_PAGE_ALLOCATION_COUNT = 64;
    static const uint32_t CACHE_LINE_SIZE = sysconf(_SC_LEVEL1_DCACHE_LINESIZE) > 0 ? sysconf(_SC_LEVEL1_DCACHE_LINESIZE) : 64;

    template <typename T>
    constexpr T align_up(T n, size_t align = alignof(std::max_align_t)) noexcept
    {
        return (n + align - 1) & ~(align - 1);
    }
}

class cache_t // for now jsut keep one page per slab
{
    using ctor = void (*)(void *);
    using dtor = void (*)(void *);

    uint32_t obj_size, obj_cnt; // cnt in one slab

    slab_t list_full;
    slab_t list_partial;
    slab_t list_empty; // using sentinels now as dummy

    ctor cons;
    dtor dest;

    uint64_t PAGE_SIZE = 4096; // BYTES
    //
    uint32_t color, color_offset, color_next; // for cache coloring

    // slab movement helpers
    void move_to_partial(slab_t *slab)
    {
        slab->unlink();
        slab->link_after(&list_partial);
    }

    void move_to_full(slab_t *slab)
    {
        slab->unlink();
        slab->link_after(&list_full);
    }

    void move_to_empty(slab_t *slab)
    {
        slab->unlink();
        slab->link_after(&list_empty);
    }
    // get helpers

    uint8_t *add_cache_coloring(uint8_t *obj_start) noexcept
    {
        if (color < 2)
            return obj_start;

        uintptr_t new_obj_start = (uintptr_t)obj_start + color_next * color_offset;
        color_next = (color_next + 1) % color;
        return (uint8_t *)new_obj_start;
    }

    void initiliaze_slab(void *mem, bool is_aligned, bool is_front)
    {
        slab_t *slab_obj = new (mem) slab_t(is_aligned, is_front);

        uint8_t *memptr = reinterpret_cast<uint8_t *>(mem);
        uint8_t *freelist_start = memptr + sizeof(slab_t);

        for (uint8_t i = 0; i < obj_cnt; i++)
        {
            freelist_start[i] = i + 1;
        }

        uint8_t obj_offset = align_up(sizeof(slab_t) + obj_cnt, CACHE_LINE_SIZE);
        slab_obj->mem = add_cache_coloring(memptr + obj_offset);

        if (dest == nullptr && cons != nullptr)
            for (size_t i = 0; i < obj_cnt; i++)
                cons(slab_obj->mem + i * obj_size);

        slab_obj->link_after(&list_empty);
    }

    void allocate_free_slab()
    {
        //
        void *mem;
        bool got_aligned = true;

        void *pos_mem = mmap(nullptr, PAGE_SIZE * DEFAULT_PAGE_ALLOCATION_COUNT, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(pos_mem != MAP_FAILED);

        uintptr_t mem_location = reinterpret_cast<uintptr_t>(pos_mem);
        uintptr_t og_mem_location = mem_location;
        if (mem_location % PAGE_SIZE)
        {
            got_aligned = false;
            mem_location = (mem_location + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1));

            uintptr_t *metadata_space = (uintptr_t *)mem_location;
            metadata_space[-1] = og_mem_location;
        }

        mem = reinterpret_cast<void *>(mem_location);

        for (int i = 0; i < DEFAULT_PAGE_ALLOCATION_COUNT - (!got_aligned); i++)
            initiliaze_slab((uint8_t *)mem + PAGE_SIZE * i, got_aligned, i == 0);
    }

public:
    cache_t(uint32_t obj_size_, ctor ctr = nullptr, dtor dtr = nullptr) : obj_size(obj_size_),
                                                                          cons(ctr), dest(dtr)
    {

        assert(obj_size > 0);

        obj_size = std::max(obj_size, MIN_OBJECT_SIZE);
        obj_size = (1 << (32 - __builtin_clz(obj_size - 1)));

        uint32_t metadata_min_requirement = CACHE_LINE_SIZE;
        uint32_t required = obj_size * MIN_OBJECTS_PER_SLAB + metadata_min_requirement;

        PAGE_SIZE = std::max(4096u, required);
        PAGE_SIZE = (1 << (32 - __builtin_clz(PAGE_SIZE - 1)));

        obj_cnt = PAGE_SIZE / obj_size; // assuming

        size_t size_left = 0;

        while (true)
        {
            uint32_t metadata_req = (sizeof(slab_t) + obj_cnt + CACHE_LINE_SIZE - 1) & (~(CACHE_LINE_SIZE - 1)); // byte aligned for cache line

            uint32_t total_used = metadata_req + obj_cnt * obj_size;

            if (total_used <= PAGE_SIZE)
            {
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

    void *cache_alloc()
    {
        slab_t *slab = list_partial.next;

        if (__builtin_expect(slab == &list_partial, 0))
        {
            slab = list_empty.next;
            if (slab == &list_empty)
            {
                allocate_free_slab();
                slab = list_empty.next;
            }
            move_to_partial(slab);
        }

        uint8_t next_idx = slab->free;
        uint8_t *freelist = (uint8_t *)slab + sizeof(slab_t); // size of slab_t

        slab->free = freelist[next_idx];
        slab->active_obj_cnt++;

        if (slab->active_obj_cnt == obj_cnt)
            move_to_full(slab);

        void *obj = (uint8_t *)slab->mem + (next_idx * obj_size);

        // __builtin_prefetch(obj, 1, 3);

        if (cons)
            cons(obj);
        return obj;
    }

    void cache_free(void *obj)
    {

        if (dest)
            dest(obj);

        uintptr_t obj_addr = (uintptr_t)obj;
        slab_t *slab = (slab_t *)(obj_addr & ~(PAGE_SIZE - 1));

        uint8_t obj_ind = (obj_addr - (uintptr_t)slab->mem) / obj_size;

        // Push to freelist
        uint8_t *freelist = (uint8_t *)slab + sizeof(slab_t); // size of slab_t
        freelist[obj_ind] = slab->free;
        slab->free = obj_ind;

        slab->active_obj_cnt--;

        if (slab->active_obj_cnt == obj_cnt - 1)
        {
            move_to_partial(slab);
        }
        else if (slab->active_obj_cnt == 0)
        {
            move_to_empty(slab);
        }
    }

    //

    cache_t(const cache_t &) = delete;
    auto operator=(const cache_t &) = delete;
    // not keeping move for now too

    ~cache_t()
    {
        std::vector<std::pair<void *, uint32_t>> to_free;

        auto collect_pages = [&](slab_t &sentinel)
        {
            slab_t *cur = sentinel.next;
            while (cur != &sentinel)
            {
                if (cur->flags.is_mmap_front)
                {
                    if (cur->flags.perfectly_aligned)
                        to_free.emplace_back(cur, PAGE_SIZE * DEFAULT_PAGE_ALLOCATION_COUNT);
                    else
                    {
                        uintptr_t *metadata_space = (uintptr_t *)cur;
                        void *raw_ptr = (void *)metadata_space[-1];

                        to_free.emplace_back(raw_ptr, PAGE_SIZE * DEFAULT_PAGE_ALLOCATION_COUNT);
                    }
                }
                cur = cur->next;
            }
        };

        collect_pages(list_full);
        collect_pages(list_partial);
        collect_pages(list_empty);

        for (auto [i, j] : to_free)
            munmap(i, j);
    }
};