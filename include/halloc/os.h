#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

namespace halloc {
namespace os {

using page_size_t = size_t;

page_size_t get_page_size();

void* map_pages(size_t size);
void unmap_pages(void* ptr, size_t size);

enum class AdviseMode {
    Normal,
    Free,
    DontNeed,
    Unsupported
};

AdviseMode advise(void* ptr, size_t size, AdviseMode mode);

bool supports_advise();

} // namespace os
} // namespace halloc