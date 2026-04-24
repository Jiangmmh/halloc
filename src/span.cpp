#include <halloc/span.h>
#include <halloc/os.h>
#include <cstdlib>
#include <cstring>

namespace halloc {

PageMap* create_pagemap(uintptr_t base, size_t size) {
    PageMap* pm = static_cast<PageMap*>(os::map_pages(sizeof(PageMap)));
    if (!pm) return nullptr;
    std::memset(pm, 0, sizeof(PageMap));
    pm->base_addr = base;
    return pm;
}

void destroy_pagemap(PageMap* pm) {
    if (pm) {
        os::unmap_pages(pm, sizeof(PageMap));
    }
}

Span* pagemap_lookup(PageMap* pm, void* ptr) {
    if (!pm || !ptr) return nullptr;
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t base = pm->base_addr;
    if (addr < base) return nullptr;
    
    size_t idx = (addr - base) >> PAGEMAP_LEVEL_BITS;
    if (idx >= PAGEMAP_ENTRIES) return nullptr;
    return pm->entries[idx];
}

void pagemap_insert(PageMap* pm, void* page_start, Span* span) {
    if (!pm || !page_start || !span) return;
    uintptr_t addr = reinterpret_cast<uintptr_t>(page_start);
    uintptr_t base = pm->base_addr;
    if (addr < base) return;
    
    size_t idx = (addr - base) >> PAGEMAP_LEVEL_BITS;
    if (idx < PAGEMAP_ENTRIES) {
        pm->entries[idx] = span;
    }
}

} // namespace halloc