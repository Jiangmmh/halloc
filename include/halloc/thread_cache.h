#pragma once

#include <halloc/size_class.h>
#include <halloc/span.h>
#include <halloc/os.h>
#include <vector>
#include <array>

namespace halloc {

struct ThreadCache {
    static ThreadCache* get();
    
    void* allocate(size_t size);
    void deallocate(void* ptr, size_t size);
    void drain();
    
    size_t cached_bytes() const { return cached_bytes_; }
    
private:
    ThreadCache();
    ~ThreadCache();
    
    std::array<void*, SIZE_CLASS_COUNT> free_lists_;
    std::array<size_t, SIZE_CLASS_COUNT> free_counts_;
    size_t cached_bytes_ = 0;
};

} // namespace halloc