#include <iostream>
#include <vector>
#include <algorithm>
#include <random>
#include <unistd.h>
#include <fstream>
#include "slab_allocator.hpp" // Updated header

// 32KB Packets
struct Packet {
    char data[32 * 1024]; 
};

// 100,000 packets * 32KB = ~3.2 GB of virtual alloc per cycle
const int PACKET_COUNT = 100'000;
const int CYCLES = 10;
const double FREE_RATIO = 0.90; // Release 90%

// NOTE: Global cache instance removed. 
// We now use slab_provider<Packet, "ChurnTest">

// Helper to check RAM usage
long get_rss_mb() {
    long rss = 0, virt = 0;
    std::ifstream statm("/proc/self/statm");
    if (statm >> virt >> rss) {
        long page_size = sysconf(_SC_PAGE_SIZE);
        return (rss * page_size) / (1024 * 1024);
    }
    return 0;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "        MEMORY CHURN STRESS TEST        " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Packet Size:   32 KB" << std::endl;
    std::cout << "Count:         " << PACKET_COUNT << " (~3.2 GB Payload)" << std::endl;
    std::cout << "Cycles:        " << CYCLES << std::endl;
    std::cout << "Churn Rate:    " << (FREE_RATIO * 100) << "% per cycle" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    std::vector<void*> packets;
    packets.reserve(PACKET_COUNT);
    
    // Random number generator for shuffling
    std::random_device rd;
    std::mt19937 g(rd());

    long baseline = get_rss_mb();
    std::cout << "Baseline RSS: " << baseline << " MB" << std::endl << std::endl;

    for (int cycle = 1; cycle <= CYCLES; cycle++) {
        // 1. ALLOCATE PHASE
        // Refill up to max capacity
        int current_size = packets.size();
        int to_alloc = PACKET_COUNT - current_size;
        
        for (int i = 0; i < to_alloc; i++) {
            // Using the singleton provider for allocation
            packets.push_back(slab_provider<Packet, "ChurnTest">::alloc_raw());
        }

        long peak_rss = get_rss_mb();

        // 2. CHURN PHASE (Free 90% randomly)
        // Shuffle to ensure we create holes (fragmentation)
        std::shuffle(packets.begin(), packets.end(), g);

        int to_free = (int)(PACKET_COUNT * FREE_RATIO);
        
        // Free the last 'to_free' items
        for (int i = 0; i < to_free; i++) {
            void* ptr = packets.back();
            // Cast void* back to Packet* for the typed provider
            slab_provider<Packet, "ChurnTest">::free_raw(static_cast<Packet*>(ptr));
            packets.pop_back();
        }

        long after_free_rss = get_rss_mb();

        std::cout << "[Cycle " << cycle << "]" 
                  << " Peak: " << peak_rss << " MB"
                  << " | After Free: " << after_free_rss << " MB" 
                  << std::endl;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "Final RSS: " << get_rss_mb() << " MB" << std::endl;
    
    return 0;
}