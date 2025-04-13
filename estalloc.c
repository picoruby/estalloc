/*! @file
  @brief
  TLSF memory allocator for embedded systems.

  <pre>
  Original Copyright:
    (C) 2015- Kyushu Institute of Technology.
    (C) 2015- Shimane IT Open-Innovation Center.
  Modification Contents:
    (C) 2025- HASUMI Hitoshi @hasumikin

  This file is distributed under BSD 3-Clause License.

  STRATEGY
   Using TLSF and First-Fit algorithm.

  MEMORY POOL USAGE (see struct MEMORY_POOL)
     | Memory pool header | Memory block pool to provide to application |
     +--------------------+---------------------------------------------+
     | size, bitmap, ...  | USED_BLOCK, FREE_BLOCK, ..., (SentinelBlock)|

  MEMORY BLOCK LINK
      with USED flag and PREV_IN_USE flag in size member's bit 0 and 1.

     |  USED_BLOCK     |  FREE_BLOCK                   |  USED_BLOCK     |...
     +-----------------+-------------------------------+-----------------+---
     |size| (contents) |size|*next|*prev| (empty) |*top|size| (contents) |
 USED|   1|            |   0|     |     |         |    |   1|            |
 PREV|  1 |            |  1 |     |     |         |    |  0 |            |

                                           Sentinel block at the link tail.
                                      ...              |  USED_BLOCK     |
                                     ------------------+-----------------+
                                                       |size| (contents) |
                                                   USED|   1|            |
                                                   PREV|  ? |            |
    size : block size.
    *next: linked list, pointer to the next free block of same block size.
    *prev: linked list, pointer to the previous free block of same block size.
    *top : pointer to this block's top.

  </pre>
*/

/***** Feature test switches ************************************************/
/***** System headers *******************************************************/
//@cond
#include <stdint.h>
#include <assert.h>
#include <stddef.h>
#if defined(ESTALLOC_PRINT_DEBUG)
# include <stdio.h>
#include <inttypes.h>
#endif
//@endcond

/***** Local headers ********************************************************/
#include "estalloc.h"

/***** Constant values ******************************************************/
/*
  Layer 1st(f) and 2nd(s) model
  last 4bit is ignored

                                                                    BlockSize by alignment
 FLI range      SLI0  1     2     3     4     5     6     7          4-byte  8-byte
  0  0000-007f unused 0010- 0020- 0030- 0040- 0050- 0060- 0070-007f     16      32
  1  0080-00ff  0080- 0090- 00a0- 00b0- 00c0- 00d0- 00e0- 00f0-00ff     16      32
  2  0100-01ff  0100- 0120- 0140- 0160- 0180- 01a0- 01c0- 01e0-01ff     32      64
  3  0200-03ff  0200- 0240- 0280- 02c0- 0300- 0340- 0380- 03c0-03ff     64     128
  4  0400-07ff  0400- 0480- 0500- 0580- 0600- 0680- 0700- 0780-07ff    128     256
  5  0800-0fff  0800- 0900- 0a00- 0b00- 0c00- 0d00- 0e00- 0f00-0fff    256     512
  6  1000-1fff  1000- 1200- 1400- 1600- 1800- 1a00- 1c00- 1e00-1fff    512    1024
  7  2000-3fff  2000- 2400- 2800- 2c00- 3000- 3400- 3800- 3c00-3fff   1024    2048
  8  4000-7fff  4000- 4800- 5000- 5800- 6000- 6800- 7000- 7800-7fff   2048    4096
  9  8000-ffff  8000- 9000- a000- b000- c000- d000- e000- f000-ffff   4096    8192
*/

/*
  ESTALLOC_ALIGNMENT == 4:
   0 0000 0000 0000 0000
     ^^^^ ^^^^ ^          ESTALLOC_FLI_BIT_WIDTH
                ^^^       ESTALLOC_SLI_BIT_WIDTH
                    ^^^^  ESTALLOC_IGNORE_LSBS
  ESTALLOC_ALIGNMENT == 8:
   0 0000 0000 0000 0000
   ^ ^^^^ ^^^^            ESTALLOC_FLI_BIT_WIDTH
               ^^^        ESTALLOC_SLI_BIT_WIDTH
                  ^ ^^^^  ESTALLOC_IGNORE_LSBS
*/
#ifndef ESTALLOC_FLI_BIT_WIDTH
# define ESTALLOC_FLI_BIT_WIDTH   9
#endif
#ifndef ESTALLOC_SLI_BIT_WIDTH
# define ESTALLOC_SLI_BIT_WIDTH   3
#endif
#ifdef PLATFORM_64BIT
# define ESTALLOC_IGNORE_LSBS    5
#else
# ifndef ESTALLOC_IGNORE_LSBS
#  if ESTALLOC_ALIGNMENT == 4
#   define ESTALLOC_IGNORE_LSBS    4
#  elif ESTALLOC_ALIGNMENT == 8
#   define ESTALLOC_IGNORE_LSBS    5
#  endif
# endif
#endif

#define SIZE_FREE_BLOCKS ((ESTALLOC_FLI_BIT_WIDTH + 1) * (1 << ESTALLOC_SLI_BIT_WIDTH))
/*
   Minimum memory block size parameter.
*/
#if !defined(ESTALLOC_MIN_MEMORY_BLOCK_SIZE)
# define ESTALLOC_MIN_MEMORY_BLOCK_SIZE (1 << ESTALLOC_IGNORE_LSBS)
#endif


/***** Macros ***************************************************************/
#define FLI(x) ((x) >> ESTALLOC_SLI_BIT_WIDTH)
#define SLI(x) ((x) & ((1 << ESTALLOC_SLI_BIT_WIDTH) - 1))


/***** Typedefs *************************************************************/
/*
  define memory block header for 16 bit

  (note)
  Typical size of
    USED_BLOCK is 2 bytes
    FREE_BLOCK is 8 bytes
  on 16bit machine.
*/
#if defined(ESTALLOC_ADDRESS_16BIT)

typedef struct USED_BLOCK {
  ESTALLOC_MEMSIZE_T size;    //!< block size, header included
  uint8_t pad[2];  // for alignment compatibility on 16bit and 32bit machines
} USED_BLOCK;

typedef struct FREE_BLOCK {
  ESTALLOC_MEMSIZE_T size;    //!< block size, header included

  struct FREE_BLOCK *next_free;
  struct FREE_BLOCK *prev_free;
  struct FREE_BLOCK *top_adrs;    //!< dummy for calculate sizeof(FREE_BLOCK)
} FREE_BLOCK;


/*
  define memory block header for 24/32 bit.

  (note)
  Typical size of
    USED_BLOCK is  4 bytes
    FREE_BLOCK is 16 bytes
  on 32bit machine.
*/
#elif defined(ESTALLOC_ADDRESS_24BIT)

typedef struct USED_BLOCK {
  ESTALLOC_MEMSIZE_T size;
  uint8_t pad[2];  // for alignment compatibility on 16bit and 32bit machines
} USED_BLOCK;

typedef struct FREE_BLOCK {
  ESTALLOC_MEMSIZE_T size;

  struct FREE_BLOCK *next_free;
  struct FREE_BLOCK *prev_free;
  struct FREE_BLOCK *top_adrs;    //!< dummy for calculate sizeof(FREE_BLOCK)
} FREE_BLOCK;

#endif

/*
  and operation macro
*/
#define BLOCK_SIZE(p)       (((p)->size) & ~ALIGNMENT_MASK)
#define PHYS_NEXT(p)        ((void *)((uint8_t *)(p) + BLOCK_SIZE(p)))
#define SET_USED_BLOCK(p)   ((p)->size |=  0x01)
#define SET_FREE_BLOCK(p)   ((p)->size &= ~0x01)
#define IS_USED_BLOCK(p)    ((p)->size &   0x01)
#define IS_FREE_BLOCK(p)    (!IS_USED_BLOCK(p))
#define SET_PREV_USED(p)    ((p)->size |=  0x02)
#define SET_PREV_FREE(p)    ((p)->size &= ~0x02)
#define IS_PREV_USED(p)     ((p)->size &   0x02)
#define IS_PREV_FREE(p)     (!IS_PREV_USED(p))


/*
  define memory pool header
*/
typedef struct MEMORY_POOL {
  ESTALLOC est;

  ESTALLOC_MEMSIZE_T size;

  // free memory bitmap
  uint16_t free_fli_bitmap;
  uint8_t  free_sli_bitmap[ESTALLOC_FLI_BIT_WIDTH +1 +1]; // +1=bit_width, +1=sentinel
  uint8_t  pad[3]; // for alignment compatibility on 16bit and 32bit machines

  // free memory block index
  FREE_BLOCK *free_blocks[SIZE_FREE_BLOCKS +1];  // +1=sentinel
} MEMORY_POOL;

#define BPOOL_TOP(memory_pool) ((void *)((uint8_t *)(memory_pool) + sizeof(MEMORY_POOL)))
#define BPOOL_END(memory_pool) ((void *)((uint8_t *)(memory_pool) + ((MEMORY_POOL *)(memory_pool))->size))
#define BLOCK_ADRS(p) ((void *)((uint8_t *)(p) - sizeof(USED_BLOCK)))

#define MSB_BIT1_FLI 0x8000
#define MSB_BIT1_SLI 0x80
#define NLZ_FLI(x) nlz16(x)
#define NLZ_SLI(x) nlz8(x)


#if defined(ESTALLOC_DEBUG)
static void take_profile(ESTALLOC *est);
# define PROFILE() do { \
    if (est->prof.profiling) take_profile(est); \
  } while(0)
#else
# define PROFILE()
#endif


//================================================================
/*! Number of leading zeros. 16bit version.

  @param  x  target (16bit unsigned)
  @retval int  nlz value
*/
static inline int
nlz16(uint16_t x)
{
  if (x == 0 ) return 16;

  int n = 1;
  if((x >>  8) == 0) { n += 8; x <<= 8; }
  if((x >> 12) == 0) { n += 4; x <<= 4; }
  if((x >> 14) == 0) { n += 2; x <<= 2; }
  return n - (x >> 15);
}


//================================================================
/*! Number of leading zeros. 8bit version.

  @param  x  target (8bit unsigned)
  @retval int  nlz value
*/
static inline int
nlz8(uint8_t x)
{
  if (x == 0 ) return 8;

  int n = 1;
  if((x >> 4) == 0) { n += 4; x <<= 4; }
  if((x >> 6) == 0) { n += 2; x <<= 2; }
  return n - (x >> 7);
}


//================================================================
/*! calc f and s, and returns fli,sli of free_blocks

  @param  alloc_size  alloc size
  @retval unsigned int  index of free_blocks
*/
static inline unsigned int
calc_index(ESTALLOC_MEMSIZE_T alloc_size)
{
  // check overflow
  if ((alloc_size >> (ESTALLOC_FLI_BIT_WIDTH
                      + ESTALLOC_SLI_BIT_WIDTH
                      + ESTALLOC_IGNORE_LSBS)) != 0) {
    return SIZE_FREE_BLOCKS - 1;
  }

  // calculate First Level Index.
  unsigned int fli = 16 - nlz16( alloc_size >> (ESTALLOC_SLI_BIT_WIDTH + ESTALLOC_IGNORE_LSBS));

  // calculate Second Level Index.
  unsigned int shift = (fli == 0) ? ESTALLOC_IGNORE_LSBS :
                                   (ESTALLOC_IGNORE_LSBS - 1 + fli);

  unsigned int sli = (alloc_size >> shift) & ((1 << ESTALLOC_SLI_BIT_WIDTH) - 1);
  unsigned int index = (fli << ESTALLOC_SLI_BIT_WIDTH) + sli;

  assert(fli <= ESTALLOC_FLI_BIT_WIDTH);
  assert(sli <= (1 << ESTALLOC_SLI_BIT_WIDTH) - 1);

  return index;
}


//================================================================
/*! Mark that block free and register it in the free index table.

  @param  pool    Pointer to ESTALLOC.
  @param  target  Pointer to target block.
*/
static void
add_free_block(MEMORY_POOL *pool, FREE_BLOCK *target)
{
  SET_FREE_BLOCK(target);

  FREE_BLOCK **top_adrs = (FREE_BLOCK **)((uint8_t*)target + BLOCK_SIZE(target) - sizeof(FREE_BLOCK *));
  *top_adrs = target;

  unsigned int index = calc_index(BLOCK_SIZE(target));
  unsigned int fli = FLI(index);
  unsigned int sli = SLI(index);
  assert(index < SIZE_FREE_BLOCKS);

  pool->free_fli_bitmap      |= (MSB_BIT1_FLI >> fli);
  pool->free_sli_bitmap[fli] |= (MSB_BIT1_SLI >> sli);

  target->prev_free = NULL;
  target->next_free = pool->free_blocks[index];
  if (target->next_free != NULL) {
    target->next_free->prev_free = target;
  }
  pool->free_blocks[index] = target;
}


//================================================================
/*! just remove the free_block *target from index

  @param  pool    Pointer to ESTALLOC.
  @param  target  pointer to target block.
*/
static void
remove_free_block(MEMORY_POOL *pool, FREE_BLOCK *target)
{
  // top of linked list?
  if (target->prev_free == NULL) {
    unsigned int index = calc_index(BLOCK_SIZE(target));

    pool->free_blocks[index] = target->next_free;
    if (target->next_free == NULL) {
      unsigned int fli = FLI(index);
      unsigned int sli = SLI(index);
      pool->free_sli_bitmap[fli] &= ~(MSB_BIT1_SLI >> sli);
      if (pool->free_sli_bitmap[fli] == 0 ) pool->free_fli_bitmap &= ~(MSB_BIT1_FLI >> fli);
    }
  }
  else {
    target->prev_free->next_free = target->next_free;
  }

  if (target->next_free != NULL) {
    target->next_free->prev_free = target->prev_free;
  }
}


//================================================================
/*! Split block by size

  @param  target  pointer to target block
  @param  size    size
  @retval NULL    no split.
  @retval FREE_BLOCK *  pointer to splitted free block.
*/
static inline FREE_BLOCK *
split_block(FREE_BLOCK *target, ESTALLOC_MEMSIZE_T size)
{
  assert(BLOCK_SIZE(target) >= size);
  if ((BLOCK_SIZE(target) - size) <= ESTALLOC_MIN_MEMORY_BLOCK_SIZE) return NULL;

  // split block, free
  FREE_BLOCK *split = (FREE_BLOCK *)((uint8_t *)target + size);

  split->size  = BLOCK_SIZE(target) - size;
  target->size = size | (target->size & ALIGNMENT_MASK);  // copy a size with flags.

  return split;
}


//================================================================
/*! merge target and next block.
    next will disappear

  @param  target  pointer to free block 1
  @param  next  pointer to free block 2
*/
static inline
void merge_block(FREE_BLOCK *target, FREE_BLOCK *next)
{
  assert(target < next);

  // merge target and next
  target->size += BLOCK_SIZE(next);    // copy a size but save flags.
}


/***** Global functions *****************************************************/
//================================================================
/*! initialize

  @param  ptr  pointer to free memory block.
  @param  size  size. (max 64KB. see ESTALLOC_MEMSIZE_T)
  @return ESTALLOC *  pointer to memory pool.
*/
ESTALLOC *
est_init(void *ptr, unsigned int size)
{
  assert(ESTALLOC_MIN_MEMORY_BLOCK_SIZE >= sizeof(FREE_BLOCK));
  assert(ESTALLOC_MIN_MEMORY_BLOCK_SIZE >= (1 << ESTALLOC_IGNORE_LSBS));
  /*
    If you get this assertion, you can change minimum memory block size
    parameter to `ESTALLOC_MIN_MEMORY_BLOCK_SIZE (1 << ESTALLOC_IGNORE_LSBS)`
    and #define ESTALLOC_ADDRESS_16BIT.
  */

  assert((sizeof(MEMORY_POOL) & ALIGNMENT_MASK) == 0);
#if defined(UINTPTR_MAX)
  assert(((uintptr_t)ptr & ALIGNMENT_MASK) == 0);
#else
  assert(((uint32_t)ptr & ALIGNMENT_MASK) == 0);
#endif
  assert(size != 0);
  assert(size <= (ESTALLOC_MEMSIZE_T)(~0));

  size &= ~(unsigned int)ALIGNMENT_MASK;

  MEMORY_POOL zero_pool = {0};
  MEMORY_POOL *memory_pool = (MEMORY_POOL *)ptr;
  *memory_pool = zero_pool;
  memory_pool->size = size;

  // initialize memory pool
  //  large free block + zero size used block (sentinel).
  ESTALLOC_MEMSIZE_T sentinel_size = sizeof(USED_BLOCK);
  sentinel_size += (-sentinel_size & ALIGNMENT_MASK);
  ESTALLOC_MEMSIZE_T free_size = size - sizeof(MEMORY_POOL) - sentinel_size;
  FREE_BLOCK *free_block = BPOOL_TOP(memory_pool);
  USED_BLOCK *used_block = (USED_BLOCK *)((uint8_t *)free_block + free_size);

  free_block->size = free_size | 0x02;      // flag prev=1, used=0
  used_block->size = sentinel_size | 0x01;  // flag prev=0, used=1

  add_free_block(memory_pool, free_block);

  return (ESTALLOC *)memory_pool;
}


//================================================================
/*! cleanup memory pool

  @param  est     Pointer to ESTALLOC.
*/
void
est_cleanup(ESTALLOC *est)
{
#if defined(ESTALLOC_DEBUG)
  MEMORY_POOL *memory_pool = (MEMORY_POOL *)est;
  char *p = (char *)est;
  if (p) {
    for (unsigned int i = 0; i < memory_pool->size; i++) {
      p[i] = 0;
    }
  }
#else
  (void)est;
#endif
}


//================================================================
/*! allocate memory

  @param  est     Pointer to ESTALLOC.
  @param  size  request size.
  @return void * pointer to allocated memory.
  @retval NULL  Out of memory.
*/
void *
est_malloc(ESTALLOC *est, unsigned int size)
{
  MEMORY_POOL *pool = (MEMORY_POOL *)est;
  ESTALLOC_MEMSIZE_T alloc_size = size + sizeof(USED_BLOCK);

  alloc_size += (-alloc_size & ALIGNMENT_MASK);

  // check minimum alloc size.
  if (alloc_size < ESTALLOC_MIN_MEMORY_BLOCK_SIZE ) alloc_size = ESTALLOC_MIN_MEMORY_BLOCK_SIZE;

  FREE_BLOCK *target;
  unsigned int fli, sli;
  unsigned int index = calc_index(alloc_size);

  // At first, check only the beginning of the same size block.
  // because it immediately responds to the pattern in which
  // same size memory are allocated and released continuously.
  target = pool->free_blocks[index];
  if (target && BLOCK_SIZE(target) >= alloc_size) {
    fli = FLI(index);
    sli = SLI(index);
    goto FOUND_TARGET_BLOCK;
  }

  // and then, check the next (larger) size block.
  target = pool->free_blocks[++index];
  fli = FLI(index);
  sli = SLI(index);
  if (target) goto FOUND_TARGET_BLOCK;

  // check in SLI bitmap table.
  uint16_t masked = pool->free_sli_bitmap[fli] & ((MSB_BIT1_SLI >> sli) - 1);
  if (masked != 0) {
    sli = NLZ_SLI(masked);
    goto FOUND_FLI_SLI;
  }

  // check in FLI bitmap table.
  masked = pool->free_fli_bitmap & ((MSB_BIT1_FLI >> fli) - 1);
  if (masked != 0) {
    fli = NLZ_FLI(masked);
    sli = NLZ_SLI(pool->free_sli_bitmap[fli]);
    goto FOUND_FLI_SLI;
  }

  // Change strategy to First-fit.
  target = pool->free_blocks[--index];
  while (target) {
    if (BLOCK_SIZE(target) >= alloc_size) {
      remove_free_block( pool, target);
      goto SPLIT_BLOCK;
    }
    target = target->next_free;
  }

  // else out of memory
  return NULL;

 FOUND_FLI_SLI:
  index = (fli << ESTALLOC_SLI_BIT_WIDTH) + sli;
  assert(index <= SIZE_FREE_BLOCKS);
  target = pool->free_blocks[index];
  //assert(target != NULL);
  if (target == NULL) {
    return NULL;
  }

 FOUND_TARGET_BLOCK:
  assert(BLOCK_SIZE(target) >= alloc_size);

  // remove free_blocks index
  pool->free_blocks[index] = target->next_free;
  if (target->next_free == NULL) {
    pool->free_sli_bitmap[fli] &= ~(MSB_BIT1_SLI >> sli);
    if (pool->free_sli_bitmap[fli] == 0 ) pool->free_fli_bitmap &= ~(MSB_BIT1_FLI >> fli);
  }
  else {
    target->next_free->prev_free = NULL;
  }

 SPLIT_BLOCK: {
    FREE_BLOCK *release = split_block(target, alloc_size);
    if (release != NULL) {
      SET_PREV_USED(release);
      add_free_block(pool, release);
    } else {
      FREE_BLOCK *next = PHYS_NEXT(target);
      SET_PREV_USED(next);
    }
  }

  SET_USED_BLOCK(target);

#if defined(ESTALLOC_DEBUG)
  char *p = (char *)target;
  for (unsigned int i = 0; i < alloc_size - sizeof(USED_BLOCK); i++) {
    p[sizeof(USED_BLOCK) + i] = 0xaa;
  }
#endif

  PROFILE();

  return (uint8_t *)target + sizeof(USED_BLOCK);
}


//================================================================
/*! allocate memory that cannot free and realloc

  @param  est     Pointer to ESTALLOC.
  @param  size  request size.
  @return void * pointer to allocated memory.
  @retval NULL  error.
*/
void *
est_permalloc(ESTALLOC *est, unsigned int size)
{
  MEMORY_POOL *pool = (MEMORY_POOL *)est;
  ESTALLOC_MEMSIZE_T alloc_size = size + (-size & ALIGNMENT_MASK);

  // find the tail block
  FREE_BLOCK *tail = BPOOL_TOP(pool);
  FREE_BLOCK *prev;
  do {
    prev = tail;
    tail = PHYS_NEXT(tail);
  } while (PHYS_NEXT(tail) < BPOOL_END(pool));

  // can resize it block?
  if (IS_USED_BLOCK(prev) ) goto FALLBACK;
  if ((BLOCK_SIZE(prev) - sizeof(USED_BLOCK)) < alloc_size ) goto FALLBACK;

  remove_free_block( pool, prev);
  ESTALLOC_MEMSIZE_T free_size = BLOCK_SIZE(prev) - alloc_size;

  if (free_size <= ESTALLOC_MIN_MEMORY_BLOCK_SIZE) {
    // no split, use all
    prev->size += BLOCK_SIZE(tail);
    SET_USED_BLOCK( prev);
    tail = prev;
  }
  else {
    // split block
    ESTALLOC_MEMSIZE_T tail_size = tail->size + alloc_size;  // w/ flags.
    tail = (FREE_BLOCK*)((uint8_t *)tail - alloc_size);
    tail->size = tail_size;
    prev->size -= alloc_size;    // w/ flags.
    add_free_block( pool, prev);

#if defined(ESTALLOC_DEBUG)
    char *p = (char *)tail;
    for (unsigned int i = 0; i < alloc_size; i++) {
      p[sizeof(USED_BLOCK) + i] = 0xaa;
    }
#endif
  }

  return (uint8_t *)tail + sizeof(USED_BLOCK);

 FALLBACK:
  return est_malloc(est, size);
}


//================================================================
/*! allocate memory for compatibility with calloc

  @param  nmemb  number of elements.
  @param  size   size of an element.
  @return void * pointer to allocated memory.
  @retval NULL   error.
*/
void *
est_calloc(ESTALLOC *est, unsigned int nmemb, unsigned int size)
{
  unsigned int total_size = nmemb * size;
  void* ptr = est_malloc(est, total_size);
  if (ptr != NULL) {
    // Use a volatile pointer to prevent unexpected optimization.
    volatile unsigned char *vptr = (volatile unsigned char *)ptr;
    while (total_size--) {
      *vptr++ = 0;
    }
  }
  return ptr;
}


//================================================================
/*! release memory

  @param  est     Pointer to ESTALLOC.
  @param  ptr  Return value of est_malloc()
*/
void
est_free(ESTALLOC *est, void *ptr)
{
  MEMORY_POOL *pool = (MEMORY_POOL *)est;

  if (ptr == NULL) return;

#if defined(ESTALLOC_DEBUG)
  {
    FREE_BLOCK *target = BLOCK_ADRS(ptr);
    if (target < (FREE_BLOCK *)BPOOL_TOP(pool) || target > (FREE_BLOCK *)BPOOL_END(pool)) {
      est->error_message = "est_free(): Outside memory pool address was specified.\n";
      return;
    }
    FREE_BLOCK *block = BPOOL_TOP(pool);
    while(1) {
      if (block == target ) break;
      if (PHYS_NEXT(block) >= BPOOL_END(pool) ) break;
      block = PHYS_NEXT(block);
    }
    if (block == target) {
      // Found target block.
      if (IS_FREE_BLOCK(block)) {
        est->error_message = "est_free(): double free detected.\n";
        return;
      }
      if (PHYS_NEXT(block) >= BPOOL_END(pool)) {  // reach to sentinel
        est->error_message = "est_free(): permalloc address was specified.\n";
        return;
      }
    } else {
      // Not found target block.
      if (block < target) {
        est->error_message = "est_free(): permalloc address was specified.\n";
        return;
      }
      est->error_message = "est_free(): Illegal address.\n";
      return;
    }
    char *p = (char *)ptr;
    for (unsigned int i = 0; i < BLOCK_SIZE(target) - sizeof(USED_BLOCK); i++) {
      p[i] = 0xff;
    }
    est->error_message = NULL;
  }
#endif

  // get target block
  FREE_BLOCK *target = BLOCK_ADRS(ptr);

  // check next block, merge?
  FREE_BLOCK *next = PHYS_NEXT(target);

  if (IS_FREE_BLOCK(next)) {
    remove_free_block( pool, next);
    merge_block(target, next);
  } else {
    SET_PREV_FREE(next);
  }

  // check prev block, merge?
  if (IS_PREV_FREE(target)) {
    FREE_BLOCK *prev = *((FREE_BLOCK **)((uint8_t*)target - sizeof(FREE_BLOCK *)));

    assert(IS_FREE_BLOCK(prev));
    remove_free_block( pool, prev);
    merge_block(prev, target);
    target = prev;
  }

  // target, add to index
  add_free_block( pool, target);

  PROFILE();
}


//================================================================
/*! re-allocate memory

  @param  est     Pointer to ESTALLOC.
  @param  ptr  Return value of est_malloc()
  @param  size  request size
  @return void * pointer to allocated memory.
  @retval NULL  error.
*/
void *
est_realloc(ESTALLOC *est, void *ptr, unsigned int size)
{
  if (ptr == NULL) return est_malloc(est, size);

  MEMORY_POOL *pool = (MEMORY_POOL *)est;
  volatile USED_BLOCK *target = BLOCK_ADRS(ptr);
  ESTALLOC_MEMSIZE_T alloc_size = size + sizeof(USED_BLOCK);
  FREE_BLOCK *next;

  alloc_size += (-alloc_size & ALIGNMENT_MASK);

  // check minimum alloc size.
  if (alloc_size < ESTALLOC_MIN_MEMORY_BLOCK_SIZE ) alloc_size = ESTALLOC_MIN_MEMORY_BLOCK_SIZE;

  // expand? part1.
  // next phys block is free and enough size?
  if (alloc_size > BLOCK_SIZE(target)) {
    next = PHYS_NEXT(target);
    if (IS_USED_BLOCK(next)) goto ALLOC_AND_COPY;
    if ((BLOCK_SIZE(target) + BLOCK_SIZE(next)) < alloc_size) goto ALLOC_AND_COPY;

    remove_free_block(pool, next);
    merge_block((FREE_BLOCK *)target, next);
  }
  next = PHYS_NEXT(target);

  // try shrink.
  FREE_BLOCK *release = split_block((FREE_BLOCK *)target, alloc_size);
  if (release != NULL) {
    SET_PREV_USED(release);
  } else {
    SET_PREV_USED(next);
    PROFILE();
    return ptr;
  }

  // check next block, merge?
  if (IS_FREE_BLOCK(next)) {
    remove_free_block( pool, next);
    merge_block(release, next);
  } else {
    SET_PREV_FREE(next);
  }
  add_free_block(pool, release);
  PROFILE();
  return ptr;

  // expand part2.
  // new alloc and copy
 ALLOC_AND_COPY: {
    void *new_ptr = est_malloc(est, size);
    if (new_ptr == NULL) return NULL;  // ENOMEM

    // At this point, BLOCK_SIZE(target) is new alloc size.
    for (unsigned int i = 0; i < BLOCK_SIZE(target) - sizeof(USED_BLOCK); i++) {
      ((uint8_t *)new_ptr)[i] = ((uint8_t *)ptr)[i];
    }
    est_free(est, ptr);
    return new_ptr;
  }
}


//================================================================
/*! allocated memory size

  @param  est     Pointer to ESTALLOC.
  @param  ptr           Return value of est_malloc()
  @retval unsigned int  pointer to allocated memory.
*/
unsigned int
est_usable_size(ESTALLOC *est, void *ptr)
{
  (void)est;
  USED_BLOCK *target = BLOCK_ADRS(ptr);
  return (unsigned int)(BLOCK_SIZE(target) - sizeof(USED_BLOCK));
}


#if defined(ESTALLOC_DEBUG)
//================================================================
/*! statistics

  @param  est     Pointer to ESTALLOC.
*/
void
est_take_statistics(ESTALLOC *est)
{
  MEMORY_POOL *pool = (MEMORY_POOL *)est;
  USED_BLOCK *block = BPOOL_TOP(pool);
  uint32_t flag_used_free = IS_USED_BLOCK(block);

  est->stat.total = pool->size;
  est->stat.used = 0;
  est->stat.free = 0;
  est->stat.frag = -1;

  while (block < (USED_BLOCK *)BPOOL_END(pool)) {
    if (IS_FREE_BLOCK(block)) {
      est->stat.free += BLOCK_SIZE(block);
    } else {
      est->stat.used += BLOCK_SIZE(block);
    }
    if (flag_used_free != IS_USED_BLOCK(block)) {
      est->stat.frag++;
      flag_used_free = IS_USED_BLOCK(block);
    }
    block = PHYS_NEXT(block);
  }
}


//================================================================
/*! Record current memory usage for profiling

  @param  est     Pointer to ESTALLOC.
*/
static void
take_profile(ESTALLOC *est)
{
  MEMORY_POOL *pool = (MEMORY_POOL *)est;
  ESTALLOC_PROF prof = est->prof;
  USED_BLOCK *block = BPOOL_TOP(pool);
  unsigned int used = 0;

  while (block < (USED_BLOCK *)BPOOL_END(pool)) {
    if (!IS_FREE_BLOCK(block)) {
      used += BLOCK_SIZE(block);
    }
    block = PHYS_NEXT(block);
  }

  if (prof.max < used) prof.max = used;
  if (used < prof.min) prof.min = used;
}

//================================================================
/*! Start memory allocation profiling

  @param  est     Pointer to ESTALLOC.
*/
void
est_start_profiling(ESTALLOC *est)
{
  ESTALLOC_PROF prof = est->prof;
  if (prof.profiling) return;
  prof.profiling = 1;
  prof.max = 0;
  PROFILE();
  prof.initial = prof.min = prof.max;
}

//================================================================
/*! Stop memory allocation profiling

  @param  est     Pointer to ESTALLOC.
*/
void
est_stop_profiling(ESTALLOC *est)
{
  est->prof.profiling = 0;
}


//================================================================
/*! Check the health of memory pool and all blocks
 *
 * This function walks through all memory blocks in the pool and checks:
 * 1. Block alignment is valid
 * 2. No memory blocks are overlapping
 * 3. Next/Previous pointers are consistent
 * 4. Free block list integrity
 *
 * @param  est    Pointer to ESTALLOC.
 * @return int    0 if pool is healthy, non-zero if issues found
 *                Error bitmask:
 *                - 0x01: Block alignment error
 *                - 0x02: Invalid block size (too large)
 *                - 0x04: Invalid next block address (out of bounds or overlapping)
 *                - 0x08: Previous block is used but current block says it's free
 *                - 0x10: Previous block is free but current block says it's used
 */
int
est_sanity_check(ESTALLOC *est)
{
  MEMORY_POOL *pool = (MEMORY_POOL *)est;
  USED_BLOCK *block = BPOOL_TOP(pool);
  USED_BLOCK *prev_block = NULL;
  int errors = 0;

  // Check pool boundaries
  if (pool == NULL || pool->size == 0) {
    return 1; // Invalid pool
  }

  // Walk through all blocks
  while (block < (USED_BLOCK *)BPOOL_END(pool)) {
    // Check block alignment
    if ((BLOCK_SIZE(block) & ALIGNMENT_MASK) != 0) {
      // Block size is not properly aligned
      errors |= 0x01;
    }

    // Check if block size is reasonable
    if (pool->size < BLOCK_SIZE(block)) {
      errors |= 0x02;
    }

    // Check if next block address is valid
    USED_BLOCK *next = PHYS_NEXT(block);
    if (next < block || (USED_BLOCK *)BPOOL_END(pool) < next) {
      // Next block address is invalid
      errors |= 0x04;
    }

    // Check prev_used flag consistency
    if (prev_block != NULL) {
      if (IS_USED_BLOCK(prev_block) && !IS_PREV_USED(block)) {
        // Prev block is used but current block says it's free
        errors |= 0x08;
      }
      if (IS_FREE_BLOCK(prev_block) && IS_PREV_USED(block)) {
        // Prev block is free but current block says it's used
        errors |= 0x10;
      }
    }

    // Move to next block
    prev_block = block;
    block = next;
  }

  return errors;
}
#endif // ESTALLOC_DEBUG


#if defined(ESTALLOC_PRINT_DEBUG)
//================================================================
/*! print pool header for debug.

  @param  est     Pointer to ESTALLOC.
  @param  fp      File pointer.
*/
void
est_fprint_pool_header(ESTALLOC *est, FILE *fp)
{
  MEMORY_POOL *pool = (MEMORY_POOL *)est;
  if (fp == NULL) fp = stderr;

  fprintf(fp, "== MEMORY POOL HEADER DUMP ==\n");
  fprintf(fp, " Address:%p - %p - %p  ", pool, BPOOL_TOP(pool), BPOOL_END(pool));
  fprintf(fp, " Size Total:%d User:%" PRIu32 "\n", pool->size, (ESTALLOC_MEMSIZE_T)(pool->size - sizeof(MEMORY_POOL)));
  fprintf(fp, " sizeof MEMORY_POOL:%" PRIu32 "(%04" PRIx32 "), USED_BLOCK:%" PRIu32 "(%02" PRIx32 "), FREE_BLOCK:%" PRIu32 "(%02" PRIx32 ")\n",
              (uint32_t)sizeof(MEMORY_POOL), (uint32_t)sizeof(MEMORY_POOL),
              (uint32_t)sizeof(USED_BLOCK), (uint32_t)sizeof(USED_BLOCK),
              (uint32_t)sizeof(FREE_BLOCK), (uint32_t)sizeof(FREE_BLOCK));

  fprintf(fp, " FLI/SLI bitmap and free_blocks table.\n");
  fprintf(fp, "    FLI :S[0123 4567] -- free_blocks ");
  for (unsigned int i = 0; i < 64; i++) { fprintf(fp, "-"); }
  fprintf(fp, "\n");
  for (unsigned int i = 0; i < sizeof(pool->free_sli_bitmap); i++) {
    fprintf(fp, " [%2d] %d :  ", i, !!((pool->free_fli_bitmap << i) & MSB_BIT1_FLI));
    for (int j = 0; j < 8; j++) {
      fprintf(fp, "%d", !!((pool->free_sli_bitmap[i] << j) & MSB_BIT1_SLI));
      if ((j % 4) == 3 ) fprintf(fp, " ");
    }

    for (int j = 0; j < 8; j++) {
      unsigned int idx = i * 8 + j;
      if (idx >= sizeof(pool->free_blocks) / sizeof(FREE_BLOCK *) ) break;
      fprintf(fp, " %p", pool->free_blocks[idx]);
    }
    fprintf(fp,  "\n");
  }
}

//================================================================
/*! print memory block for debug.

  @param  est     Pointer to ESTALLOC.
  @param  fp      File pointer.
*/
void
est_fprint_memory_block(ESTALLOC *est, FILE *fp)
{
  MEMORY_POOL *pool = (MEMORY_POOL *)est;
  if (fp == NULL) fp = stderr;

  const int DUMP_BYTES = 32;

  fprintf(fp, "== MEMORY BLOCK DUMP ==\n");
  FREE_BLOCK *block = BPOOL_TOP(pool);

  while (block < (FREE_BLOCK *)BPOOL_END(pool)) {
    fprintf(fp, "%p", block);
    fprintf(fp, " size:%5d($%04x) use:%d prv:%d ",
                block->size & ~ALIGNMENT_MASK, block->size & ~ALIGNMENT_MASK,
                !!(block->size & 0x01), !!(block->size & 0x02));

    if (IS_USED_BLOCK(block)) {
      /* Used block */
      unsigned int n = DUMP_BYTES;
      if (n > (BLOCK_SIZE(block) - sizeof(USED_BLOCK))) {
        n = BLOCK_SIZE(block) - sizeof(USED_BLOCK);
      }
      uint8_t *p = (uint8_t *)block + sizeof(USED_BLOCK);
      unsigned int i;
      for (i = 0; i < n; i++) fprintf(fp, " %02x", *p++);
      for (; i < (unsigned int)DUMP_BYTES; i++ ) fprintf(fp, "   ");

      fprintf(fp, "  ");
      p = (uint8_t *)block + sizeof(USED_BLOCK);
      for (i = 0; i < n; i++) {
        int ch = *p++;
        fprintf(fp, "%c", (' ' <= ch && ch < 0x7f)? ch : '.');
      }

    } else {
      /* Free block */
      unsigned int index = calc_index(BLOCK_SIZE(block));
      fprintf(fp, " fli:%d sli:%d pf:%p nf:%p",
      FLI(index), SLI(index), block->prev_free, block->next_free);
    }

    fprintf(fp, "\n");
    block = PHYS_NEXT(block);
  }
}

#endif // ESTALLOC_PRINT_DEBUG
