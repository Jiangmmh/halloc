# Plan: Remote-Free Path Implementation (Phase 3)

## Goal
Implement cross-thread free handling in halloc to support producer/consumer workloads.

## Why This Matters
- Real multi-threaded programs: allocate in Thread A, free in Thread B
- Without this: memory leaks or global lock contention
- DESIGN.md Section 12.1 calls this "hardest part" but essential

## Implementation Strategy
Per DESIGN.md Section 9: **simple explicit remote-free queue** at span level.

## Changes Required

### 1. Span Header (span.h)
Add owner tracking:
```cpp
uint64_t owner_thread_id;
std::atomic<uint32_t> remote_free_count;
```

### 2. PageHeap (page_heap.cpp)
Track span ownership:
- Add `span_to_owner_` map: Span* → thread_id
- `register_span_owner(span, thread_id)`
- `get_span_owner(span)`

### 3. ThreadCache (thread_cache.cpp)

#### allocate() path:
- Before refill from page heap, drain remote frees
- Pull from span's `remote_free_head` queue

#### deallocate() path:
- Lookup span via pagemap
- If `owner_thread_id != current_thread`:
  - Push to span's `remote_free_head`
  - Increment `remote_free_count`
- Else (same thread): local free as before

### 4. Tests (mt_stress.cpp)
Add cross-thread patterns:
- Producer/consumer (1→1)
- Fan-out (1→many threads alloc, one frees)
- Fan-in (many threads alloc, one thread frees)

## Files to Modify
- `include/halloc/span.h` - Add owner fields
- `include/halloc/page_heap.h` - Add owner map
- `src/page_heap.cpp` - Implement owner tracking
- `src/thread_cache.cpp` - Remote-free enqueue/drain
- `benchmarks/mt_stress.cpp` - Add cross-thread tests

## Acceptance Criteria
- [x] Add owner fields to Span header (span.h): owner_thread_id, remote_free_count
- [x] Add span owner map to PageHeap (page_heap.h/cpp)
- [x] Implement remote-free enqueue in ThreadCache::deallocate()
- [x] Implement remote-free drain in ThreadCache::allocate()
- [x] Add cross-thread tests in mt_stress.cpp
- [x] Verify all tests pass

## QA Scenarios
1. Same-thread alloc+free: baseline not regressed
2. Producer/consumer: throughput scales
3. Cross-thread free: memory returned to correct cache
4. High concurrency: no deadlock/assertion failures