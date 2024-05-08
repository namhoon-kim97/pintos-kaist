#include "userprog/syscall.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "intrinsic.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

#define FD_TABLE_SIZE 193
#define pid_t tid_t
static struct lock fs_lock;
static struct lock fd_table_lock;

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/* Projects 2 and later. */
void halt(void) NO_RETURN;
void exit(int status) NO_RETURN;
pid_t fork(const char *thread_name, struct intr_frame *f);
int exec(const char *file);
int wait(pid_t);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned length);
int write(int fd, const void *buffer, unsigned length);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

int dup2(int oldfd, int newfd);
void check_address(void *addr);
void check_valid_fd(int fd);

void syscall_init(void) {
  write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG)
                                                               << 32);
  write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

  /* The interrupt service rountine should not serve any interrupts
   * until the syscall_entry swaps the userland stack to the kernel
   * mode stack. Therefore, we masked the FLAG_FL. */
  write_msr(MSR_SYSCALL_MASK,
            FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

  lock_init(&fs_lock);
  lock_init(&fd_table_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED) {
  // TODO: Your implementation goes here.
  int sys_num = f->R.rax;
  switch (sys_num) {
  case SYS_HALT:
    halt();
    break;
  case SYS_EXIT:
    exit(f->R.rdi);
    break;
  case SYS_CREATE:
    f->R.rax = create(f->R.rdi, f->R.rsi);
    break;
  case SYS_REMOVE:
    f->R.rax = remove(f->R.rdi);
    break;
  case SYS_OPEN:
    f->R.rax = open(f->R.rdi);
    break;
  case SYS_CLOSE:
    close(f->R.rdi);
    break;
  case SYS_FILESIZE:
    f->R.rax = filesize(f->R.rdi);
    break;
  case SYS_READ:
    f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
    break;
  case SYS_WRITE:
    f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
    break;
  case SYS_SEEK:
    seek(f->R.rdi, f->R.rsi);
    break;
  case SYS_TELL:
    tell(f->R.rdi);
    break;
  case SYS_EXEC:
    exec(f->R.rdi);
    break;
  case SYS_FORK:
    f->R.rax = fork(f->R.rdi, f);
    break;

  default:
    break;
  }
}

void check_address(void *addr) {
  if (addr == NULL || is_kernel_vaddr(addr) ||
      pml4_get_page(thread_current()->pml4, addr) == NULL)
    exit(-1);
}

void halt() { power_off(); }

void exit(int status) {
  printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_current()->exit_status = status;
  thread_exit();
}

bool create(const char *file, unsigned initial_size) {
  check_address(file);

  return filesys_create(file, initial_size);
}
bool remove(const char *file) {
  check_address(file);

  return filesys_remove(file);
}

int open(const char *file) {
  check_address(file);

  struct thread *cur = thread_current();
  struct file *opened_file = filesys_open(file);

  if (opened_file == NULL)
    return -1;

  for (int i = 2; i < FD_TABLE_SIZE; i++) {
    if (cur->fdt[i] == NULL) {
      cur->fdt[i] = opened_file;
      return i;
    }
  }

  return -1;
}

void check_valid_fd(int fd) {
  if (0 > fd || fd >= FD_TABLE_SIZE)
    exit(-1);
}

void close(int fd) {
  check_valid_fd(fd);

  struct thread *cur = thread_current();
  if (cur->fdt[fd] == NULL)
    exit(-1);
  cur->fdt[fd] = NULL;

  file_close(cur->fdt[fd]);
}

int filesize(int fd) {
  check_valid_fd(fd);
  struct thread *cur = thread_current();
  if (cur->fdt[fd] == NULL)
    exit(-1);

  return file_length(cur->fdt[fd]);
}

int read(int fd, void *buffer, unsigned size) {
  check_valid_fd(fd);
  check_address(buffer);

  if (fd == 0) {
    for (int i = 0; i < size; i++) {
      ((char *)buffer)[i] = input_getc();
    }
    return size;
  }
  if (fd == 1)
    return -1;

  struct thread *cur = thread_current();
  if (cur->fdt[fd] == NULL)
    exit(-1);

  lock_acquire(&fs_lock);
  int ret = file_read(cur->fdt[fd], buffer, size);
  lock_release(&fs_lock);

  return ret;
}

int write(int fd, const void *buffer, unsigned size) {
  check_valid_fd(fd);
  check_address(buffer);

  if (fd == 0)
    return -1;
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  }

  struct thread *cur = thread_current();
  if (cur->fdt[fd] == NULL)
    exit(-1);

  lock_acquire(&fs_lock);
  int ret = file_write(cur->fdt[fd], buffer, size);
  lock_release(&fs_lock);

  return ret;
}

void seek(int fd, unsigned position) {
  check_valid_fd(fd);
  struct thread *cur = thread_current();
  if (cur->fdt[fd] == NULL)
    exit(-1);
  file_seek(cur->fdt[fd], position);
}

unsigned tell(int fd) {
  check_valid_fd(fd);
  struct thread *cur = thread_current();
  if (cur->fdt[fd] == NULL)
    exit(-1);
  return file_tell(cur->fdt[fd]);
}

int exec(const char *file) { return process_exec(file); }

pid_t fork(const char *thread_name, struct intr_frame *f) {
  memcpy(&(thread_current()->parent_tf), f, sizeof(struct intr_frame));
  return process_fork(thread_name, f);
}

int wait(pid_t pid) {}
