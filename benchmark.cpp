#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <iomanip>
#include <random>
#include "slabAllocator.hpp"

using namespace std::chrono;

// --- Configuration ---
constexpr int NUM_THREADS = 2;
constexpr int ALLOCS_PER_THREAD = 10'000'000; // 10 Million per thread
constexpr size_t OBJ_SIZE = 64;               // 64-byte objects

// --- Malloc Benchmark ---
void benchmark_malloc(std::atomic<long long> &total_us)
{
    // We use a raw array or vector to hold pointers.
    // To minimize vector resizing noise, we reserve.
    std::vector<void *> ptrs;
    ptrs.reserve(ALLOCS_PER_THREAD);

    auto start = high_resolution_clock::now();

    // 1. Burst Allocation
    for (int i = 0; i < ALLOCS_PER_THREAD; ++i)
    {
        ptrs.push_back(std::malloc(OBJ_SIZE));
    }

    // 2. Burst Deallocation (LIFO order helps cache, but we test raw speed)
    for (int i = ALLOCS_PER_THREAD - 1; i >= 0; --i)
    {
        std::free(ptrs[i]);
    }

    auto end = high_resolution_clock::now();
    total_us += duration_cast<microseconds>(end - start).count();
}

// --- Slab Benchmark ---
void benchmark_slab(slabAllocator &allocator, cache_t *cache, std::atomic<long long> &total_us)
{
    std::vector<void *> ptrs;
    ptrs.reserve(ALLOCS_PER_THREAD);

    auto start = high_resolution_clock::now();

    // 1. Burst Allocation
    // This tests the "Active Slab -> Local Partial -> Local Empty -> Global" chain
    for (int i = 0; i < ALLOCS_PER_THREAD; ++i)
    {
        ptrs.push_back(allocator.thread_safe_cache_alloc(cache));
    }

    // 2. Burst Deallocation
    // This tests the "Active Slab -> Local Partial -> Local Empty" transitions
    // and the "Hoarding Control" (return to global if too many empty)
    for (int i = ALLOCS_PER_THREAD - 1; i >= 0; --i)
    {
        allocator.thread_safe_cache_free(cache, ptrs[i]);
    }

    auto end = high_resolution_clock::now();
    total_us += duration_cast<microseconds>(end - start).count();
}

int main()
{
    std::cout << "================================================" << std::endl;
    std::cout << "      High-Performance Allocator Benchmark      " << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << "Threads:       " << NUM_THREADS << std::endl;
    std::cout << "Allocs/Thread: " << ALLOCS_PER_THREAD << std::endl;
    std::cout << "Object Size:   " << OBJ_SIZE << " bytes" << std::endl;
    std::cout << "Total Ops:     " << (long long)NUM_THREADS * ALLOCS_PER_THREAD * 2 << " (Alloc + Free)" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;

    // --- MALLOC TEST ---
    {
        std::cout << "Running MALLOC Benchmark...   " << std::flush;
        std::atomic<long long> total_us{0};
        std::vector<std::thread> threads;

        auto wall_start = high_resolution_clock::now();

        for (int i = 0; i < NUM_THREADS; ++i)
        {
            threads.emplace_back(benchmark_malloc, std::ref(total_us));
        }
        for (auto &t : threads)
            t.join();

        auto wall_end = high_resolution_clock::now();
        auto wall_ms = duration_cast<milliseconds>(wall_end - wall_start).count();
        double avg_lat = (total_us.load() / 1000.0) / NUM_THREADS;

        std::cout << "Done." << std::endl;
        std::cout << std::left << std::setw(20) << "[ MALLOC ]"
                  << "Wall Time: " << std::setw(6) << wall_ms << " ms | "
                  << "Thread Avg: " << std::fixed << std::setprecision(2) << avg_lat << " ms" << std::endl;
    }

    // --- SLAB ALLOCATOR TEST ---
    {
        std::cout << "Running SLAB Benchmark...     " << std::flush;
        slabAllocator allocator;
        // Threads share the cache handle, but utilize their own ThreadContext internally
        cache_t *cache = allocator.cache_create("bench_cache", OBJ_SIZE);

        // Forced Warmup: Ensure any static/global initialization is done
        void *warm = allocator.thread_safe_cache_alloc(cache);
        allocator.thread_safe_cache_free(cache, warm);

        std::atomic<long long> total_us{0};
        std::vector<std::thread> threads;

        auto wall_start = high_resolution_clock::now();

        for (int i = 0; i < NUM_THREADS; ++i)
        {
            threads.emplace_back(benchmark_slab, std::ref(allocator), cache, std::ref(total_us));
        }
        for (auto &t : threads)
            t.join();

        auto wall_end = high_resolution_clock::now();
        auto wall_ms = duration_cast<milliseconds>(wall_end - wall_start).count();
        double avg_lat = (total_us.load() / 1000.0) / NUM_THREADS;

        std::cout << "Done." << std::endl;
        std::cout << std::left << std::setw(20) << "[ SLAB ]"
                  << "Wall Time: " << std::setw(6) << wall_ms << " ms | "
                  << "Thread Avg: " << std::fixed << std::setprecision(2) << avg_lat << " ms" << std::endl;
    }

    std::cout << "================================================" << std::endl;

    return 0;
}