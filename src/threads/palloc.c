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
        pte = lookup_page (cur->pagedir, page, true);
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

/* Page out a page from the frame table in POOL and then return the page's
   virtual kernel address */
static void *
page_out_then_get_page (struct pool *pool, enum palloc_flags flags, uint8_t *page)
{
  printf("page out then get page for va= %p\n", page); 
// uint32_t i; 
/*
  for ( i= 0; i<pool->frame_table.page_cnt; i++)
  {
    uint32_t *fte = pool->frame_table.frames[i];
    printf("fte[%d] = %p\n", i, fte);
  }*/

  uint32_t *fte = NULL;

  if (flags & PAL_USER)
  {
    struct thread *cur = thread_current ();
    uint32_t *pte = lookup_page (cur->pagedir, page, true);
    if (*pte & PTE_M)
    {
      ASSERT (flags & PAL_MMAP);
      fte = (uint32_t *) suppl_pt_get_spte (&cur->suppl_pt, pte);
    }
    else
      fte = pte;
  }
  else
  {
    printf("Wrong, only user can request!\n");
  }

  ASSERT (((flags & PAL_USER) && (void *) fte > PHYS_BASE)
          || (!(flags & PAL_USER) && (fte == NULL)) );
  
  //TODO: two things can modify the page table entry: by its owner thread, or kicked out by another process
  //there could be race conditions. need to solve. currently just hold the user pool lock.
  lock_acquire (&pool->lock);
  while (1)
  {
    size_t clock_cur = pool->frame_table.clock_cur;
    uint32_t *frame = pool->frame_table.frames[clock_cur];
    //shouldn't assert here! frame could be less than PHYS_BASE if MMF and shall cause page fault. Debuged me 2 hours...
    //ASSERT (*frame != 0);
    //TODO: pls delete this after you see this.
    //printf("frame[%d]= %p\n", clock_cur, frame);

    uint32_t *pte;
    struct suppl_pte *s_pte = NULL;
    bool mmap = false;
    if ((void *) frame > PHYS_BASE)
      pte = frame;
    else
    {
      mmap = true;
      s_pte = (struct suppl_pte *) ((uint8_t *)frame + (unsigned) PHYS_BASE);
      pte = s_pte->pte;
    }

    ASSERT (*pte & PTE_P);
    bool accessed = (*pte & PTE_A);
    bool dirty = (*pte & PTE_D);
    //printf("MMF = %d, accessed = %d, dirty = %d\n", mmap, accessed, dirty);
    
    /* If neither accessed nor dirty, throw away current page */
    size_t swap_frame_no;
    uint8_t *kpage = pool->base + clock_cur * PGSIZE;
    ASSERT (!mmap || (kpage == ptov (*s_pte->pte & PTE_ADDR)));
    if (!accessed)
    {
      /* if mmap is true, there are still four cases: true mmap, code, 
         uninitilized data, initialized data. Only the first case need 
         write back to file, the latter three cases need write back to swap */
      if (mmap && (s_pte->flags & SPTE_MMF))
      {
        if (dirty)
          file_write_at (s_pte->file, kpage,
                         s_pte->bytes_read, s_pte->offset);
      }
      else
      {
        swap_frame_no = swap_allocate_page (&swap_table);
        swap_write (&swap_table, swap_frame_no, kpage);
      }
      /* For all cases when !accessed, choose this frame to return (kicked out)*/
      if (flags & PAL_MMAP)
        fte = (uint32_t *) ((uint8_t *) fte - (unsigned) PHYS_BASE);
      pool->frame_table.frames[clock_cur] = fte;
      *pte = vtop (kpage);
      if (flags & PAL_ZERO)
        memset ((void *) kpage, 0, PGSIZE);
      // TODO
      *pte |= PTE_A;
      pool_increase_clock (pool);
      lock_release (&pool->lock);
      return ptov (*pte & PTE_ADDR);
    }
    else  /* If accessed */
    {
      *pte &= ~PTE_A;
      pool_increase_clock (pool);
    }
  }
}

/* Obtains a single free page and returns its kernel virtual
   address.
   If PAL_USER is set, the page is obtained from the user pool,
   otherwise from the kernel pool.  If PAL_ZERO is set in FLAGS,
   then the page is filled with zeros.  If no pages are
   available, returns a null pointer, unless PAL_ASSERT is set in
   FLAGS, in which case the kernel panics. */
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

#ifndef NDEBUG
  memset (pages, 0xcc, PGSIZE * page_cnt);
#endif

  ASSERT (frame_table_all (&pool->frame_table, page_idx, page_cnt));
  size_t i;
  for (i = 0; i < page_cnt; i++)
  {
    pool->frame_table.frames[page_idx + i] = NULL;
  }
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

