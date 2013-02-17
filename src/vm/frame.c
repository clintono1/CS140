#include "vm/frame.h"



struct list frame_list;
struct lock frame_list_lock;
bool full;


void frame_table_init (struct list *frame_list)
{
  full = 0;
  list_init(frame_list);
  lock_init(&frame_list_lock);
}

//the last argument upage means: map which virtual adress to the newly allocated page
void *
vm_allocate_frame(enum palloc_flags flags, uint8_t *upage) 
{
  lock_acquire(&frame_list_lock);
	void *kpage = NULL;
	kpage = palloc_get_page(flags);
  if (kpage == NULL)		
  {
    full = 1;  //next step: evict another page 
    lock_release (&frame_list_lock);
    return NULL;
  }
  struct frame_table *f_t ;
  f_t = (struct frame_table *)malloc(sizeof(struct frame_table));
  f_t->t = thread_current();
  f_t->kpage = kpage;
  f_t->upage = upage;
  f_t->pte = pagedir_get_page(thread_current()->pagedir, upage);
  f_t->recent = 0;
  list_push_back(&frame_list, &f_t->elem);
  lock_release (&frame_list_lock);
  	
	return kpage;
}
