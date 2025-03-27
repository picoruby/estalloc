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

#include "estalloc.h"

#if defined(ESTALLOC_16BIT)
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
        est_print_pool_header(est, stdout);
        est_print_memory_block(est, stdout);
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
  est_print_pool_header(est, stdout);
  est_print_memory_block(est, stdout);
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

