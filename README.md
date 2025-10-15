## 🚀 Performance Benchmarks

**Comprehensive benchmarks demonstrate significant performance advantages over system malloc:**

| Benchmark | Slab Allocator | System malloc | Speedup | Throughput |
|-----------|----------------|---------------|---------|------------|
| Mixed Workload | 85.0ms | 462.4ms | **5.44x** | 4.7M ops/sec |
| Small Objects (32B) | 28.7ms | 37.9ms | **1.32x** | 34.9M ops/sec |
| Medium Objects (256B) | 52.1ms | 110.9ms | **2.13x** | 9.6M ops/sec |
| Large Objects (1KB) | 50.6ms | 97.5ms | **1.93x** | 2.0M ops/sec |

### 🎯 Performance Highlights:

- **5.44x faster** for complex mixed workloads - ideal for real-world applications
- **Consistent 2x improvements** for medium to large objects (256B-1KB)
- **Peak throughput of 34.9M operations/second** for small allocations
- **Zero memory fragmentation** overhead
- **Predictable, constant-time allocations** regardless of heap state

### 📊 Technical Advantages:

- **Slab-based architecture** eliminates search overhead in allocation paths
- **Object caching** avoids repeated initialization costs
- **Memory locality** within slabs improves CPU cache efficiency
- **Fixed-size allocation** provides O(1) performance guarantees

### Design Overview
The allocator follows a Linux-style slab allocation model:
- Each `cache_t` manages slabs containing fixed-size objects.
- Slabs are divided into `full`, `partial`, and `empty` lists.
- Objects are allocated from partially filled slabs first for better reuse.
- Uses custom free lists and cache coloring for improved CPU cache locality.
- All allocations and frees are **O(1)** — no traversal or search overhead.

### Implementation Highlights
- Backed by **`mmap()`** for direct page-level memory management
- **Cache coloring** minimizes inter-object cache line contention
- **Placement new** used for slab metadata construction
- **Alignment-safe** for all object types
- Integrated **RAII cleanup** for automatic memory unmapping

### Benchmark Methodology
All benchmarks were run on:
- CPU: Intel i5 9th gen 8 core
- Compiler: gcc version 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04) , `-O3` optimization
- OS: Ubuntu 22.04 (WSL2)
- Credit: Benchmarks were created with the help of deepseek

### Future Improvements
- Lock-free per-CPU caches for multithreading
- Large object support with multi-page slabs
- Integration tests with real workloads
- Performance comparison with more allocators

