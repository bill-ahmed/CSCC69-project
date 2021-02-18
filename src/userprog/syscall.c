#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

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


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  /**
   * TODO: probably want to check memory is initialized 
   *       before trying to reference it
  */
  int *sys_call_num = f->esp;

  // big boi
  switch (*sys_call_num)
    {
    // SYS_HALT
    case 0:
      break;
    
    // SYS_EXIT
    case 1:
    {
      int *exit_code = f->esp + 4;
      exit (*exit_code);
    }

    // SYS_EXEC
    case 2:
      break;

    // SYS_WAIT
    case 3:
      break;

    // SYS_CREATE
    case 4:
      break;

    // SYS_REMOVE
    case 5:
      break;

    // SYS_OPEN
    case 6:
      break;

    // SYS_FILESIZE
    case 7:
      break;

    // SYS_READ
    case 8:
      break;

    // SYS_WRITE
    case 9:
    {
      int *fd_addr = f->esp + 4;
      int *buff_addr = f->esp + 8;
      int *buff_size = f->esp + 12;

      f->eax = write (*fd_addr, *buff_addr, *buff_size);
    }

    // SYS_SEEK
    case 10:
      break;

    // SYS_TELL
    case 11:
      break;

    // SYS_CLOSE
    case 12:
      break;
      
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
  // TODO
  return 0;
}

bool 
remove (const char *file)
{
  // TODO
  return 0;
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
