#include <halloc/halloc.h>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <cstdlib>

using namespace std;
using namespace std::chrono;

struct Result {
    int threads;
    double total_ops;
    double ns_per_op;
    double mops_per_sec;
};

Result run_alloc_threads(int threads, int ops_per_thread) {
    atomic<bool> start{false};
    atomic<uint64_t> total{0};
    vector<thread> workers;
    
    auto workload = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        uint64_t local = 0;
        for (int i = 0; i < ops_per_thread; ++i) {
            void* p = halloc::allocate(64);
            local++;
        }
        total.fetch_add(local, memory_order_relaxed);
    };
    
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back(workload);
    }
    
    start.store(true, memory_order_release);
    auto t0 = high_resolution_clock::now();
    
    for (auto& w : workers) {
        w.join();
    }
    
    auto t1 = high_resolution_clock::now();
    double ms = duration<double>(t1 - t0).count() * 1000.0;
    double ops = total.load(memory_order_relaxed);
    
    return {threads, ops, ms * 1e6 / ops, ops / ms / 1e6};
}

Result run_alloc_free_threads(int threads, int ops_per_thread) {
    atomic<bool> start{false};
    atomic<uint64_t> total{0};
    vector<thread> workers;
    
    auto workload = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        uint64_t local = 0;
        for (int i = 0; i < ops_per_thread; ++i) {
            void* p = halloc::allocate(64);
            halloc::deallocate(p, 64);
            local += 2;
        }
        total.fetch_add(local, memory_order_relaxed);
    };
    
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back(workload);
    }
    
    start.store(true, memory_order_release);
    auto t0 = high_resolution_clock::now();
    
    for (auto& w : workers) {
        w.join();
    }
    
    auto t1 = high_resolution_clock::now();
    double ms = duration<double>(t1 - t0).count() * 1000.0;
    double ops = total.load(memory_order_relaxed);
    
    return {threads, ops, ms * 1e6 / ops, ops / ms / 1e6};
}

Result run_system_alloc_threads(int threads, int ops_per_thread) {
    atomic<bool> start{false};
    atomic<uint64_t> total{0};
    vector<thread> workers;
    
    auto workload = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        uint64_t local = 0;
        for (int i = 0; i < ops_per_thread; ++i) {
            void* p = ::operator new(64);
            local++;
        }
        total.fetch_add(local, memory_order_relaxed);
    };
    
    for (int t = 0; t < threads; ++t) {
        workers.emplace_back(workload);
    }
    
    start.store(true, memory_order_release);
    auto t0 = high_resolution_clock::now();
    
    for (auto& w : workers) {
        w.join();
    }
    
    auto t1 = high_resolution_clock::now();
    double ms = duration<double>(t1 - t0).count() * 1000.0;
    double ops = total.load(memory_order_relaxed);
    
    return {threads, ops, ms * 1e6 / ops, ops / ms / 1e6};
}

// ─── Cross-thread free tests ────────────────────────────────────────────────

struct CrossResult {
    const char* name;
    int alloc_threads;
    int free_threads;
    double total_ops;
    double ms;
    double ops_per_sec;
};

CrossResult test_producer_consumer(int alloc_threads, int free_threads) {
    const int allocs_per_producer = 10000;
    const int total_allocs = allocs_per_producer * alloc_threads;
    const int frees_per_consumer = total_allocs / free_threads;

    atomic<bool> start{false};
    vector<void*> shared_ptrs(total_allocs);
    atomic<int> alloc_idx{0};
    atomic<int> free_idx{0};

    // Producer: allocate and store pointers
    auto producer = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        int idx;
        while ((idx = alloc_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            shared_ptrs[idx] = halloc::allocate(64);
        }
    };

    // Consumer: free pointers from the array
    auto consumer = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        int idx;
        while ((idx = free_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            halloc::deallocate(shared_ptrs[idx], 64);
        }
    };

    vector<thread> producers;
    for (int t = 0; t < alloc_threads; ++t)
        producers.emplace_back(producer);

    vector<thread> consumers;
    for (int t = 0; t < free_threads; ++t)
        consumers.emplace_back(consumer);

    start.store(true, memory_order_release);
    auto t0 = high_resolution_clock::now();

    for (auto& w : producers) w.join();
    for (auto& w : consumers) w.join();

    auto t1 = high_resolution_clock::now();
    double ms = duration<double>(t1 - t0).count() * 1000.0;

    return {"producer_consumer", alloc_threads, free_threads,
            (double)total_allocs * 2, ms, (double)total_allocs * 2 / ms * 1000.0};
}

CrossResult test_fan_out(int alloc_threads, int free_threads) {
    const int allocs_per_producer = 2000;
    const int total_allocs = allocs_per_producer * alloc_threads;
    const int frees_per_consumer = total_allocs / free_threads;

    atomic<bool> start{false};
    vector<void*> shared_ptrs(total_allocs);
    atomic<int> alloc_idx{0};
    atomic<int> free_idx{0};

    auto producer = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        int idx;
        while ((idx = alloc_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            shared_ptrs[idx] = halloc::allocate(64);
        }
    };

    auto consumer = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        int idx;
        while ((idx = free_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            halloc::deallocate(shared_ptrs[idx], 64);
        }
    };

    vector<thread> producers;
    for (int t = 0; t < alloc_threads; ++t)
        producers.emplace_back(producer);

    vector<thread> consumers;
    for (int t = 0; t < free_threads; ++t)
        consumers.emplace_back(consumer);

    start.store(true, memory_order_release);
    auto t0 = high_resolution_clock::now();

    for (auto& w : producers) w.join();
    for (auto& w : consumers) w.join();

    auto t1 = high_resolution_clock::now();
    double ms = duration<double>(t1 - t0).count() * 1000.0;

    return {"fan_out", alloc_threads, free_threads,
            (double)total_allocs * 2, ms, (double)total_allocs * 2 / ms * 1000.0};
}

CrossResult test_fan_in(int alloc_threads, int free_threads) {
    const int allocs_per_producer = 8000;
    const int total_allocs = allocs_per_producer * alloc_threads;
    const int frees_per_consumer = total_allocs / free_threads;

    atomic<bool> start{false};
    vector<void*> shared_ptrs(total_allocs);
    atomic<int> alloc_idx{0};
    atomic<int> free_idx{0};

    auto producer = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        int idx;
        while ((idx = alloc_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            shared_ptrs[idx] = halloc::allocate(64);
        }
    };

    auto consumer = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        int idx;
        while ((idx = free_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            halloc::deallocate(shared_ptrs[idx], 64);
        }
    };

    vector<thread> producers;
    for (int t = 0; t < alloc_threads; ++t)
        producers.emplace_back(producer);

    vector<thread> consumers;
    for (int t = 0; t < free_threads; ++t)
        consumers.emplace_back(consumer);

    start.store(true, memory_order_release);
    auto t0 = high_resolution_clock::now();

    for (auto& w : producers) w.join();
    for (auto& w : consumers) w.join();

    auto t1 = high_resolution_clock::now();
    double ms = duration<double>(t1 - t0).count() * 1000.0;

    return {"fan_in", alloc_threads, free_threads,
            (double)total_allocs * 2, ms, (double)total_allocs * 2 / ms * 1000.0};
}

// ─── Extended cross-thread tests ─────────────────────────────────────────────

CrossResult test_producer_consumer_balanced(int alloc_threads, int free_threads) {
    const int allocs_per_producer = 10000;
    const int total_allocs = allocs_per_producer * alloc_threads;

    atomic<bool> start{false};
    vector<void*> shared_ptrs(total_allocs);
    atomic<int> alloc_idx{0};
    atomic<int> free_idx{0};

    auto producer = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        int idx;
        while ((idx = alloc_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            shared_ptrs[idx] = halloc::allocate(64);
        }
    };

    auto consumer = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        int idx;
        while ((idx = free_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            halloc::deallocate(shared_ptrs[idx], 64);
        }
    };

    vector<thread> producers;
    for (int t = 0; t < alloc_threads; ++t)
        producers.emplace_back(producer);

    vector<thread> consumers;
    for (int t = 0; t < free_threads; ++t)
        consumers.emplace_back(consumer);

    start.store(true, memory_order_release);
    auto t0 = high_resolution_clock::now();

    for (auto& w : producers) w.join();
    for (auto& w : consumers) w.join();

    auto t1 = high_resolution_clock::now();
    double ms = duration<double>(t1 - t0).count() * 1000.0;

    char buf[64];
    snprintf(buf, sizeof(buf), "producer_consumer(%d,%d)", alloc_threads, free_threads);

    return {strdup(buf), alloc_threads, free_threads,
            (double)total_allocs * 2, ms, (double)total_allocs * 2 / ms * 1000.0};
}

CrossResult test_fan_out_extreme(int alloc_threads, int free_threads) {
    const int allocs_per_producer = 2000;
    const int total_allocs = allocs_per_producer * alloc_threads;

    atomic<bool> start{false};
    vector<void*> shared_ptrs(total_allocs);
    atomic<int> alloc_idx{0};
    atomic<int> free_idx{0};

    auto producer = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        int idx;
        while ((idx = alloc_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            shared_ptrs[idx] = halloc::allocate(64);
        }
    };

    auto consumer = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        int idx;
        while ((idx = free_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            halloc::deallocate(shared_ptrs[idx], 64);
        }
    };

    vector<thread> producers;
    for (int t = 0; t < alloc_threads; ++t)
        producers.emplace_back(producer);

    vector<thread> consumers;
    for (int t = 0; t < free_threads; ++t)
        consumers.emplace_back(consumer);

    start.store(true, memory_order_release);
    auto t0 = high_resolution_clock::now();

    for (auto& w : producers) w.join();
    for (auto& w : consumers) w.join();

    auto t1 = high_resolution_clock::now();
    double ms = duration<double>(t1 - t0).count() * 1000.0;

    char buf[64];
    snprintf(buf, sizeof(buf), "fan_out_extreme(%d,%d)", alloc_threads, free_threads);

    return {strdup(buf), alloc_threads, free_threads,
            (double)total_allocs * 2, ms, (double)total_allocs * 2 / ms * 1000.0};
}

CrossResult test_fan_in_extreme(int alloc_threads, int free_threads) {
    const int allocs_per_producer = 8000;
    const int total_allocs = allocs_per_producer * alloc_threads;

    atomic<bool> start{false};
    vector<void*> shared_ptrs(total_allocs);
    atomic<int> alloc_idx{0};
    atomic<int> free_idx{0};

    auto producer = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        int idx;
        while ((idx = alloc_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            shared_ptrs[idx] = halloc::allocate(64);
        }
    };

    auto consumer = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        int idx;
        while ((idx = free_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            halloc::deallocate(shared_ptrs[idx], 64);
        }
    };

    vector<thread> producers;
    for (int t = 0; t < alloc_threads; ++t)
        producers.emplace_back(producer);

    vector<thread> consumers;
    for (int t = 0; t < free_threads; ++t)
        consumers.emplace_back(consumer);

    start.store(true, memory_order_release);
    auto t0 = high_resolution_clock::now();

    for (auto& w : producers) w.join();
    for (auto& w : consumers) w.join();

    auto t1 = high_resolution_clock::now();
    double ms = duration<double>(t1 - t0).count() * 1000.0;

    char buf[64];
    snprintf(buf, sizeof(buf), "fan_in_extreme(%d,%d)", alloc_threads, free_threads);

    return {strdup(buf), alloc_threads, free_threads,
            (double)total_allocs * 2, ms, (double)total_allocs * 2 / ms * 1000.0};
}

CrossResult test_mixed_size_free() {
    const int sizes[] = {16, 32, 64, 128, 256};
    const int num_sizes = 5;
    const int allocs_per_size = 2000;
    const int total_allocs = num_sizes * allocs_per_size;

    atomic<bool> start{false};
    vector<void*> shared_ptrs(total_allocs);
    vector<int> alloc_sizes(total_allocs);
    atomic<int> alloc_idx{0};
    atomic<int> free_idx{0};

    auto producer = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        int idx;
        while ((idx = alloc_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            int sz = sizes[idx % num_sizes];
            shared_ptrs[idx] = halloc::allocate(sz);
            alloc_sizes[idx] = sz;
        }
    };

    auto consumer = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        int idx;
        while ((idx = free_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            halloc::deallocate(shared_ptrs[idx], alloc_sizes[idx]);
        }
    };

    vector<thread> producers;
    producers.emplace_back(producer);

    vector<thread> consumers;
    consumers.emplace_back(consumer);

    start.store(true, memory_order_release);
    auto t0 = high_resolution_clock::now();

    for (auto& w : producers) w.join();
    for (auto& w : consumers) w.join();

    auto t1 = high_resolution_clock::now();
    double ms = duration<double>(t1 - t0).count() * 1000.0;

    return {"mixed_size_free", 1, 1,
            (double)total_allocs * 2, ms, (double)total_allocs * 2 / ms * 1000.0};
}

CrossResult test_roundtrip() {
    const int iterations = 5000;

    atomic<bool> start{false};
    vector<void*> ptrs(iterations);
    atomic<int> step{0}; // 0=alloc, 1=free, 2=re-alloc

    auto thread_a = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        // Phase 1: allocate
        for (int i = 0; i < iterations; ++i) {
            ptrs[i] = halloc::allocate(64);
        }
        step.store(1, memory_order_release);
    };

    auto thread_b = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        // Wait for phase 1 to complete
        while (step.load(memory_order_acquire) < 1) { this_thread::yield(); }
        // Phase 2: free all
        for (int i = 0; i < iterations; ++i) {
            halloc::deallocate(ptrs[i], 64);
        }
        step.store(2, memory_order_release);
    };

    auto thread_c = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        // Wait for phase 2 to complete
        while (step.load(memory_order_acquire) < 2) { this_thread::yield(); }
        // Phase 3: re-allocate same size - should reuse
        for (int i = 0; i < iterations; ++i) {
            ptrs[i] = halloc::allocate(64);
        }
        // Free the re-allocated blocks
        for (int i = 0; i < iterations; ++i) {
            halloc::deallocate(ptrs[i], 64);
        }
    };

    vector<thread> workers;
    workers.emplace_back(thread_a);
    workers.emplace_back(thread_b);
    workers.emplace_back(thread_c);

    start.store(true, memory_order_release);
    auto t0 = high_resolution_clock::now();

    for (auto& w : workers) w.join();

    auto t1 = high_resolution_clock::now();
    double ms = duration<double>(t1 - t0).count() * 1000.0;

    // 3 phases: alloc + free + re-alloc+free = iterations * 4 ops
    double total_ops = (double)iterations * 4;
    return {"roundtrip", 1, 1,
            total_ops, ms, total_ops / ms * 1000.0};
}

CrossResult test_mixed_st() {
    const int half_ops = 5000;
    const int total_allocs = half_ops; // cross-thread allocs

    atomic<bool> start{false};
    vector<void*> shared_ptrs(total_allocs);
    atomic<int> alloc_idx{0};
    atomic<int> free_idx{0};

    // Thread A: half same-thread alloc+free, half cross-thread alloc
    auto thread_a = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        // Same-thread alloc+free
        for (int i = 0; i < half_ops; ++i) {
            void* p = halloc::allocate(64);
            halloc::deallocate(p, 64);
        }
        // Cross-thread allocs
        int idx;
        while ((idx = alloc_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            shared_ptrs[idx] = halloc::allocate(64);
        }
    };

    // Thread B: half same-thread alloc+free, half cross-thread free
    auto thread_b = [&]() {
        while (!start.load(memory_order_acquire)) { this_thread::yield(); }
        // Same-thread alloc+free
        for (int i = 0; i < half_ops; ++i) {
            void* p = halloc::allocate(64);
            halloc::deallocate(p, 64);
        }
        // Cross-thread frees
        int idx;
        while ((idx = free_idx.fetch_add(1, memory_order_relaxed)) < total_allocs) {
            halloc::deallocate(shared_ptrs[idx], 64);
        }
    };

    vector<thread> workers;
    workers.emplace_back(thread_a);
    workers.emplace_back(thread_b);

    start.store(true, memory_order_release);
    auto t0 = high_resolution_clock::now();

    for (auto& w : workers) w.join();

    auto t1 = high_resolution_clock::now();
    double ms = duration<double>(t1 - t0).count() * 1000.0;

    // Each thread: half_ops*2 same-thread ops + total_allocs cross-thread ops
    // total = 2 * (half_ops*2) + total_allocs*2 = 4*half_ops + 2*total_allocs
    double total_ops = 4.0 * half_ops + 2.0 * total_allocs;
    return {"mixed_st", 2, 2,
            total_ops, ms, total_ops / ms * 1000.0};
}

int main() {
    const int ops_per_thread = 100000;
    
    cout << "\n=== Extreme Multi-Thread Contention Test ===\n";
    cout << "=== " << ops_per_thread << " ops/thread ===\n\n";
    
    cout << "--- halloc allocate only (no free) ---\n";
    cout << "| threads |    ops | ns/op |  M/s |\n";
    cout << "|--------|-------:|------:|-----:|\n";
    
    for (int threads : {1, 2, 4, 8, 16, 32, 64, 80}) {
        auto r = run_alloc_threads(threads, ops_per_thread);
        cout << "| " << setw(6) << r.threads << " | "
            << setw(5) << (int)r.total_ops << " | "
            << setw(4) << fixed << setprecision(1) << r.ns_per_op << " | "
            << setw(4) << fixed << setprecision(2) << r.mops_per_sec << " |\n";
    }
    
    cout << "\n--- halloc allocate+free (same thread) ---\n";
    cout << "| threads |    ops | ns/op |  M/s |\n";
    cout << "|--------|-------:|------:|-----:|\n";
    
    for (int threads : {1, 2, 4, 8, 16, 32, 64, 80}) {
        auto r = run_alloc_free_threads(threads, ops_per_thread);
        cout << "| " << setw(6) << r.threads << " | "
            << setw(5) << (int)r.total_ops << " | "
            << setw(4) << fixed << setprecision(1) << r.ns_per_op << " | "
            << setw(4) << fixed << setprecision(2) << r.mops_per_sec << " |\n";
    }
    
    cout << "\n--- system malloc allocate only ---\n";
    cout << "| threads |    ops | ns/op |  M/s |\n";
    cout << "|--------|-------:|------:|-----:|\n";
    
    for (int threads : {1, 2, 4, 8, 16, 32, 64, 80}) {
        auto r = run_system_alloc_threads(threads, ops_per_thread);
        cout << "| " << setw(6) << r.threads << " | "
            << setw(5) << (int)r.total_ops << " | "
            << setw(4) << fixed << setprecision(1) << r.ns_per_op << " | "
            << setw(4) << fixed << setprecision(2) << r.mops_per_sec << " |\n";
    }
    
    cout << "\n=== Summary ===\n";
    auto r1 = run_alloc_threads(64, ops_per_thread);
    auto r2 = run_alloc_free_threads(64, ops_per_thread);
    auto r3 = run_system_alloc_threads(64, ops_per_thread);
    
    cout << "64 threads, alloc only:\n";
    cout << "  halloc:      " << fixed << setprecision(2) << r1.mops_per_sec << " M ops/sec\n";
    cout << "  system m.:   " << fixed << setprecision(2) << r3.mops_per_sec << " M ops/sec\n";
    cout << "  speedup:    " << fixed << setprecision(1) << r3.mops_per_sec / r1.mops_per_sec << "x\n";
    
    cout << "64 threads, alloc+free:\n";
    cout << "  halloc:      " << fixed << setprecision(2) << r2.mops_per_sec << " M ops/sec\n";
    
    // ─── Cross-thread free tests ──────────────────────────────────────────
    cout << "\n=== Cross-Thread Free Tests ===\n\n";
    cout << "| test             | alloc | free |     ops | time (ms) |  ops/sec |\n";
    cout << "|------------------|------:|-----:|--------:|----------:|---------:|\n";

    auto print_cross = [](const CrossResult& r) {
        cout << "| " << setw(16) << r.name << " | "
             << setw(4) << r.alloc_threads << " | "
             << setw(3) << r.free_threads << " | "
             << setw(6) << (int)r.total_ops << " | "
             << setw(8) << fixed << setprecision(2) << r.ms << " | "
             << setw(7) << fixed << setprecision(0) << r.ops_per_sec << " |\n";
    };

    print_cross(test_producer_consumer(1, 1));
    print_cross(test_fan_out(8, 1));
    print_cross(test_fan_in(1, 4));
    print_cross(test_producer_consumer_balanced(2, 2));
    print_cross(test_producer_consumer_balanced(4, 4));
    print_cross(test_fan_out_extreme(16, 1));
    print_cross(test_fan_in_extreme(1, 8));
    print_cross(test_mixed_size_free());
    print_cross(test_roundtrip());
    print_cross(test_mixed_st());
    
    return 0;
}