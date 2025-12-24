# High-Performance Thread-Safe Slab Allocator

A production-grade, lock-free (fast path) memory allocator written in C++20. This allocator implements a **Hierarchical Slab Architecture** designed to outperform `glibc malloc` in high-concurrency, low-latency environments.

## 🚀 Key Features

- **Thread-Local Caching (Lock-Free):** Each thread maintains its own private `Active`, `Partial`, and `Empty` slab lists. 99% of allocations and deallocations happen without touching a mutex.
- **Hierarchical Design:** Threads "buy" slabs in bulk from a Global Pool and "retail" objects locally.
- **Intrusive Freelist:** Uses the memory of free objects to store `next` pointers, eliminating mathematical overhead (`div` instructions) and allowing **O(1)** remote freeing.
- **Cache Coloring:** Automatically offsets the starting address of slabs to prevent L1/L2 cache set conflicts (False Sharing reduction).
- **Remote Free Support:** Threads can free objects belonging to other threads using a wait-free atomic inbox mechanism.
- **Hoarding Protection:** Automatically returns unused memory to the global pool if a thread is holding too many empty slabs.

## 🏆 Benchmark Results: High Contention

Tested on 40,000,000 operations (Alloc + Free) across 2 threads.
**Object Size:** 64 bytes.

| Metric         | System Malloc (glibc) | Slab Allocator (Yours) | Improvement          |
| :------------- | :-------------------- | :--------------------- | :------------------- |
| **Best Time**  | 1,659 ms              | **1,252 ms**           | **~32% Faster**      |
| **Worst Time** | 6,332 ms              | **1,537 ms**           | **~4.1x Faster**     |
| **Stability**  | High Jitter           | Consistent             | **Extremely Stable** |

### Why It Wins

1.  **Hot-Path Optimization:** The thread-local `pop_local()` function avoids all locking for 99% of operations.
2.  **No Fragmentation Logic:** Unlike general-purpose allocators, the slab design does not need to search for "best fit" blocks or merge adjacent free chunks.
3.  **Cache Locality:** Objects are packed densely in 4KB pages, maximizing L1 CPU cache hits.
