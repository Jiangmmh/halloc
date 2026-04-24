# halloc v1 System Design Plan

## TL;DR
> **Summary**: Implement `halloc` as a C++17 library allocator with a thread-local fast path, size-classed spans, a centralized page heap, a per-span remote-free inbox, and a separate large-allocation path. Build and test infrastructure starts in Phase 0 so every allocator phase is validated with CMake, CTest, stress tests, benchmarks, and sanitizer builds.
> **Deliverables**:
> - CMake/CTest-based project scaffold with unit, stress, and benchmark targets
> - Explicit library API and API-semantics tests
> - Size-class table, span metadata, 2-level pagemap, page heap, thread cache, remote-free path, and large-allocation path
> - Built-in stats/observability and machine-readable benchmark outputs
> **Effort**: XL
> **Parallel**: YES - 2 waves
> **Critical Path**: 1 → 2 → 5 → 6 → 7 → 8 → 9 → 10

## Context
### Original Request
Create a specific system design based on `DESIGN.md` for the following implementation.

### Interview Summary
- v1 integration model is **library API only**.
- v1 implementation language is **C++17**.
- v1 must include **formal test infrastructure from the start**.
- Existing `DESIGN.md` remains the architecture source; implementation planning must convert its open decisions into concrete defaults.

### Metis Review (gaps addressed)
- Added an explicit **Phase 0 / scaffolding** task before allocator internals.
- Resolved `DESIGN.md` open decisions into concrete v1 defaults instead of leaving them advisory.
- Added explicit API semantics, platform scope, module split, and executable QA commands.
- Added a hard v1 exclusion list to prevent scope creep into per-CPU, NUMA, huge pages, hardened/debug modes, and malloc interposition.

## Work Objectives
### Core Objective
Deliver an implementation-ready plan for a greenfield C++17 allocator that matches the high-level architecture in `DESIGN.md` while freezing all v1 decisions needed for code execution.

### Deliverables
- Public C++17 library API for allocation, deallocation, aligned allocation, and stats snapshot access
- Internal modules for size mapping, spans, pagemap, central shards, page heap, large allocations, OS abstraction, thread caches, remote-free handling, and stats
- CMake/CTest build, unit tests, stress tests, sanitizer targets, and benchmark binary with JSON/CSV output
- Tunable but fixed starter defaults for size classes, batch sizes, cache limits, and large-allocation threshold

### Definition of Done (verifiable conditions with commands)
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` succeeds
- `cmake --build build -j` succeeds
- `ctest --test-dir build --output-on-failure` passes
- `cmake -S . -B build-asan -DHALLOC_ENABLE_ASAN=ON -DHALLOC_ENABLE_UBSAN=ON && cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure` passes
- `cmake -S . -B build-tsan -DHALLOC_ENABLE_TSAN=ON && cmake --build build-tsan -j && ctest --test-dir build-tsan --output-on-failure -L concurrency` passes on supported platforms
- `./build/benchmarks/halloc_bench --format json --out .sisyphus/evidence/final-bench.json` completes and emits throughput, latency, RSS, and fragmentation metrics

### Must Have
- C++17 implementation with CMake + CTest
- Library API only; no malloc interposition in v1
- 16-byte minimum alignment
- Explicit fixed starter size-class table up to 32 KiB
- 2-level pagemap mapping page number to `Span*`
- Per-thread cache with hard byte cap and bounded batch refill/drain
- Per-span intrusive MPSC remote-free inbox drained by the owning path
- Large allocations routed to dedicated runs above 32 KiB
- Machine-readable benchmark outputs and built-in counters from day one

### Must NOT Have (guardrails, AI slop patterns, scope boundaries)
- No per-CPU frontend
- No NUMA-aware policies
- No huge-page-specific policies
- No hardened/debug-only allocator modes beyond sanitizer-enabled tests
- No malloc/free interposition ABI
- No per-object headers for small allocations
- No global hash-map metadata lookup
- No unbounded thread caches
- No direct cross-thread insertion into another thread’s local freelist
- No performance acceptance criteria based on “must beat jemalloc/mimalloc/etc.”

## Verification Strategy
> ZERO HUMAN INTERVENTION - all verification is agent-executed.
- Test decision: **tests-from-start** with **CMake + CTest**
- QA policy: Every task includes agent-executed happy-path and failure/edge-case scenarios
- Evidence: `.sisyphus/evidence/task-{N}-{slug}.{ext}`

## Execution Strategy
### Parallel Execution Waves
> Target: 5-8 tasks per wave. <3 per wave (except final) = under-splitting.
> Extract shared dependencies as Wave-1 tasks for max parallelism.

Wave 1: 1 scaffolding/build/test, 2 public API contract, 3 size classes/constants, 4 OS abstraction, 5 span+pagemap core

Wave 2: 6 central page heap + large path, 7 thread cache fast path, 8 remote-free path, 9 stats/observability, 10 benchmarks + sanitizer/concurrency matrix

### Dependency Matrix (full, all tasks)
| Task | Depends On | Blocks |
|---|---|---|
| 1 | - | 2,3,4,5,10 |
| 2 | 1 | 6,7,8,9,10 |
| 3 | 1 | 5,6,7,8,10 |
| 4 | 1 | 5,6,10 |
| 5 | 2,3,4 | 6,7,8,9,10 |
| 6 | 2,3,4,5 | 7,8,9,10 |
| 7 | 2,3,5,6 | 8,9,10 |
| 8 | 2,5,6,7 | 9,10 |
| 9 | 2,5,6,7,8 | 10 |
| 10 | 1,2,3,4,5,6,7,8,9 | F1-F4 |

### Agent Dispatch Summary (wave → task count → categories)
- Wave 1 → 5 tasks → unspecified-high (1,2,3,4), deep (5)
- Wave 2 → 5 tasks → unspecified-high (6,9,10), deep (7,8)

## TODOs
> Implementation + Test = ONE task. Never separate.
> EVERY task MUST have: Agent Profile + Parallelization + QA Scenarios.

- [x] 1. Bootstrap repository layout, build system, and formal test harness

  **What to do**: Create the initial repo structure and toolchain foundation: `CMakeLists.txt`, `cmake/` helpers, `include/halloc/`, `src/`, `tests/`, `benchmarks/`, and `docs/` only if required by build output references. Add build options `HALLOC_ENABLE_ASAN`, `HALLOC_ENABLE_UBSAN`, and `HALLOC_ENABLE_TSAN`. Define three binaries/targets from the start: `halloc_unit_tests`, `halloc_stress_tests`, and `halloc_bench`. Add CTest labels `unit`, `stress`, `concurrency`, `bench`, and `smoke`. Standardize machine-readable output flags for stress/bench binaries: `--format json --out <path>`.
  **Must NOT do**: Do not add third-party test or benchmark dependencies in v1. Do not add malloc interposition targets. Do not create allocator implementation beyond scaffolding helpers needed to compile placeholder tests.

  **Recommended Agent Profile**:
  - Category: `unspecified-high` - Reason: multi-file project bootstrap with build/test constraints
  - Skills: `[]` - No additional repo-local skills available
  - Omitted: `[]` - No extra skills required

  **Parallelization**: Can Parallel: NO | Wave 1 | Blocks: 2,3,4,5,10 | Blocked By: none

  **References** (executor has NO interview context - be exhaustive):
  - Pattern: `DESIGN.md:9-27` - v1 goals and exclusions must shape build targets and scope
  - Pattern: `DESIGN.md:272-312` - benchmark and validation dimensions that the harness must support
  - Pattern: `DESIGN.md:314-359` - roadmap requires scaffolding before allocator internals even though the design doc starts later

  **Acceptance Criteria** (agent-executable only):
- [x] `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug` succeeds
- [x] `cmake --build build -j` succeeds
- [x] `ctest --test-dir build --output-on-failure -L smoke` passes
- [x] `./build/tests/halloc_unit_tests --list-cases` exits 0 and lists API, size-class, metadata, page-heap, thread-cache, remote-free, and large-allocation suites

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Debug bootstrap succeeds
    Tool: Bash
    Steps: cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j && ctest --test-dir build --output-on-failure -L smoke
    Expected: Configure/build/test complete with exit code 0; smoke label passes
    Evidence: .sisyphus/evidence/task-1-bootstrap.txt

  Scenario: Unsupported sanitizer combination fails cleanly
    Tool: Bash
    Steps: cmake -S . -B build-invalid -DHALLOC_ENABLE_ASAN=ON -DHALLOC_ENABLE_TSAN=ON
    Expected: Configure exits non-zero with a clear message that ASan and TSan cannot be enabled together
    Evidence: .sisyphus/evidence/task-1-bootstrap-error.txt
  ```

  **Commit**: NO | Message: `build: scaffold halloc project layout` | Files: `CMakeLists.txt`, `cmake/**`, `include/**`, `src/**`, `tests/**`, `benchmarks/**`

- [x] 2. Freeze the public API contract and semantic rules

  **What to do**: Define the v1 public surface as a C++17 library API in `include/halloc/` with namespace-level entry points and an allocator object for explicit usage. Include: `allocate(size_t)`, `deallocate(void*, size_t)`, `allocate_aligned(size_t, size_t)`, `deallocate_aligned(void*, size_t, size_t)`, `usable_size(void*)`, and `stats_snapshot()`. Freeze semantics for `allocate(0)`, `free(nullptr)` equivalent behavior, overflow rejection, alignment guarantees, large-allocation threshold routing, invalid pointer behavior in release vs test builds, and thread-exit handling. Add API-contract unit tests first and make later tasks conform to them.
  **Must NOT do**: Do not expose malloc/free interposition hooks. Do not add `realloc` or `calloc` to v1. Do not leave invalid-pointer behavior ambiguous; specify that invalid frees are undefined behavior in release builds and diagnosed only in dedicated test/debug helpers.

  **Recommended Agent Profile**:
  - Category: `unspecified-high` - Reason: API semantics control all downstream modules
  - Skills: `[]` - No additional repo-local skills available
  - Omitted: `[]` - No extra skills required

  **Parallelization**: Can Parallel: YES | Wave 1 | Blocks: 6,7,8,9,10 | Blocked By: 1

  **References** (executor has NO interview context - be exhaustive):
  - Pattern: `DESIGN.md:29-46` - success criteria define library behavior and observability expectations
  - Pattern: `DESIGN.md:62-86` - architecture layers that the API must target
  - Pattern: `DESIGN.md:361-383` - open decisions to resolve into explicit semantics

  **Acceptance Criteria** (agent-executable only):
  - [ ] `ctest --test-dir build --output-on-failure -R api_contract` passes
  - [ ] `./build/tests/halloc_unit_tests --case api_contract --format json --out .sisyphus/evidence/task-2-api.json` emits pass/fail entries for zero-size, null-free, alignment, overflow, threshold routing, and stats access

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: API contract edge cases pass
    Tool: Bash
    Steps: ./build/tests/halloc_unit_tests --case api_contract --format json --out .sisyphus/evidence/task-2-api.json
    Expected: JSON contains passing checks for allocate(0), deallocate(nullptr, 0), 16-byte alignment, overflow rejection, and stats_snapshot access
    Evidence: .sisyphus/evidence/task-2-api.json

  Scenario: Misaligned aligned-allocation request is rejected deterministically
    Tool: Bash
    Steps: ./build/tests/halloc_unit_tests --case api_invalid_alignment --format json --out .sisyphus/evidence/task-2-api-error.json
    Expected: Test exits 0 and reports the request was rejected according to the documented API contract
    Evidence: .sisyphus/evidence/task-2-api-error.json
  ```

  **Commit**: NO | Message: `feat(api): define explicit allocator surface` | Files: `include/halloc/**`, `tests/**`

- [x] 3. Implement the starter size-class table and cache policy constants

  **What to do**: Create a fixed starter size-class table with 16-byte minimum alignment. Use dense classes from `16..256` by 16, then `{320,384,448,512,640,768,896,1024,1280,1536,1792,2048,3072,4096,6144,8192,12288,16384,24576,32768}`. Freeze the v1 large-allocation cutoff at requests `> 32768` bytes. Define batch sizing as `clamp(8192 / class_size, 8, 64)`. Define per-class local cache caps as 2 batches for classes `<= 1024` and 1 batch above that. Define per-thread cached-byte cap at 256 KiB with drain-back to 128 KiB when exceeded.
  **Must NOT do**: Do not use pure power-of-two classes. Do not leave class spacing “to be benchmarked later” in v1. Do not set the large threshold above 32 KiB.

  **Recommended Agent Profile**:
  - Category: `unspecified-high` - Reason: starter geometry drives fragmentation and fast-path shape
  - Skills: `[]` - No additional repo-local skills available
  - Omitted: `[]` - No extra skills required

  **Parallelization**: Can Parallel: YES | Wave 1 | Blocks: 5,6,7,8,10 | Blocked By: 1

  **References** (executor has NO interview context - be exhaustive):
  - Pattern: `DESIGN.md:126-148` - size-class and span-layout guidance
  - Pattern: `DESIGN.md:264-266` - size-class geometry mistakes are a permanent cost center
  - Pattern: `DESIGN.md:385-396` - recommended v1 summary requires a fixed size-class table and separate large-allocation subsystem

  **Acceptance Criteria** (agent-executable only):
  - [ ] `ctest --test-dir build --output-on-failure -R size_class` passes
  - [ ] `./build/tests/halloc_unit_tests --case size_class_boundaries --format json --out .sisyphus/evidence/task-3-size-classes.json` confirms all documented classes, threshold routing, and batch-size calculations

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Boundary mapping is correct
    Tool: Bash
    Steps: ./build/tests/halloc_unit_tests --case size_class_boundaries --format json --out .sisyphus/evidence/task-3-size-classes.json
    Expected: JSON shows exact-class mapping for 0,1,16,17,256,257,1024,1025,32768,32769
    Evidence: .sisyphus/evidence/task-3-size-classes.json

  Scenario: Oversized request routes to large path
    Tool: Bash
    Steps: ./build/tests/halloc_unit_tests --case large_threshold_routing --format json --out .sisyphus/evidence/task-3-size-classes-error.json
    Expected: JSON confirms requests above 32768 bypass small-size classes and enter the large-allocation path
    Evidence: .sisyphus/evidence/task-3-size-classes-error.json
  ```

  **Commit**: NO | Message: `feat(core): freeze size classes and cache budgets` | Files: `include/halloc/**`, `src/**`, `tests/**`

- [x] 4. Add the Unix-like virtual-memory and platform abstraction layer

  **What to do**: Introduce a small OS layer that isolates `mmap`, `munmap`, `madvise`, runtime page-size discovery, and platform feature detection for Linux and macOS. Use runtime page size from `sysconf(_SC_PAGESIZE)`. Expose a consistent VM interface used by the page heap and large-allocation path. For release-to-OS advice, support `MADV_FREE` on macOS and `MADV_DONTNEED` on Linux behind one abstraction; if advice is unsupported, return a documented “not supported” status without failing allocation paths.
  **Must NOT do**: Do not use `sbrk`. Do not inline OS calls throughout allocator code. Do not assume a fixed 4096-byte page size.

  **Recommended Agent Profile**:
  - Category: `unspecified-high` - Reason: low-level platform boundary with correctness and portability implications
  - Skills: `[]` - No additional repo-local skills available
  - Omitted: `[]` - No extra skills required

  **Parallelization**: Can Parallel: YES | Wave 1 | Blocks: 5,6,10 | Blocked By: 1

  **References** (executor has NO interview context - be exhaustive):
  - Pattern: `DESIGN.md:15-17` - portability and observability are first-class goals
  - Pattern: `DESIGN.md:230-242` - v1 reclamation is conservative and OS release is optional/simple
  - Pattern: `DESIGN.md:268-270` - reclaim policy must be measurable, not guessed

  **Acceptance Criteria** (agent-executable only):
  - [ ] `ctest --test-dir build --output-on-failure -R os_layer` passes
  - [ ] `./build/tests/halloc_unit_tests --case os_page_size_and_advise --format json --out .sisyphus/evidence/task-4-os.json` emits runtime page size, supported advise mode, and success/fallback paths

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Runtime page size and VM primitives work
    Tool: Bash
    Steps: ./build/tests/halloc_unit_tests --case os_page_size_and_advise --format json --out .sisyphus/evidence/task-4-os.json
    Expected: JSON contains positive page size, mmap/munmap success, and either a supported advise mode or explicit fallback status
    Evidence: .sisyphus/evidence/task-4-os.json

  Scenario: Invalid advise request fails cleanly
    Tool: Bash
    Steps: ./build/tests/halloc_unit_tests --case os_invalid_advise_mode --format json --out .sisyphus/evidence/task-4-os-error.json
    Expected: JSON reports rejected/unsupported mode without crashing or corrupting state
    Evidence: .sisyphus/evidence/task-4-os-error.json
  ```

  **Commit**: NO | Message: `feat(os): add virtual memory abstraction` | Files: `src/os/**`, `include/halloc/**`, `tests/**`

- [x] 5. Implement span metadata, terminology, and the 2-level pagemap

  **What to do**: Standardize on **span** as the primary term internally; do not alternate between slab and span in code-level names. Implement `Span` metadata for small/medium classes and large runs, including size class id, state, page count, capacity, live count, local-free count, remote-free pending head, owner token, and flags for `large` vs `small`. Implement a 2-level radix pagemap keyed by runtime page number, mapping every page in a span to its owning `Span*`. Keep metadata lookup unified for both small spans and large runs.
  **Must NOT do**: Do not use per-object headers. Do not use a generic hash table for pointer lookup. Do not make pagemap allocation recursively depend on the allocator being implemented.

  **Recommended Agent Profile**:
  - Category: `deep` - Reason: metadata and lookup design are core architecture choices with high correctness risk
  - Skills: `[]` - No additional repo-local skills available
  - Omitted: `[]` - No extra skills required

  **Parallelization**: Can Parallel: NO | Wave 1 | Blocks: 6,7,8,9,10 | Blocked By: 2,3,4

  **References** (executor has NO interview context - be exhaustive):
  - Pattern: `DESIGN.md:88-125` - required structures and pagemap-friendly metadata lookup
  - Pattern: `DESIGN.md:256-258` - metadata lookup cost is a critical bottleneck
  - Pattern: `DESIGN.md:369-375` - metadata should be out-of-band / pagemap-friendly and span sizing fixed in v1

  **Acceptance Criteria** (agent-executable only):
  - [ ] `ctest --test-dir build --output-on-failure -R metadata_lookup` passes
  - [ ] `./build/tests/halloc_unit_tests --case pagemap_lookup --format json --out .sisyphus/evidence/task-5-pagemap.json` proves small-span and large-run pointer classification is correct

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Pointer-to-span lookup works for small and large paths
    Tool: Bash
    Steps: ./build/tests/halloc_unit_tests --case pagemap_lookup --format json --out .sisyphus/evidence/task-5-pagemap.json
    Expected: JSON shows page-to-span lookups returning the correct Span metadata for small-span pages and large-run pages
    Evidence: .sisyphus/evidence/task-5-pagemap.json

  Scenario: Foreign pointer classification is rejected in tests
    Tool: Bash
    Steps: ./build/tests/halloc_unit_tests --case pagemap_foreign_pointer --format json --out .sisyphus/evidence/task-5-pagemap-error.json
    Expected: JSON reports the pointer is outside allocator-managed space or invalid for diagnostic builds
    Evidence: .sisyphus/evidence/task-5-pagemap-error.json
  ```

  **Commit**: NO | Message: `feat(core): add span metadata and pagemap` | Files: `src/core/**`, `include/halloc/**`, `tests/**`

- [x] 6. Implement central class shards, page heap, and the large-allocation path

  **What to do**: Build the central allocator backend in three cooperating pieces: per-size-class central shard, page heap, and large allocator. Use OS pages as the only v1 page unit. Use span families of `{4, 8, 16, 32}` pages and choose the smallest family that provides approximately 64 objects/span for classes up to 1 KiB, 16-32 objects/span for 1-8 KiB, and 4-8 objects/span above that. The page heap must split and coalesce page runs. Large requests `> 32768` bytes must bypass small spans and allocate dedicated runs tracked through the same pagemap.
  **Must NOT do**: Do not put the page heap on the fast path. Do not mix large allocations into small-class shards. Do not aggressively return every empty run to the OS in v1.

  **Recommended Agent Profile**:
  - Category: `unspecified-high` - Reason: multi-module backend assembly with measurable fragmentation behavior
  - Skills: `[]` - No additional repo-local skills available
  - Omitted: `[]` - No extra skills required

  **Parallelization**: Can Parallel: YES | Wave 2 | Blocks: 7,8,9,10 | Blocked By: 2,3,4,5

  **References** (executor has NO interview context - be exhaustive):
  - Pattern: `DESIGN.md:71-84` - shared span management, central page heap, and separate large path
  - Pattern: `DESIGN.md:200-213` - large-allocation policy and separation rationale
  - Pattern: `DESIGN.md:223-242` - reuse partial spans, return empty spans centrally, conservative OS release
  - Pattern: `DESIGN.md:260-270` - page-heap contention and reclaim instability risks

  **Acceptance Criteria** (agent-executable only):
  - [ ] `ctest --test-dir build --output-on-failure -R page_heap` passes
  - [ ] `ctest --test-dir build --output-on-failure -R large_alloc` passes
  - [ ] `./build/tests/halloc_unit_tests --case page_heap_and_large_runs --format json --out .sisyphus/evidence/task-6-backend.json` emits split/coalesce and large-run routing results

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Page heap splits, coalesces, and large path routes correctly
    Tool: Bash
    Steps: ./build/tests/halloc_unit_tests --case page_heap_and_large_runs --format json --out .sisyphus/evidence/task-6-backend.json
    Expected: JSON shows partial-span reuse, page-run split/coalesce, and >32768-byte requests classified as large runs
    Evidence: .sisyphus/evidence/task-6-backend.json

  Scenario: Double-return of a run is detected in tests
    Tool: Bash
    Steps: ./build/tests/halloc_unit_tests --case page_heap_double_return --format json --out .sisyphus/evidence/task-6-backend-error.json
    Expected: JSON reports the invalid second return was rejected in diagnostic/test mode
    Evidence: .sisyphus/evidence/task-6-backend-error.json
  ```

  **Commit**: NO | Message: `feat(backend): add central shards page heap and large path` | Files: `src/core/**`, `src/os/**`, `include/halloc/**`, `tests/**`

- [x] 7. Implement the thread-local cache fast path and same-thread allocation flow

  **What to do**: Add one thread-local cache per thread with one freelist per size class, cached counts, and cached-byte accounting. Refills must request batches from the central shard using `clamp(8192 / class_size, 8, 64)`. Per-class caps are 2 batches for classes `<= 1024` bytes and 1 batch above that. Per-thread cached-byte cap is 256 KiB and drains back to ~128 KiB when exceeded. The fast path for same-thread alloc/free must terminate entirely in the local cache when inventory is available.
  **Must NOT do**: Do not allow unbounded per-thread freelists. Do not touch the central page heap on every fast-path operation. Do not share local caches across threads.

  **Recommended Agent Profile**:
  - Category: `deep` - Reason: thread-local fast path is the main performance-critical subsystem
  - Skills: `[]` - No additional repo-local skills available
  - Omitted: `[]` - No extra skills required

  **Parallelization**: Can Parallel: YES | Wave 2 | Blocks: 8,9,10 | Blocked By: 2,3,5,6

  **References** (executor has NO interview context - be exhaustive):
  - Pattern: `DESIGN.md:64-86` - fast path must end in thread-local caches
  - Pattern: `DESIGN.md:150-173` - fast, refill, and slow path flow
  - Pattern: `DESIGN.md:252-254` - thread-cache memory blowup must be explicitly controlled

  **Acceptance Criteria** (agent-executable only):
  - [ ] `ctest --test-dir build --output-on-failure -R thread_cache` passes
  - [ ] `./build/tests/halloc_stress_tests --case same_thread_hot_path --format json --out .sisyphus/evidence/task-7-thread-cache.json` confirms local-hit dominated execution and enforced cache caps

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Same-thread hot path stays local
    Tool: Bash
    Steps: ./build/tests/halloc_stress_tests --case same_thread_hot_path --format json --out .sisyphus/evidence/task-7-thread-cache.json
    Expected: JSON shows local-hit count dominates local-miss count and cached bytes remain <= 262144 per thread
    Evidence: .sisyphus/evidence/task-7-thread-cache.json

  Scenario: Cache cap forces drain-back
    Tool: Bash
    Steps: ./build/tests/halloc_stress_tests --case thread_cache_cap_enforcement --format json --out .sisyphus/evidence/task-7-thread-cache-error.json
    Expected: JSON shows cached bytes exceed the soft target only transiently and drain back toward 131072 after pressure
    Evidence: .sisyphus/evidence/task-7-thread-cache-error.json
  ```

  **Commit**: NO | Message: `feat(frontend): add thread local cache fast path` | Files: `src/core/**`, `include/halloc/**`, `tests/**`

- [x] 8. Implement remote-free handling and owner-side drain behavior

  **What to do**: Implement a per-span intrusive MPSC singly linked remote-free inbox. Remote frees must push onto the owning span with atomics; the owner drains via `exchange(nullptr)` on refill/miss/maintenance points and splices reclaimed blocks into the local freelist in bounded batches. Add explicit thread-exit handling so thread-local cache destruction returns retained local inventory to the central layer and does not strand spans. Label concurrency tests under CTest `concurrency`.
  **Must NOT do**: Do not push remote frees directly into another thread’s local freelist. Do not switch to a distributed message-passing graph in v1. Do not leave thread-exit behavior unspecified.

  **Recommended Agent Profile**:
  - Category: `deep` - Reason: highest-risk concurrency and ownership task in the plan
  - Skills: `[]` - No additional repo-local skills available
  - Omitted: `[]` - No extra skills required

  **Parallelization**: Can Parallel: YES | Wave 2 | Blocks: 9,10 | Blocked By: 2,5,6,7

  **References** (executor has NO interview context - be exhaustive):
  - Pattern: `DESIGN.md:175-199` - same-thread vs cross-thread free contract
  - Pattern: `DESIGN.md:248-250` - remote-free correctness and throughput are the hardest v1 problem
  - Pattern: `DESIGN.md:377-379` - start with a simple bounded queue/list design in v1

  **Acceptance Criteria** (agent-executable only):
  - [ ] `ctest --test-dir build --output-on-failure -L concurrency` passes
  - [ ] `./build/tests/halloc_stress_tests --case remote_free_matrix --format json --out .sisyphus/evidence/task-8-remote-free.json` emits passing results for 1→1, 1→N, N→1, and mixed producer/consumer patterns
  - [ ] `./build/tests/halloc_stress_tests --case thread_exit_trim --format json --out .sisyphus/evidence/task-8-thread-exit.json` confirms cache cleanup on thread exit

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Remote-free matrix passes
    Tool: Bash
    Steps: ./build/tests/halloc_stress_tests --case remote_free_matrix --format json --out .sisyphus/evidence/task-8-remote-free.json
    Expected: JSON contains passing subcases for 1_to_1, 1_to_n, n_to_1, and mixed_producer_consumer with no lost blocks
    Evidence: .sisyphus/evidence/task-8-remote-free.json

  Scenario: Thread exit returns retained inventory
    Tool: Bash
    Steps: ./build/tests/halloc_stress_tests --case thread_exit_trim --format json --out .sisyphus/evidence/task-8-thread-exit.json
    Expected: JSON shows cached bytes and retained spans decrease after worker teardown and no leaked thread-local inventory remains
    Evidence: .sisyphus/evidence/task-8-thread-exit.json
  ```

  **Commit**: NO | Message: `feat(concurrency): add remote free inbox and drain` | Files: `src/core/**`, `include/halloc/**`, `tests/**`

- [x] 9. Add stats, diagnostics, and allocator self-observability

  **What to do**: Implement a minimal but sufficient stats subsystem exporting local hits/misses, refill/drain counts, remote-free enqueue/drain counts, spans by state, cached bytes, reserved bytes, live requested bytes, large-run bytes, and returned-to-OS bytes. Expose these via `stats_snapshot()` and make tests/benchmarks serialize them to JSON. Add diagnostic assertions in test/debug builds around pagemap lookup, span state transitions, and page-heap accounting.
  **Must NOT do**: Do not make stats collection optional in a way that removes required benchmark outputs. Do not add a heavy runtime telemetry framework. Do not require human log inspection as the only validation mechanism.

  **Recommended Agent Profile**:
  - Category: `unspecified-high` - Reason: cross-cutting observability with correctness hooks
  - Skills: `[]` - No additional repo-local skills available
  - Omitted: `[]` - No extra skills required

  **Parallelization**: Can Parallel: YES | Wave 2 | Blocks: 10 | Blocked By: 2,5,6,7,8

  **References** (executor has NO interview context - be exhaustive):
  - Pattern: `DESIGN.md:16-17` - observability is a core goal
  - Pattern: `DESIGN.md:41-46` - success criteria require measurable RSS, reserved, live, and fragmentation metrics
  - Pattern: `DESIGN.md:345-350` - Phase 5 explicitly calls for richer stats and tuning inputs

  **Acceptance Criteria** (agent-executable only):
  - [ ] `ctest --test-dir build --output-on-failure -R stats` passes
  - [ ] `./build/tests/halloc_unit_tests --case stats_snapshot --format json --out .sisyphus/evidence/task-9-stats.json` emits all required counters and ratios

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Stats snapshot exposes all required counters
    Tool: Bash
    Steps: ./build/tests/halloc_unit_tests --case stats_snapshot --format json --out .sisyphus/evidence/task-9-stats.json
    Expected: JSON includes local_hit, local_miss, refill, drain, remote_enqueue, remote_drain, spans_by_state, cached_bytes, reserved_bytes, live_requested_bytes, large_run_bytes, returned_to_os_bytes
    Evidence: .sisyphus/evidence/task-9-stats.json

  Scenario: Diagnostic invariant catches invalid state transition in tests
    Tool: Bash
    Steps: ./build/tests/halloc_unit_tests --case invalid_span_transition --format json --out .sisyphus/evidence/task-9-stats-error.json
    Expected: JSON reports the invariant violation was detected in diagnostic/test mode
    Evidence: .sisyphus/evidence/task-9-stats-error.json
  ```

  **Commit**: NO | Message: `feat(stats): add allocator counters and diagnostics` | Files: `src/core/**`, `include/halloc/**`, `tests/**`

- [x] 10. Deliver benchmark coverage, sanitizer matrix, and implementation completion gate

  **What to do**: Finish the benchmark and stress coverage defined in `DESIGN.md`. The benchmark binary must support `single_thread_hot`, `mixed_size_steady`, `burst_refill_drain`, `remote_free_matrix`, `long_run_fragmentation`, and `large_alloc_interference`. Output JSON (and optionally CSV) to a caller-provided path. Add ASan/UBSan and TSan build presets/labels to CI-style local execution commands. Ensure the benchmark success condition is structural and metric-emitting, not “must beat allocator X”. External allocator comparisons may be added as optional commands but must not block completion.
  **Must NOT do**: Do not require manual spreadsheet analysis. Do not make jemalloc/mimalloc/rpmalloc/TCMalloc comparisons mandatory for task completion. Do not ship benchmarks that only print human-readable prose.

  **Recommended Agent Profile**:
  - Category: `unspecified-high` - Reason: final integration across tests, stress, and benchmark outputs
  - Skills: `[]` - No additional repo-local skills available
  - Omitted: `[]` - No extra skills required

  **Parallelization**: Can Parallel: YES | Wave 2 | Blocks: F1-F4 | Blocked By: 1,2,3,4,5,6,7,8,9

  **References** (executor has NO interview context - be exhaustive):
  - Pattern: `DESIGN.md:272-312` - exact benchmark dimensions and matrix to implement
  - Pattern: `DESIGN.md:345-350` - tuning and external comparison are late-phase activities
  - Pattern: `DESIGN.md:385-396` - completion must stay within the recommended v1 summary

  **Acceptance Criteria** (agent-executable only):
  - [ ] `./build/benchmarks/halloc_bench --suite single_thread_hot --format json --out .sisyphus/evidence/task-10-bench-single.json` completes with throughput and latency metrics
  - [ ] `./build/benchmarks/halloc_bench --suite remote_free_matrix --format json --out .sisyphus/evidence/task-10-bench-remote.json` completes with concurrency metrics and remote-free counters
  - [ ] `./build/benchmarks/halloc_bench --suite long_run_fragmentation --format json --out .sisyphus/evidence/task-10-bench-frag.json` completes with RSS/reserved/live/fragmentation metrics
  - [ ] `cmake -S . -B build-asan -DHALLOC_ENABLE_ASAN=ON -DHALLOC_ENABLE_UBSAN=ON && cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure` passes
  - [ ] `cmake -S . -B build-tsan -DHALLOC_ENABLE_TSAN=ON && cmake --build build-tsan -j && ctest --test-dir build-tsan --output-on-failure -L concurrency` passes on supported platforms

  **QA Scenarios** (MANDATORY - task incomplete without these):
  ```
  Scenario: Full benchmark matrix emits machine-readable metrics
    Tool: Bash
    Steps: ./build/benchmarks/halloc_bench --suite all --format json --out .sisyphus/evidence/task-10-bench-all.json
    Expected: JSON contains entries for single_thread_hot, mixed_size_steady, burst_refill_drain, remote_free_matrix, long_run_fragmentation, and large_alloc_interference with throughput, p50, p99, p99.9, RSS, reserved, live, fragmentation, and remote-free counters
    Evidence: .sisyphus/evidence/task-10-bench-all.json

  Scenario: Concurrency sanitizer catches no data races on supported platforms
    Tool: Bash
    Steps: cmake -S . -B build-tsan -DHALLOC_ENABLE_TSAN=ON && cmake --build build-tsan -j && ctest --test-dir build-tsan --output-on-failure -L concurrency
    Expected: TSan-enabled concurrency suite exits 0 with no reported data races; if platform unsupported, configure step skips cleanly with documented message
    Evidence: .sisyphus/evidence/task-10-bench-tsan.txt
  ```

  **Commit**: NO | Message: `test(bench): add benchmark matrix and sanitizer gates` | Files: `benchmarks/**`, `tests/**`, `cmake/**`, `include/halloc/**`, `src/**`

## Final Verification Wave (MANDATORY — after ALL implementation tasks)
> 4 review agents run in PARALLEL. ALL must APPROVE. Present consolidated results to user and get explicit "okay" before completing.
> **Do NOT auto-proceed after verification. Wait for user's explicit approval before marking work complete.**
> **Never mark F1-F4 as checked before getting user's okay.** Rejection or user feedback -> fix -> re-run -> present again -> wait for okay.
- [x] F1. Plan Compliance Audit — oracle
- [x] F2. Code Quality Review — unspecified-high
- [x] F3. Real Manual QA — unspecified-high (+ playwright if UI)
- [x] F4. Scope Fidelity Check — deep

## Commit Strategy
- Prefer one commit per numbered task only when the user explicitly requests commits during execution.
- Default execution mode should keep changes local until the user asks for a commit.
- If commits are requested, use the task-specific messages listed below.

## Success Criteria
- The allocator builds cleanly in Debug, ASan/UBSan, and supported TSan configurations.
- Unit tests cover API semantics, size-class boundaries, metadata lookup, page-heap behavior, thread-cache behavior, remote frees, and large allocations.
- Stress tests cover same-thread, 1→1 remote free, 1→N, N→1, mixed producer/consumer, and thread-exit trimming behavior.
- Benchmark output includes throughput, p50/p99/p99.9 latency, RSS, reserved bytes, live requested bytes, fragmentation ratio, and remote-free counts.
- The v1 feature set stays within scope and defers per-CPU, NUMA, huge pages, hardening, and malloc interposition.
