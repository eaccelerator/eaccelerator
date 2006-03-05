/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 - 2006 eAccelerator                               |
   | http://eaccelerator.net                                              |
   +----------------------------------------------------------------------+
   | This program is free software; you can redistribute it and/or        |
   | modify it under the terms of the GNU General Public License          |
   | as published by the Free Software Foundation; either version 2       |
   | of the License, or (at your option) any later version.               |
   |                                                                      |
   | This program is distributed in the hope that it will be useful,      |
   | but WITHOUT ANY WARRANTY; without even the implied warranty of       |
   | MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        |
   | GNU General Public License for more details.                         |
   |                                                                      |
   | You should have received a copy of the GNU General Public License    |
   | along with this program; if not, write to the Free Software          |
   | Foundation, Inc., 59 Temple Place - Suite 330, Boston,               |
   | MA  02111-1307, USA.                                                 |
   |                                                                      |
   | A copy is availble at http://www.gnu.org/copyleft/gpl.txt            |
   +----------------------------------------------------------------------+
   $Id$
*/

/* libmm replacement */

#if !defined(MM_TEST_SHM) && !defined(MM_TEST_SEM)
# ifdef HAVE_CONFIG_H
#  include "config.h"
# endif
# include "php.h"
#endif

#ifdef WIN32
#  if 1
#    define MM_SHM_WIN32
#    define MM_SEM_WIN32
#  else
#    define MM_SHM_MALLOC
#    define MM_SEM_NONE
#  endif
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

#ifdef WIN32
#  include <limits.h>
#else
#  ifdef HAVE_UNISTD_H
#    include <unistd.h>
#  endif
#  ifdef HAVE_SYS_PARAM_H
#    include <sys/param.h>
#  endif
#  ifdef HAVE_LIMITS_H
#    include <limits.h>
#  endif
#endif

#if defined(MM_SHM_MMAP_FILE) || defined(MM_SHM_MMAP_ZERO) || defined(MM_SHM_MMAP_ANON) || defined(MM_SHM_MMAP_POSIX) || defined(HAVE_MPROTECT)
#  include <sys/mman.h>
#endif
#if defined(MM_SHM_IPC) || defined(MM_SEM_IPC)
#  include <sys/ipc.h>
#endif
#ifdef MM_SHM_IPC
#  include <sys/shm.h>
#endif
#if defined(MM_SHM_WIN32) || defined(MM_SEM_WIN32)
#  include <windows.h>
#endif
#ifdef MM_SHM_MALLOC
#  include <malloc.h>
#endif
#ifdef MM_SEM_IPC
#  include <sys/sem.h>
#endif
#ifdef MM_SEM_FLOCK
#  include <sys/file.h>
#endif
#ifdef MM_SEM_POSIX
#  include <semaphore.h>
#endif
#ifdef MM_SEM_PTHREAD
#  include <pthread.h>
#endif

struct mm_mutex;

typedef struct mm_free_bucket {
  size_t                 size;
  struct mm_free_bucket* next;
} mm_free_bucket;

typedef struct mm_core {
  size_t           size;
  void*            start;
  size_t           available;
  void*            attach_addr;
  struct mm_mutex* lock;
  mm_free_bucket*  free_list;
} mm_core;

typedef union mm_mem_head {
  size_t size;
  double a1;
  int (*a2)(int);
  void *a3;
} mm_mem_head;

#define MM_SIZE(sz)       (sizeof(mm_mem_head)+(sz))
#define PTR_TO_HEAD(p)    (((mm_mem_head *)(p)) - 1)
#define HEAD_TO_PTR(p)    ((void *)(((mm_mem_head *)(p)) + 1))

#define MM mm_core
#define MM_PRIVATE
#include "mm.h"

typedef union mm_word {
  size_t size;
  void*  ptr;
  double d;
  int (*func)(int);
} mm_word;

#if (defined (__GNUC__) && __GNUC__ >= 2)
#define MM_PLATFORM_ALIGNMENT (__alignof__ (mm_word))
#else
#define MM_PLATFORM_ALIGNMENT (sizeof(mm_word))
#endif

#define MM_ALIGN(n) (void*)((((size_t)(n)-1) & ~(MM_PLATFORM_ALIGNMENT-1)) + MM_PLATFORM_ALIGNMENT)
/*#define MM_ALIGN(n) (void*)((1+(((size_t)(n)-1) / sizeof(mm_word))) * sizeof(mm_word))*/

#ifndef MAXPATHLEN
#  ifdef PATH_MAX
#    define MAXPATHLEN PATH_MAX
#  elif defined(_POSIX_PATH_MAX)
#    define MAXPATHLEN _POSIX_PATH_MAX
#  else
#    define MAXPATHLEN 256
#  endif
#endif

#undef MM_SHM_CAN_ATTACH

static int strxcat(char* dst, const char* src, int size) {
  int dst_len = strlen(dst);
  int src_len = strlen(src);
  if (dst_len + src_len < size) {
    memcpy(dst+dst_len, src, src_len+1);
    return 1;
  } else {
    memcpy(dst+dst_len, src, (size-1)-dst_len);
    dst[size-1] = '\000';
    return 0;
  }
}

#if defined(MM_SEM_SPINLOCK)

#if defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#  include "x86_spinlocks.h"
#else
#  error "spinlocks are not implemented for your system"
#endif

/*********************************/
/* Semaphores                    */
/********************************/

/* ######################################################################### */

#define MM_SEM_TYPE "spinlock"
#define MM_SEM_CAN_ATTACH

typedef struct mm_mutex {
  spinlock_t spinlock;
} mm_mutex;

static int mm_attach_lock(const char* key, mm_mutex* lock) {
  return 1;
}

static int mm_init_lock(const char* key, mm_mutex* lock) {
  spinlock_init(&lock->spinlock);
  return 1;
}

static int mm_do_lock(mm_mutex* lock, int kind) {
  spinlock_lock(&lock->spinlock);
  return 1;
}

static int mm_do_unlock(mm_mutex* lock) {
  spinlock_unlock(&lock->spinlock);
  return 1;
}

static void mm_destroy_lock(mm_mutex* lock) {
}

/* ######################################################################### */

#elif defined(MM_SEM_PTHREAD)

#define MM_SEM_TYPE "pthread"

typedef struct mm_mutex {
  pthread_mutex_t mutex;
} mm_mutex;

static int mm_init_lock(const char* key, mm_mutex* lock) {
  pthread_mutexattr_t mattr;

  if (pthread_mutexattr_init(&mattr) != 0) {
    return 0;
  }
  if (pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED) != 0) {
    return 0;
  }
  if (pthread_mutex_init(&lock->mutex, &mattr) != 0) {
    return 0;
  }
  pthread_mutexattr_destroy(&mattr);
  return 1;
}

static int mm_do_lock(mm_mutex* lock, int kind) {
  if (pthread_mutex_lock(&lock->mutex) != 0) {
    return 0;
  }
  return 1;
}

static int mm_do_unlock(mm_mutex* lock) {
  if (pthread_mutex_unlock(&lock->mutex) != 0) {
    return 0;
  }
  return 1;
}

static void mm_destroy_lock(mm_mutex* lock) {
  pthread_mutex_destroy(&lock->mutex);
}

/* ######################################################################### */

#elif defined(MM_SEM_POSIX)

/* not tested */

#define MM_SEM_TYPE "posix"

typedef struct mm_mutex {
  sem_t* sem;
} mm_mutex;

static int mm_init_lock(const char* key, mm_mutex* lock) {
#ifdef SEM_NAME_LEN
  char s[SEM_NAME_LEN];

  strncpy(s,key,SEM_NAME_LEN-1);
  strxcat(s,".sem.XXXXXX",SEM_NAME_LEN);
#else
  char s[MAXPATHLEN];

  strncpy(s,key,MAXPATHLEN-1);
  strxcat(s,".sem.XXXXXX",MAXPATHLEN);
#endif
  if (mktemp(s) == NULL) return 0;
  if ((lock->sem = sem_open(s, O_CREAT, S_IRUSR | S_IWUSR, 1)) == (sem_t*)SEM_FAILED) {
    return 0;
  }
  sem_unlink(s);
  return 1;
}

static int mm_do_lock(mm_mutex* lock, int kind) {
  return (sem_wait(lock->sem) == 0);
}

static int mm_do_unlock(mm_mutex* lock) {
  return (sem_post(lock->sem) == 0);
}

static void mm_destroy_lock(mm_mutex* lock) {
  sem_close(lock->sem);
}

/* ######################################################################### */

#elif defined(MM_SEM_IPC)

#define MM_SEM_TYPE "sysvipc"

#ifndef HAVE_UNION_SEMUN
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};
#endif

typedef struct mm_mutex {
  int semid;
} mm_mutex;

static int mm_init_lock(const char* key, mm_mutex* lock) {
  int rc;
  union semun arg;
  struct semid_ds buf;

  if ((lock->semid = semget(IPC_PRIVATE, 1, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR)) < 0) {
    return 0;
  }

  arg.buf = &buf;
  do {
    rc = semctl(lock->semid, 0, IPC_STAT, arg);
  } while (rc < 0 && errno == EINTR);

  buf.sem_perm.uid = EA_USERID;

  do {
    rc = semctl(lock->semid, 0, IPC_SET, arg);
  } while (rc < 0 && errno == EINTR);
  
  arg.val = 1;
  do {
    rc = semctl(lock->semid, 0, SETVAL, arg);
  } while (rc < 0 && errno == EINTR);
  if (rc < 0) {
    do {
      semctl(lock->semid, 0, IPC_RMID, 0);
    } while (rc < 0 && errno == EINTR);
    return 0;
  }
  return 1;
}

static int mm_do_lock(mm_mutex* lock, int kind) {
  int rc;
  struct sembuf op;

  op.sem_num = 0;
  op.sem_op  = -1;
  op.sem_flg = SEM_UNDO;
  do {
    rc = semop(lock->semid, &op, 1);
  } while (rc < 0 && errno == EINTR);
  return (rc == 0);
}

static int mm_do_unlock(mm_mutex* lock) {
  int rc;
  struct sembuf op;

  op.sem_num = 0;
  op.sem_op  = 1;
  op.sem_flg = SEM_UNDO;
  do {
    rc = semop(lock->semid, &op, 1);
  } while (rc < 0 && errno == EINTR);
  return (rc == 0);
}

static void mm_destroy_lock(mm_mutex* lock) {
  int rc;
  do {
    rc = semctl(lock->semid, 0, IPC_RMID, 0);
  } while (rc < 0 && errno == EINTR);
}

#elif defined(MM_SEM_FCNTL)

#define MM_SEM_TYPE "fcntl"

typedef struct mm_mutex {
  int fd;
} mm_mutex;

static int mm_init_lock(const char* key, mm_mutex* lock) {
  char s[MAXPATHLEN];

  strncpy(s,key,MAXPATHLEN-1);
  strxcat(s,".sem.XXXXXX",MAXPATHLEN);
  lock->fd =mkstemp(s);
  if (lock->fd != -1) {
    unlink(s);
  }
  return (lock->fd != -1);
}

static int mm_do_lock(mm_mutex* lock, int kind) {
  int rc;
  struct flock l;
  l.l_whence   = SEEK_SET;
  l.l_start    = 0;
  l.l_len      = 0;
  l.l_pid      = 0;
  if (kind == MM_LOCK_RD) {
    l.l_type     = F_RDLCK;
  } else {
    l.l_type     = F_WRLCK;
  }
  do {
    rc = fcntl(lock->fd, F_SETLKW, &l);
  } while (rc < 0 && errno == EINTR);
  return (rc == 0);
}

static int mm_do_unlock(mm_mutex* lock) {
  int rc;
  struct flock l;
  l.l_whence   = SEEK_SET;
  l.l_start    = 0;
  l.l_len      = 0;
  l.l_pid      = 0;
  l.l_type     = F_UNLCK;
  do {
    rc = fcntl(lock->fd, F_SETLKW, &l);
  } while (rc < 0 && errno == EINTR);
  return (rc == 0);
}

static void mm_destroy_lock(mm_mutex* lock) {
  close(lock->fd);
}

/* ######################################################################### */

#elif defined(MM_SEM_FLOCK)

/* this method is not thread safe */

#define MM_SEM_TYPE "flock"

static int   mm_flock_fd  = -1;
static pid_t mm_flock_pid = -1;

typedef struct mm_mutex {
  char filename[MAXPATHLEN];
} mm_mutex;

static int mm_init_lock(const char* key, mm_mutex* lock) {
  strncpy(lock->filename,key,MAXPATHLEN-1);
  strxcat(lock->filename,".sem.XXXXXX",MAXPATHLEN);
  mm_flock_fd =mkstemp(lock->filename);
  if (mm_flock_fd != -1) {
#if defined(F_SETFD) && defined(FD_CLOEXEC)
    fcntl(mm_flock_fd, F_SETFD, FD_CLOEXEC);
#endif
    mm_flock_pid = getpid();
    return 1;
  }
  return 0;
}

static int mm_do_lock(mm_mutex* lock, int kind) {
  pid_t pid = getpid();
  int rc;
  if (kind == MM_LOCK_RD) {
    kind = LOCK_SH;
  } else {
    kind = LOCK_EX;
  }

  if (mm_flock_fd == -1 || mm_flock_pid != pid) {
    mm_flock_fd = open(lock->filename, O_RDWR, S_IRUSR | S_IWUSR);
    if (mm_flock_fd == -1) {
      return 0;
    }
#if defined(F_SETFD) && defined(FD_CLOEXEC)
    fcntl(mm_flock_fd, F_SETFD, FD_CLOEXEC);
#endif
    mm_flock_pid = pid;
  }
  do {
    rc = flock(mm_flock_fd, kind);
  } while (rc < 0 && errno == EINTR);
  return (rc == 0);
}

static int mm_do_unlock(mm_mutex* lock) {
  int rc;
  if (mm_flock_fd == -1) {
    mm_flock_fd = open(lock->filename, O_RDWR, S_IRUSR | S_IWUSR);
    if (mm_flock_fd == -1) {
      return 0;
    }
  }
  do {
    rc = flock(mm_flock_fd, LOCK_UN);
  } while (rc < 0 && errno == EINTR);
  return (rc == 0);
}

static void mm_destroy_lock(mm_mutex* lock) {
  close(mm_flock_fd);
  unlink(lock->filename);
}

/* ######################################################################### */

#elif defined(MM_SEM_BEOS)

#define MM_SEM_TYPE "beos"
#error "Semophore type (MM_SEM_BEOS) is not implemented"

#elif defined(MM_SEM_OS2)

#define MM_SEM_TYPE "os2"
#error "Semophore type (MM_SEM_OS2) is not implemented"

#elif defined(MM_SEM_WIN32)

#define MM_SEM_TYPE "win32"
#define MM_SEM_CAN_ATTACH

typedef struct mm_mutex {
  HANDLE hMutex;
} mm_mutex;

static mm_mutex g_lock;

static int mm_attach_lock(const char* key, mm_mutex* lock) {
  char* ch;
  char name[256];
  HANDLE hMutex;

  strncpy(name, key, 255);
  strxcat(name, ".sem", 255);
  for (ch = name; *ch; ++ch) {
    if (*ch == ':' || *ch == '/' || *ch == '\\') {
      *ch = '_';
    }
  }
  g_lock.hMutex = hMutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, name);
  if (!g_lock.hMutex) {
    return 0;
  }
  return 1;
}

static int mm_init_lock(const char* key, mm_mutex* lock) {
  char* ch;
  char name[256];
  strncpy(name, key, 255);
  strxcat(name, ".sem", 255);
  for (ch = name; *ch; ++ch) {
    if (*ch == ':' || *ch == '/' || *ch == '\\') {
      *ch = '_';
    }
  }
  g_lock.hMutex = CreateMutex(NULL, FALSE, name);
  if (!g_lock.hMutex) {
    return 0;
  }
  return 1;
}

static void mm_destroy_lock(mm_mutex* lock) {
  CloseHandle(g_lock.hMutex);
}

static int mm_do_lock(mm_mutex* lock, int kind) {
  DWORD rv;

  rv = WaitForSingleObject(g_lock.hMutex, INFINITE);

  if (rv == WAIT_OBJECT_0 || rv == WAIT_ABANDONED) {
    return 1;
  }
  return 0;
}

static int mm_do_unlock(mm_mutex* lock) {
  if (ReleaseMutex(g_lock.hMutex) == 0) {
    return 0;
  }
  return 1;
}

/* ######################################################################### */

#elif defined(MM_SEM_NONE)

#define MM_SEM_TYPE "none"
#define MM_SEM_CAN_ATTACH


typedef struct mm_mutex {
  int semid;
} mm_mutex;

static int mm_attach_lock(const char* key, mm_mutex* lock) {
  return 1;
}

static int mm_init_lock(const char* key, mm_mutex* lock) {
  return 1;
}

static void mm_destroy_lock(mm_mutex* lock) {
}

static int mm_do_lock(mm_mutex* lock, int kind) {
  return 1;
}

static int mm_do_unlock(mm_mutex* lock) {
  return 1;
}

#else
#  error "Semaohore type is not selected. Define one of the following: MM_SEM_SPINLOCK, MM_SEM_PTHREAD, MM_SEM_POSIX, MM_SEM_IPC, MM_SEM_FCNTL, MM_SEM_FLOCK, MM_SEM_BEOS, MM_SEM_OS2, MM_SEM_WIN32"
#endif

int mm_lock(MM* mm, int kind) {
  if (mm_do_lock(mm->lock, kind)) {
    return 1;
  } else {
#if !defined(MM_TEST_SEM) && !defined(MM_TEST_SHM)
    ea_debug_error("eAccelerator: Could not lock!\n");
#endif
    return 0;
  }
}

int mm_unlock(MM* mm) {
  if (mm_do_unlock(mm->lock)) {
    return 1;
  } else {
#if !defined(MM_TEST_SEM) && !defined(MM_TEST_SHM)
    ea_debug_error("eAccelerator: Could not release lock!\n");
#endif
    return 0;
  }
}

/* Shared Memory Implementations */

/* ######################################################################### */

#if defined(MM_SHM_IPC)

#define MM_SHM_TYPE "sysvipc"

#ifndef SHM_R
# define SHM_R 0444 /* read permission */
#endif
#ifndef SHM_W
# define SHM_W 0222 /* write permission */
#endif

static MM* mm_create_shm(const char* key, size_t size) {
  int fd;
  void** segment = NULL;
  if ((fd = shmget(IPC_PRIVATE, size, (IPC_CREAT | SHM_R | SHM_W))) != -1) {
    MM* p;
    if ((p = (MM*)shmat(fd, NULL, 0)) != ((void *)-1)) {
      struct shmid_ds shmbuf;
      if (shmctl(fd, IPC_STAT, &shmbuf) == 0) {
        shmbuf.shm_perm.uid = getuid();
        shmbuf.shm_perm.gid = getgid();
        if (shmctl(fd, IPC_SET, &shmbuf) == 0) {
          shmctl(fd, IPC_RMID, NULL);
          p->size = size;
          segment = (void**)((char*)p+sizeof(MM));
          *segment = (void*)-1;
          segment++;
          p->start = segment;
          return p;
        }
      }
      shmdt(p);
    }
    shmctl(fd, IPC_RMID, NULL);
  } else {
    /* can't get one shared memory segment, trying to get several */
    size_t seg_size = 1024*1024;
    size_t orig_size = size;
    void*  p;
    MM*    root = NULL;
    char*  prev = NULL;

    while (seg_size <= size/2) {
      seg_size *= 2;
    }
    while ((fd = shmget(IPC_PRIVATE, seg_size, (IPC_CREAT | SHM_R | SHM_W))) == -1) {
      if (seg_size <= 1024*1024) {
        return (MM*)-1;
      }
      seg_size /= 2;
    }
    while (size > 0) {
      if (fd != -1 ||
          (fd = shmget(IPC_PRIVATE, (size > seg_size)?seg_size:size, (IPC_CREAT | SHM_R | SHM_W))) != -1) {
        if ((p = (void *)shmat(fd, prev?(prev+seg_size):NULL, 0)) != ((void *)-1) &&
            (prev == NULL || prev + seg_size == p)) {
          struct shmid_ds shmbuf;
/*???
          memset(p, 0, (size > seg_size)?seg_size:size);
*/
          if (shmctl(fd, IPC_STAT, &shmbuf) == 0) {
            shmbuf.shm_perm.uid = getuid();
            shmbuf.shm_perm.gid = getgid();
            if (shmctl(fd, IPC_SET, &shmbuf) == 0) {
              shmctl(fd, IPC_RMID, NULL);
              if (root == NULL) {
                root = (MM*)p;
                segment = (void**)((char*)p+sizeof(MM));
              } else {
                *segment = p;
                segment++;
              }
              prev = (char*)p;
              fd = -1;
              if (size > seg_size) {
                size -= seg_size;
              } else {
                size = 0;
              }
              continue;
            }
          }
          shmdt(p);
        }
        shmctl(fd, IPC_RMID, NULL);
      }
      if (root != NULL) {
        while (segment > (void**)((char*)root+sizeof(MM))) {
          segment--;
          shmdt(*segment);
        }
      }
      shmdt(root);
      return (MM*)-1;
    }
    *segment = (void*)-1;
    segment++;
    root->size  = orig_size;
    root->start = (void*)segment;
    return root;
  }
  return (MM*)-1;
}

static void mm_destroy_shm(MM* mm) {
  void** segment = (void**)((char*)mm+sizeof(MM));
  while (*segment != (void*)-1) {
    shmdt(*segment);
    ++segment;
  }
  shmdt(mm);
}

/* ######################################################################### */

#elif defined(MM_SHM_MMAP_ANON)

#define MM_SHM_TYPE "mmap_anon"

#ifndef MAP_ANON
#  ifdef MAP_ANONYMOUS
#    define MAP_ANON MAP_ANONYMOUS
#  endif
#endif

static MM* mm_create_shm(const char* key, size_t size) {
  MM* p;
  p = (MM*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
  if (p != (MM*)-1) {
    p->size = size;
    p->start = (char*)p+sizeof(MM);
  }
  return p;
}

static void mm_destroy_shm(MM* mm) {
  munmap(mm,mm->size);
}

/* ######################################################################### */

#elif defined(MM_SHM_MMAP_ZERO)

#define MM_SHM_TYPE "mmap_zero"

static MM* mm_create_shm(const char* key, size_t size) {
  MM* p;
  int fd = open("/dev/zero", O_RDWR, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    return (MM*)-1;
  }
  p = (MM*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (p != (MM*)-1) {
    p->size = size;
    p->start = (char*)p+sizeof(MM);
  }
  return p;
}

static void mm_destroy_shm(MM* mm) {
  munmap(mm,mm->size);
}

/* ######################################################################### */

#elif defined(MM_SHM_MMAP_POSIX)

#define MM_SHM_TYPE "mmap_posix"

/* Not Tested */

static MM* mm_create_shm(const char* key, size_t size) {
  MM* p;
  int fd;
  char s[MAXPATHLEN];

  strncpy(s,key,MAXPATHLEN-1);
  strxcat(s,".shm.XXXXXX",MAXPATHLEN);
  if (mktemp(s) == NULL) {
    return (MM*)-1;
  }
  if ((fd = shm_open(s, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR)) == -1) {
    return (MM*)-1;
  }
  if (ftruncate(fd, size) < 0) {
    close(fd);
    shm_unlink(s);
    return (MM*)-1;
  }
  p = (MM*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  shm_unlink(s);
  close(fd);
  if (p != (MM*)-1) {
    p->size = size;
    p->start = (char*)p+sizeof(MM);
  }
  return p;
}

static void mm_destroy_shm(MM* mm) {
  munmap(mm,mm->size);
}

/* ######################################################################### */

#elif defined(MM_SHM_MMAP_FILE)

#define MM_SHM_TYPE "mmap_file"

static MM* mm_create_shm(const char* key, size_t size) {
  MM* p;
  int fd;
  char s[MAXPATHLEN];

  strncpy(s,key,MAXPATHLEN-1);
  strxcat(s,".shm.XXXXXX",MAXPATHLEN);
  fd = mkstemp(s);
  if (fd < 0) {
    return (MM*)-1;
  }
  if (ftruncate(fd, size) < 0) {
    return (MM*)-1;
  }
  p = (MM*)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  unlink(s);
  if (p != (MM*)-1) {
    p->size = size;
    p->start = (char*)p+sizeof(MM);
  }
  return p;
}

static void mm_destroy_shm(MM* mm) {
  munmap(mm,mm->size);
}

/* ######################################################################### */

#elif defined(MM_SHM_BEOS)

#define MM_SHM_TYPE "beos"
#error "Shared memeory type (MM_SHM_BEOS) is not implemented"

/* ######################################################################### */

#elif defined(MM_SHM_OS2)

#define MM_SHM_TYPE "os2"
#error "Shared memeory type (MM_SHM_OS2) is not implemented"

/* ######################################################################### */

#elif defined(MM_SHM_WIN32)

#define MM_SHM_TYPE "win32"
#define MM_SHM_CAN_ATTACH

static MM* mm_attach_shm(const char* key, size_t size) {
  HANDLE  shm_handle;
  MM*     mm;
  MM*     addr;
  MM**    addr_ptr;
  char    s[MAXPATHLEN];
  char*   ch;


  strcpy(s,key);
  for (ch = s; *ch; ++ch) {
    if (*ch == ':' || *ch == '/' || *ch == '\\') {
      *ch = '_';
    }
  }

  shm_handle = OpenFileMapping(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, s);
  if (shm_handle) {
    mm = (MM*)MapViewOfFile(shm_handle, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (mm == NULL) {
      return (MM*)-1;
    }
/*
    if (mm->size != size) {
      UnmapViewOfFile(mm);
      CloseHandle(shm_handle);
      return (MM*)-1;
    }
*/
    addr_ptr = (MM**)(((char*)mm)+sizeof(MM));
    addr = *addr_ptr;
    if (addr != mm) {
      UnmapViewOfFile(mm);
      mm = (MM*)MapViewOfFileEx(shm_handle, FILE_MAP_ALL_ACCESS, 0, 0, 0, addr);
      if (mm == NULL) {
        return (MM*)-1;
      }
    }
/*  CloseHandle(shm_handle);*/
    return mm;
  }
  return (MM*)-1;
}

static MM* mm_create_shm(const char* key, size_t size) {
  HANDLE  shm_handle;
  MM*     mm;
  MM**    addr_ptr;
  char    s[MAXPATHLEN];
  char*   ch;


  strcpy(s,key);
  for (ch = s; *ch; ++ch) {
    if (*ch == ':' || *ch == '/' || *ch == '\\') {
      *ch = '_';
    }
  }

  shm_handle = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, size, s);
  if (!shm_handle) {
    return (MM*)-1;
  }
  mm = (MM*)MapViewOfFileEx(shm_handle, FILE_MAP_ALL_ACCESS, 0, 0, 0, NULL);
  if (mm == NULL) {
    return (MM*)-1;
  }
  addr_ptr = (MM**)(((char*)mm)+sizeof(MM));
  *addr_ptr = mm;
  mm->size = size;
  mm->start = ((char*)mm)+sizeof(MM)+sizeof(void*);

/*  CloseHandle(shm_handle);*/
  return mm;
}

static void mm_destroy_shm(MM* mm) {
  UnmapViewOfFile(mm);
}

/* ######################################################################### */

#elif defined(MM_SHM_MALLOC)

#define MM_SHM_TYPE "malloc"

static void* mm_create_shm(const char* key, size_t size) {
  MM* p = (MM*)malloc(sizeof(MM));
  if (p == NULL) {
    return (MM*)-1;
  }
  p->size  = size;
  p->start = NULL;
  return p;
}

static void mm_destroy_shm(MM* mm) {
  free(mm);
}

/* ######################################################################### */

#else
#define MM_SHM_TYPE "none"
#  error "Shared memeory type is not selected. Define one of the following: MM_SHM_IPC, MM_SHM_MMAP_ANON, MM_SHM_MMAP_ZERO, MM_SHM_MMAP_FILE, MM_SHM_MALLOC, MM_SHM_BEOS, MM_SHM_OS2, MM_SHM_WIN32"
#endif

#ifdef MM_SHM_MALLOC
static void mm_init(MM* mm) {
  mm->available = mm->size - sizeof(MM);
  mm->lock = malloc(sizeof(mm_mutex));
}

void* mm_malloc_nolock(MM* mm, size_t size) {
  if (size > 0) {
    mm_mem_head *p = NULL;
    if (mm->available >= MM_SIZE(size)) {
      p = malloc(MM_SIZE(size));
      if (p != NULL) {
        p->size = MM_SIZE(size);
        mm->available -= MM_SIZE(size);
      }
    }
    if (p != NULL) {
      return HEAD_TO_PTR(p);
    }
  }
  return NULL;
}

void mm_free_nolock(MM* mm, void* x) {
  if (x != NULL) {
    mm_mem_head *p;
    p = PTR_TO_HEAD(x);
    mm->available += p->size;
    free(p);
  }
}

size_t mm_maxsize(MM* mm) {
  size_t ret;
  if (!mm_lock(mm, MM_LOCK_RD)) {
    return 0;
  }
  ret = mm->available - MM_SIZE(0);
  mm_unlock(mm);
  return ret;
}

#else
static void mm_init(MM* mm) {
  mm->start = MM_ALIGN(mm->start);
  mm->attach_addr = (void*)mm;
  mm->lock = mm->start;
  mm->start = MM_ALIGN((void*)(((char*)(mm->start)) + sizeof(mm_mutex)));
  mm->available = mm->size - (((char*)(mm->start))-(char*)mm);
  mm->free_list = (mm_free_bucket*)mm->start;
  mm->free_list->size = mm->available;
  mm->free_list->next = NULL;
}

void* mm_malloc_nolock(MM* mm, size_t size) {
  if (size > 0) {
    mm_mem_head* x = NULL;
    size_t realsize = (size_t)MM_ALIGN(MM_SIZE(size));
    if (realsize <= mm->available) {
      /* Search for free bucket */
      mm_free_bucket* p = mm->free_list;
      mm_free_bucket* q = NULL;
      mm_free_bucket* best = NULL;
      mm_free_bucket* best_prev = NULL;
      while (p != NULL) {
        if (p->size == realsize) {
          /* Found free bucket with the same size */
          if (q == NULL) {
            mm->free_list = p->next;
            x = (mm_mem_head*)p;
          } else {
            q->next = p->next;
            x = (mm_mem_head*)p;
          }
          break;
        } else if (p->size > realsize && (best == NULL || best->size > p->size)) {
          /* Found best bucket (smallest bucket with the grater size) */
          best = p;
          best_prev = q;
        }
        q = p;
        p = p->next;
      }
      if (x == NULL && best != NULL) {
        if (best->size-realsize < sizeof(mm_free_bucket)) {
          realsize = best->size;
          x = (mm_mem_head*)best;
          if (best_prev == NULL) {
            mm->free_list = best->next;
          } else {
            best_prev->next = best->next;
          }
        } else {
          if (best_prev == NULL) {
            mm->free_list = (mm_free_bucket*)((char*)best + realsize);
            mm->free_list->size = best->size-realsize;
            mm->free_list->next = best->next;
          } else {
            best_prev->next = (mm_free_bucket*)((char*)best + realsize);
            best_prev->next->size = best->size-realsize;
            best_prev->next->next = best->next;
          }
          best->size = realsize;
          x = (mm_mem_head*)best;
        }
      }
      if (x != NULL) {
        mm->available -= realsize;
      }
    }
    if (x != NULL) {
      return HEAD_TO_PTR(x);
    }
  }
  return NULL;
}

void mm_free_nolock(MM* mm, void* x) {
  if (x != NULL) {
    if (x >= mm->start && x < (void*)((char*)mm + mm->size)) {
      mm_mem_head *p = PTR_TO_HEAD(x);
      size_t size = p->size;
      if ((char*)p+size <= (char*)mm + mm->size) {
        mm_free_bucket* b = (mm_free_bucket*)p;
        b->next = NULL;
        if (mm->free_list == NULL) {
          mm->free_list = b;
        } else {
          mm_free_bucket* q = mm->free_list;
          mm_free_bucket* prev = NULL;
          mm_free_bucket* next = NULL;
          while (q != NULL) {
            if (b < q) {
              next = q;
              break;
            }
            prev = q;
            q = q->next;
          }
          if (prev != NULL && (char*)prev+prev->size == (char*)b) {
            if ((char*)next == (char*)b+size) {
              /* merging with prev and next */
              prev->size += size + next->size;
              prev->next = next->next;
            } else {
              /* merging with prev */
              prev->size += size;
            }
          } else {
            if ((char*)next == (char*)b+size) {
              /* merging with next */
              b->size += next->size;
              b->next = next->next;
            } else {
              /* don't merge */
              b->next = next;
            }
            if (prev != NULL) {
              prev->next = b;
            } else {
              mm->free_list = b;
            }
          }
        }
        mm->available += size;
      }
    }
  }
}

size_t mm_maxsize(MM* mm) {
  size_t ret = MM_SIZE(0);
  mm_free_bucket* p;
  if (!mm_lock(mm, MM_LOCK_RD)) {
    return 0;
  }
  p = mm->free_list;
  while (p != NULL) {
    if (p->size > ret) {
      ret = p->size;
    }
    p = p->next;
  }
  mm_unlock(mm);
  return ret - MM_SIZE(0);
}
#endif

void* mm_malloc_lock(MM* mm, size_t size) {
  void *ret;
  if (!mm_lock(mm, MM_LOCK_RW)) {
    return NULL;
  }
  ret = mm_malloc_nolock(mm,size);
  mm_unlock(mm);
  return ret;
}

void mm_free_lock(MM* mm, void* x) {
  mm_lock(mm, MM_LOCK_RW);
  mm_free_nolock(mm,x);
  mm_unlock(mm);
}

void mm_set_attach(MM* mm, void* attach_addr) {
  mm->attach_addr = attach_addr;
}

void* mm_attach(size_t size, const char* key) {
#ifdef MM_SHM_CAN_ATTACH
  MM* mm = mm_attach_shm(key, size);
  if (mm == (MM*)-1) {
    return NULL;
  }
#ifdef MM_SEM_CAN_ATTACH
  if (!mm_attach_lock(key, mm->lock)) {
    mm_destroy_shm(mm);
    return NULL;
  }
#endif
  return mm->attach_addr;
#else
  return NULL;
#endif
}

MM* mm_create(size_t size, const char* key) {
  MM* p;
  if (size == 0) {
    size = 32 * 1024 * 1024;
  }
  p = mm_create_shm(key, size);
  if (p == (MM*)-1) {
    return NULL;
  }
  mm_init(p);
  if (p->lock == NULL) {
    mm_destroy_shm(p);
    return NULL;
  }
  if (!mm_init_lock(key, p->lock)) {
    mm_destroy_shm(p);
    return NULL;
  }
  return p;
}

void mm_destroy(MM* mm) {
  if (mm != NULL) {
    mm_destroy_lock(mm->lock);
    mm_destroy_shm(mm);
  }
}

size_t mm_size(MM* mm) {
  if (mm != NULL) {
    return mm->size;
  }
  return 0;
}

size_t mm_sizeof(MM* mm, void* x) {
  mm_mem_head *p;
  size_t ret;
  if (mm == NULL || x == NULL || !mm_lock(mm, MM_LOCK_RD)) {
    return 0;
  }
  p = PTR_TO_HEAD(x);
  ret = p->size;
  mm_unlock(mm);
  return ret;
}

size_t mm_available(MM* mm) {
  size_t available;
  if (mm != NULL && mm_lock(mm, MM_LOCK_RD)) {
    available = mm->available;
    mm_unlock(mm);
    return available;
  }
  return 0;
}

const char* mm_shm_type() {
  return MM_SHM_TYPE;
}

const char* mm_sem_type() {
  return MM_SEM_TYPE;
}

int mm_protect(MM* mm, int mode) {
#ifdef HAVE_MPROTECT
  int pmode = 0;
  if (mode & MM_PROT_NONE) {
    pmode |= PROT_NONE;
  }
  if (mode & MM_PROT_READ) {
    pmode |= PROT_READ;
  }
  if (mode & MM_PROT_WRITE) {
    pmode |= PROT_WRITE;
  }
  if (mode & MM_PROT_EXEC) {
    pmode |= PROT_EXEC;
  }
  return (mprotect(mm, mm->size, pmode) == 0);
#endif
  return 0;
}

#ifdef MM_TEST_SHM
int main() {
  char key[] = "/tmp/mm";
  size_t size = 32*1024*1024;
  MM *mm = mm_create(size, key);
  if (mm == NULL) {
    return 1;
  }
  mm_destroy(mm);
  return 0;
}
#endif

#ifdef MM_TEST_SEM
int main() {
  int ret = 0;
  char key[] = "/tmp/mm";
  size_t size = 1*1024*1024;
  MM *mm = mm_create(size, key);
  if (mm == NULL) {
    return 1;
  }
  if (!mm_lock(mm, MM_LOCK_RW)) {
    ret = 1;
  }
  if (!mm_unlock(mm)) {
    ret = 1;
  }
  if (!mm_lock(mm, MM_LOCK_RD)) {
    ret = 1;
  }
  if (!mm_unlock(mm)) {
    ret = 1;
  }
  mm_destroy(mm);
  return ret;
}
#endif
