#include <halloc/halloc.h>
#include <halloc/thread_cache.h>

namespace halloc {

void* allocate(size_t size) {
    if (size == 0) {
        return nullptr;
    }
    return ThreadCache::get()->allocate(size);
}

void deallocate(void* ptr, size_t size) {
    if (!ptr) return;
    ThreadCache::get()->deallocate(ptr, size);
}

void* allocate_aligned(size_t size, size_t alignment) {
    if (size == 0) {
        return nullptr;
    }
    if (alignment < MIN_ALIGNMENT || (alignment & (alignment - 1)) != 0) {
        return nullptr;
    }
    void* ptr = ThreadCache::get()->allocate(size);
    if (ptr && (reinterpret_cast<uintptr_t>(ptr) & (alignment - 1)) != 0) {
        deallocate(ptr, size);
        return nullptr;
    }
    return ptr;
}

void deallocate_aligned(void* ptr, size_t size, size_t) {
    deallocate(ptr, size);
}

size_t usable_size(void* ptr) {
    return ptr ? 1 : 0;
}

Stats stats_snapshot() {
    Stats s = {};
    s.cached_bytes = ThreadCache::get()->cached_bytes();
    return s;
}

} // namespace halloc