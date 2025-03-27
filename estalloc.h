/*! @file
  @brief
  TLSF memory allocator for embedded systems.

  <pre>
  Original Copyright:
    (C) 2015- Kyushu Institute of Technology.
    (C) 2015- Shimane IT Open-Innovation Center.
  Modification Copyright:
    (C) 2025- HASUMI Hitoshi @hasumikin

  This file is distributed under BSD 3-Clause License.

  </pre>
*/

#ifndef ESTALLOC_H_
#define ESTALLOC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined (__alpha__) || defined (__ia64__) || defined (__x86_64__) \
    || defined (_WIN64) || defined (__LP64__) || defined (__LLP64__) \
    || defined(__ppc64__) || defined(__aarch64__)
# define PLATFORM_64BIT
#endif

#if defined(ESTALLOC_16BIT) && defined(PLATFORM_64BIT)
# error "ESTALLOC_16BIT is not compatible with 64-bit architecture."
#endif
#if !defined(ESTALLOC_16BIT) && !defined(ESTALLOC_24BIT)
# define ESTALLOC_24BIT
#endif
#if defined(ESTALLOC_16BIT)
# define ESTALLOC_MEMSIZE_T  uint16_t
#elif defined(ESTALLOC_24BIT)
# define ESTALLOC_MEMSIZE_T  uint32_t
#endif

#if !defined(ESTALLOC_ALIGNMENT)
# define ESTALLOC_ALIGNMENT 8
#endif

#if ESTALLOC_ALIGNMENT == 4 || ESTALLOC_ALIGNMENT == 8
# define ALIGNMENT_MASK (ESTALLOC_ALIGNMENT - 1)
#else
# error 'ESTALLOC_ALIGNMENT' must be 4 or 8.
#endif

/*!@brief
  Structure for est_take_statistics function.
  If you use this, define ESTALLOC_DEBUG pre-processor macro.
*/
typedef struct ESTALLOC_STAT {
  ESTALLOC_MEMSIZE_T total;   // total memory
  ESTALLOC_MEMSIZE_T used;    // used memory
  ESTALLOC_MEMSIZE_T free;    // free memory
  ESTALLOC_MEMSIZE_T frag;    // memory fragmentation count
} ESTALLOC_STAT;

#if defined(ESTALLOC_DEBUG)
/*!@brief
  Structure for est_start_profiling and est_stop_profiling functions.
  If you use this, define ESTALLOC_DEBUG pre-processor macro.
*/
typedef struct ESTALLOC_PROF {
  uint8_t profiling;
  ESTALLOC_MEMSIZE_T initial;
  ESTALLOC_MEMSIZE_T max;
  ESTALLOC_MEMSIZE_T min;
} ESTALLOC_PROF;

typedef struct ESTALLOC {
  ESTALLOC_STAT stat;
  ESTALLOC_PROF prof;
  const char *error_message;
#if ESTALLOC_ALIGNMENT == 8
  char padding[4];
#endif
} ESTALLOC;
#else
//typedef void ESTALLOC;
typedef struct ESTALLOC {
  ESTALLOC_STAT stat;
  char *error_message;
#if ESTALLOC_ALIGNMENT == 8
  char padding[4];
#endif
} ESTALLOC;
#endif

ESTALLOC *est_init(void *ptr, unsigned int size);
void est_cleanup(ESTALLOC *est);

void *est_permalloc(ESTALLOC *est, unsigned int size);
void *est_malloc(ESTALLOC *est, unsigned int size);
void *est_realloc(ESTALLOC *est, void *ptr, unsigned int size);
void *est_calloc(ESTALLOC *est, unsigned int nmemb, unsigned int size);
void est_free(ESTALLOC *est, void *ptr);
unsigned int est_usable_size(ESTALLOC *est, void *ptr);

void est_take_statistics(ESTALLOC *est);

#if defined(ESTALLOC_DEBUG)
int est_sanity_check(ESTALLOC *est);
void est_start_profiling(ESTALLOC *est);
void est_stop_profiling(ESTALLOC *est);
#endif

#if defined(ESTALLOC_PRINT_DEBUG)
#include <stdio.h>
void est_print_pool_header(ESTALLOC *est, FILE *fp);
void est_print_memory_block(ESTALLOC *est, FILE *fp);
#endif

#ifdef __cplusplus
}
#endif
#endif
