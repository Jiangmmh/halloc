# halloc

A high-performance memory pool allocator for C++ workloads with small/medium allocations, high concurrency, and mixed same-thread/cross-thread free patterns.

## What is halloc?

`halloc` is a hybrid span allocator optimized for low latency and strong multicore scalability. It's designed for C/C++ applications that allocate many small-to-medium objects across multiple threads.

## Key Features

- **Thread-local caching**: Per-thread freelists keep the fast path lock-free
- **36 size classes**: Optimized spacing for small/medium objects
- **Central page heap**: Backing memory management kept off the hot path
- **Remote-free handling**: Explicit cross-thread free queue to avoid contention
- **Large allocation path**: Direct path for allocations > 32KB

## API

```cpp
#include <halloc/halloc.h>

void* ptr = halloc::allocate(128);
halloc::deallocate(ptr, 128);

// Aligned allocation
void* aligned = halloc::allocate_aligned(256, 64);
halloc::deallocate_aligned(aligned, 256, 64);

// Query usable size
size_t usable = halloc::usable_size(ptr);

// Statistics
halloc::Stats stats = halloc::stats_snapshot();
```

## Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Testing

```bash
ctest --output-on-failure
./halloc_unit_tests
./halloc_stress_tests
./halloc_bench
```

## Architecture Overview

```
┌─────────────────────┐
│  Thread Local Cache │  ← Fast path: lock-free alloc/free
├─────────────────────┤
│    Span Management  │  ← Batch refill/drain
├─────────────────────┤
│    Central Page Heap│  ← OS memory, reclamation
├─────────────────────┤
│ Large Allocation    │  ← Direct path for >32KB
└─────────────────────┘
```

## Performance

### Single-thread (256 ops)

| Size | halloc | system malloc | speedup |
|------|-------|--------------|--------|
| 64B | 1.67 μs | 2.89 μs | 1.7x |
| 256B | 2.30 μs | 5.21 μs | 2.3x |
| 1KB | 2.75 μs | 18.6 μs | 6.8x |

### Multi-thread contention (100K ops/thread)

| Threads | halloc alloc+free | halloc alloc only | system malloc |
|---------|-------------------|------------------|--------------|
| 1 | 3.3 ns/op | 32.9 ns/op | 9.5 ns/op |
| 8 | 0.8 ns/op | 24.9 ns/op | 10.5 ns/op |
| 32 | 0.6 ns/op | 21.5 ns/op | 9.2 ns/op |
| 64 | 0.5 ns/op | 201 ns/op | 10.0 ns/op |
| 80 | 0.5 ns/op | 290 ns/op | 10.5 ns/op |

Key findings:
- **Same-thread alloc+free**: Excellent scaling to 80 threads (0.5 ns/op)
- **Alloc-only contention**: Regresses at 64+ threads (central page heap bottleneck)
- **System malloc**: Flat ~10 ns/op across all thread counts

### Cross-Thread Free (Remote-Free Path)

| test | alloc | free | ops | time | ops/sec |
|------|------:|-----:|----:|-----:|--------:|
| producer_consumer | 1 | 1 | 20K | 0.29 ms | 69 M/s |
| fan_out | 8 | 1 | 32K | 0.82 ms | 39 M/s |
| fan_in | 1 | 4 | 16K | 0.34 ms | 47 M/s |
| producer_consumer(2,2) | 2 | 2 | 40K | 0.81 ms | 49 M/s |
| producer_consumer(4,4) | 4 | 4 | 80K | 2.13 ms | 37 M/s |
| fan_out_extreme(16,1) | 16 | 1 | 64K | 1.68 ms | 38 M/s |
| fan_in_extreme(1,8) | 1 | 8 | 16K | 0.36 ms | 44 M/s |
| mixed_size_free | 1 | 1 | 20K | 0.32 ms | 62 M/s |
| roundtrip | 1 | 1 | 20K | 0.30 ms | 67 M/s |
| mixed_st | 2 | 2 | 30K | 0.20 ms | 151 M/s |

Remote-free path supports: producer/consumer, fan-out, fan-in, mixed sizes, and roundtrip patterns.

Run benchmarks:
```bash
./build-bench/halloc_bench
./build-bench/halloc_mt_stress
```

## Design

See [DESIGN.md](DESIGN.md) for the full design document including goals, non-goals, size class strategy, and implementation roadmap.

## Requirements

- C++17 compiler
- CMake 3.20+
- Unix-like OS (Linux/macOS tested)