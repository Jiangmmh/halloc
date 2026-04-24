#include <halloc/thread_cache.h>
#include <halloc/page_heap.h>
#include <cstdlib>
#include <cstring>

namespace halloc {

thread_local ThreadCache* g_thread_cache = nullptr;

ThreadCache::ThreadCache() = default;
ThreadCache::~ThreadCache() {
    for (size_t i = 0; i < SIZE_CLASS_COUNT; ++i) {
        if (free_lists_[i]) {
            Span* span = get_page_heap()->allocate_span(4);
            if (span) {
                span->size_class = i;
                get_page_heap()->deallocate_span(span);
            }
        }
    }
}

ThreadCache* ThreadCache::get() {
    if (!g_thread_cache) {
        g_thread_cache = new ThreadCache();
    }
    return g_thread_cache;
}

void* ThreadCache::allocate(size_t size) {
    if (size == 0) return nullptr;
    if (is_large(size)) return get_page_heap()->allocate_large(size);
    
    size_t class_idx = get_size_class(size);
    if (class_idx >= SIZE_CLASS_COUNT) return nullptr;
    
    if (free_counts_[class_idx] > 0) {
        void* ptr = free_lists_[class_idx];
        free_lists_[class_idx] = *static_cast<void**>(ptr);
        free_counts_[class_idx]--;
        cached_bytes_ -= SIZE_CLASSES[class_idx].size;
        return ptr;
    }
    
    size_t batch_size = get_batch_size(class_idx);
    size_t span_pages = SIZE_CLASSES[class_idx].size * batch_size / os::get_page_size();
    if (span_pages < 4) span_pages = 4;
    
    Span* span = get_page_heap()->allocate_span(span_pages);
    if (!span) return nullptr;
    
    char* memory = static_cast<char*>(span->memory);
    for (size_t i = 0; i < batch_size - 1; ++i) {
        void* ptr = memory + (i * SIZE_CLASSES[class_idx].size);
        *static_cast<void**>(ptr) = memory + ((i + 1) * SIZE_CLASSES[class_idx].size);
    }
    free_lists_[class_idx] = memory;
    free_counts_[class_idx] = batch_size - 1;
    cached_bytes_ += (batch_size - 1) * SIZE_CLASSES[class_idx].size;
    
    return memory + ((batch_size - 1) * SIZE_CLASSES[class_idx].size);
}

void ThreadCache::deallocate(void* ptr, size_t size) {
    if (!ptr || size == 0) return;
    if (is_large(size)) {
        get_page_heap()->deallocate_large(reinterpret_cast<Span**>(ptr)[-1]);
        return;
    }
    
    size_t class_idx = get_size_class(size);
    if (class_idx >= SIZE_CLASS_COUNT) return;
    
    *static_cast<void**>(ptr) = free_lists_[class_idx];
    free_lists_[class_idx] = ptr;
    free_counts_[class_idx]++;
    cached_bytes_ += SIZE_CLASSES[class_idx].size;
    
    if (cached_bytes_ > THREAD_CACHE_SOFT_CAP) {
        drain();
    }
}

void ThreadCache::drain() {
    size_t target = THREAD_CACHE_HARD_CAP;
    if (cached_bytes_ <= target) return;
    
    for (size_t i = 0; i < SIZE_CLASS_COUNT && cached_bytes_ > target; ++i) {
        while (free_counts_[i] > 0 && cached_bytes_ > target) {
            void* ptr = free_lists_[i];
            free_lists_[i] = *static_cast<void**>(ptr);
            free_counts_[i]--;
            cached_bytes_ -= SIZE_CLASSES[i].size;
        }
    }
}

} // namespace halloc