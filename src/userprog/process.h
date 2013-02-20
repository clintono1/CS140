#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
void get_first_string(const char * , char *);
tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
bool install_page (void *, void *, bool);
void mmap_files_init(struct thread *);
#define MAX_FILE_LENGTH 16  /* max length of file name */
#endif /* userprog/process.h */
