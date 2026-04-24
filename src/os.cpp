#include <halloc/os.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>

namespace halloc {
namespace os {

page_size_t get_page_size() {
    long rc = sysconf(_SC_PAGESIZE);
    return static_cast<page_size_t>(rc);
}

void* map_pages(size_t size) {
    void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        return nullptr;
    }
    return ptr;
}

void unmap_pages(void* ptr, size_t size) {
    if (ptr && size > 0) {
        munmap(ptr, size);
    }
}

AdviseMode advise(void* ptr, size_t size, AdviseMode mode) {
#ifdef __APPLE__
    int advice = MADV_FREE;
    if (madvise(ptr, size, advice) == 0) {
        return AdviseMode::Free;
    }
    return AdviseMode::Unsupported;
#else
    int advice = 0;
    switch (mode) {
        case AdviseMode::Free:
            advice = MADV_FREE;
            break;
        case AdviseMode::DontNeed:
            advice = MADV_DONTNEED;
            break;
        default:
            return AdviseMode::Unsupported;
    }
    if (madvise(ptr, size, advice) == 0) {
        if (mode == AdviseMode::Free) return AdviseMode::Free;
        if (mode == AdviseMode::DontNeed) return AdviseMode::DontNeed;
    }
    return AdviseMode::Unsupported;
#endif
}

bool supports_advise() {
    return true;
}

} // namespace os
} // namespace halloc