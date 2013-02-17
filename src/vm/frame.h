#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"
#include "lib/kernel/list.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"

/* Frame table entry */
struct fte
{
  uint8_t *kpage;
  uint8_t *upage;//these two have the same usage: to read the reference/dirty bit. choose one later
  uint32_t *pte;//these two have the same usage: to read the reference/dirty bit. choose one later
  struct thread *t;
  struct list_elem elem;
};

void *vm_allocate_frame(enum palloc_flags flags, uint8_t *upage);

#endif /* vm/frame.h */
