# slabAllocator

A userspace slab allocator in C++20, modelled on the one in the Linux kernel.

You create a cache for one fixed object size, then allocate and free objects from it. The cache gets its memory from `mmap` up front and carves it into slabs, so the hot path never calls into `malloc`. It's meant for code that churns through millions of same-shaped objects per second — packet buffers, order nodes, particles — where the general-purpose allocator spends most of its time on bookkeeping you don't need.

Header-only. Copy `slabAllocator.hpp`, `cache.hpp` and `slab.hpp` into your project and include the first one. Linux only (it uses `mmap`/`munmap` directly).

## Usage

```cpp
#include "slabAllocator.hpp"

slabAllocator allocator;

// name, object size, optional ctor, optional dtor
cache_t* packets = allocator.cache_create("packets", 1024, nullptr, nullptr);

void* p = allocator.cache_alloc(packets);
allocator.cache_free(packets, p);

// all mappings are released when the allocator goes out of scope
```

`cache_create` returns `nullptr` if the name is already taken. The alloc/free calls do no validation: passing a pointer that didn't come from that cache is undefined behaviour, by design — the checks would cost more than the allocation.

## How it works

**Caches.** Requested object sizes are clamped to 16 bytes and rounded up to the next power of two. The cache then picks a slab size (also a power of two, at least 4 KB) large enough to hold at least 8 objects plus metadata, and works out how many objects actually fit once metadata and cache-line alignment are accounted for.

Each cache keeps three circular doubly-linked lists of slabs — full, partial and empty — each with a sentinel node. Sentinels mean `link_after` and `unlink` are four pointer writes with no null checks and no branches, which matters because every allocation that fills a slab and every free that empties one moves a node between lists.

**Slab layout.** A slab is one aligned block:

```
[ 27-byte header ][ freelist bytes ][ padding to cache line ][ objects... ]
```

The header is `#pragma pack(1)`, so it's exactly 27 bytes: two list pointers, a pointer to the object area, an active-object count, the head of the freelist, and a bitfield of two flags. The freelist itself is a plain array of one-byte indices immediately after it, so the header and the front of the freelist share a cache line and a small slab's metadata is a single line total.

Storing indices instead of pointers is what keeps the freelist a byte per object, and it's also the reason a slab can hold at most 255 objects.

**Allocation** takes the first slab off the partial list, or off the empty list, or maps 64 more slabs' worth of pages if both are empty. Then it pops the freelist head, bumps the count, and moves the slab to the full list if that was the last object.

**Freeing** finds the slab by masking the object pointer down to the slab boundary — slabs are aligned to their own size, so the header is just `ptr & ~(slab_size - 1)`. No hash map, no header per object.

**Cache colouring.** Whatever space is left over in a slab after metadata and objects is distributed across slabs in cache-line steps, so objects at the same index in different slabs don't all land in the same cache set.

**Unmapping.** `mmap` hands back 64 slabs at a time, and only the first slab in that block is flagged as the head; the destructor walks the three lists and unmaps once per block. If the mapping didn't start out aligned to the slab size, the original `mmap` pointer is written into the 8 bytes just before the aligned start, so the destructor can recover it without a side table.

## Benchmarks

Under `benchmarks/`. Each file is standalone:

```bash
cd benchmarks
g++ -std=c++20 -O3 benchmark3.cpp -o bench && ./bench
```

- `benchmark.cpp` and `benchmark2.cpp` compare against glibc `malloc` across object sizes and mixed workloads.
- `benchmark3.cpp` simulates an RUDP receiver: 10M 1 KB packets through a 64k sliding window.

All numbers are single-threaded, measured on an Intel i5 9th Gen (8 core), Ubuntu 22.04 under WSL2, GCC 13.3.0 at `-O3`, against glibc `malloc`.

| Workload      | Object size | Slab    | glibc malloc | Speedup | Throughput   |
| ------------- | ----------- | ------- | ------------ | ------- | ------------ |
| Mixed         | 64B + 512B  | 85.0 ms | 462.4 ms     | 5.44x   | 4.7M ops/sec |
| Large objects | 1 KB        | 11.6 ms | 37.8 ms      | 3.26x   | 8.6M ops/sec |
| Medium        | 256 B       | 52.1 ms | 110.9 ms     | 2.13x   | 9.6M ops/sec |
| Small         | 32 B        | 25.0 ms | 56.6 ms      | 2.26x   | 40M ops/sec  |

Those run through a `std::vector` of pointers, so they include the container's overhead. In a raw loop with no container in the way, the allocator is limited by L1/L2 rather than by anything in its own code:

- allocation: ~28M ops/sec
- deallocation: ~108M ops/sec
- combined: ~42M ops/sec

Deallocation is the fast side because it's a mask, a store and a decrement, with no list traversal in the common case.

The benchmark harnesses were written with Gemini.

## Limitations

- **Not thread safe.** One cache, one thread. There's a `thread-safe` branch with per-thread caches, still in progress.
- **Sizes round up to a power of two.** A 130-byte object costs 256 bytes. If your sizes sit just above a boundary, this allocator is the wrong tool.
- **255 objects per slab, maximum**, because the freelist is one byte per object.
- **Memory is only returned at destruction.** Slabs that go fully empty stay on the empty list and are reused; nothing is unmapped until the cache is destroyed. That's deliberate for steady-state workloads, but it means peak usage is the resident usage.
- **No validation anywhere.** Freeing a foreign pointer, double-freeing, or destroying a cache name that doesn't exist is undefined behaviour.

## Possible next steps

- Finish the per-thread cache work on the `thread-safe` branch.
- 2 MB huge pages, to cut TLB misses on large working sets.
- NUMA-aware slab placement.
