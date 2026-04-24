#include <benchmark/benchmark.h>
#include <halloc/halloc.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <atomic>

static void BM_Allocate_SameSize(benchmark::State& state) {
    const size_t size = state.range(0);
    void* ptrs[256];
    
    for (auto _ : state) {
        for (int i = 0; i < 256; ++i) {
            ptrs[i] = halloc::allocate(size);
        }
        for (int i = 0; i < 256; ++i) {
            halloc::deallocate(ptrs[i], size);
        }
        benchmark::DoNotOptimize(ptrs);
    }
}
BENCHMARK(BM_Allocate_SameSize)->Arg(8)->Arg(32)->Arg(64)->Arg(128)->Arg(256)->Arg(1024);

static void BM_Allocate_MixedSizes(benchmark::State& state) {
    std::vector<size_t> sizes = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096};
    void* ptrs[256];
    size_t size_idx = 0;
    
    for (auto _ : state) {
        for (int i = 0; i < 256; ++i) {
            size_t size = sizes[size_idx % sizes.size()];
            ptrs[i] = halloc::allocate(size);
            size_idx++;
        }
        for (int i = 0; i < 256; ++i) {
            halloc::deallocate(ptrs[i], 64);
        }
        benchmark::DoNotOptimize(ptrs);
    }
}
BENCHMARK(BM_Allocate_MixedSizes);

static void BM_Allocate_Large(benchmark::State& state) {
    const size_t size = state.range(0);
    void* ptrs[64];
    
    for (auto _ : state) {
        for (int i = 0; i < 64; ++i) {
            ptrs[i] = halloc::allocate(size);
        }
        for (int i = 0; i < 64; ++i) {
            halloc::deallocate(ptrs[i], size);
        }
        benchmark::DoNotOptimize(ptrs);
    }
}
BENCHMARK(BM_Allocate_Large)->Arg(32768)->Arg(65536)->Arg(131072);

static void BM_System_Allocate(benchmark::State& state) {
    const size_t size = state.range(0);
    void* ptrs[256];
    
    for (auto _ : state) {
        for (int i = 0; i < 256; ++i) {
            ptrs[i] = ::operator new(size);
        }
        for (int i = 0; i < 256; ++i) {
            ::operator delete(ptrs[i]);
        }
        benchmark::DoNotOptimize(ptrs);
    }
}
BENCHMARK(BM_System_Allocate)->Arg(64)->Arg(256)->Arg(1024);

BENCHMARK_MAIN();