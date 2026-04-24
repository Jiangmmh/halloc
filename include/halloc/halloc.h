#pragma once

#include <cstddef>
#include <cstdint>

namespace halloc {

// Large allocation threshold
constexpr size_t LARGE_THRESHOLD = 32768;

// Minimum alignment
constexpr size_t MIN_ALIGNMENT = 16;

// API entry points
void* allocate(size_t size);
void deallocate(void* ptr, size_t size);
void* allocate_aligned(size_t size, size_t alignment);
void deallocate_aligned(void* ptr, size_t size, size_t alignment);
size_t usable_size(void* ptr);

// Statistics snapshot
struct Stats {
    uint64_t local_hit;
    uint64_t local_miss;
    uint64_t refill;
    uint64_t drain;
    uint64_t remote_enqueue;
    uint64_t remote_drain;
    uint64_t spans_empty;
    uint64_t spans_partial;
    uint64_t spans_full;
    uint64_t cached_bytes;
    uint64_t reserved_bytes;
    uint64_t live_requested_bytes;
    uint64_t large_run_bytes;
    uint64_t returned_to_os_bytes;
};

Stats stats_snapshot();

} // namespace halloc