# ESTALLOC - Embedded Systems TLSF Memory Allocator

[![C](https://github.com/picoruby/estalloc/actions/workflows/test.yml/badge.svg)](https://github.com/picoruby/estalloc/actions/workflows/test.yml)

> EST! EST!! EST!!!

ESTALLOC is a high-performance memory allocator specifically designed for embedded systems, implementing the Two-Level Segregated Fit (TLSF) algorithm with a First-Fit fallback strategy.
It provides deterministic O(1) time complexity for memory allocation and deallocation, making it ideal for real-time applications and resource-constrained environments.

## Acknowledgement

ESTALLOC is derived from the memory allocator originally developed for [mrubyc/mrubyc](https://github.com/mrubyc/mrubyc), a lightweight Ruby implementation for embedded systems.
The source code was adapted from mruby/c and refactored into a standalone, general-purpose allocator with minimal dependencies and improved portability for broader use beyond the original project.

## Features

- **Deterministic Performance**: O(1) time complexity for malloc and free operations
- **Low Memory Fragmentation**: Efficient memory management reduces fragmentation
- **Configurable**: Supports different alignment options (4-byte and 8-byte)
- **Embedded-Friendly**: Optimized for 16-bit and 32-bit architectures
- **Memory Pool Based**: Uses pre-allocated memory pools for management
- **Thread Safety**: Optional critical section callbacks for use in RTOS or interrupt-driven environments
- **Debug Support**: Optional debugging features for memory leak detection and pool health checks

## API Reference

ESTALLOC provides the following memory management functions:

- `est_init(void *ptr, unsigned int size)`: Initialize a memory pool
- `est_malloc(ESTALLOC *est, unsigned int size)`: Allocate memory
- `est_free(ESTALLOC *est, void *ptr)`: Free previously allocated memory
- `est_realloc(ESTALLOC *est, void *ptr, unsigned int size)`: Resize allocated memory
- `est_calloc(ESTALLOC *est, unsigned int nmemb, unsigned int size)`: Allocate zero-initialized memory
- `est_permalloc(ESTALLOC *est, unsigned int size)`: Allocate permanent (non-freeable) memory
- `est_usable_size(ESTALLOC *est, void *ptr)`: Get usable size of allocated memory block
- `est_set_critical_section(ESTALLOC *est, void (*enter)(void), void (*exit)(void))`: Register callbacks for wrapping critical sections (see [Thread Safety](#thread-safety))

### Debug Functions

In any build:

- `est_take_statistics(ESTALLOC *est)`: Collect memory usage statistics
    ```c
    est_take_statistics(est);
    est->stat.total;    // Total memory
    est->stat.used;     // Used memory
    est->stat.free;     // Free memory
    est->stat.frag;     // Number of fragmentation
    ```

When compiled with `ESTALLOC_DEBUG` defined:

- `est_start_profiling(ESTALLOC *est)`: Start memory profiling
- `est_stop_profiling(ESTALLOC *est)`: Stop memory profiling
    ```c
    est_start_profiling(est);
    {
      // Do someting. ESTALLOC records the memory usage
    }
    est_stop_profiling(est);
    est->prof.initial;  // Initial memory usage at `est_start_profiling()` called
    est->prof.max;      // Maximum memory usage during profiling
    est->prof.min;      // Minimum memory usage during profiling
    ```

- `est_sanity_check(ESTALLOC *est)`: Check memory pool integrity
    ```c
    int result = est_sanity_check(est);
    if (result == 0) {
      // It's sanity
    } else {
      // Something went wrong
      // See comment in estalloc.c for details
    }
    ```

When compiled with `ESTALLOC_PRINT_DEBUG` defined:

- `est_fprint_pool_header(ESTALLOC *est, FILE *fp)`: Print memory pool header information
- `est_fprint_memory_block(ESTALLOC *est, FILE *fp)`: Print detailed memory block information

## Thread Safety

ESTALLOC is not thread-safe by default. To enable thread safety, register enter/exit callbacks via `est_set_critical_section()`.
The callbacks wrap each allocator operation in a critical section provided by the platform.

- Must be called during initialization, before any concurrent access.
- `est_set_critical_section()` itself is not thread-safe.
- Passing `NULL` for both callbacks disables critical section protection (default behavior).

Example using pico-sdk:

```c
#include <pico/critical_section.h>

static critical_section_t heap_critsec;

static void heap_enter_critical(void)
{
  critical_section_enter_blocking(&heap_critsec);
}

static void heap_exit_critical(void)
{
  critical_section_exit(&heap_critsec);
}

int main(void)
{
  ESTALLOC *est = est_init(mem, bytes);
  critical_section_init(&heap_critsec); // from pico-sdk
  est_set_critical_section(est, heap_enter_critical, heap_exit_critical);
}
```

## Usage Example

```c
#include "estalloc.h"
#include <stdio.h>

#define POOL_SIZE (64 * 1024 - 1)  // 64KB memory pool

int
main()
{
  static uint8_t memory_pool[POOL_SIZE];
  
  // Initialize memory pool
  ESTALLOC *est = est_init(memory_pool, POOL_SIZE);
  if (!est) {
    printf("Failed to initialize memory pool\n");
    return 1;
  }
  
  // Allocate memory
  int *numbers = (int*)est_malloc(est, 10 * sizeof(int));
  if (numbers) {
    // Use allocated memory
    for (int i = 0; i < 10; i++) {
      numbers[i] = i * 10;
    }
  
    // Print values
    for (int i = 0; i < 10; i++) {
      printf("%d ", numbers[i]);
    }
    printf("\n");
  
    // Free memory
    est_free(est, numbers);
  }
  
  return 0;
}
```

## Configuration

ESTALLOC can be configured using the following macros:

- `ESTALLOC_ALIGNMENT`: Memory alignment, `4` or `8` (default: `8`, or `4` with `ESTALLOC_ADDRESS_16BIT`)
- `ESTALLOC_ADDRESS_16BIT` or `ESTALLOC_ADDRESS_24BIT`: Addressable memory range bit width (default:`ESTALLOC_ADDRESS_24BIT`)

`ESTALLOC_ALIGNMENT 8` is rejected with `ESTALLOC_ADDRESS_16BIT`: the block
header is 4 bytes in that mode, so user data can never start on an 8-byte
boundary.

### Build Matrix

|                 | ESTALLOC_ADDRESS_16BIT | ESTALLOC_ADDRESS_24BIT |
|-----------------|:--------------:|:--------------:|
| 16-bit Platform | ✅ (alignment 4)  | ✅             |
| 32-bit Platform | ✅ (alignment 4)  | ✅             |
| 64-bit Platform | ❌             | ✅             |

### Changing these macro is not tested enough:

- `ESTALLOC_FLI_BIT_WIDTH`: First level index bit width (default: `9`)
- `ESTALLOC_SLI_BIT_WIDTH`: Second level index bit width (default: `3`)
- `ESTALLOC_IGNORE_LSBS`: Least significant bits to ignore when calculating the first-level index (default: `4` or `5`)
- `ESTALLOC_MIN_MEMORY_BLOCK_SIZE`: Minimum memory block size (defalut: `16` or `32`)

(See details in [estalloc.c](estalloc.c))
