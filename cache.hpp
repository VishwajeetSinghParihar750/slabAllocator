#pragma once
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdexcept>
#include "slab.hpp"

size_t PAGE_COUNT = 1;
const size_t PAGE_SIZE = 4096; // BYTES

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

    unsigned int get_and_modify_next_free(slab_t *cur_slab)
    {
        unsigned int nextfree = cur_slab->free;
        unsigned int *freelist_offset = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(cur_slab) + sizeof(slab_t));
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

        return (char *)p->mem + obj_size * nextfree;
    }
    void *get_free_obj()
    {
        if (slabs_empty == nullptr)
            return nullptr;

        auto p = slabs_empty;
        auto nextfree = get_and_modify_next_free(slabs_empty);

        ++slabs_empty->active_obj_cnt;
        move_slab_empty_to_partial(slabs_empty);

        return (char *)p->mem + obj_size * nextfree;
    }

    void allocate_free_slab()
    {
        //
        void *mem = mmap(nullptr, PAGE_COUNT * PAGE_SIZE,
                         PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED)
            throw std::runtime_error("mmap failed");

        slab_t *slab_obj = new (mem) slab_t{};

        char *base = reinterpret_cast<char *>(mem);
        unsigned int *freelist_arr = reinterpret_cast<unsigned int *>(base + sizeof(slab_t));
        char *obj_mem = base + sizeof(slab_t) + sizeof(unsigned int) * obj_cnt;

        slab_obj->mem = obj_mem;
        slab_obj->free = 0;
        slab_obj->active_obj_cnt = 0;

        for (unsigned int i = 0; i < obj_cnt; i++)
        {
            freelist_arr[i] = i + 1; // last element may have BUFCTL_END
        }

        if (dest == nullptr && cons != nullptr) // if dest != nullptr, cons/dest are called when giving/getting objects
            for (size_t i = 0; i < obj_cnt; i++)
                cons(obj_mem + i * obj_size);

        // put slab in empty list
        if (slabs_empty == nullptr)
            slabs_empty = slab_obj;
        else
            slab_obj->connect_after(slabs_empty);
    }

public:
    cache_t(const char *name_, size_t obj_size_, ctor ctr = nullptr, dtor dtr = nullptr) : name(name_), obj_size(obj_size_),
                                                                                           slabs_full(nullptr), slabs_partial(nullptr), slabs_empty(nullptr),
                                                                                           cons(ctr), dest(dtr)
    {
        size_t slab_metadata = sizeof(slab_t);

        obj_cnt = (PAGE_COUNT * PAGE_SIZE - slab_metadata) / (obj_size + sizeof(unsigned int)); // size for free list array too
    }

    void *cache_alloc()
    {
        void *toret = get_partial_obj();
        if (toret != nullptr)
            return toret;

        toret = get_free_obj();
        if (toret != nullptr)
            return toret;

        allocate_free_slab();

        auto p = get_free_obj();

        if (dest && cons)
            cons(p);

        return p;
    }

    void cache_free(void *obj)
    {
        if (dest)
            dest(obj);

        // gettign addr
        uintptr_t obj_add = reinterpret_cast<uintptr_t>(obj);
        slab_t *slab = reinterpret_cast<slab_t *>(obj_add & ~(PAGE_SIZE * PAGE_COUNT - 1)); // 🚨⚠️ this works for (apge * pagecount) alinged memory only [so rn only 1 page can come coz mmap give one page algined ]

        // resetting free list
        unsigned int obj_ind = (obj_add - reinterpret_cast<uintptr_t>(slab->mem)) / obj_size;
        unsigned int next_free = slab->free;

        unsigned int *freelist_offset = reinterpret_cast<unsigned int *>(
            reinterpret_cast<char *>(slab) + sizeof(slab_t));

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
            while (cur_list)
            {
                void *p = cur_list;
                cur_list = cur_list->next;
                munmap(p, PAGE_COUNT * PAGE_SIZE);
            }
    }
};