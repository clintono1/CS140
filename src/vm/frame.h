#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"

#define FRAME_TABLE_ERROR SIZE_MAX
#define KERNEL_PAGE_DIR   0xc000f000
#define KERNEL_PAGE_TABLE 0xc0010000

/* Frame table */
struct frame_table
{
  size_t page_cnt;
  uint32_t **frames;
};

size_t frame_table_size (size_t page_cnt);
void frame_table_create (struct frame_table *ft, size_t page_cnt,
                         void *block, size_t block_size UNUSED);
bool frame_table_all (const struct frame_table *ft, size_t start, size_t cnt);
size_t frame_table_scan (struct frame_table *ft, size_t start, size_t cnt);
void frame_table_set_multiple (struct frame_table *ft, size_t start, size_t cnt,
                               uint32_t *pd, uint8_t *vaddr, bool create);

#endif /* vm/frame.h */
