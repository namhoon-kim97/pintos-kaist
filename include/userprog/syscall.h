#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "filesys/file.h"
void syscall_init(void);
void exit(int status);
void check_address(const uint64_t *addr);
void decrease_fd_ref(struct file *file, int fd);
void increase_fd_ref(struct file *file, int fd);
#endif /* userprog/syscall.h */
