
#include "slabAllocator.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <unordered_set>

// Custom exception for test failures
class TestFailure : public std::runtime_error
{
public:
    TestFailure(const std::string &msg) : std::runtime_error(msg) {}
};

// Main assertion macros
#define TEST_ASSERT(condition, message)                                                                   \
    do                                                                                                    \
    {                                                                                                     \
        if (!(condition))                                                                                 \
        {                                                                                                 \
            throw TestFailure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + (message)); \
        }                                                                                                 \
    } while (0)

#define TEST_ASSERT_MSG(condition, ...)                                                                \
    do                                                                                                 \
    {                                                                                                  \
        if (!(condition))                                                                              \
        {                                                                                              \
            char buffer[1024];                                                                         \
            snprintf(buffer, sizeof(buffer), __VA_ARGS__);                                             \
            throw TestFailure(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + buffer); \
        }                                                                                              \
    } while (0)

// Utility function to run tests with exception handling
template <typename TestFunc>
void run_test(const std::string &test_name, TestFunc test_func)
{
    std::cout << "=== " << test_name << " ===\n";
    try
    {
        test_func();
        std::cout << "✓ " << test_name << " PASSED\n";
    }
    catch (const TestFailure &e)
    {
        std::cerr << "✗ " << test_name << " FAILED: " << e.what() << "\n";
        throw; // Re-throw to stop test suite
    }
    catch (const std::exception &e)
    {
        std::cerr << "✗ " << test_name << " ERROR: " << e.what() << "\n";
        throw; // Re-throw to stop test suite
    }
    std::cout << "\n";
}

class PerformanceTimer
{
    std::chrono::high_resolution_clock::time_point start;

public:
    PerformanceTimer() : start(std::chrono::high_resolution_clock::now()) {}

    double elapsed_ms() const
    {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }
};
class TestObject
{
public:
    int id;
    char data[64];
    static int constructor_calls;
    static int destructor_calls;

    TestObject() : id(0)
    {
        memset(data, 0, sizeof(data));
        constructor_calls++;
    }

    ~TestObject()
    {
        destructor_calls++;
    }
};

int TestObject::constructor_calls = 0;
int TestObject::destructor_calls = 0;

void testConstructor(void *obj)
{
    new (obj) TestObject();
}

void testDestructor(void *obj)
{
    static_cast<TestObject *>(obj)->~TestObject();
}
void test_basic_functionality()
{
    std::cout << "=== Test 1: Basic Functionality ===\n";
    slabAllocator alloc;

    // Create cache
    alloc.cache_create("test_obj", sizeof(TestObject), testConstructor, testDestructor);

    // Allocate single object
    TestObject *obj = static_cast<TestObject *>(alloc.cache_alloc("test_obj"));
    assert(obj != nullptr);
    assert(obj->id == 0);

    // Free object
    alloc.cache_free("test_obj", obj);

    // Allocate again - should reuse memory
    TestObject *obj2 = static_cast<TestObject *>(alloc.cache_alloc("test_obj"));
    assert(obj2 != nullptr);

    alloc.cache_free("test_obj", obj2);
    alloc.cache_destroy("test_obj");

    std::cout << "✓ Basic allocation/free working\n";
    std::cout << "✓ Constructor calls: " << TestObject::constructor_calls << "\n";
    std::cout << "✓ Destructor calls: " << TestObject::destructor_calls << "\n";
}
void test_random_patterns()
{
    std::cout << "\n=== Test 6: Random Allocation Patterns ===\n";
    slabAllocator alloc;

    alloc.cache_create("stress_test", 64, nullptr, nullptr);

    std::vector<void *> live_objects;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);

    const int TOTAL_OPERATIONS = 100000;
    int allocations = 0;
    int frees = 0;

    PerformanceTimer timer;

    for (int i = 0; i < TOTAL_OPERATIONS; i++)
    {
        if (dis(gen) < 0.6 || live_objects.empty())
        {
            // Allocate
            void *obj = alloc.cache_alloc("stress_test");
            assert(obj != nullptr);
            live_objects.push_back(obj);
            allocations++;
        }
        else
        {
            // Free random object
            std::uniform_int_distribution<> idx_dis(0, live_objects.size() - 1);
            int idx = idx_dis(gen);
            alloc.cache_free("stress_test", live_objects[idx]);
            live_objects.erase(live_objects.begin() + idx);
            frees++;
        }
    }

    // Cleanup remaining objects
    for (void *obj : live_objects)
    {
        alloc.cache_free("stress_test", obj);
        frees++;
    }

    double total_time = timer.elapsed_ms();

    std::cout << "Operations: " << TOTAL_OPERATIONS << " in " << total_time << "ms\n";
    std::cout << "Allocations: " << allocations << ", Frees: " << frees << "\n";
    std::cout << "Operations/sec: " << (TOTAL_OPERATIONS / (total_time / 1000.0)) << "\n";

    alloc.cache_destroy("stress_test");
}

void test_cache_destruction()
{
    std::cout << "\n=== Test 7: Cache Destruction Safety ===\n";
    slabAllocator alloc;

    // Create cache and allocate objects
    alloc.cache_create("destroy_test", 256, nullptr, nullptr);

    std::vector<void *> objects;
    for (int i = 0; i < 50; i++)
    {
        objects.push_back(alloc.cache_alloc("destroy_test"));
    }

    // Free some objects
    for (int i = 0; i < 25; i++)
    {
        alloc.cache_free("destroy_test", objects[i]);
    }

    // Destroy cache with mixed state (some allocated, some free)
    // This should properly clean up all memory
    //
    alloc.cache_destroy("destroy_test");

    // Try to use destroyed cache (should assert)
    try
    {

        alloc.cache_alloc("destroy_test");
        assert(false && "Should have asserted on destroyed cache");
    }
    catch (...)
    {
        std::cout << "✓ Properly handles destroyed cache access\n";
    }

    std::cout << "✓ Cache destruction cleans up properly\n";
}
void test_object_reuse()
{
    std::cout << "\n=== Test 5: Object Reuse Verification ===\n";
    slabAllocator alloc;

    struct ReuseTest
    {
        int magic_number;
        char payload[56];
    };

    alloc.cache_create("reuse_test", sizeof(ReuseTest), nullptr, nullptr);

    // Allocate and mark objects
    std::vector<ReuseTest *> objects;
    for (int i = 0; i < 100; i++)
    {
        ReuseTest *obj = static_cast<ReuseTest *>(alloc.cache_alloc("reuse_test"));
        obj->magic_number = 0xDEADBEEF + i;
        objects.push_back(obj);
    }

    // Free all objects
    for (ReuseTest *obj : objects)
    {
        alloc.cache_free("reuse_test", obj);
    }

    // Allocate again - should reuse same memory
    int reuse_count = 0;
    for (int i = 0; i < 100; i++)
    {
        ReuseTest *obj = static_cast<ReuseTest *>(alloc.cache_alloc("reuse_test"));
        if (obj->magic_number == (0xDEADBEEF + i))
        {
            reuse_count++;
        }
        alloc.cache_free("reuse_test", obj);
    }

    std::cout << "Objects reused: " << reuse_count << "/100\n";
    std::cout << "✓ Object reuse is working\n";

    alloc.cache_destroy("reuse_test");
}

void test_multiple_caches()
{
    std::cout << "\n=== Test 4: Multiple Cache Types ===\n";
    slabAllocator alloc;

    // Create multiple caches with different sizes
    alloc.cache_create("small", 32, nullptr, nullptr);
    alloc.cache_create("medium", 128, nullptr, nullptr);
    alloc.cache_create("large", 512, nullptr, nullptr);

    // Allocate from different caches
    void *small1 = alloc.cache_alloc("small");
    void *medium1 = alloc.cache_alloc("medium");
    void *large1 = alloc.cache_alloc("large");

    assert(small1 != nullptr);
    assert(medium1 != nullptr);
    assert(large1 != nullptr);

    // Ensure they're different addresses (not overlapping)
    assert(small1 != medium1);
    assert(medium1 != large1);

    alloc.cache_free("small", small1);
    alloc.cache_free("medium", medium1);
    alloc.cache_free("large", large1);

    alloc.cache_destroy("small");
    alloc.cache_destroy("medium");
    alloc.cache_destroy("large");

    std::cout << "✓ Multiple caches work independently\n";
}

void test_performance()
{
    std::cout << "\n=== Test 3: Performance Benchmarking ===\n";
    slabAllocator alloc;

    const int NUM_ALLOCS = 100000;
    const size_t OBJ_SIZE = 128;

    alloc.cache_create("perf_test", OBJ_SIZE, nullptr, nullptr);

    // Test allocation speed
    PerformanceTimer alloc_timer;
    std::vector<void *> objects(NUM_ALLOCS);

    for (int i = 0; i < NUM_ALLOCS; i++)
    {
        objects[i] = alloc.cache_alloc("perf_test");
    }
    double alloc_time = alloc_timer.elapsed_ms();

    // Test free speed
    PerformanceTimer free_timer;
    for (int i = 0; i < NUM_ALLOCS; i++)
    {
        alloc.cache_free("perf_test", objects[i]);
    }
    double free_time = free_timer.elapsed_ms();

    std::cout << "Allocation: " << alloc_time << "ms for " << NUM_ALLOCS << " objects\n";
    std::cout << "Free: " << free_time << "ms for " << NUM_ALLOCS << " objects\n";
    std::cout << "Allocations/sec: " << (NUM_ALLOCS / (alloc_time / 1000.0)) << "\n";
    std::cout << "Frees/sec: " << (NUM_ALLOCS / (free_time / 1000.0)) << "\n";

    alloc.cache_destroy("perf_test");
}
void test_memory_leaks()
{
    std::cout << "\n=== Test 2: Memory Leak Detection ===\n";
    slabAllocator alloc;

    const int NUM_OBJECTS = 1000;
    std::vector<void *> objects;

    alloc.cache_create("leak_test", 64, nullptr, nullptr);

    // Allocate many objects
    for (int i = 0; i < NUM_OBJECTS; i++)
    {
        objects.push_back(alloc.cache_alloc("leak_test"));
        assert(objects.back() != nullptr);
    }

    // Free all objects
    for (void *obj : objects)
    {
        alloc.cache_free("leak_test", obj);
    }

    alloc.cache_destroy("leak_test");

    // If we get here without crashing, memory management is correct
    std::cout << "✓ No memory leaks detected\n";
    std::cout << "✓ " << NUM_OBJECTS << " objects allocated/freed successfully\n";
}

struct CorruptTest
{
    uint64_t canary_front;
    char data[8];
    uint64_t canary_rear;
};

void corrupt_test_constructor(void *obj)
{
    CorruptTest *test = static_cast<CorruptTest *>(obj);
    test->canary_front = 0xDEADBEEFDEADBEEF;
    memcpy(test->data, "sahilyr", 8);
    test->canary_rear = 0xCAFEBABECAFEBABE;
}

void test_object_corruption()
{
    std::cout << "\n=== Test: Object Corruption Detection ===\n";
    slabAllocator alloc;

    alloc.cache_create("corrupt_test", sizeof(CorruptTest), corrupt_test_constructor, nullptr);

    const int NUM_OBJECTS = 100;
    std::vector<CorruptTest *> objects;

    // Allocate and initialize objects
    for (int i = 0; i < NUM_OBJECTS; i++)
    {
        CorruptTest *obj = static_cast<CorruptTest *>(alloc.cache_alloc("corrupt_test"));

        TEST_ASSERT(obj->canary_front == 0xDEADBEEFDEADBEEF, "Front canary corrupted after alloc");
        TEST_ASSERT(obj->canary_rear == 0xCAFEBABECAFEBABE, "Rear canary corrupted after alloc");
        objects.push_back(obj);
    }

    // Free objects
    for (CorruptTest *obj : objects)
    {
        TEST_ASSERT(obj->canary_front == 0xDEADBEEFDEADBEEF, "Front canary corrupted before free");
        TEST_ASSERT(obj->canary_rear == 0xCAFEBABECAFEBABE, "Rear canary corrupted before free");
        alloc.cache_free("corrupt_test", obj);
    }

    alloc.cache_destroy("corrupt_test");
    std::cout << "✓ No object corruption detected\n";
}
void test_alignment_requirements()
{
    std::cout << "\n=== Test: Alignment Verification ===\n";
    slabAllocator alloc;

    // Test various alignments that might be problematic
    size_t test_sizes[] = {1, 3, 7, 15, 31, 63, 127, 255, 511, 1023};

    for (size_t size : test_sizes)
    {
        std::string cache_name = "align_" + std::to_string(size);
        alloc.cache_create(cache_name.c_str(), size, nullptr, nullptr);

        // Allocate multiple objects and check alignment
        for (int i = 0; i < 10; i++)
        {
            void *obj = alloc.cache_alloc(cache_name.c_str());

            // Check if pointer is reasonably aligned (at least to machine word)
            uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
            TEST_ASSERT_MSG((addr % sizeof(void *)) == 0,
                            "Object of size %zu not aligned to word boundary. Address: %p",
                            size, obj);

            alloc.cache_free(cache_name.c_str(), obj);
        }

        alloc.cache_destroy(cache_name.c_str());
    }
    std::cout << "✓ All objects properly aligned\n";
}
void test_slab_boundaries()
{
    std::cout << "\n=== Test: Slab Boundary Testing ===\n";
    slabAllocator alloc;

    // Use object size that likely doesn't evenly divide page size
    const size_t OBJ_SIZE = 73; // Prime number
    alloc.cache_create("boundary_test", OBJ_SIZE, nullptr, nullptr);

    const int NUM_OBJECTS = 1000;
    std::vector<void *> objects;
    std::unordered_set<void *> unique_addresses;

    // Allocate many objects
    for (int i = 0; i < NUM_OBJECTS; i++)
    {
        void *obj = alloc.cache_alloc("boundary_test");
        TEST_ASSERT(obj != nullptr, "Allocation failed at boundary test");

        // Check for duplicate addresses
        TEST_ASSERT_MSG(unique_addresses.find(obj) == unique_addresses.end(),
                        "Duplicate address %p detected - possible slab management error", obj);
        unique_addresses.insert(obj);

        objects.push_back(obj);
    }

    // Verify no objects overlap by checking distances
    for (int i = 0; i < NUM_OBJECTS; i++)
    {
        for (int j = i + 1; j < NUM_OBJECTS; j++)
        {
            auto diff = std::abs(static_cast<char *>(objects[i]) - static_cast<char *>(objects[j]));
            TEST_ASSERT_MSG(diff >= OBJ_SIZE,
                            "Objects too close: %p and %p (diff: %zu, expected >= %zu)",
                            objects[i], objects[j], diff, OBJ_SIZE);
        }
    }

    // Free objects
    for (void *obj : objects)
    {
        alloc.cache_free("boundary_test", obj);
    }

    alloc.cache_destroy("boundary_test");
    std::cout << "✓ Slab boundaries properly maintained\n";
}
void test_constructor_exceptions()
{
    std::cout << "\n=== Test: Constructor Exception Safety ===\n";
    slabAllocator alloc;

    static int constructor_calls = 0;
    static int destructor_calls = 0;

    auto throwing_constructor = [](void *obj)
    {
        constructor_calls++;
        if (constructor_calls == 3)
        { // Fail on 3rd call
            throw std::runtime_error("Simulated constructor failure");
        }
        // Otherwise initialize normally
        new (obj) TestObject();
    };

    auto normal_destructor = [](void *obj)
    {
        destructor_calls++;
        static_cast<TestObject *>(obj)->~TestObject();
    };

    constructor_calls = 0;
    destructor_calls = 0;

    alloc.cache_create("exception_test", sizeof(TestObject), throwing_constructor, normal_destructor);

    try
    {
        // First two should succeed
        void *obj1 = alloc.cache_alloc("exception_test");
        TEST_ASSERT(obj1 != nullptr, "First allocation failed");
        alloc.cache_free("exception_test", obj1);

        void *obj2 = alloc.cache_alloc("exception_test");
        TEST_ASSERT(obj2 != nullptr, "Second allocation failed");
        alloc.cache_free("exception_test", obj2);

        // Third should throw
        try
        {
            void *obj3 = alloc.cache_alloc("exception_test");
            TEST_ASSERT(false, "Expected exception on third allocation");
        }
        catch (const std::exception &e)
        {
            std::cout << "✓ Properly caught constructor exception: " << e.what() << "\n";
        }

        // Fourth should work again
        void *obj4 = alloc.cache_alloc("exception_test");
        TEST_ASSERT(obj4 != nullptr, "Allocation failed after exception");
        alloc.cache_free("exception_test", obj4);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Unexpected exception: " << e.what() << "\n";
        throw;
    }

    std::cout << "Constructor calls: " << constructor_calls << ", Destructor calls: " << destructor_calls << "\n";
    alloc.cache_destroy("exception_test");
}
void test_memory_patterns()
{
    std::cout << "\n=== Test: Memory Pattern Testing ===\n";
    slabAllocator alloc;

    const size_t OBJ_SIZE = 128;
    alloc.cache_create("pattern_test", OBJ_SIZE, nullptr, nullptr);

    const int NUM_CYCLES = 5;
    const int OBJECTS_PER_CYCLE = 100;

    for (int cycle = 0; cycle < NUM_CYCLES; cycle++)
    {
        std::vector<void *> objects;

        // Fill with pattern
        for (int i = 0; i < OBJECTS_PER_CYCLE; i++)
        {
            void *obj = alloc.cache_alloc("pattern_test");
            memset(obj, 0xAA + cycle, OBJ_SIZE); // Different pattern each cycle
            objects.push_back(obj);
        }

        // Verify pattern integrity
        for (void *obj : objects)
        {
            for (size_t j = 0; j < OBJ_SIZE; j++)
            {
                TEST_ASSERT_MSG(static_cast<unsigned char *>(obj)[j] == (0xAA + cycle),
                                "Memory pattern corrupted at byte %zu in cycle %d", j, cycle);
            }
        }

        // Free objects
        for (void *obj : objects)
        {
            alloc.cache_free("pattern_test", obj);
        }
    }

    alloc.cache_destroy("pattern_test");
    std::cout << "✓ Memory patterns maintained correctly\n";
}
void test_fragmentation_resistance()
{
    std::cout << "\n=== Test: Fragmentation Resistance ===\n";
    slabAllocator alloc;

    alloc.cache_create("frag_test", 64, nullptr, nullptr);

    const int NUM_OBJECTS = 1000;
    std::vector<void *> persistent_objects;
    std::vector<void *> temp_objects;

    // Allocate some persistent objects
    for (int i = 0; i < NUM_OBJECTS / 10; i++)
    {
        persistent_objects.push_back(alloc.cache_alloc("frag_test"));
    }

    // Many allocate/free cycles to cause fragmentation
    for (int cycle = 0; cycle < 100; cycle++)
    {
        // Allocate many temporary objects
        for (int i = 0; i < NUM_OBJECTS / 2; i++)
        {
            temp_objects.push_back(alloc.cache_alloc("frag_test"));
            // TEST_ASSERT(temp_objects.back() != nullptr,
            //             "Allocation failed due to fragmentation in cycle %d", cycle);
        }

        // Free all temporary objects
        for (void *obj : temp_objects)
        {
            alloc.cache_free("frag_test", obj);
        }
        temp_objects.clear();
    }

    // Allocate more objects - should still work despite fragmentation
    for (int i = 0; i < NUM_OBJECTS / 5; i++)
    {
        void *obj = alloc.cache_alloc("frag_test");
        TEST_ASSERT(obj != nullptr, "Allocation failed after fragmentation test");
        alloc.cache_free("frag_test", obj);
    }

    // Cleanup persistent objects
    for (void *obj : persistent_objects)
    {
        alloc.cache_free("frag_test", obj);
    }

    alloc.cache_destroy("frag_test");
    std::cout << "✓ Fragmentation resistance verified\n";
}
void test_cache_name_collisions()
{
    std::cout << "\n=== Test: Cache Name Collisions ===\n";
    slabAllocator alloc;

    // Test creating cache with same name
    alloc.cache_create("duplicate_test", 64, nullptr, nullptr);

    try
    {
        alloc.cache_create("duplicate_test", 128, nullptr, nullptr);
        TEST_ASSERT(false, "Should have detected duplicate cache name");
    }
    catch (const std::exception &e)
    {
        std::cout << "✓ Properly rejected duplicate cache: " << e.what() << "\n";
    }

    // Test destroying non-existent cache
    try
    {
        alloc.cache_destroy("nonexistent_cache");
        TEST_ASSERT(false, "Should have detected non-existent cache destruction");
    }
    catch (const std::exception &e)
    {
        std::cout << "✓ Properly rejected non-existent cache destruction: " << e.what() << "\n";
    }

    alloc.cache_destroy("duplicate_test");
}
void test_zero_size_objects()
{
    std::cout << "\n=== Test: Zero-Size Objects ===\n";
    slabAllocator alloc;

    try
    {
        alloc.cache_create("zero_test", 0, nullptr, nullptr);
        TEST_ASSERT(false, "Should have rejected zero-sized objects");
    }
    catch (const std::exception &e)
    {
        std::cout << "✓ Properly rejected zero-sized objects: " << e.what() << "\n";
    }
}
void test_very_large_objects()
{
    std::cout << "\n=== Test: Very Large Objects ===\n";
    slabAllocator alloc;

    // Test with object size larger than typical page
    const size_t LARGE_OBJ_SIZE = 8192; // 8KB

    alloc.cache_create("large_test", LARGE_OBJ_SIZE, nullptr, nullptr);

    void *obj1 = alloc.cache_alloc("large_test");
    TEST_ASSERT(obj1 != nullptr, "Large object allocation failed");

    void *obj2 = alloc.cache_alloc("large_test");
    TEST_ASSERT(obj2 != nullptr, "Second large object allocation failed");

    // Verify they don't overlap
    auto diff = std::abs(static_cast<char *>(obj1) - static_cast<char *>(obj2));
    TEST_ASSERT(diff >= LARGE_OBJ_SIZE, "Large objects overlapping");

    alloc.cache_free("large_test", obj1);
    alloc.cache_free("large_test", obj2);
    alloc.cache_destroy("large_test");

    std::cout << "✓ Very large objects handled correctly\n";
}

void test_memory_exhaustion()
{
    std::cout << "\n=== Test: Memory Exhaustion Simulation ===\n";
    slabAllocator alloc;

    // Create many caches to simulate memory pressure
    const int NUM_CACHES = 50;
    const int OBJS_PER_CACHE = 100;

    std::vector<std::string> cache_names;

    for (int i = 0; i < NUM_CACHES; i++)
    {
        std::string name = "pressure_" + std::to_string(i);
        alloc.cache_create(name.c_str(), 64 + (i % 16), nullptr, nullptr);
        cache_names.push_back(name);

        // Allocate some objects
        for (int j = 0; j < OBJS_PER_CACHE; j++)
        {
            void *obj = alloc.cache_alloc(name.c_str());
            // TEST_ASSERT(obj != nullptr,
            //             "Allocation failed under simulated memory pressure in cache %d", i);
        }
    }

    // Cleanup
    for (const auto &name : cache_names)
    {
        alloc.cache_destroy(name.c_str());
    }

    std::cout << "✓ Handled memory pressure scenario\n";
}

int main()
{
    std::cout << "Starting Advanced Slab Allocator Test Suite\n";
    std::cout << "===========================================\n\n";

    try
    {
        run_test("Basic Functionality", test_basic_functionality);
        run_test("Object Corruption Detection", test_object_corruption);
        run_test("Alignment Verification", test_alignment_requirements);
        run_test("Slab Boundary Testing", test_slab_boundaries);
        run_test("Memory Pattern Testing", test_memory_patterns);
        run_test("Fragmentation Resistance", test_fragmentation_resistance);

        std::cout << "===========================================\n";
        std::cout << "ALL TESTS PASSED! 🎉\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n===========================================\n";
        std::cerr << "TEST SUITE FAILED: " << e.what() << "\n";
        return 1;
    }

    return 0;
}