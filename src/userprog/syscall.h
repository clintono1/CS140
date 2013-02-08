#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "lib/user/syscall.h"

/* Retrieve the n-th argument */
#define GET_ARGUMENT(sp, n) (*(sp + n))

void syscall_init (void);

#endif /* userprog/syscall.h */
