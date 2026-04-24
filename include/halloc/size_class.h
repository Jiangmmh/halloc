#pragma once

#include <cstddef>
#include <algorithm>

namespace halloc {

struct SizeClass {
    size_t size;
    size_t batch_size;
    size_t class_idx;
};

constexpr size_t SIZE_CLASS_COUNT = 36;
constexpr size_t MAX_SMALL_SIZE = 32768;

constexpr SizeClass SIZE_CLASSES[SIZE_CLASS_COUNT] = {
    {16, 512, 0},
    {32, 256, 1},
    {48, 170, 2},
    {64, 128, 3},
    {80, 102, 4},
    {96, 85, 5},
    {112, 73, 6},
    {128, 64, 7},
    {144, 56, 8},
    {160, 51, 9},
    {176, 46, 10},
    {192, 42, 11},
    {208, 39, 12},
    {224, 36, 13},
    {240, 34, 14},
    {256, 32, 15},
    {320, 25, 16},
    {384, 21, 17},
    {448, 18, 18},
    {512, 16, 19},
    {640, 12, 20},
    {768, 10, 21},
    {896, 9, 22},
    {1024, 8, 23},
    {1280, 6, 24},
    {1536, 5, 25},
    {1792, 4, 26},
    {2048, 4, 27},
    {3072, 2, 28},
    {4096, 2, 29},
    {6144, 1, 30},
    {8192, 1, 31},
    {12288, 1, 32},
    {16384, 1, 33},
    {24576, 1, 34},
    {32768, 1, 35}
};

constexpr size_t MIN_ALIGN = 16;
constexpr size_t LARGE_CUTOFF = 32768;
constexpr size_t MAX_REFILL_BATCH = 64;
constexpr size_t MIN_REFILL_BATCH = 8;

constexpr size_t PER_CLASS_CAP_SMALL = 2;
constexpr size_t PER_CLASS_CAP_LARGE = 1;

constexpr size_t THREAD_CACHE_SOFT_CAP = 262144;
constexpr size_t THREAD_CACHE_HARD_CAP = 131072;

inline size_t get_size_class(size_t size) {
    if (size == 0) return SIZE_CLASS_COUNT;
    for (size_t i = 0; i < SIZE_CLASS_COUNT; ++i) {
        if (size <= SIZE_CLASSES[i].size) return i;
    }
    return SIZE_CLASS_COUNT;
}

inline size_t round_up_size(size_t size) {
    size_t idx = get_size_class(size);
    if (idx == SIZE_CLASS_COUNT) return size;
    return SIZE_CLASSES[idx].size;
}

inline size_t get_batch_size(size_t class_idx) {
    if (class_idx >= SIZE_CLASS_COUNT) return MIN_REFILL_BATCH;
    return SIZE_CLASSES[class_idx].batch_size;
}

inline bool is_large(size_t size) {
    return size > LARGE_CUTOFF;
}

} // namespace halloc