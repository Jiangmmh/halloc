#pragma once

#include <atomic>
#include <cstdint>

namespace halloc {

struct GlobalStats {
    std::atomic<uint64_t> local_hit{0};
    std::atomic<uint64_t> local_miss{0};
    std::atomic<uint64_t> refill{0};
    std::atomic<uint64_t> drain{0};
    std::atomic<uint64_t> remote_enqueue{0};
    std::atomic<uint64_t> remote_drain{0};
    std::atomic<uint64_t> spans_empty{0};
    std::atomic<uint64_t> spans_partial{0};
    std::atomic<uint64_t> spans_full{0};
    std::atomic<uint64_t> reserved_bytes{0};
    std::atomic<uint64_t> large_run_bytes{0};
    std::atomic<uint64_t> returned_to_os_bytes{0};
};

GlobalStats* get_global_stats();

} // namespace halloc