#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);

void halt();
void exit(int status);
_Bool create(const char *file, unsigned initial_size);
_Bool remove(const char *file);
int open(const char *file);
void close(int fd);

#endif /* userprog/syscall.h */
