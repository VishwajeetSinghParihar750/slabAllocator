#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include "slabAllocator.hpp"
#include <cstring>

// Simple object for testing
struct DataObject
{
    int id;
    double values[16];
    char description[64];

    DataObject() : id(0)
    {
        strcpy(description, "default");
    }
};

// Constructor and destructor
void dataObjectCtor(void *obj)
{
    new (obj) DataObject();
}

void dataObjectDtor(void *obj)
{
    static_cast<DataObject *>(obj)->~DataObject();
}

int main()
{
    slabAllocator allocator;

    // Create caches
    allocator.cache_create("DataObject", sizeof(DataObject), dataObjectCtor, dataObjectDtor);
    allocator.cache_create("IntArray", sizeof(int) * 100, nullptr, nullptr);
    allocator.cache_create("DoubleArray", sizeof(double) * 50, nullptr, nullptr);

    std::cout << "=== Test 1: Large Number of Small Objects ===\n";
    {
        const int NUM_OBJECTS = 100000;
        std::vector<DataObject *> objects;
        objects.reserve(NUM_OBJECTS);

        auto start = std::chrono::high_resolution_clock::now();

        // Allocate many objects
        for (int i = 0; i < NUM_OBJECTS; i++)
        {
            DataObject *obj = static_cast<DataObject *>(allocator.cache_alloc("DataObject"));
            obj->id = i;
            objects.push_back(obj);
        }

        auto alloc_end = std::chrono::high_resolution_clock::now();

        // Free all objects
        for (DataObject *obj : objects)
        {
            allocator.cache_free("DataObject", obj);
        }

        auto free_end = std::chrono::high_resolution_clock::now();

        auto alloc_time = std::chrono::duration_cast<std::chrono::milliseconds>(alloc_end - start);
        auto free_time = std::chrono::duration_cast<std::chrono::milliseconds>(free_end - alloc_end);

        std::cout << "Allocated " << NUM_OBJECTS << " objects in " << alloc_time.count() << "ms\n";
        std::cout << "Freed " << NUM_OBJECTS << " objects in " << free_time.count() << "ms\n";
        std::cout << "Total time: " << (alloc_time + free_time).count() << "ms\n\n";
    }

    std::cout << "=== Test 2: Large Arrays Allocation ===\n";
    {
        const int NUM_ARRAYS = 50000;
        std::vector<int *> arrays;
        arrays.reserve(NUM_ARRAYS);

        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < NUM_ARRAYS; i++)
        {
            int *arr = static_cast<int *>(allocator.cache_alloc("IntArray"));
            arr[0] = i; // Simple initialization
            arrays.push_back(arr);
        }

        auto alloc_end = std::chrono::high_resolution_clock::now();

        for (int *arr : arrays)
        {
            allocator.cache_free("IntArray", arr);
        }

        auto free_end = std::chrono::high_resolution_clock::now();

        auto alloc_time = std::chrono::duration_cast<std::chrono::milliseconds>(alloc_end - start);
        auto free_time = std::chrono::duration_cast<std::chrono::milliseconds>(free_end - alloc_end);

        std::cout << "Allocated " << NUM_ARRAYS << " int arrays in " << alloc_time.count() << "ms\n";
        std::cout << "Freed " << NUM_ARRAYS << " int arrays in " << free_time.count() << "ms\n\n";
    }

    std::cout << "=== Test 3: Mixed Size Allocations ===\n";
    {
        const int NUM_ITERATIONS = 20000;
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < NUM_ITERATIONS; i++)
        {
            // Mix different allocation types
            DataObject *obj = static_cast<DataObject *>(allocator.cache_alloc("DataObject"));
            int *intArr = static_cast<int *>(allocator.cache_alloc("IntArray"));
            double *doubleArr = static_cast<double *>(allocator.cache_alloc("DoubleArray"));

            // Simple usage
            obj->id = i;
            intArr[0] = i * 2;
            doubleArr[0] = i * 3.14;

            // Free them
            allocator.cache_free("DataObject", obj);
            allocator.cache_free("IntArray", intArr);
            allocator.cache_free("DoubleArray", doubleArr);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "Completed " << NUM_ITERATIONS * 3 << " mixed allocations/deallocations in "
                  << total_time.count() << "ms\n\n";
    }

    std::cout << "=== Test 4: Batch Allocation and Staggered Free ===\n";
    {
        const int BATCH_SIZE = 50000;
        const int KEEP_COUNT = 10000; // Keep some allocated

        std::vector<DataObject *> persistent_objects;
        persistent_objects.reserve(KEEP_COUNT);

        auto start = std::chrono::high_resolution_clock::now();

        // Allocate large batch
        for (int i = 0; i < BATCH_SIZE; i++)
        {
            DataObject *obj = static_cast<DataObject *>(allocator.cache_alloc("DataObject"));
            obj->id = i;

            // Keep some objects allocated
            if (i < KEEP_COUNT)
            {
                persistent_objects.push_back(obj);
            }
            else
            {
                allocator.cache_free("DataObject", obj);
            }
        }

        auto batch_end = std::chrono::high_resolution_clock::now();

        // Free the persistent objects
        for (DataObject *obj : persistent_objects)
        {
            allocator.cache_free("DataObject", obj);
        }

        auto final_end = std::chrono::high_resolution_clock::now();

        auto alloc_time = std::chrono::duration_cast<std::chrono::milliseconds>(batch_end - start);
        auto free_time = std::chrono::duration_cast<std::chrono::milliseconds>(final_end - batch_end);

        std::cout << "Batch allocated " << BATCH_SIZE << " objects in " << alloc_time.count() << "ms\n";
        std::cout << "Freed " << KEEP_COUNT << " persistent objects in " << free_time.count() << "ms\n";
        std::cout << "Immediately freed " << (BATCH_SIZE - KEEP_COUNT) << " objects during allocation\n\n";
    }

    std::cout << "=== Test 5: High Frequency Allocation Pattern ===\n";
    {
        const int CYCLES = 1000;
        const int ALLOCS_PER_CYCLE = 100;

        auto start = std::chrono::high_resolution_clock::now();

        for (int cycle = 0; cycle < CYCLES; cycle++)
        {
            std::vector<DataObject *> cycle_objects;
            cycle_objects.reserve(ALLOCS_PER_CYCLE);

            // Allocate many objects in this cycle
            for (int i = 0; i < ALLOCS_PER_CYCLE; i++)
            {
                DataObject *obj = static_cast<DataObject *>(allocator.cache_alloc("DataObject"));
                obj->id = cycle * 1000 + i;
                cycle_objects.push_back(obj);
            }

            // Free all objects in this cycle
            for (DataObject *obj : cycle_objects)
            {
                allocator.cache_free("DataObject", obj);
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        std::cout << "Completed " << CYCLES << " cycles of " << ALLOCS_PER_CYCLE
                  << " allocations each in " << total_time.count() << "ms\n";
        std::cout << "Total operations: " << (CYCLES * ALLOCS_PER_CYCLE * 2) << "\n";
        std::cout << "Operations per second: "
                  << (CYCLES * ALLOCS_PER_CYCLE * 2 * 1000LL / total_time.count()) << "\n\n";
    }

    // Cleanup
    allocator.cache_destroy("DataObject");
    allocator.cache_destroy("IntArray");
    allocator.cache_destroy("DoubleArray");

    std::cout << "=== All performance tests completed ===\n";
    return 0;
}