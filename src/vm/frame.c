#include "vm/frame.h"

void *
vm_allocate_frame(enum palloc_flags flags){
	void *frame = NULL;
	if(flags & PAL_USER){
		if(flags & PAL_ZERO){
			frame = palloc_get_page(PAL_USER | PAL_ZERO);
		}
		else{
			frame = palloc_get_page(PAL_USER);
		}
	}
	if(frame){
		/* add to frame list */
	}
	else{
		/* eviction */
	}
	return frame;
}
