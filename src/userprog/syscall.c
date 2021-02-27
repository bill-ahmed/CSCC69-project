#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "lib/string.h"

static void syscall_handler (struct intr_frame *);

// Syscalls
void halt (void);
void exit (int status);

int exec (const char *cmd_line);
int wait (int pid);

bool create (const char *file, unsigned initial_size);
bool remove (const char *file);

int open (const char *file);
void close (int fd);
int filesize (int fd);

int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);

// Helper prototypes
void* get_stack_arg(void *esp, int offset);
void exit_if_null(void *ptr);

// Synchronization variables
struct lock modification_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  
  // Initialize locks
  lock_init(&modification_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  /**
   * TODO: probably want to check memory is initialized 
   *       before trying to dereference it
  */
  int *sys_call_num = f->esp;

  if (sys_call_num == NULL) {
    // Throw invalid pointer exception
  }

  switch (*sys_call_num)
  {
    case SYS_HALT:
    {
      break;
    }
    
    case SYS_EXIT:
    {
      int *exit_code = f->esp + 4;
      exit (*exit_code);
    }

    case SYS_EXEC:
    {
      break;
    }

    case SYS_WAIT:
    {
      break;
    }

    case SYS_CREATE:
    {
      int *string_buffer_addr = (int*) get_stack_arg (f->esp, 4);
      exit_if_null (string_buffer_addr);

      // Can't remember which way this comparator goes
      if (string_buffer_addr > PHYS_BASE)
      {
        // Throw a page fault.
        // This is should pass the last test case (create-bad-ptr)
      }

      void *file_name = *string_buffer_addr;
      exit_if_null (file_name);
      int *file_size = (int*) get_stack_arg (f->esp, 8);
      exit_if_null (file_size);

      lock_acquire (&modification_lock);
      f->eax = create (file_name, *file_size);
      lock_release (&modification_lock);
      break;
    }
      
    case SYS_REMOVE:
    {
      int *string_buffer_addr = (int*) get_stack_arg (f->esp, 4);
      exit_if_null (string_buffer_addr);

      if (string_buffer_addr > PHYS_BASE)
      {
        // Throw a page fault.
      }

      void *file_name = *string_buffer_addr;
      exit_if_null (file_name);

      lock_acquire (&modification_lock);
      f->eax = remove (file_name);
      lock_release (&modification_lock);
      break;
    }
      
    case SYS_OPEN:
    {
      int *name_buffer_ptr = (int*) get_stack_arg (f->esp, 4);
      exit_if_null (name_buffer_ptr);

      if (name_buffer_ptr > PHYS_BASE)
      {
        // Throw a page fault.
      }

      void *file_name = *name_buffer_ptr;
      exit_if_null (file_name);

      // Anyone can open the file at the same time to read
      f->eax = open (file_name);
      break;
    }
    case SYS_FILESIZE:
    {
      int *fd = (int*) get_stack_arg (f->esp, 4);
      exit_if_null(fd);
      exit_if_null(*fd);
      if (*fd < 2) exit (-1);
      f->eax = filesize (*fd);
      break;
    }
    case SYS_READ:
    {
      break;
    }

    case SYS_WRITE:
    {
      int *fd_addr = f->esp + 4;
      int *buff_addr = f->esp + 8;
      int *buff_size = f->esp + 12;

      f->eax = write (*fd_addr, *buff_addr, *buff_size);
    }

    case SYS_SEEK:
    {
      break;
    }

    case SYS_TELL:
    {
      break;
    }

    case SYS_CLOSE:
    {
      int *fd = (int*) get_stack_arg (f->esp, 4);
      if (fd > PHYS_BASE)
      {
        // Throw a page fault.
      }

      exit_if_null (fd);

      // No need to synchronize since fds are process dependent.
      close (*fd);
      break;
    }

    // Unhandled case
    default:
      break;
  }
  thread_yield ();
}

/** Helper methods **/
void*
get_stack_arg (void *esp, int offset) 
{
  return esp + offset; 
}

void
exit_if_null (void *ptr) 
{
  if (ptr == NULL) exit (-1);
}


/** Implement system calls below!! **/

void 
halt (void)
{
  shutdown_power_off ();
}

void
exit (int status)
{
  // TODO: return status of program to parent!
  printf ("%s: exit(%d)\n", thread_current ()->process_name, status);
  thread_exit ();
}

int // Should be pid_t?? 
exec (const char *cmd_line)
{
  // TODO
  return 0;
}

int 
wait (int pid /* Should be pid_t?? */)
{
  // TODO
  return 0;
}

bool 
create (const char *file, unsigned initial_size)
{
  /*
    - Validate file name?
    - See if a such file already exists?
    - Is there enough room on disk to create it?
  */
  return filesys_create (file, initial_size);
}

bool 
remove (const char *file)
{
  /* 
    - Validate file name?
    - Are there other processes with this fd open?
    - Is someone writing to it?
    - Does the calling process think it's removed if this returns
      but another process still has it open? Should that be an error?
  */
  return filesys_remove (file);
}

int
open (const char *file_name)
{
  const char* empty = "";
  if (strcmp(file_name, empty) == 0)
    return -1;

  struct file *file = filesys_open(file_name);
  
  if (file == NULL)
    return -1;

  return thread_get_next_descriptor (file);
}

void 
close (int fd)
{ 
  exit_if_null (fd);
  struct file* file = thread_get_file_by_fd (fd);
  exit_if_null (file);

  thread_remove_descriptor (fd);
  file_close (file);
  return 0;
}

int 
filesize (int fd)
{
  struct file *file = thread_get_file_by_fd (fd);
  exit_if_null (file);
  return file_length (file);
}

int 
read (int fd, void *buffer, unsigned size)
{
  // TODO
  return 0;
}

/* Handle writing to console. */
int
write (int fd, const void *buffer, unsigned size)
{  
  if(fd == 1)
  {    
    // Write to console
    putbuf (buffer, size);
    return size;
  }
  else
  {
    printf("ERROR: unknown file descripter for 'sys_write' of type: %d\n", fd);
    return -1;
  }
}

void 
seek (int fd, unsigned position)
{
  // TODO
}

unsigned 
tell (int fd)
{
  // TODO
  return 0;
}
