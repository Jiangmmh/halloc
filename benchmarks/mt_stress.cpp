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
    
    return 0;
}