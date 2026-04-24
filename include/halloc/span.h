#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace halloc {

enum class SpanState : uint8_t {
    Empty = 0,
    Partial = 1,
    Full = 2
};

struct alignas(64) Span {
    uint32_t size_class;
    uint32_t page_count;
    uint32_t capacity;
    uint32_t live_count;
    uint32_t local_free_count;
    SpanState state;
    bool is_large;
    void* memory;
    Span* next;
    void* remote_free_head;
};

constexpr size_t MAX_PAGEMAP_BITS = 42;
constexpr size_t PAGEMAP_LEVEL_BITS = 21;
constexpr size_t PAGEMAP_ENTRIES = 1 << PAGEMAP_LEVEL_BITS;

struct PageMap {
    uintptr_t base_addr;
    Span* entries[PAGEMAP_ENTRIES];
};

PageMap* create_pagemap(uintptr_t base, size_t size);
void destroy_pagemap(PageMap* pm);
Span* pagemap_lookup(PageMap* pm, void* ptr);
void pagemap_insert(PageMap* pm, void* page_start, Span* span);

inline uintptr_t page_number(void* ptr, size_t page_size) {
    return reinterpret_cast<uintptr_t>(ptr) / page_size;
}

} // namespace halloc