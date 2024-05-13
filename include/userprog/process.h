#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "filesys/file.h"
#include "threads/thread.h"
#include <list.h>

struct file_fd {
  struct file *file;
  int ref_count;
  struct list_elem file_fd_elem;
};

tid_t process_create_initd(const char *file_name);
tid_t process_fork(const char *name, struct intr_frame *if_);
int process_exec(void *f_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(struct thread *next);
struct file *find_child_file(struct file *parent);

#endif /* userprog/process.h */
