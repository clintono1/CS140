#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "lib/user/syscall.h"



void syscall_init (void);
static bool checkvaddr(const void * vaddr, unsigned size);

#endif /* userprog/syscall.h */
