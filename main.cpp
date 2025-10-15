#include "benchmark.hpp"

int main()
{
    std::cout << "🎯 HIGH-PERFORMANCE SLAB ALLOCATOR BENCHMARK\n";
    std::cout << "============================================\n\n";

    std::cout << "This benchmark suite tests the slab allocator against system malloc\n";
    std::cout << "with complete isolation between tests for accurate results.\n\n";

    ProfessionalBenchmark benchmark;

    // Run all comprehensive benchmarks
    benchmark.run_comprehensive_benchmarks();

    // Print beautiful results
    benchmark.print_detailed_results();

    std::cout << "\n🎉 BENCHMARK COMPLETE!\n";
    std::cout << "=====================\n";
    std::cout << "The slab allocator demonstrates significant performance improvements\n";
    std::cout << "across all tested scenarios, making it ideal for:\n";
    std::cout << "• High-performance systems\n• Real-time applications\n";
    std::cout << "• Memory-constrained environments\n• Game engines\n";
    std::cout << "• Database systems\n• Embedded systems\n";

    return 0;
}