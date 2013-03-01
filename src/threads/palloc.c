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
extern struct lock pin_lock;
extern struct condition pin_cond;

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
      // TODO
      printf ("(tid=%d) palloc_get_multiple %p\n", thread_current()->tid, page);
      struct thread *cur = thread_current ();
      if (flags & PAL_MMAP)
      {
        /* Do NOT support allocating multiple pages for memory mapped
           files at a time */
        ASSERT (page_cnt == 1);
        uint32_t *pte, *fte;
        pte = lookup_page (cur->pagedir, page, false);

        lock_acquire (&pin_lock);
        *pte |= PTE_I;
        lock_release (&pin_lock);

        ASSERT (pte != NULL);
        ASSERT (*pte & PTE_M);
        fte = (uint32_t *) suppl_pt_get_spte (&cur->suppl_pt, pte);
        pool->frame_table.frames[page_idx] =
            (uint32_t *) ((uint8_t *)fte - (unsigned) PHYS_BASE);
      }
      else
      {
        frame_table_set_multiple (&pool->frame_table, page_idx, page_cnt,
                                  cur->pagedir, page, true);
      }
    }
    else /* Kernel Pool */
    {
      ASSERT (page == NULL);
      uint32_t *pd = init_page_dir ? init_page_dir : (uint32_t*)KERNEL_PAGE_DIR;
      uint8_t *kpage = pool->base + page_idx * PGSIZE;
      frame_table_set_multiple (&pool->frame_table, page_idx, page_cnt,
                                pd, kpage, false);
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
   UPAGE denotes the user virtual address to set the frame table entry to if
   the page is allocated for a user process. */
static void *
page_out_then_get_page (struct pool *pool, enum palloc_flags flags, uint8_t *upage)
{
  uint32_t *pte_new;
  uint32_t *fte_new = NULL;
  struct thread *cur = thread_current ();

  if (flags & PAL_USER)
  {
    pte_new = lookup_page (cur->pagedir, upage, true);
    ASSERT ((void *) pte_new > PHYS_BASE);

    lock_acquire (&pin_lock);
    *pte_new |= PTE_I;
    lock_release (&pin_lock);

    if (*pte_new & PTE_M)
    {
      struct suppl_pte *spte = suppl_pt_get_spte (&cur->suppl_pt, pte_new);
      ASSERT ((void *) spte > PHYS_BASE);
      if (flags & PAL_MMAP)
        fte_new = (uint32_t *) ((uint8_t *) spte - (unsigned) PHYS_BASE);
      else
        fte_new = pte_new;
    }
    else
      fte_new = pte_new;
  }

  ASSERT (((flags & PAL_USER) && (void *) fte_new != NULL)
          || (!(flags & PAL_USER) && (fte_new == NULL)) );

  lock_acquire (&pool->lock);
  while (1)
  {
    size_t clock_cur = pool->frame_table.clock_cur;
    uint32_t *fte_old = pool->frame_table.frames[clock_cur];
    uint8_t *page = pool->base + clock_cur * PGSIZE;

    if (fte_old == NULL)
    {
      // TODO
      printf ("page out found empty page %p\n", page);
      pool->frame_table.frames[clock_cur] = fte_new;
      pool_increase_clock (pool);
      lock_release (&pool->lock);
      if (flags & PAL_ZERO)
        memset ((void *) page, 0, PGSIZE);
      return page;
    }

    uint32_t *pte_old;
    struct suppl_pte *spte = NULL;
    if ((void *) fte_old > PHYS_BASE)
      pte_old = fte_old;
    else
    {
      spte = (struct suppl_pte *) ((uint8_t *) fte_old + (unsigned) PHYS_BASE);
      pte_old = spte->pte;
      ASSERT(*pte_old & PTE_M);
    }

    lock_acquire (&pin_lock);
    bool pinned = *pte_old & PTE_I;
    lock_release (&pin_lock);

    /* If the page is pinned, skip this frame table entry */
    if (pinned)
    {
      // TODO
      printf ("(tid=%d) page out skip pinned %p\n", thread_current()->tid, page);
      pool_increase_clock (pool);
      continue;
    }

    /* If another process releases its pages from the frame table,
       an unpresent PTE will show up here. */
    // TODO This following situation is never true if locked in palloc_free_multiple
    if (!(*pte_old & PTE_P))
    {
      // TODO
      ASSERT (0);
    }

    ASSERT (page == ptov (*pte_old & PTE_ADDR));
    if (!(*pte_old & PTE_A))
    {
      // TODO
      printf ("(tid=%d) page out replace %p\n", thread_current()->tid, page);

      lock_acquire (&pin_lock);
      *pte_old |= PTE_I;
      lock_release (&pin_lock);

      *pte_old &= ~PTE_P;
      invalidate_pagedir (thread_current ()->pagedir);

      pool->frame_table.frames[clock_cur] = fte_new;
      pool_increase_clock (pool);
      lock_release (&pool->lock);
      if (*pte_old & PTE_M)
      {
        // TODO
        printf ("(tid=%d) page out unmap %p\n", thread_current()->tid, page);
        /* Initialized/uninitialized data pages are changed to normal memory
           pages once loaded. Thus they should not reach here. */
        ASSERT ((spte->flags & SPTE_C) || (spte->flags & SPTE_M));
        if ((spte->flags & SPTE_M) && (*pte_old & PTE_D))
        {
          ASSERT ((spte->flags & ~SPTE_M) == 0);
          file_write_at (spte->file, page, spte->bytes_read, spte->offset);
        }
      }
      else
      {
        // TODO
        printf ("(tid=%d) page out swap %p\n", thread_current()->tid, page);
        *pte_old &= PTE_FLAGS;
        size_t swap_frame_no = swap_allocate_page (&swap_table);
        *pte_old |= swap_frame_no << PGBITS;
        swap_write (&swap_table, swap_frame_no, page);
      }

      lock_acquire (&pin_lock);
      *pte_old &= ~PTE_I;
      cond_broadcast (&pin_cond, &pin_lock);
      lock_release (&pin_lock);

      if (flags & PAL_ZERO)
        memset ((void *) page, 0, PGSIZE);
      return page;
    }
    else  /* If accessed */
    {
      // TODO
      printf ("(tid=%d) page out skip accessed %p\n", thread_current()->tid, page);
      *pte_old &= ~PTE_A;
      // TODO
      invalidate_pagedir (thread_current()->pagedir);
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

  // TODO
  if (flags & PAL_USER)
    printf ("(tid=%d) palloc_get_page %p for %p\n", thread_current()->tid, frame, page);

  return frame;
}

/* Frees the PAGE_CNT pages starting at PAGES. */
void
palloc_free_multiple (void *kpage, size_t page_cnt)
{
  struct pool *pool;
  size_t page_idx;

  ASSERT (pg_ofs (kpage) == 0);
  if (kpage == NULL || page_cnt == 0)
    return;

  if (page_from_pool (&kernel_pool, kpage))
    pool = &kernel_pool;
  else if (page_from_pool (&user_pool, kpage))
    pool = &user_pool;
  else
    NOT_REACHED ();
  page_idx = pg_no (kpage) - pg_no (pool->base);

#ifndef NDEBUG
  // TODO
  memset (kpage, 0xdd, PGSIZE * page_cnt);
#endif

  lock_acquire(&pool->lock);
  size_t i;
  for (i = 0; i < page_cnt; i++)
  {
      ASSERT (pool->frame_table.frames[page_idx + i] != NULL)
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

