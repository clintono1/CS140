#include "threads/palloc.h"
#include <bitmap.h>
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/loader.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/frame.h"
#include "vm/swap.h"

extern uint32_t *init_page_dir;
extern struct swap_table swap_table;


/* Page allocator.  Hands out memory in page-size (or
   page-multiple) chunks.  See malloc.h for an allocator that
   hands out smaller chunks.

   System memory is divided into two "pools" called the kernel
   and user pools.  The user pool is for user (virtual) memory
   pages, the kernel pool for everything else.  The idea here is
   that the kernel needs to have memory for its own operations
   even if user processes are swapping like mad.

   By default, half of system RAM is given to the kernel pool and
   half to the user pool.  That should be huge overkill for the
   kernel pool, but that's just fine for demonstration purposes. */

/* A memory pool. */
struct pool
  {
    struct lock lock;                   /* Mutual exclusion. */
    struct frame_table frame_table;     /* Frame table of the pool */
    uint8_t *base;                      /* Base of pool. */
  };

/* Two pools: one for kernel data, one for user pages. */
static struct pool kernel_pool, user_pool;

static void init_pool (struct pool *, void *base, size_t page_cnt,
                       const char *name);
static bool page_from_pool (const struct pool *, void *page);


/* Initializes the page allocator.  At most USER_PAGE_LIMIT
   pages are put into the user pool. */
void
palloc_init (size_t user_page_limit)
{
  /* Free memory starts at 1 MB and runs to the end of RAM. */
  uint8_t *free_start = ptov (1024 * 1024);
  uint8_t *free_end = ptov (init_ram_pages * PGSIZE);
  size_t free_pages = (free_end - free_start) / PGSIZE;
  size_t user_pages = free_pages / 2;
  size_t kernel_pages;
  if (user_pages > user_page_limit)
    user_pages = user_page_limit;
  kernel_pages = free_pages - user_pages;

  /* Give half of memory to kernel, half to user. */
  init_pool (&kernel_pool, free_start, kernel_pages, "kernel pool");
  init_pool (&user_pool, free_start + kernel_pages * PGSIZE,
             user_pages, "user pool");

  /*Clock algorithm initialization is done inside the init_pool function
   *init_pool calls frame_table_create */
}

/* Obtains and returns a group of PAGE_CNT contiguous free pages.
   If PAL_USER is set, the pages are obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the pages are filled with zeros.  If too few pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
void *
palloc_get_multiple (enum palloc_flags flags, size_t page_cnt, uint8_t *page)
{
  struct pool *pool = flags & PAL_USER ? &user_pool : &kernel_pool;
  void *pages;
  size_t page_idx;

  if (page_cnt == 0)
    return NULL;

  lock_acquire (&pool->lock);
  page_idx = frame_table_scan (&pool->frame_table, 0, page_cnt);
  if (page_idx != FRAME_TABLE_ERROR)
  {
    if (flags & PAL_USER)
    {
      ASSERT (page != NULL);
      ASSERT ((void *) page < PHYS_BASE);
      struct thread *cur = thread_current ();
      if (flags & PAL_MMAP)
      {
        /* Do NOT support allocating multiple pages for memory mapped
           files at a time */
        ASSERT (page_cnt == 1);
        uint32_t *pte, *fte;
        pte = lookup_page (cur->pagedir, page, false);
        ASSERT (pte != NULL);
        ASSERT (*pte & PTE_M);
        *pte |= PTE_I;
        fte = (uint32_t *) suppl_pt_get_spte (&cur->suppl_pt, pte);
        pool->frame_table.frames[page_idx] =
            (uint32_t *) ((uint8_t *)fte - (unsigned) PHYS_BASE);
      }
      else
      {
        frame_table_set_multiple (&pool->frame_table, page_idx, page_cnt,
                                  cur->pagedir, page, true, true);
      }
    }
    else /* Kernel Pool */
    {
      ASSERT (page == NULL);
      uint32_t *pd = init_page_dir ? init_page_dir : (uint32_t*)KERNEL_PAGE_DIR;
      uint8_t *kpage = pool->base + page_idx * PGSIZE;
      frame_table_set_multiple (&pool->frame_table, page_idx, page_cnt,
                                pd, kpage, false, false);
    }
  }
  lock_release (&pool->lock);

  if (page_idx != FRAME_TABLE_ERROR)
    pages = pool->base + PGSIZE * page_idx;
  else  /* There's not enough empty spaces */
    pages = NULL;

  if (pages != NULL)
  {
    if (flags & PAL_ZERO)
      memset (pages, 0, PGSIZE * page_cnt);
  }
  else
  {
    if (flags & PAL_ASSERT)
      PANIC ("palloc_get: out of pages");
  }

  return pages;
}

static inline void
pool_increase_clock (struct pool *pool)
{
  /* Advance the current clock by 1 */
  pool->frame_table.clock_cur = (pool->frame_table.clock_cur + 1)
                                % pool->frame_table.page_cnt;
}

// TODO: remove before submit
void print_user_frame_table (void)
{
  uint32_t i;
  for (i = 0; i < user_pool.frame_table.page_cnt; i++)
  {
    uint32_t *fte = user_pool.frame_table.frames[i];
    printf ("fte[%d] = %p\n", i, fte);
  }
}

/* Page out a page from the frame table in POOL and then return the page's
   virtual kernel address.
   FLAGS carries the allocation specification.
   PAGE denotes the user virtual address to set the frame table entry to if
   the page is allocated for a user process. */
static void *
page_out_then_get_page (struct pool *pool, enum palloc_flags flags, uint8_t *page)
{
  uint32_t *fte_new = NULL;
  struct thread *cur = thread_current ();

  if (flags & PAL_USER)
  {
    uint32_t *pte = lookup_page (cur->pagedir, page, true);
    ASSERT ((void *) pte > PHYS_BASE);
    if (*pte & PTE_M)
    {
      struct suppl_pte *spte = suppl_pt_get_spte (&cur->suppl_pt, pte);
      ASSERT ((void *) spte > PHYS_BASE);
      if (flags & PAL_MMAP)
        fte_new = (uint32_t *) ((uint8_t *) spte - (unsigned) PHYS_BASE);
      else
        fte_new = pte;
    }
    else
      fte_new = pte;
  }

  ASSERT (((flags & PAL_USER) && (void *) fte_new != NULL)
          || (!(flags & PAL_USER) && (fte_new == NULL)) );

  //TODO: two things can modify the page table entry: by its owner thread, or kicked out by another process
  //there could be race conditions. need to solve. currently just hold the user pool lock.
  lock_acquire (&pool->lock);
  while (1)
  {
    size_t clock_cur = pool->frame_table.clock_cur;
    uint32_t *fte_old = pool->frame_table.frames[clock_cur];
    uint8_t *page = pool->base + clock_cur * PGSIZE;
    ASSERT (fte_old != 0);

    uint32_t *pte;
    struct suppl_pte *spte = NULL;
    if ((void *) fte_old > PHYS_BASE)
      pte = fte_old;
    else
    {
      spte = (struct suppl_pte *) ((uint8_t *) fte_old + (unsigned) PHYS_BASE);
      pte = spte->pte;
      ASSERT(*pte & PTE_M);
    }

    if (*pte & PTE_I)
    {
      pool_increase_clock (pool);
      continue;
    }

    /* If another process releases its pages from the frame table,
       an unpresent PTE will show up here. */
    if (!(*pte & PTE_P))
    {
      *pte |= PTE_I;
      invalidate_pagedir (thread_current ()->pagedir);
      pool->frame_table.frames[clock_cur] = fte_new;
      pool_increase_clock (pool);
      lock_release (&pool->lock);
      if (flags & PAL_ZERO)
        memset ((void *) page, 0, PGSIZE);
      return page;
    }

    /* If neither accessed nor dirty, throw away current page */
    ASSERT (page == ptov (*pte & PTE_ADDR));
    if (!(*pte & PTE_A))
    {
      *pte &= ~PTE_P;
      *pte |= PTE_I;
      invalidate_pagedir (thread_current ()->pagedir);
      pool->frame_table.frames[clock_cur] = fte_new;
      pool_increase_clock (pool);
      lock_release (&pool->lock);
      if (*pte & PTE_M)
      {
        /* Initialized/uninitialized data pages are changed to normal memory
           pages once loaded. Thus they should not reach here. */
        ASSERT ((spte->flags & SPTE_C) || (spte->flags & SPTE_M));
        if ((spte->flags & SPTE_M) && (*pte & PTE_D))
        {
          ASSERT ((spte->flags & ~SPTE_M) == 0);
          file_write_at (spte->file, page, spte->bytes_read, spte->offset);
        }
      }
      else
      {
        *pte &= PTE_FLAGS;
        size_t swap_frame_no = swap_allocate_page (&swap_table);
        *pte |= swap_frame_no << PGBITS;
        invalidate_pagedir (thread_current ()->pagedir);
        swap_write (&swap_table, swap_frame_no, page);
      }
      if (flags & PAL_ZERO)
        memset ((void *) page, 0, PGSIZE);
      return page;
    }
    else  /* If accessed */
    {
      *pte &= ~PTE_A;
      pool_increase_clock (pool);
    }
  }
}

/* Obtains a single free page and returns its kernel virtual address.
   If PAL_USER is set, the page is obtained from the user pool, otherwise
   from the kernel pool.  If PAL_ZERO is set in FLAGS, then the page is
   filled with zeros.  If no pages are available, returns a null pointer,
   unless PAL_ASSERT is set in FLAGS, in which case the kernel panics. */
void *
palloc_get_page (enum palloc_flags flags, uint8_t *page)
{
  ASSERT (pg_ofs (page) == 0);

  void * frame = palloc_get_multiple (flags, 1, page);
  if (frame == NULL)  /* Not enough frames. Need page-out */
  {
    if (flags & PAL_USER)
    {
      frame = page_out_then_get_page (&user_pool, flags, page);
    }
    else
      PANIC ("Running out of kernel memory pages... Kill the kernel :-(");
  }
  return frame;
}

/* Frees the PAGE_CNT pages starting at PAGES. */
void
palloc_free_multiple (void *pages, size_t page_cnt)
{
  struct pool *pool;
  size_t page_idx;

  ASSERT (pg_ofs (pages) == 0);
  if (pages == NULL || page_cnt == 0)
    return;

  if (page_from_pool (&kernel_pool, pages))
    pool = &kernel_pool;
  else if (page_from_pool (&user_pool, pages))
    pool = &user_pool;
  else
    NOT_REACHED ();
  page_idx = pg_no (pages) - pg_no (pool->base);
  lock_acquire(&pool->lock);
#ifndef NDEBUG
  memset (pages, 0xcc, PGSIZE * page_cnt);
#endif

  ASSERT (frame_table_all (&pool->frame_table, page_idx, page_cnt));
  size_t i;
  for (i = 0; i < page_cnt; i++)
  {
    pool->frame_table.frames[page_idx + i] = NULL;
  }
  lock_release(&pool->lock);
}

/* Frees the page at PAGE. */
void
palloc_free_page (void *page)
{
  palloc_free_multiple (page, 1);
}

/* Initializes pool P as starting at START and ending at END,
   naming it NAME for debugging purposes. */
static void
init_pool (struct pool *p, void *base, size_t page_cnt, const char *name)
{
  size_t ft_pages = DIV_ROUND_UP (frame_table_size (page_cnt), PGSIZE);
  if (ft_pages > page_cnt)
    PANIC ("Not enough memory in %s for frame table.", name);
  page_cnt -= ft_pages;

  printf ("%zu pages available in %s.\n", page_cnt, name);

  /* Initialize the pool. */
  lock_init (&p->lock);
  frame_table_create (&p->frame_table, page_cnt, base, ft_pages * PGSIZE);
  p->base = base + ft_pages * PGSIZE;
}

/* Returns true if PAGE was allocated from POOL,
   false otherwise. */
static bool
page_from_pool (const struct pool *pool, void *page)
{
  size_t page_no = pg_no (page);
  size_t start_page = pg_no (pool->base);
  size_t end_page = start_page + pool->frame_table.page_cnt;

  return page_no >= start_page && page_no < end_page;
}

/* Update the frame table entries in the kernel pool according to the new
   kernel page table. */
void
palloc_kernel_pool_change_pd (uint32_t *pd)
{
  frame_table_change_pagedir (&kernel_pool.frame_table, pd);
}

