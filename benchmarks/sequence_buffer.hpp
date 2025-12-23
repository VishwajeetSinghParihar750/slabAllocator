#pragma once
#include <vector>
#include <cstdint>
#include <cassert>
#include <cstring> // for memset

// A Lock-Free(ish) Circular Buffer for Sequence Tracking
template <typename T>
class SequenceBuffer
{
public:
    struct Entry
    {
        uint32_t sequence; // Absolute sequence number (handle wrapping logic externally)
        bool exists;       // Is this slot occupied?
        T *data;           // Pointer to your slab-allocated object
    };

private:
    std::vector<Entry> buffer;
    uint32_t size;
    uint32_t mask; // Optimization: use bitwise AND instead of modulo

public:
    // Size MUST be a power of 2 (e.g., 1024, 65536)
    SequenceBuffer(uint32_t buffer_size = 65536) : size(buffer_size)
    {
        // Enforce Power of 2 for speed
        assert((buffer_size & (buffer_size - 1)) == 0 && "Buffer size must be power of 2");
        mask = buffer_size - 1;

        buffer.resize(size);
        reset();
    }

    void reset()
    {
        for (auto &entry : buffer)
        {
            entry.exists = false;
            entry.data = nullptr;
            entry.sequence = 0xFFFFFFFF; // Sentinel
        }
    }
    uint32_t generate_ack_bitfield(uint16_t baseline) const
    {
        uint32_t bitfield = 0;

        // Check the previous 32 packets relative to 'baseline'
        for (int i = 1; i <= 32; ++i)
        {
            uint16_t seq = baseline - i; // Implicit uint16_t wrap-around handles the math
        }
        return bitfield;
    }

    // 1. INSERT: Attempt to reserve a slot
    // Returns: T* if duplicate/occupied, nullptr if success
    T *insert(uint16_t sequence)
    {
        uint32_t index = sequence & mask;

        // Check if this specific sequence is already here (Duplicate)
        if (buffer[index].exists && (uint16_t)buffer[index].sequence == sequence)
        {
            return buffer[index].data;
        }

        // If occupied by an OLD sequence, we assume the window logic
        // has already processed it. We perform a "Lazy Overwrite".

        buffer[index].sequence = sequence;
        buffer[index].exists = true;
        buffer[index].data = nullptr; // Clear old pointer

        return nullptr; // Success, slot reserved
    }

    // 2. STORE: Save the slab pointer
    void store(uint16_t sequence, T *packet_data)
    {
        uint32_t index = sequence & mask;
        // Verify we are storing to the correct reserved slot
        if (buffer[index].exists && (uint16_t)buffer[index].sequence == sequence)
        {
            buffer[index].data = packet_data;
        }
    }

    // 3. REMOVE: Extract pointer and clear slot
    T *remove(uint16_t sequence)
    {
        uint32_t index = sequence & mask;
        if (buffer[index].exists && (uint16_t)buffer[index].sequence == sequence)
        {
            buffer[index].exists = false;
            return buffer[index].data;
        }
        return nullptr;
    }

    // 4. FIND: Peek without removing
    T *find(uint16_t sequence) const
    {
        uint32_t index = sequence & mask;
        if (buffer[index].exists && (uint16_t)buffer[index].sequence == sequence)
            return buffer[index].data;
        return nullptr;
    }

    uint32_t get_size() const { return size; }
};