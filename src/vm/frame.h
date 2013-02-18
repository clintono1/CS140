#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"

#define FRAME_TABLE_ERROR SIZE_MAX
#define KERNEL_PAGE_TABLE_BASE 0x00010000

/* Frame table */
struct frame_table
{
  size_t page_cnt;
  uint32_t **frames;
};

size_t frame_table_size (size_t page_cnt);
void frame_table_create (struct frame_table *ft, size_t page_cnt,
                         void *block, size_t block_size UNUSED);
size_t frame_table_scan_and_set (struct frame_table *ft, size_t start,
                                 size_t cnt, uint8_t *vaddr);
bool frame_table_all (const struct frame_table *ft, size_t start, size_t cnt);
void frame_table_set_multiple (struct frame_table *ft, size_t start,
                               size_t cnt, uint32_t *start_pte_addr);



#endif /* vm/frame.h */
