# Issues Found During Scope Fidelity Check

## Critical Issues (Scope Violations)

1. **Large path uses per-object header** (`thread_cache.cpp:67`)
   - `reinterpret_cast<Span**>(ptr)[-1]` reads a Span* from the memory before the user pointer
   - This is a per-object header pattern, explicitly forbidden by DESIGN.md §6 ("No per-object headers for small allocations") and §12 ("Metadata should be reachable via pointer-to-span lookup")
   - The pagemap should be used instead, but it's not integrated

2. **Pagemap is flat, not 2-level** (`span.h:32-35`)
   - DESIGN.md §6 specifies "A radix-tree or direct-mapped page table style structure"
   - Plan task 5 specifies "2-level radix pagemap"
   - Current implementation is a single flat array with 2^21 entries

## Major Issues (Missing v1 Features)

3. **Remote-free path is completely non-functional**
   - `remote_free_head` is declared but never accessed atomically
   - `ThreadCache::deallocate` never checks span ownership
   - No cross-thread detection logic exists
   - This is the hardest v1 problem per DESIGN.md §12.1

4. **Pagemap is not integrated into the allocator**
   - `pagemap_lookup`/`pagemap_insert` are defined but never called
   - No pagemap is created during initialization
   - Pointer-to-span resolution is impossible, making cross-thread free detection impossible

5. **No central class shards**
   - Thread cache drains directly to page heap (or nowhere)
   - No per-size-class batch source for refill/drain coordination
   - No partial-span tracking at the central level

6. **Stats counters are never incremented**
   - `GlobalStats` struct exists but all counters stay at zero
   - `stats_snapshot()` only returns `cached_bytes`

7. **Page heap does not coalesce or reuse**
   - `deallocate_span` unmaps and deletes immediately
   - No empty-span return to page heap for reuse
   - No page-run coalescing

8. **Thread-exit cleanup is a no-op**
   - Destructor allocates dummy spans instead of returning inventory

## Minor Issues

9. **Batch sizes hardcoded** instead of computed from formula
10. **Span families not implemented** — always uses single page count
11. **`usable_size` returns 1** instead of actual usable size
12. **`allocate_aligned` doesn't actually align** — just checks if naturally aligned
13. **Stress tests and benchmarks are stubs** — no actual test/benchmark logic

14. **Plan-defined binary paths do not exist** (`CMakeLists.txt:23-34`)
   - The plan requires `./build/tests/halloc_unit_tests` and `./build/benchmarks/halloc_bench`
   - CMake emits `halloc_unit_tests` and `halloc_bench` at the build root instead
   - Verification command `./build/tests/halloc_unit_tests --list-cases` fails with `no such file or directory`

15. **CTest coverage is smoke-only** (`CMakeLists.txt:33-34`)
   - Only two smoke tests are registered
   - No labeled `concurrency`, `bench`, or task-specific tests from the plan are wired into CTest
