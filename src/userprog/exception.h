#include "threads/pte.h"
#include "vm/page.h"


#ifndef USERPROG_EXCEPTION_H
#define USERPROG_EXCEPTION_H


/* Page fault error code bits that describe the cause of the exception.  */
#define PF_P 0x1    /* 0: not-present page. 1: access rights violation. */
#define PF_W 0x2    /* 0: read, 1: write. */
#define PF_U 0x4    /* 0: kernel, 1: user process. */

void exception_init (void);
void exception_print_stats (void);
void load_page_from_swap (uint32_t *pte, void *fault_page, bool pin);
void load_page_from_file (struct suppl_pte *s_pte, uint8_t *upage, bool pin);

#endif /* userprog/exception.h */
