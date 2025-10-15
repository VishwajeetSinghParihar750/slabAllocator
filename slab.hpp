#pragma once
#include <stdlib.h>

class cache_t;

struct free_list
{
    free_list(unsigned int *arr, size_t n) noexcept
    {
        for (unsigned int i = 0; i < n; i++)
        {
            arr[i] = i + 1; // last element may have BUFCTL_END
        }
    }
};

class slab_t
{
    //
    void *mem;
    unsigned int active_obj_cnt;
    unsigned int free; // first free

    slab_t *prev, *next;
    //
    friend class cache_t;

public:
    slab_t() : mem(nullptr), active_obj_cnt(0), free(0), prev(nullptr), next(nullptr) {}

    slab_t *connect_before(slab_t *node) noexcept // return cur node
    {
        if (node == nullptr)
            return this;

        next = node;
        prev = node->prev;

        if (node->prev != nullptr)
            node->prev->next = this;
        node->prev = this;

        return this;
    }
    slab_t *connect_after(slab_t *node) noexcept // return cur node
    {
        assert(node != nullptr);

        prev = node;
        next = node->next;

        if (node->next != nullptr)
            node->next->prev = this;
        node->next = this;

        return this;
    }
    slab_t *disconnect()  noexcept // returns next is list
    {
        if (prev != nullptr)
            prev->next = next;
        if (next != nullptr)
            next->prev = prev;

        prev = nullptr, next = nullptr;
        return next;
    }
};
