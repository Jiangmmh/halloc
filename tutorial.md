# Memory Pool Allocator Tutorial

A step-by-step guide to understanding how memory pools work, using halloc as the example codebase.

---

## Table of Contents

1. [What is Memory Allocation?](#1-what-is-memory-allocation)
2. [The Problem with malloc/free](#2-the-problem-with-mallocfree)
3. [What is a Memory Pool?](#3-what-is-a-memory-pool)
4. [Basic Concepts](#4-basic-concepts)
5. [Step 1: Fixed-Size Block Allocator](#step-1-fixed-size-block-allocator)
6. [Step 2: Multiple Size Classes](#step-2-multiple-size-classes)
7. [Step 3: Thread-Local Caches](#step-3-thread-local-caches)
8. [Step 4: Span (Slab) Management](#step-4-span-slab-management)
9. [Step 5: Central Page Heap](#step-5-central-page-heap)
10. [Step 6: Remote-Free Path](#step-6-remote-free-path)
11. [How It All Fits Together](#how-it-all-fits-together)
12. [Key Takeaways](#key-takeaways)

---

## 1. What is Memory Allocation?

When you write:
```cpp
int* p = new int(42);
```

You're asking the operating system for memory. The `new` keyword calls a memory allocator, which finds a free chunk of bytes in RAM and gives it to your program.

Think of RAM like a giant parking lot with millions of parking spaces. Each space is one byte. When you need to park a car (store data), you need to find empty spaces.

**Questions to ponder:**
- Where does the memory come from?
- How does the allocator find free space?
- What happens when you free memory?
- Why is allocation sometimes slow?

---

## 2. The Problem with malloc/free

The standard `malloc` and `free` functions work, but they have issues:

### Problem 1: Metadata Overhead
Every `malloc` call stores metadata (size, next free block pointer, etc.) in the memory itself. This takes extra space.

```cpp
// You ask for 8 bytes, but malloc might give you 16+ bytes
// to store bookkeeping information
void* p = malloc(8);  // Actually uses 16+ bytes of memory
```

### Problem 2: Fragmentation
After many allocations and frees, memory becomes scattered:

```
Before: [AAAA][BBBB][CCCC][DDDD][EEEE]
After:  [AAAA][    ][CCCC][      ][EEEE]
                ^ gap                ^ gap

The gaps are too small for large allocations but waste space
```

### Problem 3: Lock Contention (Multi-threaded)
When 100 threads all call `malloc` at the same time, they compete for the same data structure (a global lock). Only one thread can allocate at a time!

### Problem 4: Cache Unfriendliness
`malloc` doesn't consider CPU caches. Objects that your program uses together might be far apart in memory.

---

## 3. What is a Memory Pool?

A **memory pool** (also called an **allocator** or **arena**) pre-allocates large chunks of memory and subdivides them. Instead of asking the OS for every small allocation, you ask once and subdivide.

**Analogy: Hotel vs. Camping**

- **Camping (malloc)**: Each tent (allocation) requires finding a spot in the wilderness (RAM), setting up individually.
- **Hotel (memory pool)**: You rent a floor (pre-allocated block), then assign rooms (smaller divisions) to guests (objects).

---

## 4. Basic Concepts

Before we start coding, let's understand the vocabulary:

| Term | Definition | Example |
|------|------------|---------|
| **Block** | One unit of allocated memory | `malloc(64)` returns a 64-byte block |
| **Span/Slab** | A contiguous region of memory, divided into blocks | A 4KB page split into 64-byte blocks |
| **Size Class** | A category of similar-sized allocations | All 60-80 byte requests → size class 64 |
| **Freelist** | A linked list of free blocks | `block1 → block2 → block3 → nullptr` |
| **Page** | OS memory unit (usually 4KB) | The smallest unit `mmap/munmap` works with |
| **Cache Line** | CPU's unit of memory access (usually 64 bytes) | Data is fetched from RAM in 64-byte chunks |

---

## Running the Code Examples

All code examples in this tutorial are compilable C++. To run them:

```bash
# Compile and run the simple pool example
g++ -std=c++17 simple_pool.cpp -o simple_pool
./simple_pool
```

The examples use standard POSIX APIs (`mmap`, `munmap`) which work on Linux and macOS.

---

## Step 1: Fixed-Size Block Allocator

Let's build the simplest possible allocator. It manages blocks of exactly ONE size.

### The Idea

```
Pool (pre-allocated):
┌─────────────────────────────────────────────────────┐
│ [block0][block1][block2][block3][block4]...[blockN]│
└─────────────────────────────────────────────────────┘
  ↑free   ↑free   ↑free   ↑free   ↑free

Allocate: Pop a block from the free list
Free: Push a block back to the free list
```

### Implementation

```cpp
class SimplePool {
    void* memory;
    void** free_list;
    size_t block_size;
    size_t pool_capacity;

public:
    SimplePool(size_t block_size, size_t num_blocks) {
        this->block_size = block_size;
        this->pool_capacity = num_blocks;

        memory = mmap(nullptr, block_size * num_blocks,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        free_list = (void**)memory;
        for (size_t i = 0; i < num_blocks - 1; i++) {
            free_list[i] = (char*)memory + (i + 1) * block_size;
        }
        free_list[num_blocks - 1] = nullptr;
    }

    void* allocate() {
        if (!free_list) return nullptr;
        void* block = free_list;
        free_list = (void**)*free_list;
        return block;
    }

    void deallocate(void* ptr) {
        *(void**)ptr = free_list;
        free_list = (void**)ptr;
    }
};
```

### Try It

```cpp
SimplePool pool(64, 1000);  // 1000 blocks of 64 bytes each

void* p1 = pool.allocate();  // Get a block
pool.deallocate(p1);          // Return it

void* p2 = pool.allocate();   // Same block as p1 (LIFO)
```

### Pros and Cons

| ✅ Advantages | ❌ Disadvantages |
|-------------|----------------|
| Extremely fast (just pointer ops) | Only works for one size |
| No fragmentation | Must pre-allocate everything |
| Predictable performance | |

### Check Your Understanding
- Why does `free_list[num_blocks - 1] = nullptr` mark the end of the free list?
- What would happen if we forgot to set `free_list[num_blocks - 1] = nullptr`?
- Why is `*(void**)ptr = free_list` used for the free operation?

---

## Step 2: Multiple Size Classes

The simple pool only handles one size. Real programs allocate objects of many sizes.

### The Idea

```
Size Class 16:  [16B][16B][16B][16B]...
Size Class 32:  [32B][32B][32B][32B]...
Size Class 64:  [64B][64B][64B][64B]...
Size Class 128: [128B][128B][128B][128B]...
```

### Size Class Table

```cpp
struct SizeClass {
    size_t block_size;     // The actual block size
    size_t batch_size;      // How many blocks to grab at once from page heap
};

static const SizeClass CLASSES[] = {
    {16,  256},   // Class 0: 16-byte blocks
    {32,  128},   // Class 1: 32-byte blocks
    {64,  64},    // Class 2: 64-byte blocks
    {128, 32},    // Class 3: 128-byte blocks
    // ... up to 32KB
};
```

### Which Class Do We Use?

```cpp
size_t get_size_class(size_t request) {
    if (request <= 16)  return 0;
    if (request <= 32)  return 1;
    if (request <= 64)  return 2;
    if (request <= 128) return 3;
    // ... etc
}
```

### Why Not Power of 2 Only?

```
Power of 2 only:  16, 32, 64, 128, 256, 512, 1024...
Waste example:    Request 60 bytes → get 64 bytes (6 bytes wasted, 10% waste)
                  Request 65 bytes → get 128 bytes (63 bytes wasted, 49% waste!)

With size classes: 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128...
Waste example:    Request 60 bytes → get 64 bytes (4 bytes wasted, 6% waste)
```

halloc uses 36 size classes with fine spacing at small sizes and wider spacing for larger allocations.

### Check Your Understanding
- If you request 70 bytes and size classes are 64 and 80, which class is used? How much waste?
- Why do small size classes have tighter spacing than large ones?
- What is the tradeoff between having many size classes vs. few?

---

## Step 3: Thread-Local Caches

Here's the key insight: **most allocations and frees happen on the same thread**.

### The Problem

```
Thread A: allocate allocate allocate
Thread B: allocate allocate allocate
Thread C: allocate allocate allocate

If all threads share ONE free list with ONE lock:
Thread A wants to allocate → WAIT for lock
Thread B wants to allocate → WAIT for lock
Thread C wants to allocate → WAIT for lock

Only ONE thread can do anything at a time! ❌
```

### The Solution: Thread-Local Storage

```cpp
// Each thread has its OWN free list
thread_local void** g_local_freelist = nullptr;
thread_local size_t  g_local_count = 0;

void* allocate(size_t size) {
    size_t class_idx = get_size_class(size);

    // Check thread-local cache first (FAST PATH - no lock!)
    if (g_local_count > 0) {
        g_local_count--;
        void* ptr = g_local_freelist;
        g_local_freelist = (void**)*g_local_freelist;
        return ptr;
    }

    // Cache miss: refill from shared pool (SLOW PATH - needs lock)
    return refill_from_shared_pool(class_idx);
}
```

### Why This Works

```
Thread A's cache:  [A1][A2][A3]  (only Thread A accesses this)
Thread B's cache:  [B1][B2][B3]  (only Thread B accesses this)

No locks needed! Each thread has its own data structure. ✅
```

### When Caches Interact

```
Thread A allocates → uses its cache → fast!
Thread B allocates → uses its cache → fast!
Thread A's cache is full → drains to shared pool
Thread B's cache is empty → refills from shared pool
```

### Check Your Understanding
- Why does thread-local caching eliminate lock contention on the fast path?
- What happens when a thread's local cache runs out of blocks?
- Why is "cache miss" slower than "cache hit"?

---

## Step 4: Span (Slab) Management

A **span** is a contiguous chunk of memory that holds blocks of the SAME size class.

### The Structure

```
Span (e.g., 4KB page = 4096 bytes)
┌────────────────────────────────────────────────────────────────────┐
│ [block0: 64B][block1: 64B][block2: 64B]...[blockN: 64B][unused] │
└────────────────────────────────────────────────────────────────────┘
  ↑free list starts here

Span Metadata (stored separately):
- size_class: 2 (64-byte blocks)
- block_size: 64
- capacity: 64 blocks
- free_count: how many blocks are free
- state: empty | partial | full
```

### Span State Machine

```
                    ┌─────────┐
         allocate   │         │   more allocs
      ────────────► │ PARTIAL │
      ◄──────────── │         │ ◄──────────────────
                    └────┬────┘
                         │
        drain all        │        allocate last
        free blocks      │        (becomes full)
                         ▼
┌─────────┐      ┌────────┐
│  EMPTY  │◄────►│  FULL  │
└─────────┘      └────────┘
     ▲              │
     │              │ deallocate span
     └──────────────┘
```

### Span Header

```cpp
struct Span {
    uint32_t size_class;        // Which size class this span serves
    uint32_t page_count;         // How many OS pages this span uses
    uint32_t capacity;           // Total bytes in this span
    uint32_t free_count;         // How many blocks are currently free
    void* memory;                // Pointer to the actual memory
    Span* next;                   // For linking spans together
};
```

### Why "Span" Instead of Just "Block"?

Because managing individual blocks is expensive. Managing spans lets us:

1. **Batch operations**: Allocate/free spans, not individual blocks
2. **Find blocks quickly**: Given a pointer, find its span (for deallocate)
3. **Coalesce memory**: Combine adjacent free spans

### Check Your Understanding
- What is the relationship between a span and an OS page?
- If a span has 64-byte blocks and we have a 4KB page, how many blocks per span?
- What does the "partial" state mean?

---

## Step 5: Central Page Heap

The **page heap** is the source of new spans. It gets memory from the OS and hands out spans.

### The Page Heap's Job

```
OS Memory (via mmap):
┌────────────────────────────────────────────────────────────────────┐
│                    256 MB or more of memory                       │
└────────────────────────────────────────────────────────────────────┘
        │
        │ allocate_pages(16 KB)  ← Request from OS
        ▼
┌──────────────────┐
│   Page Heap      │   Splits pages into spans
│                  │
│  free_spans_:    │
│    4-page: [S1]  │   ← Spans waiting to be used
│    8-page: [S2]  │
│   16-page: [S3]  │
└──────────────────┘
        │
        │ allocate_span(4 pages)
        ▼
┌──────────────────┐
│      Span        │   Hands out a span
│   (4 pages)      │
│                  │
│ [blk][blk][blk]  │
└──────────────────┘
```

### Coalescing (Combining Free Spans)

When spans are freed, the page heap tries to combine adjacent spans:

```
Before coalescing:
[4-page span][8-page span][4-page span]

After coalescing:
[         16-page span         ]

Fewer, larger spans = less waste
```

### Large Allocations

Allocations larger than a threshold (e.g., 32KB in halloc) get their own dedicated spans:

```cpp
void* allocate_large(size_t size) {
    if (size > LARGE_THRESHOLD) {
        // Get a span big enough for this allocation
        // No sharing - one span per large allocation
        return get_dedicated_span(size);
    }
    // Smaller allocations share spans (normal path)
}
```

### Check Your Understanding
- What is coalescing and why does it help reduce fragmentation?
- Why do large allocations (>32KB) get dedicated spans instead of being split into blocks?
- What is the page heap's role in the overall allocator design?

---

### How Does deallocate() Find the Span?

When you call `deallocate(ptr)`, the allocator must find which span owns that pointer. This is critical for knowing the size class and managing the free list.

**The Problem:**
```cpp
void* p = allocate(64);
// p points somewhere in the middle of a span's memory
deallocate(p, 64);  // Which span owns p?
```

**The Solution: Pagemap**

A pagemap is a lookup table that maps memory addresses to span metadata.

```
Memory Layout:
┌─────────────────────────────────────────────────────────────────┐
│ Span 1 (0x1000) │ Span 2 (0x2000) │ Span 3 (0x3000)          │
│ [64B blocks]     │ [64B blocks]     │ [64B blocks]             │
└─────────────────────────────────────────────────────────────────┘
      │                  │                  │
      ▼                  ▼                  ▼
┌─────────────────────────────────────────────────────────────┐
│ Pagemap (array indexed by page number):                       │
│ entries[0x1000/PAGE_SIZE] → Span 1 metadata                 │
│ entries[0x2000/PAGE_SIZE] → Span 2 metadata                 │
│ entries[0x3000/PAGE_SIZE] → Span 3 metadata                 │
└─────────────────────────────────────────────────────────────┘
```

**Implementation:**
```cpp
Span* pagemap_lookup(void* ptr) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t page_start = addr & ~(PAGE_SIZE - 1);  // Align to page
    return pagemap[page_start / PAGE_SIZE];
}
```

**Why not store metadata in each block?**

```cpp
// Wasteful - every block stores a pointer
struct Block { void* next; };  // 8 bytes overhead per block!
```

With pagemap, metadata is stored once per span (e.g., 1 entry per 4KB page) instead of once per block. For 64-byte blocks, that's 64x less metadata!

---

## Step 6: Remote-Free Path

This is the trickiest part. What happens when Thread A allocates memory and Thread B frees it?

### The Problem

```
Thread A (Producer):
    void* p = allocate(64);
    send_to_queue(p);  // Sends pointer to another thread

Thread B (Consumer):
    void* p = receive_from_queue();
    deallocate(p, 64);  // Frees memory allocated by Thread A!
```

If we just push `p` onto Thread B's local cache, we have a problem: Thread A's cache might be empty while Thread B's cache has memory it can't use.

### The Solution: Remote-Free Queue

```
Thread A's span (owns the memory):
┌─────────────────────────────────────┐
│ [blk][blk][blk][blk][blk][blk][blk] │
│      ↑free_list (Thread A)          │
└─────────────────────────────────────┘
          ▲
          │ Thread B tries to free
          │
          │ Enqueue to REMOTE-FREE QUEUE
          │
          ▼
┌─────────────────────────────────────┐
│         Remote-Free Queue           │
│   (at span or central page heap)   │
└─────────────────────────────────────┘
          │
          │ Thread A needs memory
          │
          │ Drain remote-free queue
          │ (happens during allocate miss)
          │
          ▼
Thread A's cache now has blocks that were freed by Thread B!
```

### The Key Insight

> **Don't return remote frees directly to another thread's cache.**
> Queue them, and let the owning thread drain them during its next allocation.

This keeps ownership clear and prevents races.

### Implementation Sketch

```cpp
void deallocate(void* ptr, size_t size) {
    Span* span = find_span_containing(ptr);
    uint64_t current_thread = get_current_thread_id();

    if (span->owner != current_thread) {
        // Remote free! Enqueue to span's remote queue
        enqueue_remote_free(span);
        return;
    }

    // Same-thread free: return to local cache
    push_to_local_cache(ptr);
}
```

### Check Your Understanding
- Why can't we just return remote frees directly to the owning thread's cache?
- What is the "duplicate enqueue prevention" check and why is it needed?
- When does the owning thread drain its remote-free queue?

---

## How It All Fits Together

Here's the complete picture of a production allocator like halloc:

```
┌─────────────────────────────────────────────────────────────────────┐
│                         APPLICATION CODE                            │
│                    allocate(64) / deallocate(p, 64)                 │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      THREAD-LOCAL CACHE                            │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │ FreeList[0]: [blk][blk]...     ← 16-byte class             │    │
│  │ FreeList[1]: [blk][blk]...     ← 32-byte class             │    │
│  │ FreeList[2]: [blk][blk]...     ← 64-byte class ← FAST PATH │    │
│  │ ...                                                         │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              │                                       │
│              cache miss ────►│◄──── cache hit                        │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │              SHARED SPAN MANAGEMENT                         │    │
│  │  Partial spans per class    │    Remote-free queues       │    │
│  │  [S1: 32 free][S2: 16 free] │    [R1][R2][R3]             │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              │                                       │
│              need more ─────►│◄──── have some                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                    CENTRAL PAGE HEAP                        │    │
│  │  ┌─────────────────────────────────────────────────────┐  │    │
│  │  │  free_spans_:  {4-page: [S..]}, {8-page: [S..]}   │  │    │
│  │  │  large_spans_: [Span: 128KB], [Span: 256KB]         │  │    │
│  │  └─────────────────────────────────────────────────────┘  │    │
│  │                              │                              │    │
│  │              out of memory ──┘                              │    │
│  └──────────────────────────────┬─────────────────────────────┘    │
│                                 ▼                                      │
│  ┌──────────────────────────────────────────────────────────────┐    │
│  │                    OPERATING SYSTEM                            │    │
│  │              mmap() / munmap() / sbrk()                      │    │
│  └──────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
```

### The Fast Path (No Contention)

```
1. allocate(64)
2. Is size <= 32KB? Yes
3. size_class = 2 (64-byte class)
4. Check local cache: FreeList[2] has blocks? Yes
5. Pop one block, return pointer
6. Done! Total time: ~5 nanoseconds, no locks needed!
```

### The Slow Path (Cache Miss)

```
1. allocate(64)
2. Is size <= 32KB? Yes
3. size_class = 2
4. Check local cache: FreeList[2] is EMPTY ❌
5. Drain remote-free queue for this thread
6. Ask shared span manager for more blocks
7. If no spans available, ask page heap for a new span
8. If page heap is empty, ask OS for more pages
9. Split span into blocks, populate local cache
10. Pop one block, return pointer
11. Done! Total time: ~100-1000 nanoseconds (but rare)
```

---

## Key Takeaways

### 1. Memory Pools Beat malloc for Specific Workloads

| Scenario | Use Memory Pool? |
|----------|------------------|
| Many small, fixed-size allocations | ✅ Yes |
| Multi-threaded allocation | ✅ Yes |
| Real-time/latency-sensitive | ✅ Yes |
| One-time large allocations | ❌ No (use malloc) |
| Variable-size big data | ❌ No (use malloc) |

### 2. The Three Levels

```
Application ──► Thread Cache ──► Shared Spans ──► Page Heap ──► OS
                    (fast)          (batch)         (refill)    (pages)
```

### 3. Remote-Free Is Critical for Multi-Threaded Programs

Without it:
- Memory leaks (objects freed by wrong thread never reclaimed)
- Contention (forcing all frees through a global lock)

With it:
- Memory returns to owning thread efficiently
- No global locks on the hot path

### 4. Size Classes Trade Off Overhead vs. Fragmentation

```
Too few classes ──► More internal fragmentation (wasted space)
Too many classes ──► More overhead (more spans to manage)

halloc's 36 classes is a sweet spot for most workloads
```

### 5. Benchmark Everything

Theoretical optimization ≠ actual performance. Always measure with realistic workloads.

---

## What's Next?

Now that you understand the concepts, explore halloc's code:

| File | What to Study |
|------|--------------|
| `include/halloc/size_class.h` | Size class table definition |
| `include/halloc/span.h` | Span structure and states |
| `src/thread_cache.cpp` | Thread-local cache implementation |
| `src/page_heap.cpp` | Central page heap and OS interaction |
| `DESIGN.md` | Full design document |

### Exercises

1. **Modify halloc's batch size**: Change `BATCH_SIZE` in size_class.h, measure performance difference
2. **Add a new size class**: Insert a class between existing ones, observe fragmentation changes
3. **Profile the slow path**: Add timing to the allocate function, measure how often you hit cache miss
4. **Trace memory lifecycle**: Use a debugger to watch a block from `mmap` through `allocate` to `deallocate` back to the OS

---

## Further Reading

- [jemalloc](http://jemalloc.net/) - Production allocator with advanced features
- [mimalloc](https://microsoft.github.io/mimalloc/) - Microsoft's allocator, simpler but fast
- [TCMalloc](https://github.com/google/tcmalloc) - Google's allocator, inspiration for many designs
- [ptmalloc2](https://www.malloc.de/en/) - The standard ptmalloc, good historical reference

---

*This tutorial is part of the halloc project. See [README.md](README.md) for the full project documentation.*