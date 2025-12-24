#include <iostream>
#include <vector>
#include <unistd.h>
#include <fstream>
#include <string>
#include "slab_allocator.hpp" // Changed to include your provider header

// Helper to read memory from /proc/self/statm
// Returns (Virtual Memory in MB, Resident Set Size in MB)
std::pair<long, long> get_memory_usage() {
    long rss = 0, virt = 0;
    std::ifstream statm("/proc/self/statm");
    if (statm >> virt >> rss) {
        long page_size = sysconf(_SC_PAGE_SIZE);
        return {
            (virt * page_size) / (1024 * 1024), // VIRT in MB
            (rss * page_size) / (1024 * 1024)   // RSS in MB
        };
    }
    return {0, 0};
}

const int NUM_OBJECTS = 10'000'000;
struct Data { char bytes[64]; };

// Choose your fighter:
// 1 = SLAB
// 0 = MALLOC
#define USE_SLAB 1 

// NOTE: Global instance removed. We now access the singleton via slab_provider.

int main() {
    std::vector<void*> ptrs;
    ptrs.reserve(NUM_OBJECTS);

    std::cout << "========================================" << std::endl;
    std::cout << "        MEMORY FOOTPRINT TEST           " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Allocating " << NUM_OBJECTS << " objects of 64 bytes." << std::endl;
    
    long expected_payload_mb = (long)NUM_OBJECTS * 64 / (1024 * 1024);
    std::cout << "Expected Payload Data: " << expected_payload_mb << " MB" << std::endl;

    auto start_mem = get_memory_usage();
    std::cout << "Baseline Memory:       " << start_mem.second << " MB" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    // --- ALLOCATION PHASE ---
    for (int i = 0; i < NUM_OBJECTS; i++) {
        #if USE_SLAB
            // Uses the singleton instance via your provider API
            ptrs.push_back(slab_provider<Data, "MemTest">::alloc_raw());
        #else
            ptrs.push_back(malloc(sizeof(Data)));
        #endif
    }

    auto end_mem = get_memory_usage();
    long used_rss = end_mem.second - start_mem.second;
    
    std::cout << "Final Memory (RSS):    " << end_mem.second << " MB" << std::endl;
    std::cout << "Actual Growth:         " << used_rss << " MB" << std::endl;
    
    double overhead = 0.0;
    if (expected_payload_mb > 0) {
        overhead = ((double)used_rss - expected_payload_mb) / expected_payload_mb * 100.0;
    }

    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Overhead:              " << overhead << "%" << std::endl;
    
    if (used_rss > expected_payload_mb * 2) {
        std::cout << "\n[CRITICAL] MEMORY LEAK/BLOAT DETECTED!" << std::endl;
    } else {
        std::cout << "\n[PASS] Memory usage is healthy." << std::endl;
    }

    // Optional: Free loop to verify the singleton accepts returns correctly
    // (Commented out for pure allocation benchmark, but useful for correctness)
    /*
    for (void* p : ptrs) {
        #if USE_SLAB
             slab_provider<Data, "MemTest">::free_raw(static_cast<Data*>(p));
        #else
             free(p);
        #endif
    }
    */

    return 0;
}