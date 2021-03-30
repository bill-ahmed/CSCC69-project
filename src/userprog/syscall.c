#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "process.h"
#include "pagedir.h"
#include "vm/page.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

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

bool chdir (char *dir);
bool mkdir (char *dir);
bool readdir (int fd, char *name);

bool isDir (int fd);
int iNumber (int fd);

// Helper prototypes
void* get_stack_arg (void *esp, int offset);
void exit_if_null (void *ptr);
void validate_user_address (void *addr);

// Synchronization variables
struct lock filesys_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  
  // Initialize locks
  lock_init(&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f) 

{

  validate_user_address (f->esp);

  int *sys_call_num = f->esp;

  switch (*sys_call_num)
  {
    case SYS_HALT:
    {
      halt ();
      break;
    }
    
    case SYS_EXIT:
    {
      int *exit_code = f->esp + 4;
      validate_user_address (f->esp + 4);
      
      exit (*exit_code);
      break;
    }

    case SYS_EXEC:
    {
      int *exec_file_addr = f->esp + 4;
      validate_user_address (exec_file_addr);
      validate_user_address ((void*) *exec_file_addr);

      void *file_name = *exec_file_addr;

      lock_acquire(&filesys_lock);
      f->eax = exec (file_name);
      lock_release(&filesys_lock);
      break;
    }

    case SYS_WAIT:
    {
      int *pid_addr = f->esp + 4;
      validate_user_address (pid_addr);

      f->eax = wait (*pid_addr);
      break;
    }

    case SYS_CREATE:
    {
      int *string_buffer_addr = (int*) get_stack_arg (f->esp, 4);
      validate_user_address (string_buffer_addr);

      void *file_name = *string_buffer_addr;
      int *file_size = (int *) get_stack_arg (f->esp, 8);

      validate_user_address (file_name);
      validate_user_address (file_size);

      lock_acquire(&filesys_lock);
      f->eax = create(file_name, *file_size);
      lock_release(&filesys_lock);
      break;
    }
      
    case SYS_REMOVE:
    {
      int *string_buffer_addr = (int*) get_stack_arg (f->esp, 4);
      validate_user_address (string_buffer_addr);

      void *file_name = *string_buffer_addr;
      validate_user_address (file_name);

      lock_acquire (&filesys_lock);
      f->eax = remove (file_name);
      lock_release (&filesys_lock);
      break;
    }
      
    case SYS_OPEN:
    {
      int *name_buffer_ptr = (int*) get_stack_arg (f->esp, 4);
      validate_user_address (name_buffer_ptr);

      void *file_name = *name_buffer_ptr;
      validate_user_address (file_name);

      // Anyone can open the file at the same time to read
      f->eax = open (file_name);
      break;
    }
    case SYS_FILESIZE:
    {
      int *fd = (int*) get_stack_arg (f->esp, 4);
      validate_user_address(fd);

      if (*fd < 2) exit (-1);
      f->eax = filesize (*fd);
      break;
    }
    case SYS_READ:
    {
      int *fd_addr = f->esp + 4;
      int *buff_addr = f->esp + 8;
      int *buff_size = f->esp + 12;
      
      validate_user_address (fd_addr);      
      validate_user_address (buff_addr);
      validate_user_address (buff_size);

      void *fault_addr = pg_round_down((void*) *buff_addr);
      void *starting_addr = pg_round_down((void*) *buff_addr);

      // Basic check, should not exceed user memory
      if(!is_user_vaddr(fault_addr))
        exit(-1);

      // Need to check if we're trying to access more of the stack
      if ((PHYS_BASE - fault_addr) <= THREAD_MAX_STACK_SIZE 
         && (uint32_t *)fault_addr >= (f->esp - 32))
      {
        // Since buff_size is arbitrary, we keep growing until we can 
        // fit the entire file.
        while(fault_addr <= pg_round_up(starting_addr + *buff_size))
        {
          // Check we don't already have an entry here!
          if(spt_get_entry(fault_addr))
            continue;
          
          // Growth the stack and make sure it succeeded
          spt_grow_stack_by_one(fault_addr);
          validate_user_address (fault_addr);
          

          // Keep incrementing until we hit minimum required pages
          fault_addr = pg_round_down(fault_addr + PGSIZE);
        }
      }

      f->eax = read (*fd_addr, *buff_addr, *buff_size);
      break;
    }

    case SYS_WRITE:
    {
      int *fd_addr = f->esp + 4;
      int *buff_addr = f->esp + 8;
      int *buff_size = f->esp + 12;

      validate_user_address (fd_addr);      
      validate_user_address (buff_addr);
      validate_user_address ((void*) *buff_addr);
      validate_user_address (buff_size);

      f->eax = write (*fd_addr, *buff_addr, *buff_size);
      break;
    }

    case SYS_SEEK:
    {
      int *fd_addr = f->esp + 4;
      int *position = f->esp + 8;

      validate_user_address (fd_addr);      
      validate_user_address (position);

      seek (*fd_addr, *position);
      break;
    }

    case SYS_TELL:
    {
      int *fd_addr = f->esp + 4;
      validate_user_address (fd_addr);

      f->eax = tell (*fd_addr);
      break;
    }

    case SYS_CLOSE:
    {
      int *fd = (int*) get_stack_arg (f->esp, 4);
      validate_user_address (fd);

      // No need to synchronize since fds are process dependent.
      close (*fd);
      break;
    }

    case SYS_CHDIR:
    {
      int *dir_addr = f->esp + 4;
      validate_user_address (dir_addr);
      validate_user_address ((void*) *dir_addr);

      f->eax = chdir (*dir_addr);
    }

    case SYS_MKDIR:
    {
      int *dir_addr = f->esp + 4;
      validate_user_address (dir_addr);
      validate_user_address ((void*) *dir_addr);

      f->eax = mkdir (*dir_addr);
    }

    case SYS_READDIR:
    {
      int *fd_addr = f->esp + 4;
      int *buff_addr = f->esp + 8;

      validate_user_address (fd_addr);
      validate_user_address (buff_addr);
      validate_user_address ((void*) *buff_addr);

      f->eax = readdir (*fd_addr, *buff_addr);
    }

    case SYS_ISDIR:
    {
      int *fd_addr = f->esp + 4;
      validate_user_address (fd_addr);

      f->eax = isDir (*fd_addr);
    }

    case SYS_INUMBER:
    {
      int *fd_addr = f->esp + 4;
      validate_user_address (fd_addr);

      f->eax = iNumber (*fd_addr);
    }

    // Unhandled case
    default:
      break;
  }
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

/* Checks that addr belongs to current user space.
   If it does, returns real physical address.
   Will exit with status -1 if addr is invalid. */
void
validate_user_address (void *addr)
{
  if(addr == NULL || is_kernel_vaddr (addr) || addr < PAGE_BOUNDARY)
  {
    exit (-1);
    return;
  }

  // Check if we have a SPTE for addr
  if(spt_get_entry(addr) != NULL)
    return;

  // Make sure address belongs to process' virtual address space,
  // and has been mapped already.
  if(!pagedir_get_page (thread_current()->pagedir, addr))
    exit(-1); 
}

/* Checks if given string is valid. Useful for stuff like exec
   where bad characters outside page boundary can be given. */
void
validate_string_value (char* str, int size)
{
  for(int i = 0; i < size; i++)
  {
    validate_user_address (str + i);
  }
}


void 
halt (void)
{
  shutdown_power_off ();
}

void
exit (int status)
{
  printf ("%s: exit(%d)\n", thread_current ()->process_name, status);
  thread_current ()->exit_status = status;
  thread_exit ();
}

int // Should be pid_t?? 
exec (const char *cmd_line)
{
  int tid;
  struct thread *child;
  struct thread *curr = thread_current ();

  validate_user_address (cmd_line);
  
  validate_string_value (cmd_line, strlen (cmd_line));

  tid = process_execute (cmd_line);

  child = find_thread_by_tid (tid);
  sema_down (&curr->child_exec_status);
  
  // Exec failed to load for some reason for child
  if(curr->child_exec_loaded == -1)
    return -1;
  
  
  return tid == TID_ERROR ? -1 : tid;
}

int 
wait (int pid)
{
  return process_wait (pid);
}

bool 
create (const char *file, unsigned initial_size)
{
  printf (">> [create] Creating file: %s\n", file);
  return filesys_create (file, initial_size);
}

bool 
remove (const char *file)
{
  // TODO: Allow directory deletion as well
  return filesys_remove (file);
}

int
open (const char *file_name)
{
  const char* empty = "";
  if (strcmp(file_name, empty) == 0)
    return -1;

  lock_acquire(&filesys_lock);

  // TODO: Allow directory opening as well

  struct file *file = filesys_open(file_name);
  lock_release(&filesys_lock);

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

  // TODO: Allow directory closing as well

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
  // If reading from STDIN
  if(fd == 0)
  {
    uint8_t *result = (uint8_t *) buffer;
    
    for(int i = 0; i < size; i++)
    {
      // Read from keyboard and store result in buffer
      result[i] = input_getc ();
    }

    return size;
  }
  else
  {
    struct file *file = thread_get_file_by_fd (fd);
    exit_if_null (file);

    lock_acquire(&filesys_lock);
    off_t bytes_read = file_read (file, buffer, size);
    lock_release(&filesys_lock);
    return bytes_read;
  }

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
    int new_size;

    struct file* file = thread_get_file_by_fd (fd);
    exit_if_null (file);

    // Make sure we're the only one 
    // writing to this file.
    lock_acquire (&filesys_lock);
    new_size = file_write (file, buffer, size);
    lock_release (&filesys_lock);
    return new_size;
  }
}

void 
seek (int fd, unsigned position)
{
  struct file* file = thread_get_file_by_fd (fd);
  exit_if_null (file);

  lock_acquire (&filesys_lock);
  file_seek (file, position);
  lock_release (&filesys_lock);
}

unsigned 
tell (int fd)
{
  struct file* file = thread_get_file_by_fd (fd);
  exit_if_null (file);

  return file_tell (file);
}

bool
chdir (char *dir)
{
  printf(">> [chdir] Change to: %s\n", dir);
  /* TODO */
  return true;
}

bool
mkdir (char *dir)
{
  bool successful = false;
  char *dir_cpy;

  printf(">> [mkdir] Goal '%s', size: %d\n", dir, strlen(dir));
  if(strlen(dir) == 0)
    goto done;
  else
  {
    // Create copy of directory name
    dir_cpy = malloc (sizeof(char) * strlen(dir));
    strlcpy (dir_cpy, dir, strlen(dir) + 1);

    char t[NAME_MAX + 1];
    struct dir *result = resolve_path (dir_cpy, thread_cwd (), t);
    printf("got result %p\n", result);
    if(result)
    {
      printf(">> [mkdir] Creating directory in: %p\n", result);
      printf(">> [mkdir] Directory to create: %s\n", t);

      // Directories have zero size
      successful = filesys_create_at_dir (t, 0, result, true);
    }

    free (dir_cpy);

    goto done;
  }
  /* TODO */
  done:
    printf(">> [mkdir] Created directory? %d\n", successful);
    return successful;
}

bool
readdir (int fd, char *name)
{
  struct file* file = thread_get_file_by_fd (fd);
  exit_if_null (file);

  /* TODO */
  return true;
}

bool
isDir (int fd)
{
  struct file* file = thread_get_file_by_fd (fd);
  exit_if_null (file);

  return inode_is_dir (file_get_inode (file)->sector);
}

int
iNumber (int fd)
{
  struct file* file = thread_get_file_by_fd (fd);
  exit_if_null (file);

  return inode_get_inumber (file_get_inode (file)->sector);
}
