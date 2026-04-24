#include <halloc/page_heap.h>
#include <halloc/os.h>
#include <halloc/size_class.h>
#include <algorithm>

namespace halloc {

static PageHeap* g_page_heap = nullptr;

PageHeap::PageHeap() {
    // Initialize pagemap with a reasonable base address range
    // The pagemap covers PAGEMAP_ENTRIES * (1 << PAGEMAP_LEVEL_BITS) bytes
    // starting from base_addr = 0, covering the entire user address space
    // in a single-level lookup
    uintptr_t base = 0;
    size_t size = static_cast<size_t>(PAGEMAP_ENTRIES) << PAGEMAP_LEVEL_BITS;
    pagemap_ = create_pagemap(base, size);
}
PageHeap::~PageHeap() {
    if (pagemap_) {
        destroy_pagemap(pagemap_);
        pagemap_ = nullptr;
    }
    for (auto& kv : free_spans_) {
        for (Span* s : kv.second) {
            if (s->memory) {
                os::unmap_pages(s->memory, s->page_count * os::get_page_size());
            }
            delete s;
        }
    }
    for (Span* s : large_spans_) {
        if (s->memory) {
            os::unmap_pages(s->memory, s->page_count * os::get_page_size());
        }
        delete s;
    }
}

Span* PageHeap::allocate_span(size_t page_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = free_spans_.find(page_count);
    if (it != free_spans_.end() && !it->second.empty()) {
        Span* span = it->second.back();
        it->second.pop_back();
        span->state = SpanState::Partial;
        return span;
    }
    
    size_t actual_pages = page_count;
    while (actual_pages < 32 && free_spans_.count(actual_pages + 1)) {
        actual_pages++;
    }
    
    Span* span = new Span();
    span->page_count = actual_pages;
    span->capacity = actual_pages * os::get_page_size();
    span->memory = os::map_pages(span->capacity);
    
    if (!span->memory) {
        delete span;
        return nullptr;
    }
    
    span->size_class = SIZE_CLASS_COUNT;
    span->live_count = 0;
    span->local_free_count = 0;
    span->state = SpanState::Partial;
    span->is_large = false;
    span->next = nullptr;
    span->remote_free_head = nullptr;
    
    total_reserved_ += span->capacity;
    
    if (actual_pages > page_count) {
        Span* remainder = new Span();
        remainder->page_count = actual_pages - page_count;
        remainder->capacity = remainder->page_count * os::get_page_size();
        remainder->memory = static_cast<char*>(span->memory) + (page_count * os::get_page_size());
        remainder->size_class = SIZE_CLASS_COUNT;
        remainder->live_count = 0;
        remainder->local_free_count = 0;
        remainder->state = SpanState::Empty;
        remainder->is_large = false;
        remainder->next = nullptr;
        remainder->remote_free_head = nullptr;
        
        free_spans_[remainder->page_count].push_back(remainder);
    }
    
    return span;
}

void PageHeap::deallocate_span(Span* span) {
    if (!span) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    span->state = SpanState::Empty;
    total_reserved_ -= span->capacity;
    
    if (span->memory) {
        os::unmap_pages(span->memory, span->capacity);
    }
    
    delete span;
}

Span* PageHeap::allocate_large(size_t size) {
    size_t page_count = (size + os::get_page_size() - 1) / os::get_page_size();
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto it = large_spans_.begin(); it != large_spans_.end(); ++it) {
        if ((*it)->page_count >= page_count) {
            Span* span = *it;
            large_spans_.erase(it);
            
            span->state = SpanState::Partial;
            return span;
        }
    }
    
    Span* span = new Span();
    span->page_count = page_count;
    span->capacity = page_count * os::get_page_size();
    span->memory = os::map_pages(span->capacity);
    
    if (!span->memory) {
        delete span;
        return nullptr;
    }
    
    span->size_class = SIZE_CLASS_COUNT;
    span->live_count = 0;
    span->local_free_count = 0;
    span->state = SpanState::Partial;
    span->is_large = true;
    span->next = nullptr;
    span->remote_free_head = nullptr;
    
    total_large_ += span->capacity;
    total_reserved_ += span->capacity;
    
    return span;
}

void PageHeap::deallocate_large(Span* span) {
    if (!span || !span->is_large) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    total_large_ -= span->capacity;
    total_reserved_ -= span->capacity;
    
    if (span->memory) {
        os::unmap_pages(span->memory, span->capacity);
    }
    
    delete span;
}

void PageHeap::register_span_owner(Span* span, uint64_t thread_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    span_to_owner_[span] = thread_id;
    span->owner_thread_id = thread_id;
}

uint64_t PageHeap::get_span_owner(Span* span) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = span_to_owner_.find(span);
    if (it != span_to_owner_.end()) {
        return it->second;
    }
    return 0;
}

void PageHeap::record_remote_free(Span* span) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (span->remote_free_count.load(std::memory_order_relaxed) > 0) {
        return;
    }

    span->remote_free_count.store(1, std::memory_order_relaxed);
    span->next = remote_free_list_;
    remote_free_list_ = span;
}

Span* PageHeap::drain_remote_frees(uint64_t thread_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    Span* result = nullptr;
    Span** tail = &result;

    Span* prev = nullptr;
    Span* curr = remote_free_list_;

    while (curr) {
        Span* next = curr->next;

        auto it = span_to_owner_.find(curr);
        uint64_t owner = (it != span_to_owner_.end()) ? it->second : 0;

        if (owner == thread_id) {
            if (prev) {
                prev->next = next;
            } else {
                remote_free_list_ = next;
            }

            curr->remote_free_count.store(0, std::memory_order_relaxed);
            curr->next = nullptr;
            *tail = curr;
            tail = &curr->next;
        } else {
            prev = curr;
        }

        curr = next;
    }

    return result;
}

Span* PageHeap::pagemap_lookup(void* ptr) const {
    return halloc::pagemap_lookup(pagemap_, ptr);
}

PageHeap* get_page_heap() {
    if (!g_page_heap) {
        g_page_heap = new PageHeap();
    }
    return g_page_heap;
}

} // namespace halloc