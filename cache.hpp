#pragma once
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdexcept>
#include "slab.hpp"

namespace
{
    constexpr size_t PAGE_COUNT = 1;
    constexpr size_t PAGE_SIZE = 4096; // BYTES

    template <typename T>
    constexpr T align_up(T n, size_t align = alignof(std::max_align_t))
    {
        return (n + align - 1) & ~(align - 1);
    }
}

class cache_t // for now jsut keep one page per slab
{
    using ctor = void (*)(void *);
    using dtor = void (*)(void *);

    const char *name;

    size_t obj_size, obj_cnt;                         // cnt in one slab
    slab_t *slabs_full, *slabs_partial, *slabs_empty; // these are list heads

    ctor cons;
    dtor dest;

    //
    unsigned int color, color_offset, color_next; // for cache coloring

    // slab movement helpers
    void move_slab_partial_to_full(slab_t *slab)
    {
        assert(slab->active_obj_cnt == obj_cnt);
        //

        auto res = slab->disconnect();
        if (slab == slabs_partial)
            slabs_partial = res;

        if (slabs_full == nullptr)
            slabs_full = slab;
        else
            slab->connect_before(slabs_full);
    }
    void move_slab_partial_to_empty(slab_t *slab)
    {

        assert(slab->active_obj_cnt == 0);

        auto res = slab->disconnect();
        if (slab == slabs_partial)
            slabs_partial = res;

        if (slabs_empty == nullptr)
            slabs_empty = slab;
        else
            slab->connect_before(slabs_empty);
    }
    void move_slab_empty_to_partial(slab_t *slab)
    {

        assert(slab->active_obj_cnt != 0);

        auto res = slab->disconnect();
        if (slab == slabs_empty)
            slabs_empty = res;

        if (slabs_partial == nullptr)
            slabs_partial = slab;
        else
            slab->connect_before(slabs_partial);

        if (slab->active_obj_cnt == obj_cnt)
            move_slab_partial_to_full(slab);
    }
    void move_slab_full_to_partial(slab_t *slab)
    {
        assert(slab->active_obj_cnt != obj_cnt);

        auto res = slab->disconnect();
        if (slab == slabs_full)
            slabs_full = res;

        if (slabs_partial == nullptr)
            slabs_partial = slab;
        else
            slab->connect_before(slabs_partial);

        if (slab->active_obj_cnt == 0)
            move_slab_partial_to_empty(slab);
    }

    // get helpers
    unsigned int *get_freelist_offset(const void *slab) const
    {
        return (unsigned int *)align_up((uintptr_t)slab + sizeof(slab_t));
    }
    void *get_obj_offset(const void *slab)
    {
        auto p = get_freelist_offset(slab);
        return (void *)align_up((uintptr_t)p + sizeof(unsigned int) * obj_cnt);
    }
    void *get_obj_at_index(char *obj_start, size_t index) const
    {
        return obj_start + index * obj_size;
    }
    unsigned int get_and_modify_next_free(slab_t *cur_slab)
    {
        unsigned int nextfree = cur_slab->free;
        unsigned int *freelist_offset = get_freelist_offset(cur_slab);
        cur_slab->free = *(freelist_offset + nextfree);
        return nextfree;
    }
    void *get_partial_obj()
    {
        if (slabs_partial == nullptr)
            return nullptr;

        auto nextfree = get_and_modify_next_free(slabs_partial);

        auto p = slabs_partial;

        if (++slabs_partial->active_obj_cnt == obj_cnt)
            move_slab_partial_to_full(slabs_partial);

        return get_obj_at_index((char *)p->mem, nextfree);
    }
    void *get_free_obj()
    {
        if (slabs_empty == nullptr)
            return nullptr;

        auto p = slabs_empty;
        auto nextfree = get_and_modify_next_free(slabs_empty);

        ++slabs_empty->active_obj_cnt;
        move_slab_empty_to_partial(slabs_empty);

        return get_obj_at_index((char *)p->mem, nextfree);
    }

    void *add_cache_coloring(void *obj_start)
    {
        if (color == 1)
            return obj_start;

        uintptr_t new_obj_start = (uintptr_t)obj_start + color_next * color_offset;
        color_next = (color_next + 1) % color;
        return (void *)new_obj_start;
    }

    void allocate_free_slab()
    {
        //
        void *mem = mmap(nullptr, PAGE_COUNT * PAGE_SIZE,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        assert(mem != MAP_FAILED);

        slab_t *slab_obj = new (mem) slab_t{};

        auto p = get_freelist_offset(slab_obj);
        new (p) free_list(p, obj_cnt); // initialisign free list

        void *obj_mem = get_obj_offset(slab_obj);

        slab_obj->mem = add_cache_coloring(obj_mem);

        if (dest == nullptr && cons != nullptr) // if dest != nullptr, cons/dest are called when giving/getting objects
            for (size_t i = 0; i < obj_cnt; i++)
                cons(get_obj_at_index((char *)slab_obj->mem, i));

        // put slab in empty list
        if (slabs_empty == nullptr)
            slabs_empty = slab_obj;
        else
            slab_obj->connect_after(slabs_empty);
    }

public:
    cache_t(const char *name_, size_t obj_size_, ctor ctr = nullptr, dtor dtr = nullptr) : name(name_), obj_size(align_up(obj_size_)),
                                                                                           slabs_full(nullptr), slabs_partial(nullptr), slabs_empty(nullptr),
                                                                                           cons(ctr), dest(dtr)
    {

        assert(obj_size > 0);

        obj_cnt = (PAGE_SIZE * PAGE_COUNT - sizeof(slab_t)) / (obj_size + sizeof(unsigned int));

        size_t size_left = 0;

        while (true)
        {
            uintptr_t freelist_size = obj_cnt * sizeof(unsigned int);
            uintptr_t obj_start = align_up(align_up(sizeof(slab_t)) + freelist_size);
            uintptr_t total_used = obj_start + obj_cnt * obj_size;

            if (total_used <= PAGE_SIZE * PAGE_COUNT)
            {
                size_left = PAGE_SIZE * PAGE_COUNT - total_used;
                break;
            }
            obj_cnt--;
        }

        assert(obj_cnt > 0);

        // set up coloring

        size_t cache_line_size = 64;
        if (auto p = sysconf(_SC_LEVEL1_DCACHE_LINESIZE); p > 0)
            cache_line_size = p;

        color = (size_left / cache_line_size) + 1;
        color_next = 0;
        color_offset = cache_line_size;
    }

    void *cache_alloc()
    {
        void *toret = nullptr;
        if (toret = get_partial_obj(); toret != nullptr)
            ;
        else if (toret = get_free_obj(); toret != nullptr)
            ;
        else
            allocate_free_slab(), toret = get_free_obj();

        if (dest != nullptr && cons != nullptr) // if no dest if would have been constructed at slab allocaiton itself
            cons(toret);

        return toret;
    }

    void cache_free(void *obj)
    {
        if (dest)
            dest(obj);

        // gettign addr
        uintptr_t obj_add = (uintptr_t)obj;
        slab_t *slab = (slab_t *)(obj_add & ~(PAGE_SIZE * PAGE_COUNT - 1)); // 🚨⚠️ this works for (apge * pagecount) alinged memory only [so rn only 1 page can come coz mmap give one page algined ]

        // resetting free list
        unsigned int obj_ind = (obj_add - (uintptr_t)slab->mem) / obj_size;
        unsigned int next_free = slab->free;

        unsigned int *freelist_offset = get_freelist_offset(slab);
        freelist_offset[obj_ind] = next_free;
        slab->free = obj_ind;

        // moving slab if need
        if (slab->active_obj_cnt == obj_cnt) // means its in full list
            slab->active_obj_cnt--, move_slab_full_to_partial(slab);
        else if (slab->active_obj_cnt - 1 == 0) // means partial to empty
            slab->active_obj_cnt--, move_slab_partial_to_empty(slab);
        else
            slab->active_obj_cnt--;
    }

    //

    cache_t(const cache_t &) = delete;
    auto operator=(const cache_t &) = delete;
    // not keeping move for now too

    ~cache_t()
    {
        for (auto cur_list : {slabs_full, slabs_partial, slabs_empty})
        {
            while (cur_list)
            {
                void *p = cur_list;
                cur_list = cur_list->next;
                munmap(p, PAGE_COUNT * PAGE_SIZE);
            }
        }
    }
};