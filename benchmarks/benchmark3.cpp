#include "slabAllocator.hpp"
#include "sequence_buffer.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>

struct Packet {
    uint8_t payload[1024]; // Standard 1KB MTU Packet
};

int main() {
    std::cout << "🚀 RUDP RECEIVER SIMULATION (Single Core)\n";
    std::cout << "========================================\n";
    
    // 1. Setup Memory Engine
    slabAllocator allocator;
    // Note: Creating cache for 1KB objects (Packet size)
    cache_t* packet_cache = allocator.cache_create("packets", sizeof(Packet), nullptr, nullptr);
    
    // 2. Setup Logic Engine (64k sliding window)
    SequenceBuffer<Packet> history(65536); 

    const int PACKETS_TO_PROCESS = 10'000'000;
    
    std::cout << "Simulating receiving " << PACKETS_TO_PROCESS << " packets...\n";
    
    auto start = std::chrono::high_resolution_clock::now();

    for(int seq = 0; seq < PACKETS_TO_PROCESS; ++seq) {
        // Simulate wrapping sequence number (uint16_t)
        uint16_t sequence = (uint16_t)(seq & 0xFFFF);

        // A. Network Logic: Check Duplicate
        if (history.find(sequence) != nullptr) continue; 

        // B. Memory Logic: Allocate Slab
        Packet* p = (Packet*)allocator.cache_alloc(packet_cache);

        // C. Storage Logic: Insert into Window
        history.insert(sequence);
        history.store(sequence, p);

        // D. Processing Logic: Immediately process and free (Simulating real-time stream)
        // In a real app, you might batch this, but we test worst-case single-packet latency here.
        Packet* processed = history.remove(sequence);
        allocator.cache_free(packet_cache, processed);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    double seconds = ms / 1000.0;
    double pps = PACKETS_TO_PROCESS / seconds;

    std::cout << "-------------------------------------------------\n";
    std::cout << "Time:       " << ms << " ms\n";
    std::cout << "Throughput: \033[1;32m" << std::fixed << std::setprecision(1) << (pps / 1'000'000.0) << " M PPS\033[0m (Packets Per Sec)\n";
    std::cout << "Bandwidth:  " << std::fixed << std::setprecision(1) << ((pps * 1024 * 8) / 1'000'000'000.0) << " Gbps\n";
    std::cout << "-------------------------------------------------\n";

    return 0;
}