#pragma once

#include <halloc/span.h>
#include <halloc/size_class.h>
#include <mutex>
#include <vector>
#include <map>

namespace halloc {

class PageHeap {
public:
    PageHeap();
    ~PageHeap();

    Span* allocate_span(size_t page_count);
    void deallocate_span(Span* span);
    Span* allocate_large(size_t size);
    void deallocate_large(Span* span);

    size_t total_reserved() const { return total_reserved_; }
    size_t total_large() const { return total_large_; }

private:
    std::mutex mutex_;
    std::map<size_t, std::vector<Span*>> free_spans_;
    std::vector<Span*> large_spans_;
    size_t total_reserved_ = 0;
    size_t total_large_ = 0;
};

PageHeap* get_page_heap();

} // namespace halloc