# Scope Fidelity Check - halloc v1 Implementation vs DESIGN.md

## Date: 2026-04-24

## Summary
The implementation covers the v1 scope from DESIGN.md but has significant gaps in completeness and correctness. No unwanted v2 features were added.

---

## V1 Features Present (DESIGN.md §5, §6, §8, §9, §10)

### ✅ Per-thread caches (DESIGN.md §5.1, §7)
- `ThreadCache` with `thread_local` storage in `thread_cache.cpp`
- Per-size-class freelist array (`free_lists_`, `free_counts_`)
- Fast path: pop from local freelist on hit
- Refill path: batch request from page heap on miss
- Cache byte cap: `THREAD_CACHE_SOFT_CAP` (256 KiB) with drain to `THREAD_CACHE_HARD_CAP` (128 KiB)
- **Issue**: Drain implementation is naive — just drops objects without returning to central shard

### ✅ Size-classed spans (DESIGN.md §6, §7)
- Fixed size-class table in `size_class.h` with 36 classes from 16 to 32768
- Dense spacing at small end, wider at large end (matches DESIGN.md §7.1)
- Batch sizing via `clamp(8192 / class_size, 8, 64)` — but the table hardcodes batch sizes instead of computing them
- **Issue**: Span families (4, 8, 16, 32 pages per DESIGN.md §7.3) are NOT implemented. `allocate_span` just uses a single page count.

### ✅ Central page heap (DESIGN.md §5.3, §6)
- `PageHeap` class with `allocate_span` / `deallocate_span`
- Mutex-protected (`std::mutex`)
- Tracks free spans by page count in `std::map`
- **Issue**: `deallocate_span` unmaps and deletes immediately — no coalescing, no reuse of empty spans (DESIGN.md §11.3 says "Empty spans return to the central page heap" and "coalesces page runs opportunistically")
- **Issue**: `allocate_span` allocates new memory from OS on every call — the free_spans_ map is populated only by splitting, never by deallocation

### ✅ Remote-free path (DESIGN.md §9.2, §6)
- `Span` has `remote_free_head` field (atomic pointer)
- **Issue**: The remote-free queue is declared but NEVER actually used. `ThreadCache::deallocate` does NOT check if the current thread owns the span — it always pushes to the local freelist. There is no cross-thread detection, no MPSC push, no owner-side drain.
- **Issue**: No atomic operations on `remote_free_head` anywhere in the codebase.

### ✅ Direct large-allocation path (DESIGN.md §10)
- `PageHeap::allocate_large` / `deallocate_large`
- Threshold at 32768 bytes (matches DESIGN.md §10)
- Large spans tracked separately in `large_spans_` vector
- **Issue**: `allocate_large` unmaps on deallocation — no reuse of large runs (DESIGN.md §10 says "Reuse is allowed, but only within the large-allocation subsystem")
- **Issue**: Large allocation in `ThreadCache::allocate` calls `get_page_heap()->allocate_large(size)` but the large deallocation path in `ThreadCache::deallocate` accesses `reinterpret_cast<Span**>(ptr)[-1]` which is a per-object header — explicitly forbidden by DESIGN.md §6 ("No per-object headers for small allocations") and §12 ("Metadata should be reachable via pointer-to-span lookup rather than an object-local header")

---

## V1 Features Missing or Incomplete

### ❌ 2-level pagemap (DESIGN.md §6, §12.3)
- `PageMap` struct exists in `span.h` but is a flat array, NOT a 2-level radix tree
- `pagemap_lookup` and `pagemap_insert` are defined but NEVER CALLED anywhere in the allocator
- No pagemap is created or used during allocation/free — the allocator has no pointer-to-span lookup at all
- This means `deallocate` cannot resolve a pointer to its owning span, making cross-thread free detection impossible

### ❌ Central class shards (DESIGN.md §6)
- No per-size-class central shard exists
- Thread cache drains directly to page heap (or nowhere, in the current implementation)
- DESIGN.md §6 specifies "Shared per-size-class batch source" and "Tracks available partial spans"

### ❌ Span state tracking (DESIGN.md §6)
- `SpanState` enum exists (Empty/Partial/Full) but state transitions are not consistently managed
- `live_count`, `local_free_count`, `capacity` fields exist but are not updated during alloc/free
- No partial-span reuse — every allocation creates a new span from the page heap

### ❌ Thread-exit handling (DESIGN.md §9.2, plan task 8)
- `ThreadCache` destructor exists but does NOT return inventory to central layer — it allocates dummy spans and immediately deallocates them (a no-op pattern)
- No `thread_local` destructor registration for cleanup

### ❌ Stats/observability (DESIGN.md §13, plan task 9)
- `GlobalStats` struct exists with atomic counters
- `stats_snapshot()` in `halloc.cpp` only returns `cached_bytes` — all other fields are zero
- `GlobalStats` counters are never incremented anywhere in the allocator code
- No diagnostic assertions in test/debug builds

### ❌ Span families (DESIGN.md §7.3, plan task 6)
- Plan specifies span families of {4, 8, 16, 32} pages
- Implementation always allocates `max(4, computed_pages)` — no family selection logic

---

## Unwanted Features Check (DESIGN.md §2.2 Non-Goals)

### ✅ No per-CPU frontend — NOT present
### ✅ No NUMA-aware policies — NOT present
### ✅ No huge-page-specific policies — NOT present
### ✅ No hardened/debug modes — NOT present
### ✅ No malloc interposition — NOT present
### ✅ No per-object headers for small allocations — NOT present (but large path uses `reinterpret_cast<Span**>(ptr)[-1]` which IS a per-object header pattern — this is a scope VIOLATION)
### ✅ No global hash-map metadata lookup — NOT present
### ✅ No unbounded thread caches — caps are defined (though not enforced correctly)
### ✅ No direct cross-thread freelist insertion — remote-free path is not implemented at all

---

## Plan Compliance (vs .sisyphus/plans/halloc-system-design.md)

### Task 1 (Bootstrap) — ✅ DONE
- CMakeLists.txt exists, build targets defined, CTest configured
- ASan/UBSan/TSan options present with mutual exclusion check
- Three binaries: unit tests, stress tests, benchmarks

### Task 2 (Public API) — ✅ DONE (partially)
- API surface matches: `allocate`, `deallocate`, `allocate_aligned`, `deallocate_aligned`, `usable_size`, `stats_snapshot`
- `allocate(0)` returns nullptr ✅
- `deallocate(nullptr, 0)` is a no-op ✅
- 16-byte alignment ✅
- **Missing**: `realloc` and `calloc` correctly excluded ✅
- **Missing**: `usable_size` returns 1 for non-null — should return actual usable size

### Task 3 (Size classes) — ✅ DONE
- Fixed table with 36 classes ✅
- Dense 16-byte spacing at small end ✅
- Large threshold at 32768 ✅
- Batch sizes defined ✅
- **Issue**: Batch sizes are hardcoded rather than computed from `clamp(8192 / class_size, 8, 64)` formula

### Task 4 (OS layer) — ✅ DONE
- `mmap`/`munmap` abstraction ✅
- Runtime page size from `sysconf(_SC_PAGESIZE)` ✅
- `madvise` with `MADV_FREE`/`MADV_DONTNEED` ✅
- No `sbrk` ✅

### Task 5 (Span + pagemap) — ❌ INCOMPLETE
- Span struct exists with required fields ✅
- Pagemap is flat, not 2-level ❌
- Pagemap is never integrated into allocator ❌

### Task 6 (Page heap + large path) — ❌ INCOMPLETE
- Page heap exists ✅
- No coalescing ❌
- No span families ❌
- Large path unmaps on deallocation (no reuse) ❌
- Large path uses per-object header ❌

### Task 7 (Thread cache) — ❌ INCOMPLETE
- Thread-local cache exists ✅
- Fast path works for same-thread ✅
- Cache caps defined but drain is naive ❌
- No central shard interaction ❌

### Task 8 (Remote free) — ❌ NOT IMPLEMENTED
- `remote_free_head` field exists but never used ❌
- No MPSC push ❌
- No owner-side drain ❌
- No thread-exit cleanup ❌

### Task 9 (Stats) — ❌ INCOMPLETE
- Struct exists ✅
- Counters never incremented ❌
- `stats_snapshot()` returns only `cached_bytes` ❌

### Task 10 (Benchmarks) — ❌ PLACEHOLDER ONLY
- Benchmark binary exists with `--list-suites` ✅
- All suites are stubs that just print names ❌
- No actual benchmarking code ❌

## Additional verification notes (2026-04-24)

- `cmake --build build -j && ctest --test-dir build --output-on-failure` passes, but only for two smoke tests.
- `cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure` also passes with the same smoke-only coverage.
- Passing smoke suites is not sufficient evidence of completion because the plan's required binary layout and task-specific test matrix are not implemented.
