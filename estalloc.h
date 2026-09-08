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

/*
  PLATFORM_64BIT means "a pointer is 8 bytes wide", not "the CPU is 64-bit".
  Deriving it from architecture macros breaks on ILP32 ABIs that run on 64-bit
  CPUs: on arm64_32 (Apple Watch Series 4 and later) clang defines __aarch64__
  while sizeof(void *) is 4, which used to give a pool header that is not a
  multiple of ESTALLOC_ALIGNMENT.
*/
#if defined(UINTPTR_MAX) && UINTPTR_MAX > 0xFFFFFFFFu
# define PLATFORM_64BIT
#endif

#if defined(ESTALLOC_ADDRESS_16BIT) && defined(PLATFORM_64BIT)
# error "ESTALLOC_ADDRESS_16BIT is not compatible with 64-bit architecture."
#endif
#if !defined(ESTALLOC_ADDRESS_16BIT) && !defined(ESTALLOC_ADDRESS_24BIT)
# define ESTALLOC_ADDRESS_24BIT
#endif
#if defined(ESTALLOC_ADDRESS_16BIT)
# define ESTALLOC_MEMSIZE_T  uint16_t
#elif defined(ESTALLOC_ADDRESS_24BIT)
# define ESTALLOC_MEMSIZE_T  uint32_t
#endif

#if !defined(ESTALLOC_ALIGNMENT)
# if defined(ESTALLOC_ADDRESS_16BIT)
/* 8-byte alignment is unreachable in this mode. See the check below. */
#  define ESTALLOC_ALIGNMENT 4
# else
#  define ESTALLOC_ALIGNMENT 8
# endif
#endif

#if ESTALLOC_ALIGNMENT == 4 || ESTALLOC_ALIGNMENT == 8
# define ALIGNMENT_MASK (ESTALLOC_ALIGNMENT - 1)
#else
# error 'ESTALLOC_ALIGNMENT' must be 4 or 8.
#endif

/*
  In 16-bit address mode USED_BLOCK is 4 bytes, so user data always starts
  4 bytes into an aligned block and can never land on an 8-byte boundary.
  Reject the combination rather than silently handing out pointers that
  violate the requested alignment.
*/
#if defined(ESTALLOC_ADDRESS_16BIT) && ESTALLOC_ALIGNMENT == 8
# error "ESTALLOC_ALIGNMENT 8 is not compatible with ESTALLOC_ADDRESS_16BIT. Use ESTALLOC_ALIGNMENT 4."
#endif

/*!@brief
  Structure for est_take_statistics function.
  If you use this, define ESTALLOC_DEBUG pre-processor macro.
*/
typedef struct ESTALLOC_STAT {
  ESTALLOC_MEMSIZE_T total;   // total memory
  ESTALLOC_MEMSIZE_T used;    // used memory
  ESTALLOC_MEMSIZE_T free;    // free memory
  ESTALLOC_MEMSIZE_T max_free;// largest free block
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
  void (*enter_critical)(void);
  void (*exit_critical)(void);
#if ESTALLOC_ALIGNMENT == 8 && defined(PLATFORM_64BIT)
  char padding[4];
#endif
} ESTALLOC;
#else
//typedef void ESTALLOC;
typedef struct ESTALLOC {
  ESTALLOC_STAT stat;
  const char *error_message;
  void (*enter_critical)(void);
  void (*exit_critical)(void);
#if ESTALLOC_ALIGNMENT == 8 && defined(PLATFORM_64BIT)
  char padding[4];
#endif
} ESTALLOC;
#endif

ESTALLOC *est_init(void *ptr, unsigned int size);
void est_cleanup(ESTALLOC *est);
void est_set_critical_section(ESTALLOC *est, void (*enter)(void), void (*exit)(void));

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
void est_fprint_pool_header(ESTALLOC *est, FILE *fp);
void est_fprint_memory_block(ESTALLOC *est, FILE *fp);
#endif

#ifdef __cplusplus
}
#endif
#endif
