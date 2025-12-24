#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <latch>
#include "slab_allocator.hpp" // Changed to include your provider header

// Configuration
const int NUM_ITEMS = 10'000'000;
const int BATCH_SIZE = 100; 

struct TestObj
{
    char data[64];
};

// Wrapper for Malloc to match interface
void *sys_alloc() { return malloc(sizeof(TestObj)); }
void sys_free(void *p) { free(p); }

// --- Wrapper for Slab (Updated for Singleton Provider) ---

// 1. Allocation: accessing the singleton via alloc_raw
void *slab_alloc_fn() { 
    return slab_provider<TestObj, "CrossThread">::alloc_raw(); 
}

// 2. Freeing: accessing the singleton via free_raw
// Note: We must cast void* back to TestObj* because the provider is typed.
void slab_free_fn(void *p) { 
    slab_provider<TestObj, "CrossThread">::free_raw(static_cast<TestObj*>(p)); 
}

// ---------------------------------------------------------

void run_test(const char *name, void *(*alloc_func)(), void (*free_func)(void *))
{
    std::vector<void *> shared_buffer;
    shared_buffer.reserve(NUM_ITEMS);

    std::cout << "Running " << name << " Benchmark..." << std::endl;

    // STEP 1: PRODUCER (Allocates everything)
    auto start_alloc = std::chrono::high_resolution_clock::now();
    std::thread producer([&]()
                         {
        // Pin to core 0 (optional)
        for (int i = 0; i < NUM_ITEMS; ++i) {
            shared_buffer.push_back(alloc_func());
        } });
    producer.join();
    auto end_alloc = std::chrono::high_resolution_clock::now();

    // STEP 2: CONSUMER (Frees everything on a DIFFERENT thread)
    // This tests the "Remote Free" capability of the allocator
    auto start_free = std::chrono::high_resolution_clock::now();
    std::thread consumer([&]()
                         {
        // Pin to core 1 (optional)
        for (void* ptr : shared_buffer) {
            free_func(ptr);
        } });
    consumer.join();
    auto end_free = std::chrono::high_resolution_clock::now();

    auto alloc_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_alloc - start_alloc).count();
    auto free_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_free - start_free).count();

    std::cout << "[ " << name << " ]" << std::endl;
    std::cout << "  Alloc Time (Local):  " << alloc_ms << " ms" << std::endl;
    std::cout << "  Free Time (Remote):  " << free_ms << " ms" << std::endl;
    std::cout << "  Total Time:          " << (alloc_ms + free_ms) << " ms" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;
}

int main()
{
    std::cout << "================================================" << std::endl;
    std::cout << "      CROSS-THREAD CONTENTION BENCHMARK         " << std::endl;
    std::cout << "================================================" << std::endl;
    std::cout << "Items: " << NUM_ITEMS << std::endl;
    std::cout << "Pattern: Thread A Allocates -> Thread B Frees" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;

    run_test("MALLOC", sys_alloc, sys_free);
    run_test("SLAB  ", slab_alloc_fn, slab_free_fn);

    return 0;
}