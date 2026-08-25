/*! @file
  @brief
  Test program for ESTALLOC library.

  <pre>
  Original Copyright:
    (C) 2025- HASUMI Hitoshi @hasumikin

  This file is distributed under BSD 3-Clause License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>

#include "../estalloc.h"

#if defined(ESTALLOC_ADDRESS_16BIT)
# define POOL_SIZE (1024 * 64 - 1)    // 64KB pool
#else
# define POOL_SIZE (1024 * 1024 - 1)  // 1MB pool
#endif
#define MAX_ALLOCS 1000          // Maximum number of allocations to track
#define MAX_ITERATIONS 10000     // Number of allocation operations to perform
# define MAX_ALLOC_SIZE 8192      // Maximum allocation size

enum operation_type {
  MALLOC = 0,
  CALLOC = 1,
  REALLOC = 2,
  PERMALLOC = 3,
  FREE = 4
};

typedef struct {
  void *ptr;
  size_t size;
  enum operation_type type;
} AllocInfo;

// Function to check calloc zero initialization
static int
is_zero_filled(void *ptr, size_t size)
{
  unsigned char *p = (unsigned char *)ptr;
  for (size_t i = 0; i < size; i++) {
    if (p[i] != 0) {
      return 0;
    }
  }
  return 1;
}

// Function to check calloc zero initialization
static void
fill_memory(void *ptr, size_t size, unsigned char value)
{
  unsigned char *p = (unsigned char *)ptr;
  for (size_t i = 0; i < size; i++) {
    p[i] = value;
  }
}

// Function to check if memory content is preserved after realloc
static int
check_memory_content(void *ptr, size_t size, unsigned char value)
{
  unsigned char *p = (unsigned char *)ptr;
  for (size_t i = 0; i < size; i++) {
    if (p[i] != value) {
      return 0;
    }
  }
  return 1;
}

// ================================================================
// Layout tests
// ================================================================

/*
  Regression test for issue #2 (arm64_32 crash).

  A pool header whose size is not a multiple of ESTALLOC_ALIGNMENT shifts every
  block off its boundary and makes the chain miss the end-of-pool sentinel. The
  symptom on the device was a wild pointer inside remove_free_block(), so check
  the alignment directly and walk the pool until it is exhausted.
*/
#define LAYOUT_ALLOCS 16

/*
  A user pointer sits sizeof(USED_BLOCK) bytes into an ESTALLOC_ALIGNMENT
  aligned block. That header is 8 bytes in 24-bit address mode but only 4 bytes
  in 16-bit address mode, so ESTALLOC_ALIGNMENT=8 combined with
  ESTALLOC_ADDRESS_16BIT hands out 4-byte aligned pointers by construction.
*/
#if defined(ESTALLOC_ADDRESS_16BIT)
# define EXPECTED_USER_ALIGNMENT 4
#else
# define EXPECTED_USER_ALIGNMENT ESTALLOC_ALIGNMENT
#endif
#define USER_ALIGNMENT_MASK (EXPECTED_USER_ALIGNMENT - 1)

static int
test_pool_layout(void)
{
  printf("--- test_pool_layout ---\n");
  const unsigned int size = 4096;
  void *mem = malloc(size);
  if (mem == NULL) {
    printf("FAILED: could not allocate the pool\n");
    return 1;
  }
  if (((uintptr_t)mem & ALIGNMENT_MASK) != 0) {
    printf("SKIPPED: malloc() returned %p, not %d-byte aligned\n", mem, ESTALLOC_ALIGNMENT);
    free(mem);
    return 0;
  }

  int failures = 0;
  ESTALLOC *est = est_init(mem, size);
  void *ptrs[LAYOUT_ALLOCS] = {0};

  for (int i = 0; i < LAYOUT_ALLOCS; i++) {
    size_t alloc_size = (size_t)(i * 7 + 1);
    ptrs[i] = est_malloc(est, (unsigned int)alloc_size);
    if (ptrs[i] == NULL) {
      printf("FAILED: est_malloc(%zu) returned NULL\n", alloc_size);
      failures++;
      break;
    }
    if (((uintptr_t)ptrs[i] & USER_ALIGNMENT_MASK) != 0) {
      printf("FAILED: est_malloc(%zu) returned %p, not %d-byte aligned\n",
             alloc_size, ptrs[i], EXPECTED_USER_ALIGNMENT);
      failures++;
    }
    fill_memory(ptrs[i], alloc_size, (unsigned char)i);
  }

  // Exhaust the pool. A misaligned header makes this walk off the sentinel.
  while (est_malloc(est, 64) != NULL) { /* no-op */ }

  for (int i = 0; i < LAYOUT_ALLOCS; i++) {
    if (ptrs[i] == NULL) continue;
    size_t alloc_size = (size_t)(i * 7 + 1);
    if (!check_memory_content(ptrs[i], alloc_size, (unsigned char)i)) {
      printf("FAILED: block %d was corrupted by a later allocation\n", i);
      failures++;
    }
  }

#if defined(ESTALLOC_DEBUG)
  int sanity = est_sanity_check(est);
  if (sanity != 0) {
    printf("FAILED: est_sanity_check() returned 0x%x\n", sanity);
    failures++;
  }
#endif

  for (int i = 0; i < LAYOUT_ALLOCS; i++) {
    if (ptrs[i] != NULL) est_free(est, ptrs[i]);
  }

  if (failures == 0) printf("PASSED\n");
  free(mem);
  return failures;
}

static int
run_layout_tests(void)
{
  printf("\n=== Layout Tests ===\n");
  fprintf(stderr, "sizeof(void *): %zu, ESTALLOC_ALIGNMENT: %d\n",
          sizeof(void *), ESTALLOC_ALIGNMENT);
#if defined(PLATFORM_64BIT)
  fprintf(stderr, "PLATFORM_64BIT: defined\n");
#else
  fprintf(stderr, "PLATFORM_64BIT: not defined\n");
#endif
  int failures = test_pool_layout();
  printf("=== Layout Tests: %s (%d failure(s)) ===\n\n",
         failures == 0 ? "PASSED" : "FAILED", failures);
  return failures;
}

// ================================================================
// Critical section test infrastructure
// ================================================================

static int cs_enter_count = 0;
static int cs_exit_count = 0;

static void
cs_enter(void)
{
  cs_enter_count++;
}

static void
cs_exit_cb(void)
{
  cs_exit_count++;
}

static void
cs_reset(void)
{
  cs_enter_count = 0;
  cs_exit_count = 0;
}

// Test: est_set_critical_section registers and clears callbacks
static int
test_cs_registration(void)
{
  printf("--- test_cs_registration ---\n");
  void *mem = malloc(4096);
  ESTALLOC *est = est_init(mem, 4096);

  est_set_critical_section(est, cs_enter, cs_exit_cb);
  if (est->enter_critical != cs_enter || est->exit_critical != cs_exit_cb) {
    printf("FAILED: callbacks not registered\n");
    free(mem);
    return 1;
  }
  est_set_critical_section(est, NULL, NULL);
  if (est->enter_critical != NULL || est->exit_critical != NULL) {
    printf("FAILED: callbacks not cleared\n");
    free(mem);
    return 1;
  }
  printf("PASSED\n");
  free(mem);
  return 0;
}

// Test: NULL callbacks do not crash
static int
test_cs_null_callbacks(void)
{
  printf("--- test_cs_null_callbacks ---\n");
  void *mem = malloc(4096);
  ESTALLOC *est = est_init(mem, 4096);

  void *ptr = est_malloc(est, 64);
  if (ptr == NULL) {
    printf("FAILED: malloc with NULL callbacks returned NULL\n");
    free(mem);
    return 1;
  }
  void *new_ptr = est_realloc(est, ptr, 128);
  if (new_ptr == NULL) {
    est_free(est, ptr);
  } else {
    est_free(est, new_ptr);
  }
#if defined(ESTALLOC_DEBUG)
  est_take_statistics(est);
#endif
  printf("PASSED\n");
  free(mem);
  return 0;
}

// Test: malloc calls enter/exit exactly once on success
static int
test_cs_malloc(void)
{
  printf("--- test_cs_malloc ---\n");
  void *mem = malloc(4096);
  ESTALLOC *est = est_init(mem, 4096);
  est_set_critical_section(est, cs_enter, cs_exit_cb);
  cs_reset();

  void *ptr = est_malloc(est, 64);
  if (ptr == NULL) {
    printf("FAILED: malloc returned NULL\n");
    free(mem);
    return 1;
  }
  if (cs_enter_count != 1 || cs_exit_count != 1) {
    printf("FAILED: enter=%d exit=%d (expected 1, 1)\n", cs_enter_count, cs_exit_count);
    free(mem);
    return 1;
  }
  printf("PASSED\n");
  free(mem);
  return 0;
}

// Test: free calls enter/exit exactly once
static int
test_cs_free(void)
{
  printf("--- test_cs_free ---\n");
  void *mem = malloc(4096);
  ESTALLOC *est = est_init(mem, 4096);
  void *ptr = est_malloc(est, 64);
  if (ptr == NULL) {
    printf("FAILED: initial malloc returned NULL\n");
    free(mem);
    return 1;
  }
  est_set_critical_section(est, cs_enter, cs_exit_cb);
  cs_reset();

  est_free(est, ptr);
  if (cs_enter_count != 1 || cs_exit_count != 1) {
    printf("FAILED: enter=%d exit=%d (expected 1, 1)\n", cs_enter_count, cs_exit_count);
    free(mem);
    return 1;
  }
  printf("PASSED\n");
  free(mem);
  return 0;
}

// Test: malloc OOM still calls exit (critical section is balanced)
static int
test_cs_malloc_oom(void)
{
  printf("--- test_cs_malloc_oom ---\n");
  void *mem = malloc(1024);
  ESTALLOC *est = est_init(mem, 1024);
  // Fill pool
  while (est_malloc(est, 64) != NULL) {}
  est_set_critical_section(est, cs_enter, cs_exit_cb);
  cs_reset();

  void *ptr = est_malloc(est, 64);
  if (ptr != NULL) {
    printf("SKIPPED: pool not full\n");
    free(mem);
    return 0;
  }
  if (cs_enter_count == 0) {
    printf("FAILED: critical section not called on OOM\n");
    free(mem);
    return 1;
  }
  if (cs_enter_count != cs_exit_count) {
    printf("FAILED: critical section not balanced on OOM (enter=%d exit=%d)\n",
           cs_enter_count, cs_exit_count);
    free(mem);
    return 1;
  }
  printf("PASSED\n");
  free(mem);
  return 0;
}

// Test: permalloc critical section is balanced
static int
test_cs_permalloc(void)
{
  printf("--- test_cs_permalloc ---\n");
  void *mem = malloc(4096);
  ESTALLOC *est = est_init(mem, 4096);
  est_set_critical_section(est, cs_enter, cs_exit_cb);
  cs_reset();

  void *ptr = est_permalloc(est, 64);
  if (ptr == NULL) {
    printf("SKIPPED: permalloc returned NULL\n");
    free(mem);
    return 0;
  }
  if (cs_enter_count == 0 || cs_enter_count != cs_exit_count) {
    printf("FAILED: enter=%d exit=%d (expected balanced, >0)\n", cs_enter_count, cs_exit_count);
    free(mem);
    return 1;
  }
  printf("PASSED (enter=%d exit=%d)\n", cs_enter_count, cs_exit_count);
  free(mem);
  return 0;
}

// Test: realloc critical section is balanced (covers in-place and alloc-copy paths)
static int
test_cs_realloc(void)
{
  printf("--- test_cs_realloc ---\n");
  int failures = 0;

  // in-place shrink path
  {
    void *mem = malloc(4096);
    ESTALLOC *est = est_init(mem, 4096);
    void *ptr = est_malloc(est, 256);
    if (ptr == NULL) { printf("FAILED: initial malloc returned NULL\n"); free(mem); return 1; }
    fill_memory(ptr, 256, 0xAA);
    est_set_critical_section(est, cs_enter, cs_exit_cb);
    cs_reset();
    void *new_ptr = est_realloc(est, ptr, 64);
    if (new_ptr == NULL) {
      printf("  shrink: SKIPPED\n");
    } else {
      if (cs_enter_count == 0 || cs_enter_count != cs_exit_count) {
        printf("  shrink: FAILED (enter=%d exit=%d)\n", cs_enter_count, cs_exit_count);
        failures++;
      } else {
        printf("  shrink: PASSED (enter=%d exit=%d)\n", cs_enter_count, cs_exit_count);
      }
      est_free(est, new_ptr);
    }
    free(mem);
  }

  // alloc-and-copy path: blocker prevents in-place expansion
  {
    void *mem = malloc(4096);
    ESTALLOC *est = est_init(mem, 4096);
    void *blocker = est_malloc(est, 128);
    void *ptr = est_malloc(est, 64);
    if (ptr == NULL || blocker == NULL) {
      printf("  alloc-copy: SKIPPED (initial malloc failed)\n");
    } else {
      fill_memory(ptr, 64, 0xBB);
      est_set_critical_section(est, cs_enter, cs_exit_cb);
      cs_reset();
      // Request larger than blocker allows in-place
      void *new_ptr = est_realloc(est, ptr, 2048);
      if (new_ptr == NULL) {
        // OOM: still must be balanced
        if (cs_enter_count == 0 || cs_enter_count != cs_exit_count) {
          printf("  alloc-copy (OOM): FAILED (enter=%d exit=%d)\n",
                 cs_enter_count, cs_exit_count);
          failures++;
        } else {
          printf("  alloc-copy (OOM): PASSED (enter=%d exit=%d)\n",
                 cs_enter_count, cs_exit_count);
        }
      } else {
        if (cs_enter_count == 0 || cs_enter_count != cs_exit_count) {
          printf("  alloc-copy: FAILED (enter=%d exit=%d)\n",
                 cs_enter_count, cs_exit_count);
          failures++;
        } else {
          printf("  alloc-copy: PASSED (enter=%d exit=%d)\n",
                 cs_enter_count, cs_exit_count);
        }
        est_free(est, new_ptr);
      }
      est_free(est, blocker);
    }
    free(mem);
  }

  return failures;
}

// Test: take_statistics calls enter/exit exactly once
#if defined(ESTALLOC_DEBUG)
static int
test_cs_statistics(void)
{
  printf("--- test_cs_statistics ---\n");
  void *mem = malloc(4096);
  ESTALLOC *est = est_init(mem, 4096);
  est_set_critical_section(est, cs_enter, cs_exit_cb);
  cs_reset();

  est_take_statistics(est);
  if (cs_enter_count != 1 || cs_exit_count != 1) {
    printf("FAILED: enter=%d exit=%d (expected 1, 1)\n", cs_enter_count, cs_exit_count);
    free(mem);
    return 1;
  }
  printf("PASSED\n");
  free(mem);
  return 0;
}
#endif

static int
run_critical_section_tests(void)
{
  printf("\n=== Critical Section Tests ===\n");
  int failures = 0;
  failures += test_cs_registration();
  failures += test_cs_null_callbacks();
  failures += test_cs_malloc();
  failures += test_cs_free();
  failures += test_cs_malloc_oom();
  failures += test_cs_permalloc();
  failures += test_cs_realloc();
#if defined(ESTALLOC_DEBUG)
  failures += test_cs_statistics();
#endif
  printf("=== Critical Section Tests: %s (%d failure(s)) ===\n\n",
         failures == 0 ? "PASSED" : "FAILED", failures);
  return failures;
}

// ================================================================

#ifdef ESTALLOC_DEBUG
// Print the meaning of sanity check errors
static void
print_sanity_error(int error_code)
{
  if (error_code == 0) {
    printf("Memory pool is healthy\n");
    return;
  }
  printf("FATAL: sanity_check error (code: 0x%x):\n", error_code);
  if (error_code & 0x01) printf("- Block alignment error\n");
  if (error_code & 0x02) printf("- Invalid block size\n");
  if (error_code & 0x04) printf("- Invalid next block address\n");
  if (error_code & 0x08) printf("- Previous block usage flag inconsistency (used->free)\n");
  if (error_code & 0x10) printf("- Previous block usage flag inconsistency (free->used)\n");
}
#endif

// Log allocation or free operation
static void
log_operation(enum operation_type op, void *ptr, size_t size, int result)
{
  const char *operation = NULL;
  switch (op) {
    case MALLOC:
      operation = "MALLOC";
      break;
    case CALLOC:
      operation = "CALLOC";
      break;
    case REALLOC:
      operation = "REALLOC";
      break;
    case PERMALLOC:
      operation = "PERMALLOC";
      break;
    case FREE:
      operation = "FREE";
      break;
    default:
      operation = "UNKNOWN";
      break;
  }
  printf("%-8s: ptr=%p, size=%zu, %s\n", operation, ptr, size, result ? "SUCCESS" : "FAILED");
}

int
main()
{
  if (run_layout_tests() != 0) {
    fprintf(stderr, "Layout tests failed\n");
    return 1;
  }

  if (run_critical_section_tests() != 0) {
    fprintf(stderr, "Critical section tests failed\n");
    return 1;
  }

  // print size of structures
#if defined(ESTALLOC_DEBUG)
  fprintf(stderr, "sizeof(ESTALLOC_PROF): %zu\n", sizeof(ESTALLOC_PROF));
#endif
  fprintf(stderr, "sizeof(ESTALLOC_STAT): %zu\n", sizeof(ESTALLOC_STAT));
  fprintf(stderr, "sizeof(ESTALLOC): %zu\n", sizeof(ESTALLOC));
  fprintf(stderr, "\n");

  void *pool_memory = malloc(POOL_SIZE);
  if (!pool_memory) {
    fprintf(stderr, "Failed to allocate memory for pool\n");
    return 1;
  }

  // Initialize memory pool
  ESTALLOC *est = est_init(pool_memory, POOL_SIZE);
  printf("Memory pool initialized at %p, size: %d bytes\n", pool_memory, POOL_SIZE);

#ifdef ESTALLOC_DEBUG
  // Start profiling if debug is enabled
  est_start_profiling(est);
#endif

  // Seed random number generator
  srand((unsigned int)time(NULL));

  // Array to keep track of allocations
  AllocInfo allocs[MAX_ALLOCS] = {0};
  int alloc_count = 0;
  int total_ops = 0;
  int malloc_ops = 0, calloc_ops = 0, realloc_ops = 0, free_ops = 0, permalloc_ops = 0;

  // Main test loop
  for (int i = 0; i < MAX_ITERATIONS; i++) {
    // Occasionally check pool health
    if (i % 1000 == 0) {
#ifdef ESTALLOC_DEBUG
      int result = est_sanity_check(est);
      printf("\n--- Sanity check at iteration %d ---\n", i);
      print_sanity_error(result);
      if (result != 0) {
#ifdef ESTALLOC_PRINT_DEBUG
        est_fprint_pool_header(est, stdout);
        est_fprint_memory_block(est, stdout);
#endif
        fprintf(stderr, "Test failed: Sanity check failed\n");
        return 1;
      }
#endif
    }

    // Decide what operation to perform (with bias towards allocation)
    int op = rand() % 100;

    if (op < 40 || alloc_count < 10) {  // 40% chance of malloc or when few allocations exist
      // Allocate memory
      size_t size = (rand() % MAX_ALLOC_SIZE) + 1;
      void *ptr = est_malloc(est, size);

      // If allocation successful, store it
      if (ptr) {
        if (alloc_count < MAX_ALLOCS) {
          allocs[alloc_count].ptr = ptr;
          allocs[alloc_count].size = size;
          allocs[alloc_count].type = MALLOC;
          alloc_count++;

          // Fill allocated memory with a pattern
          fill_memory(ptr, size, 0x99);
          log_operation(MALLOC, ptr, size, 1);
        } else {
          // Too many allocations to track, free immediately
          est_free(est, ptr);
        }
        malloc_ops++;
      } else {
        log_operation(MALLOC, NULL, size, 0);
      }
    }
    else if (op < 60 && alloc_count < MAX_ALLOCS) {  // 20% chance of calloc
      // Allocate and zero-initialize memory
      size_t nmemb = (rand() % 100) + 1;
      size_t size = (rand() % 100) + 1;
      void *ptr = est_calloc(est, nmemb, size);

      if (ptr) {
        // Verify that memory is zeroed
        if (!is_zero_filled(ptr, nmemb * size)) {
          printf("FATAL: Calloc memory not zeroed!\n");
          return 1;
        }

        allocs[alloc_count].ptr = ptr;
        allocs[alloc_count].size = nmemb * size;
        allocs[alloc_count].type = CALLOC;
        alloc_count++;
        log_operation(CALLOC, ptr, nmemb * size, 1);
        calloc_ops++;
      } else {
        log_operation(CALLOC, NULL, nmemb * size, 0);
      }
    }
    else if (op < 75 && alloc_count > 0) {  // 15% chance of realloc (when allocations exist)
      // Resize existing allocation
      int idx = rand() % alloc_count;

      // Don't try to free permalloc'd memory
      if (allocs[idx].type == PERMALLOC) {
        continue;
      }

      size_t new_size = (rand() % MAX_ALLOC_SIZE) + 1;

      // Fill with a recognizable pattern for later verification
      unsigned char pattern = 0xBB;
      size_t verify_size = allocs[idx].size < new_size ? allocs[idx].size : new_size;
      fill_memory(allocs[idx].ptr, allocs[idx].size, pattern);

      void *new_ptr = est_realloc(est, allocs[idx].ptr, new_size);

      if (new_ptr) {
        // Verify content is preserved up to the smaller of the old/new sizes
        if (!check_memory_content(new_ptr, verify_size, pattern)) {
            printf("FATAL: Realloc did not preserve memory content!\n");
            return 1;
        }

        allocs[idx].ptr = new_ptr;
        allocs[idx].size = new_size;
        allocs[idx].type = REALLOC;
        log_operation(REALLOC, new_ptr, new_size, 1);
        realloc_ops++;
      } else {
        log_operation(REALLOC, NULL, new_size, 0);
      }
    }
    else if (op < 80 && alloc_count < MAX_ALLOCS) {  // 5% chance of permalloc
      // Permanent allocation (can't be freed)
      size_t size = (rand() % 512) + 1; // Smaller size for permalloc
      void *ptr = est_permalloc(est, size);

      if (ptr) {
        allocs[alloc_count].ptr = ptr;
        allocs[alloc_count].size = size;
        allocs[alloc_count].type = PERMALLOC;
        alloc_count++;
        fill_memory(ptr, size, 0xCC);
        log_operation(PERMALLOC, ptr, size, 1);
        permalloc_ops++;
      } else {
        log_operation(PERMALLOC, NULL, size, 0);
      }
    }
    else if (alloc_count > 0) {  // Free operation (when allocations exist)
      // Free an existing allocation
      int idx = rand() % alloc_count;

      // Don't try to free permalloc'd memory
      if (allocs[idx].type == PERMALLOC) {
        continue;
      }

      void *ptr = allocs[idx].ptr;
      est_free(est, ptr);
      log_operation(FREE, ptr, allocs[idx].size, 1);
      free_ops++;

      // Remove this allocation from our tracking
      allocs[idx] = allocs[alloc_count - 1];
      alloc_count--;
    }

    total_ops++;
  }

  // Print test summary
  printf("\n=== Test Summary ===\n");
  printf("Total operations: %d\n", total_ops);
  printf("- malloc: %d\n", malloc_ops);
  printf("- calloc: %d\n", calloc_ops);
  printf("- realloc: %d\n", realloc_ops);
  printf("- permalloc: %d\n", permalloc_ops);
  printf("- free: %d\n", free_ops);
  printf("Remaining allocations: %d\n", alloc_count);

#ifdef ESTALLOC_DEBUG
  // Final sanity check
  int final_result = est_sanity_check(est);
  printf("\n--- Final Sanity Check ---\n");
  print_sanity_error(final_result);

  if (final_result != 0) {
    fprintf(stderr, "Test failed: Final sanity check failed\n");
    return 1;
  }

  // Print statistics if available
  est_take_statistics(est);
  printf("\nMemory Statistics:\n");
  printf("- Total memory: %u bytes\n", est->stat.total);
  printf("- Used memory: %u bytes\n", est->stat.used);
  printf("- Free memory: %u bytes\n", est->stat.free);
  printf("- Fragmentation count: %d\n", est->stat.frag);

  // Stop profiling
  est_stop_profiling(est);
  printf("\nMemory Usage Profile:\n");
  printf("- Initial: %" PRIu64 " bytes\n", (uint64_t)est->prof.initial);
  printf("- Minimum: %" PRIu64 " bytes\n", (uint64_t)est->prof.min);
  printf("- Maximum: %" PRIu64 " bytes\n", (uint64_t)est->prof.max);

#ifdef ESTALLOC_PRINT_DEBUG
  // Print detailed memory information
  printf("\n--- Memory Pool Details ---\n");
  est_fprint_pool_header(est, stdout);
  est_fprint_memory_block(est, stdout);
#endif
#endif

  // Free all remaining allocations
  printf("\nFreeing all remaining allocations...\n");
  for (int i = 0; i < alloc_count; i++) {
    if (allocs[i].type != PERMALLOC) {
      est_free(est, allocs[i].ptr);
    }
  }

  // Cleanup memory pool
  est_cleanup(est);
  free(pool_memory);

  printf("Test completed.\n");
  return 0;
}
