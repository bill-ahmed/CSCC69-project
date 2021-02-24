#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "threads/synch.h"

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
      int *string_buffer_addr = f->esp + 4;

      if (string_buffer_addr == NULL)
      {
        exit(-1);
      }

      // Can't remember which way this comparator goes
      if (string_buffer_addr > PHYS_BASE)
      {
        // Throw a page fault.
        // This is should pass the last test case (create-bad-ptr)
      }

      void *file_name = *string_buffer_addr;
      int *file_size = f->esp + 8;

      if (file_size == NULL || file_name == NULL)
      {
        exit(-1);
      }

      lock_acquire(&modification_lock);
      f->eax = create(file_name, *file_size);
      lock_release(&modification_lock);

      break;
    }
      
    case SYS_REMOVE:
    {
      int *string_buffer_addr = f->esp + 4;

      if (string_buffer_addr == NULL)
      {
        exit(-1);
      }

      if (string_buffer_addr > PHYS_BASE)
      {
        // Throw a page fault.
      }

      void *file_name = *string_buffer_addr;
      if (file_name == NULL)
      {
        exit(-1);
      }

      lock_acquire(&modification_lock);
      f->eax = remove(file_name);
      lock_release(&modification_lock);

      break;
    }
      
    case SYS_OPEN:
    {
      break;
    }
    case SYS_FILESIZE:
    {
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
      break;
    }

    // Unhandled case
    default:
      break;
  }
  thread_yield ();
}

/** Helper methods **/



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
  return filesys_create(file, initial_size);
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
  return filesys_remove(file);
}

int 
open (const char *file)
{
  // TODO
  return 0;
}

void 
close (int fd)
{
  // TODO
  return 0;
}

int 
filesize (int fd)
{
  // TODO
  return 0;
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
