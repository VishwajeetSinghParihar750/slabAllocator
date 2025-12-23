#include "../slabAllocator.hpp"
#include <chrono>
#include <vector>
#include <iomanip>
#include <iostream>
#include <string>
#include <memory>
#include <cstring>
#include <thread>
#include <cmath>
#include <cstdlib>

#ifdef __linux__
#include <malloc.h>
#endif

// ==========================================
// ⏱️ High Resolution Timer
// ==========================================
class HighResTimer
{
    std::chrono::high_resolution_clock::time_point start;

public:
    HighResTimer() : start(std::chrono::high_resolution_clock::now()) {}

    double elapsed_ms() const
    {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }
};

// ==========================================
// 📊 Benchmark Result Structure
// ==========================================
struct BenchmarkResult
{
    std::string name;
    double slab_time_ms;
    double system_time_ms;
    size_t operations;
    size_t object_size;

    double slab_ops_per_sec() const { return operations / (slab_time_ms / 1000.0); }
    double system_ops_per_sec() const { return operations / (system_time_ms / 1000.0); }
    double speedup() const { return system_time_ms / slab_time_ms; }
};

// ==========================================
// 🚀 Professional Benchmark Suite
// ==========================================
class ProfessionalBenchmark
{
private:
    std::vector<BenchmarkResult> results;

    void clear_system_state()
    {
        // Allocate and free large memory to flush CPU caches
        const size_t CLEAR_SIZE = 50 * 1024 * 1024; // 50MB
        void *clear_mem = malloc(CLEAR_SIZE);
        if (clear_mem)
        {
            memset(clear_mem, 0, CLEAR_SIZE);
            free(clear_mem);
        }

#ifdef __linux__
        malloc_trim(0); // Linux-specific heap cleanup
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    void print_progress(const std::string &test_name, int current, int total)
    {
        std::cout << "\r\033[K" << current << "/" << total << " " << test_name << "..." << std::flush;
    }

public:
    void run_all()
    {
        std::cout << "🎯 HIGH-PERFORMANCE SLAB ALLOCATOR BENCHMARK\n";
        std::cout << "============================================\n\n";

        // 1. Run Standard Scenarios
        run_scenarios();

        // 2. Run the "Speed of Light" Test
        run_pure_throughput_benchmark();
    }

    void run_scenarios()
    {
        std::cout << "Running 4 isolated benchmark scenarios...\n";

        results.push_back(run_small_objects_benchmark());
        print_progress("Small Objects", 1, 4);

        results.push_back(run_medium_objects_benchmark());
        print_progress("Medium Objects", 2, 4);

        results.push_back(run_large_objects_benchmark());
        print_progress("Large Objects", 3, 4);

        results.push_back(run_mixed_workload_benchmark());
        print_progress("Mixed Workload", 4, 4);

        std::cout << "\r\033[K✅ Standard benchmarks completed!\n\n";
        print_detailed_results();
    }

    // ---------------------------------------------------------
    // 🔥 THE SPEED OF LIGHT TEST (Raw Throughput)
    // ---------------------------------------------------------
    void run_pure_throughput_benchmark()
    {
        std::cout << "\n⚡ RUNNING PURE THROUGHPUT TEST (SPEED OF LIGHT)\n";
        std::cout << "=================================================\n";
        std::cout << "Mode: Raw C-Arrays (No std::vector overhead)\n";
        std::cout << "Size: 32 Bytes per object\n";
        std::cout << "Ops:  10,000,000 Allocations + 10,000,000 Frees\n";

        const size_t OPS = 10'000'000;
        
        // Setup
        slabAllocator slab;
        cache_t* cache = slab.cache_create("hot_loop", 32, nullptr, nullptr); 
        
        // Use raw array to avoid std::vector bounds checking overhead
        void** raw_ptrs = new void*[OPS]; 

        // Warmup
        for(size_t i = 0; i < 1000; i++) {
            void* p = slab.cache_alloc(cache);
            slab.cache_free(cache, p);
        }

        // --- ALLOC LOOP ---
        HighResTimer alloc_timer;
        for (size_t i = 0; i < OPS; ++i)
        {
            raw_ptrs[i] = slab.cache_alloc(cache); 
        }
        double alloc_time = alloc_timer.elapsed_ms();

        // --- FREE LOOP ---
        HighResTimer free_timer;
        for (size_t i = 0; i < OPS; ++i)
        {
            slab.cache_free(cache, raw_ptrs[i]);
        }
        double free_time = free_timer.elapsed_ms();

        // --- RESULTS ---
        double total_time = alloc_time + free_time;
        double alloc_ops = (OPS / (alloc_time / 1000.0)) / 1'000'000.0;
        double free_ops = (OPS / (free_time / 1000.0)) / 1'000'000.0;
        double total_ops = ((OPS * 2) / (total_time / 1000.0)) / 1'000'000.0;

        std::cout << "-------------------------------------------------\n";
        std::cout << "ALLOC Time: " << alloc_time << " ms\n";
        std::cout << "FREE  Time: " << free_time << " ms\n";
        std::cout << "-------------------------------------------------\n";
        std::cout << "ALLOC Speed: \033[1;32m" << std::fixed << std::setprecision(1) << alloc_ops << " M ops/sec\033[0m\n";
        std::cout << "FREE  Speed: \033[1;32m" << std::fixed << std::setprecision(1) << free_ops << " M ops/sec\033[0m\n";
        std::cout << "-------------------------------------------------\n";
        std::cout << "🔥 COMBINED: \033[1;33m" << total_ops << " M ops/sec\033[0m\n";
        std::cout << "-------------------------------------------------\n";

        delete[] raw_ptrs;
    }

    // ---------------------------------------------------------
    // Standard Benchmarks
    // ---------------------------------------------------------
    BenchmarkResult run_small_objects_benchmark()
    {
        clear_system_state();
        BenchmarkResult result{"Small Objects (32B)", 0, 0, 1000000, 32};

        { // Slab
            slabAllocator slab;
            cache_t* cache = slab.cache_create("small", 32, nullptr, nullptr);
            std::vector<void *> ptrs(result.operations);
            
            HighResTimer timer;
            for (size_t i = 0; i < result.operations; i++) ptrs[i] = slab.cache_alloc(cache);
            result.slab_time_ms = timer.elapsed_ms();
            
            for (void *p : ptrs) slab.cache_free(cache, p);
        }

        clear_system_state();

        { // System
            std::vector<void *> ptrs(result.operations);
            HighResTimer timer;
            for (size_t i = 0; i < result.operations; i++) ptrs[i] = malloc(32);
            result.system_time_ms = timer.elapsed_ms();
            for (void *p : ptrs) free(p);
        }
        return result;
    }

    BenchmarkResult run_medium_objects_benchmark()
    {
        clear_system_state();
        BenchmarkResult result{"Medium Objects (256B)", 0, 0, 500000, 256};

        {
            slabAllocator slab;
            cache_t* cache = slab.cache_create("med", 256, nullptr, nullptr);
            std::vector<void *> ptrs(result.operations);
            
            HighResTimer timer;
            for (size_t i = 0; i < result.operations; i++) ptrs[i] = slab.cache_alloc(cache);
            result.slab_time_ms = timer.elapsed_ms();
            
            for (void *p : ptrs) slab.cache_free(cache, p);
        }

        clear_system_state();

        {
            std::vector<void *> ptrs(result.operations);
            HighResTimer timer;
            for (size_t i = 0; i < result.operations; i++) ptrs[i] = malloc(256);
            result.system_time_ms = timer.elapsed_ms();
            for (void *p : ptrs) free(p);
        }
        return result;
    }

    BenchmarkResult run_large_objects_benchmark()
    {
        clear_system_state();
        BenchmarkResult result{"Large Objects (1KB)", 0, 0, 100000, 1024};

        {
            slabAllocator slab;
            cache_t* cache = slab.cache_create("large", 1024, nullptr, nullptr);
            std::vector<void *> ptrs(result.operations);
            
            HighResTimer timer;
            for (size_t i = 0; i < result.operations; i++) ptrs[i] = slab.cache_alloc(cache);
            result.slab_time_ms = timer.elapsed_ms();
            
            for (void *p : ptrs) slab.cache_free(cache, p);
        }

        clear_system_state();

        {
            std::vector<void *> ptrs(result.operations);
            HighResTimer timer;
            for (size_t i = 0; i < result.operations; i++) ptrs[i] = malloc(1024);
            result.system_time_ms = timer.elapsed_ms();
            for (void *p : ptrs) free(p);
        }
        return result;
    }

    BenchmarkResult run_mixed_workload_benchmark()
    {
        clear_system_state();
        BenchmarkResult result{"Mixed Workload", 0, 0, 400000, 288};

        {
            slabAllocator slab;
            cache_t* s_cache = slab.cache_create("m_small", 64, nullptr, nullptr);
            cache_t* m_cache = slab.cache_create("m_med", 512, nullptr, nullptr);
            
            std::vector<void *> s_ptrs(200000);
            std::vector<void *> m_ptrs(200000);
            
            HighResTimer timer;
            for (size_t i = 0; i < 200000; i++) {
                s_ptrs[i] = slab.cache_alloc(s_cache);
                m_ptrs[i] = slab.cache_alloc(m_cache);
            }
            result.slab_time_ms = timer.elapsed_ms();
            
            for (size_t i = 0; i < 200000; i++) {
                slab.cache_free(s_cache, s_ptrs[i]);
                slab.cache_free(m_cache, m_ptrs[i]);
            }
        }

        clear_system_state();

        {
            std::vector<void *> s_ptrs(200000);
            std::vector<void *> m_ptrs(200000);
            
            HighResTimer timer;
            for (size_t i = 0; i < 200000; i++) {
                s_ptrs[i] = malloc(64);
                m_ptrs[i] = malloc(512);
            }
            result.system_time_ms = timer.elapsed_ms();
            
            for (size_t i = 0; i < 200000; i++) {
                free(s_ptrs[i]);
                free(m_ptrs[i]);
            }
        }
        return result;
    }

    void print_detailed_results()
    {
        std::cout << "📊 DETAILED PERFORMANCE RESULTS\n";
        std::cout << "================================\n\n";

        std::cout << std::left
                  << std::setw(22) << "TEST CASE"
                  << std::setw(14) << "OPS/sec"
                  << std::setw(12) << "SLAB (ms)"
                  << std::setw(12) << "SYSTEM (ms)"
                  << std::setw(10) << "SPEEDUP"
                  << std::setw(12) << "EFFICIENCY" << "\n";
        std::cout << std::string(82, '-') << "\n";

        for (const auto &result : results)
        {
            std::cout << std::left
                      << std::setw(22) << result.name
                      << std::setw(14) << std::fixed << std::setprecision(2)
                      << (result.slab_ops_per_sec() / 1000000.0) << "M"
                      << std::setw(12) << std::setprecision(1) << result.slab_time_ms
                      << std::setw(12) << std::setprecision(1) << result.system_time_ms
                      << std::setw(10) << std::setprecision(2) << result.speedup() << "x"
                      << std::setw(12) << std::setprecision(1)
                      << (result.slab_ops_per_sec() / result.system_ops_per_sec() * 100.0) << "%\n";
        }
    }
};

// ==========================================
// 🏁 Main Entry Point
// ==========================================
int main()
{
    ProfessionalBenchmark benchmark;
    benchmark.run_all();
    return 0;
}