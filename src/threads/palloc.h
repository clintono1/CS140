#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stddef.h>
#include <stdint.h>

/* How to allocate pages. */
enum palloc_flags
  {
    PAL_ASSERT = 0x1,           /* Panic on failure. */
    PAL_ZERO = 0x2,             /* Zero page contents. */
    PAL_USER = 0x4,             /* User page. */
    PAL_MMAP = 0x8              /* Memory mapped files. */
  };

void palloc_init (size_t user_page_limit);
void *palloc_get_page (enum palloc_flags, uint8_t *upage);
void *palloc_get_multiple (enum palloc_flags, size_t page_cnt, uint8_t *page);
void palloc_free_page (void *);
void palloc_free_multiple (void *, size_t page_cnt);
void palloc_kernel_pool_change_pd (uint32_t *pd);
void print_frame_table();

#endif /* threads/palloc.h */
