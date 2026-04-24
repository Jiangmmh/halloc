# halloc Design Document

## 1. Overview

`halloc` is intended to be an extremely high-performance general-purpose memory pool allocator for C/C++ workloads that are dominated by small and medium allocations, high concurrency, and mixed same-thread / cross-thread free behavior. The design target is low steady-state latency on the hot path, strong multicore scalability, predictable fragmentation behavior, and a clear path to future extensions without overcomplicating v1.

The recommended starting point is a hybrid allocator: **thread-local caches** on the fast path, **size-classed spans/slabs** for small and medium objects, a **central page heap** for backing memory and reclamation, an explicit **remote-free path** for cross-thread ownership transfer, and a **direct large-allocation path** that bypasses small-object machinery.

## 2. Goals and Non-Goals

### Goals

- Very fast same-thread alloc/free for small objects.
- Good scalability across many threads without putting the central allocator on the hot path.
- Bounded and understandable fragmentation.
- A design that is portable across common Unix-like environments before any platform-specific tuning.
- Clear observability so performance and memory regressions are measurable.

### Non-Goals for v1

- A per-CPU frontend.
- NUMA-aware placement or rebalancing.
- Huge-page-aware allocation policy.
- Full hardening/debug modes such as guard pages, free-list encoding, quarantine, or poisoning.
- A production-stable `malloc` replacement ABI on day one.

These are deferred because they add meaningful complexity and can distort the core architecture before the base design is validated.

## 3. Workload Assumptions and Success Criteria

### Assumptions

- Most allocations are small or medium and hit a limited set of hot size classes.
- Allocation rate is high enough that lock contention and cache misses matter.
- Cross-thread frees are common enough to deserve a first-class design.
- Large allocations exist but are not the dominant path.
- Users care about both throughput and tail latency, not just average speed.

### Success Criteria

- High throughput for same-thread small-object allocation and free.
- Competitive p50, p99, and p99.9 latency under contention.
- Stable memory footprint under long-running mixed workloads.
- Limited thread-cache memory blowup as thread count rises.
- Cross-thread free behavior that scales without pathological stalls.
- RSS, reserved bytes, live bytes, and fragmentation all remain measurable and explainable.

## 4. Prior Research and Design Lineage

This design intentionally borrows from several outstanding allocators, but does not copy any one of them wholesale.

- **mimalloc** contributes the strongest v1 shape for a simple, fast frontend: page-local allocation, good locality, and a practical remote-free design.
- **rpmalloc** contributes the idea that span ownership should stay clear and that producer/consumer workloads deserve explicit treatment.
- **jemalloc** contributes disciplined size-class design, reclamation thinking, and a mature view of fragmentation, decay, and observability.
- **TCMalloc** contributes the front-end / middle-end / back-end mental model and the importance of keeping central structures off the hot path.
- **snmalloc** contributes the strongest warning that remote free behavior can dominate design quality, and that batching or message-passing style handoff is often better than naive central contention.

### Why the chosen hybrid

The design favors **per-thread caches over per-CPU caches** because they are simpler, more portable, and easier to reason about in a greenfield allocator. It favors a **simple explicit remote-free queue/list** over a more exotic message-routing system because correctness, bounded contention, and debuggability matter more than chasing the absolute best microbenchmark result in v1.

## 5. High-Level Architecture

`halloc` is divided into four major layers:

1. **Thread-local frontend**
   - One local cache per thread.
   - One freelist per size class.
   - Handles the steady-state fast path for same-thread alloc/free.

2. **Shared span management layer**
   - Supplies batches to thread-local caches.
   - Receives drained batches and remote frees.
   - Tracks span state transitions such as empty, partial, full, and reclaimable.

3. **Central page heap**
   - Manages page runs used to create spans.
   - Coalesces returned memory.
   - Owns release-to-OS decisions.

4. **Large-allocation path**
   - Direct path for allocations above a threshold.
   - Avoids polluting small-object spans.
   - Can reuse large runs, but is logically separate from small-object caches.

The architectural rule is simple: **the fast path should almost always terminate in the thread-local cache**. The central allocator should only appear during refill, drain, reclamation, and large-allocation operations.

## 6. Core Data Structures

The following structures are conceptually required even if the final implementation changes naming or layout.

- **Size class table**
  - Maps requested size to class index.
  - Stores rounded size, batch size, preferred span size, and refill/drain policy.

- **Thread cache / local heap**
  - Per-thread array of freelists.
  - Per-class cached object count.
  - Local pressure accounting for deciding when to drain.

- **Span header**
  - Size class identifier.
  - Owning shard or central owner reference.
  - Capacity, live count, free count, and remote-free pending state.
  - Span state: empty, partial, full, or large.

- **Central class shard**
  - Shared per-size-class batch source.
  - Tracks available partial spans and refill candidates.
  - Accepts returned batches from thread caches.

- **Page heap**
  - Owns runs of pages.
  - Splits and coalesces runs.
  - Returns empty spans to reusable page storage or the OS.

- **Metadata lookup structure**
  - Pointer-to-span lookup.
  - Should be pagemap-friendly, not an O(n) scan.
  - A radix-tree or direct-mapped page table style structure is preferred over per-object headers.

- **Remote-free queue/list**
  - Used when a block is freed by a thread other than the current span owner.
  - Designed to avoid handing memory directly into another thread’s local cache.

## 7. Size Classes and Memory Layout

### v1 recommendation

Use fixed size classes with fine spacing at the small end and wider spacing as size grows. The exact table should be benchmarked, but the design should resemble the general pattern used by jemalloc and mimalloc rather than a pure power-of-two scheme.

### Rationale

- Too few size classes increase internal fragmentation.
- Too many size classes increase metadata, management overhead, and cache pressure.
- Small-object hot paths benefit from dense spacing.
- Medium-object classes can tolerate wider spacing because allocation frequency is usually lower.

### Layout policy

- A span serves exactly one size class at a time.
- Small and medium spans should use fixed page-granular backing sizes selected per class family.
- Metadata should be reachable via pointer-to-span lookup rather than an object-local header.
- Alignment should be explicit and consistent across classes.

### Deferred decision

Huge-page-oriented span geometry is deferred until baseline performance data shows that TLB behavior is limiting throughput or tail latency.

## 8. Allocation Flow

### Fast path

1. Round requested size to a size class.
2. Check the current thread’s freelist for that class.
3. Pop one object and return immediately on hit.

### Refill path

1. Local freelist miss triggers a batch request.
2. Shared span/class state provides a batch from a partial span if possible.
3. If no suitable span is available, request a fresh span from the central page heap.
4. Split the span into blocks, seed local inventory, and return one object.

### Slow path

1. Central page heap has no reusable run for the requested span size.
2. Request additional memory from the OS.
3. Install metadata, register span ownership, and resume refill.

### Design note

The slow path must be correct and observable, but it does not need to be elegant before the fast path is proven.

## 9. Free Flow and Remote-Free Handling

### Same-thread free

1. Resolve pointer to span.
2. If the current thread owns the local cache path for that class, push onto the local freelist.
3. If the local cache exceeds its budget, drain a batch back to the shared layer.

### Cross-thread free

1. Resolve pointer to span.
2. Detect that the current thread is not the owning local context.
3. Append the object to a span-level or central remote-free queue/list.
4. The owning side drains pending remote frees during refill, allocation miss handling, or explicit maintenance points.

### v1 recommendation

Use a **simple explicit remote-free queue/list** with bounded batching. Do not return cross-thread frees directly into another thread’s local freelist. Do not start with a sophisticated message-passing graph or a fully lock-free distributed ownership system.

### Why

- This keeps ownership rules clear.
- It bounds the blast radius of concurrency bugs.
- It still handles producer/consumer patterns much better than funneling all remote frees through a monolithic global lock.

## 10. Large-Allocation Direct Path

Allocations above a configured threshold should bypass small-object spans and use the large-allocation path.

### Policy

- Large allocations get dedicated runs.
- They are tracked separately from small-object spans.
- Reuse is allowed, but only within the large-allocation subsystem.
- Release-to-OS policy can be more aggressive here than in the small-object path.

### Why separation matters

Mixing large allocations into small-object span machinery complicates reclamation, worsens fragmentation, and pollutes the hot path with cases that do not belong there.

## 11. Contention, Fragmentation, and Reclamation

### Contention control

- Thread-local caches remove the common case from shared contention.
- Shared state should be sharded by size class or span class wherever practical.
- The page heap is centralized, but should only be touched on refill, drain, reclamation, and large allocations.

### Fragmentation control

- One span per size class at a time.
- Prefer reusing partial spans before allocating new ones.
- Track empty, partial, and full spans explicitly.
- Put hard limits on thread-local cache growth.

### Reclamation policy for v1

- Empty spans return to the central page heap.
- The page heap coalesces page runs opportunistically.
- Release to the OS should exist, but be conservative and simple in v1.

### Deferred reclamation features

- Time-based decay similar to jemalloc.
- Huge-page-aware purge/reclaim.
- NUMA-sensitive reclaim heuristics.

These are worthwhile later, but are not required to validate the allocator architecture.

## 12. Expected Bottlenecks and Scaling Risks

The main technical difficulties are not evenly distributed.

### 1. Remote-free correctness and throughput

This is the hardest part of the design. It is easy to make fast in the uncontended case and still fail badly under producer/consumer stress.

### 2. Thread-cache memory blowup

Per-thread caches are the right v1 choice, but they can hoard memory when many threads become idle. Hard budgets, batch sizes, and drain heuristics must be designed deliberately.

### 3. Metadata lookup cost

Pointer-to-span lookup happens on every free and often on refill paths. A poor lookup structure silently taxes the entire allocator.

### 4. Central page-heap contention

If refill/drain pressure is too high, the page heap becomes the bottleneck. The design must push batching and reuse hard enough that the central heap stays off the hot path.

### 5. Size-class geometry mistakes

Bad class spacing creates permanent fragmentation costs that are hard to undo later without a major redesign.

### 6. Reclamation policy instability

Overly eager release to the OS increases churn and hurts latency; overly lazy release inflates RSS. This tradeoff should be measured, not guessed.

## 13. Benchmarking and Validation Plan

The allocator should be judged on multiple axes at once.

### Core benchmark dimensions

- Throughput: operations per second.
- Latency: p50, p99, and p99.9 for alloc and free.
- Scalability: thread-count sweep across low and high concurrency.
- RSS and reserved bytes over time.
- Fragmentation: live bytes vs reserved bytes.
- Cross-thread free stress: producer/consumer and fan-in/fan-out workloads.

### Benchmark matrix

1. **Single-thread hot-size test**
   - Same-thread alloc/free on one or a few hot classes.

2. **Mixed-size steady-state test**
   - Realistic distribution across small and medium sizes.

3. **Burst refill/drain test**
   - Forces local caches to oscillate and exposes refill path cost.

4. **Cross-thread free test**
   - Allocate on one thread, free on another.
   - Include one-to-one, one-to-many, and many-to-one patterns.

5. **Long-run fragmentation test**
   - Mixed lifetimes over long duration.
   - Tracks RSS drift and reserved/live divergence.

6. **Large-allocation interference test**
   - Ensures large-run handling does not degrade the small-object path.

### Comparison targets

- System allocator.
- If feasible, one or more of: mimalloc, rpmalloc, jemalloc, or TCMalloc.

The goal is not to win every benchmark immediately. The goal is to know exactly which workloads the architecture serves well and where it falls short.

## 14. Implementation Roadmap

### Phase 1: Baseline architecture

- Define size class table.
- Implement span metadata and pointer-to-span lookup.
- Implement central page heap.
- Implement single-thread allocate/free.
- Add minimal counters and correctness tests.

### Phase 2: Thread-local frontend

- Add per-thread local caches.
- Add batch refill and drain.
- Add cache budgets and basic pressure controls.
- Benchmark same-thread hot paths.

### Phase 3: Remote-free path

- Add remote-free queue/list.
- Add owner-side drain behavior.
- Stress producer/consumer workloads.
- Verify correctness under high concurrency.

### Phase 4: Reclamation and large-allocation policy

- Return empty spans to the page heap.
- Add page-run coalescing.
- Implement large-allocation direct path and reuse policy.
- Measure fragmentation and RSS behavior.

### Phase 5: Observability and tuning

- Add richer allocator stats.
- Tune class spacing, batch sizes, and cache budgets.
- Tune release-to-OS heuristics.
- Compare against external allocators.

### Phase 6: Deferred experiments

- Per-CPU frontend.
- NUMA-aware policies.
- Huge-page-aware page heap.
- Hardened/debug modes.

These experiments should happen only after the baseline architecture is proven and benchmark data shows a real need.

## 15. Open Decisions

The following items are intentionally left open, but bounded.

### Per-thread vs per-CPU frontend

Start with per-thread. Per-CPU may become valuable at very high thread counts, but it introduces portability, scheduling, and migration complexity too early.

### In-band vs out-of-band metadata

The preferred direction is pagemap-friendly pointer-to-span lookup rather than per-object headers. The exact span metadata placement can still be refined during implementation.

### Span sizing policy

Start with fixed page-granular span families by size-class range. Adaptive span geometry may help later, but should not complicate v1.

### Remote-free mechanism shape

Start with a simple bounded queue/list design. If producer/consumer workloads dominate and remote-free draining becomes the main bottleneck, evaluate richer batching or message-passing techniques inspired by snmalloc.

### Reclamation policy

Start with explicit empty-span return and conservative OS release. If RSS remains too high in long-running workloads, add time-based decay inspired by jemalloc.

## Recommended v1 Summary

The first version of `halloc` should be a **hybrid span allocator** with:

- per-thread freelist caches,
- a fixed size-class table,
- span-per-class allocation,
- a centralized page heap kept off the hot path,
- explicit remote-free handling at the span/central layer,
- and a separate large-allocation subsystem.

This is the most credible path to high performance without taking on per-CPU scheduling issues, NUMA policy, huge-page management, or complex hardening before the core allocator is stable and measurable.
