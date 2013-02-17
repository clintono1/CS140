#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/palloc.h"

void *vm_allocate_frame(enum palloc_flags);

#endif /* vm/frame.h */
