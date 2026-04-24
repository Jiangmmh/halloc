#include <halloc/stats.h>
#include <halloc/page_heap.h>
#include <halloc/thread_cache.h>

namespace halloc {

GlobalStats* get_global_stats() {
    static GlobalStats stats;
    return &stats;
}

} // namespace halloc