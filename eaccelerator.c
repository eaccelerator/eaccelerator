/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 eAccelerator                                      |
   | http://eaccelerator.sourceforge.net                                  |
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
   | Author(s): Dmitry Stogov <mmcache@turckware.ru>                      |
   |            Seung Woo <segv@sayclub.com>                              |
   |            Everaldo Canuto <everaldo_canuto@yahoo.com.br>            |
   +----------------------------------------------------------------------+
   $Id$
*/

#include "eaccelerator.h"
#include "eaccelerator_version.h"

#ifdef HAVE_EACCELERATOR

#include "opcodes.h"

#include "zend.h"
#include "zend_API.h"
#include "zend_extensions.h"

#include <sys/types.h>
#include <sys/stat.h>
#ifdef ZEND_WIN32
#  include "win32/time.h"
#  include <time.h>
#  include <sys/utime.h>
#else
#  include <sys/file.h>
#  include <sys/time.h>
#  include <utime.h>
#endif
#include <fcntl.h>

#ifndef O_BINARY
#  define O_BINARY 0
#endif

/*???
#ifdef HAVE_SCHED_H
#  include <sched.h>
#endif
*/

#ifdef ZEND_WIN32
#  include <process.h>
#  ifndef S_ISREG
#    define S_ISREG(mode) (((mode)&S_IFMT) & S_IFREG)
#  endif
#  ifndef S_IRUSR
#    define S_IRUSR S_IREAD
#  endif
#  ifndef S_IWUSR
#    define S_IWUSR S_IWRITE
#  endif
#else
#  include <dirent.h>
#endif

#include "php.h"
#include "php_ini.h"
#include "php_logos.h"
#include "main/fopen_wrappers.h"
#include "ext/standard/info.h"
#include "ext/standard/php_incomplete_class.h"
#include "ext/standard/md5.h"

#ifndef INCOMPLETE_CLASS
#  define INCOMPLETE_CLASS "__PHP_Incomplete_Class"
#endif
#ifndef MAGIC_MEMBER
#  define MAGIC_MEMBER "__PHP_Incomplete_Class_Name"
#endif

#include "SAPI.h"

#undef HAVE_PHP_SESSIONS_SUPPORT
#ifdef HAVE_EXT_SESSION_PHP_SESSION_H
#  include "ext/session/php_session.h"
#   ifdef PHP_SESSION_API
#     if PHP_SESSION_API >= 20020306
#       define HAVE_PHP_SESSIONS_SUPPORT
static int eaccelerator_sessions_registered = 0;
#       ifdef PS_CREATE_SID_ARGS
#         include "ext/standard/php_lcg.h"
#       endif
#     endif
#   endif
#endif

#define MAX_DUP_STR_LEN 256

#define offsetof(str,fld) ((size_t)&(((str*)NULL)->fld))

#ifdef EACCELERATOR_WITHOUT_FILE_LOCKING
#  ifndef LOCK_SH
#    define LOCK_SH 1
#    define LOCK_EX 2
#    define LOCK_UN 8
#  endif
#  define EACCELERATOR_FLOCK(FILE,OP)
#else
#  ifndef ZEND_WIN32
#    ifdef HAVE_FLOCK
#      define EACCELERATOR_FLOCK(FILE,OP) flock((FILE),(OP))
#    else
#      ifndef LOCK_SH
#        define LOCK_SH 1
#        define LOCK_EX 2
#        define LOCK_UN 8
#      endif
#      define EACCELERATOR_FLOCK(FILE,OP)
#    endif
#  else
#    define LOCK_SH 0
#    define LOCK_EX 1
#    define LOCK_UN 2
#    define EACCELERATOR_FLOCK(FILE,OP) {OVERLAPPED offset = {0,0,0,0,NULL};\
                                   if ((OP) == LOCK_EX) {\
                                     LockFileEx((HANDLE)_get_osfhandle(FILE), \
                                       LOCKFILE_EXCLUSIVE_LOCK, 0,\
                                       1, 0, &offset);\
                                   } else if ((OP) == LOCK_SH) {\
                                     LockFileEx((HANDLE)_get_osfhandle(FILE), \
                                       0, 0,\
                                       1, 0, &offset);\
                                   } else if ((OP) == LOCK_UN) {\
                                     UnlockFileEx((HANDLE)_get_osfhandle(FILE), \
                                       0,\
                                       1, 0, &offset);\
                                   }}
#  endif
#endif


typedef struct _eaccelerator_op_array {
  zend_uchar type;
#ifdef ZEND_ENGINE_2
  zend_bool uses_this;
#else
  zend_bool uses_globals;
#endif
  zend_bool return_reference;
#ifdef ZEND_ENGINE_2
  zend_uint num_args;
  zend_uint required_num_args;
  zend_arg_info *arg_info;
  zend_bool pass_rest_by_reference;
#else
  zend_uchar *arg_types;
#endif
  char *function_name;
  char *function_name_lc;
#ifdef ZEND_ENGINE_2
  char* scope_name;
  int   scope_name_len;
  zend_uint fn_flags;
#endif
  zend_op *opcodes;
  zend_uint last;
  zend_uint T;
  zend_brk_cont_element *brk_cont_array;
  zend_uint last_brk_cont;
#ifdef ZEND_ENGINE_2
	/* HOESH: try & catch support */
	zend_try_catch_element* try_catch_array;
	int last_try_catch;
#endif
  HashTable *static_variables;
  char *filename;
#ifdef ZEND_ENGINE_2
  zend_uint line_start;
  zend_uint line_end;
  char *doc_comment;
  zend_uint doc_comment_len;
#endif
} eaccelerator_op_array;

typedef struct _eaccelerator_class_entry {
  char type;
  char *name;
  char *name_lc;
  uint name_length;
  char *parent;
  HashTable function_table;
  HashTable default_properties;
#ifdef ZEND_ENGINE_2
  zend_uint ce_flags;
  HashTable *static_members;
  HashTable properties_info;
  HashTable constants_table;
  zend_uint num_interfaces;
  char **interfaces;
  zend_class_iterator_funcs iterator_funcs;

  zend_object_value (*create_object)(zend_class_entry *class_type TSRMLS_DC);
  zend_object_iterator *(*get_iterator)(zend_class_entry *ce, zval *object TSRMLS_DC);
  int (*interface_gets_implemented)(zend_class_entry *iface, zend_class_entry *class_type TSRMLS_DC); /* a class implements this interface */
    
  char *filename;
  zend_uint line_start;
  zend_uint line_end;
  char *doc_comment;
  zend_uint doc_comment_len;
#endif
} eaccelerator_class_entry;

/*
 * To cache functions and classes.
 */
typedef struct _mm_fc_entry {
  void   *fc;
  struct _mm_fc_entry *next;
  int    htablen;
  char   htabkey[1];         /* must be last element */
} mm_fc_entry;

/*
 * A mm_cache_entry is a bucket for one PHP script file.
 * Nested  functions and classes which defined in the file goes
 * into the list of mm_fc_entry.
 */
typedef struct _mm_cache_entry {
  struct _mm_cache_entry *next;
#ifdef EACCELERATOR_USE_INODE
  dev_t                  st_dev;         /* file's device                     */
  ino_t                  st_ino;         /* file's inode                      */
#else
  unsigned int           hv;             /* hash value                        */
#endif
  off_t                  filesize;       /* file size */
  time_t                 mtime;          /* file last modification time       */
  time_t                 ttl;            /* expiration time                   */
  int                    size;           /* entry size (bytes)                */
  int                    nhits;          /* hits count                        */
  int                    nreloads;       /* count of reloads                  */
  int                    use_cnt;        /* how many processes uses the entry */
  eaccelerator_op_array       *op_array;      /* script's global scope code        */
  mm_fc_entry            *f_head;        /* list of nested functions          */
  mm_fc_entry            *c_head;        /* list of nested classes            */
  zend_bool              removed;        /* the entry is scheduled to remove  */
  char                   realfilename[1];/* real file name (must be last el.) */
} mm_cache_entry;

/*
 * bucket for user's cache
 */
typedef struct _mm_user_cache_entry {
  struct _mm_user_cache_entry *next;
  unsigned int           hv;            /* hash value                  */
  long                   ttl;           /* expiration time             */
  int                    size;
  zval                   value;         /* value                       */
  char                   key[1];        /* key value (must be last el) */
} mm_user_cache_entry;

/*
 * Linked list of mm_cache_entry which are used by process/thread
 */
typedef struct _mm_used_entry {
  struct _mm_used_entry *next;
  mm_cache_entry        *entry;
} mm_used_entry;

/*
 * Linked list of locks
 */
typedef struct _mm_lock_entry {
  struct _mm_lock_entry *next;
  pid_t  pid;
#ifdef ZTS
  THREAD_T thread;
#endif
  char                  key[1];
} mm_lock_entry;

typedef struct _mm_file_header {
  char   magic[8];        /* "EACCELERATOR" */
  int    eaccelerator_version;
  int    zend_version;
  int    php_version;
  int    size;
  time_t mtime;
  unsigned int crc32;
} mm_file_header;

#ifdef ZTS
#  define ZTS_LOCK()    tsrm_mutex_lock(mm_mutex)
#  define ZTS_UNLOCK()  tsrm_mutex_unlock(mm_mutex)
#else
#  define ZTS_LOCK()
#  define ZTS_UNLOCK()
#endif

#include "mm.h"

#if defined(EACCELERATOR_PROTECT_SHM)
#  define EACCELERATOR_PROTECT()    do {mm_protect(eaccelerator_mm_instance->mm, MM_PROT_READ);} while(0)
#  define EACCELERATOR_UNPROTECT()  do {mm_protect(eaccelerator_mm_instance->mm, MM_PROT_READ|MM_PROT_WRITE);} while(0)
#else
#  define EACCELERATOR_PROTECT()
#  define EACCELERATOR_UNPROTECT()
#endif

#define EACCELERATOR_LOCK_RW()    do {ZTS_LOCK(); mm_lock(eaccelerator_mm_instance->mm, MM_LOCK_RW);} while(0)
#define EACCELERATOR_LOCK_RD()    do {ZTS_LOCK(); mm_lock(eaccelerator_mm_instance->mm, MM_LOCK_RD);} while(0)
#define EACCELERATOR_UNLOCK()     do {mm_unlock(eaccelerator_mm_instance->mm); ZTS_UNLOCK();} while(0)
#define EACCELERATOR_UNLOCK_RW()  EACCELERATOR_UNLOCK()
#define EACCELERATOR_UNLOCK_RD()  EACCELERATOR_UNLOCK()

#define EACCELERATOR_BLOCK_INTERRUPTIONS()   HANDLE_BLOCK_INTERRUPTIONS()
#define EACCELERATOR_UNBLOCK_INTERRUPTIONS() HANDLE_UNBLOCK_INTERRUPTIONS()

#define MM_HASH_SIZE      256
#define MM_USER_HASH_SIZE 256
#define MM_HASH_MAX       (MM_HASH_SIZE-1)
#define MM_USER_HASH_MAX  (MM_USER_HASH_SIZE-1)

#define eaccelerator_malloc(size)        mm_malloc(eaccelerator_mm_instance->mm, size)
#define eaccelerator_free(x)             mm_free(eaccelerator_mm_instance->mm, x)
#define eaccelerator_malloc_nolock(size) mm_malloc_nolock(eaccelerator_mm_instance->mm, size)
#define eaccelerator_free_nolock(x)      mm_free_nolock(eaccelerator_mm_instance->mm, x)

typedef struct {
  MM             *mm;
  pid_t          owner;
  size_t         total;
  unsigned int   hash_cnt;
  unsigned int   user_hash_cnt;
  zend_bool      enabled;
  zend_bool      optimizer_enabled;
  unsigned int   rem_cnt;
  time_t         last_prune;
  mm_cache_entry *removed;
  mm_lock_entry  *locks;

  mm_cache_entry      *hash[MM_HASH_SIZE];
  mm_user_cache_entry *user_hash[MM_USER_HASH_SIZE];
} eaccelerator_mm;

/*
 * Globals (different for each process/thread)
 */
ZEND_DECLARE_MODULE_GLOBALS(eaccelerator)

/*
 * Globals (common for each process/thread)
 */
static long eaccelerator_shm_size = 0;
static long eaccelerator_shm_max = 0;
static long eaccelerator_shm_ttl = 0;
static long eaccelerator_shm_prune_period = 0;
static long eaccelerator_debug = 0;
static zend_bool eaccelerator_check_mtime = 1;
static zend_bool eaccelerator_scripts_shm_only = 0;
static eaccelerator_cache_place eaccelerator_keys_cache_place     = eaccelerator_shm_and_disk;
static eaccelerator_cache_place eaccelerator_sessions_cache_place = eaccelerator_shm_and_disk;
       eaccelerator_cache_place eaccelerator_content_cache_place  = eaccelerator_shm_and_disk;

static eaccelerator_mm* eaccelerator_mm_instance = NULL;
static int eaccelerator_is_zend_extension = 0;
static int eaccelerator_is_extension      = 0;
static zend_extension* ZendOptimizer = NULL;
#ifdef ZTS
static MUTEX_T mm_mutex;
#endif

static HashTable eaccelerator_global_function_table;
static HashTable eaccelerator_global_class_table;

static int binary_eaccelerator_version;
static int binary_php_version;
static int binary_zend_version;

/* saved original functions */
static zend_op_array *(*mm_saved_zend_compile_file)(zend_file_handle *file_handle, int type TSRMLS_DC);

#if defined(PROFILE_OPCODES) || defined(WITH_EACCELERATOR_EXECUTOR)
static void (*mm_saved_zend_execute)(zend_op_array *op_array TSRMLS_DC);
#endif

/* external declarations */
PHPAPI void php_stripslashes(char *str, int *len TSRMLS_DC);
PHPAPI char *php_get_uname();

ZEND_DLEXPORT zend_op_array* eaccelerator_compile_file(zend_file_handle *file_handle, int type TSRMLS_DC);


#if defined(DEBUG) || defined(TEST_PERFORMANCE)  || defined(PROFILE_OPCODES)
#include <ctype.h>
#include <stdio.h>
static FILE *F_fp;

static void binary_print(char *p, int len) {
  while (len--) {
    fputc(*p++, F_fp);
  }
  fputc('\n', F_fp);
}

static void log_hashkeys(char *p, HashTable *ht)
{
  Bucket *b;
  int i = 0;

  b = ht->pListHead;

  fputs(p, F_fp);
  while (b) {
    fprintf(F_fp, "[%d] ", i);
    binary_print(b->arKey, b->nKeyLength);

    b = b->pListNext;
    i++;
  }
}

static void pad(TSRMLS_D) {
  int i = MMCG(xpad);
  while (i-- > 0) {
    fputc('\t', F_fp);
  }
}

static void start_time(struct timeval *tvstart) {
  gettimeofday(tvstart, NULL);
}

static long elapsed_time(struct timeval *tvstart) {
  struct timeval tvend;
  int sec, usec;
  gettimeofday(&tvend, NULL);
  sec = tvend.tv_sec - tvstart->tv_sec;
  usec = tvend.tv_usec - tvstart->tv_usec;
  return sec * 1000000 + usec;
}
#endif  /* #if defined(DEBUG) || defined(TEST_PERFORMANCE)  || defined(PROFILE_OPCODES) */

static inline unsigned int hash_mm(const char *data, int len) {
  unsigned int h;
  const char *e = data + len;
  for (h = 2166136261U; data < e; ) {
    h *= 16777619;
    h ^= *data++;
  }
  return h;
}

static mm_cache_entry* hash_find_mm(const char  *key,
                                    struct stat *buf,
                                    int         *nreloads,
                                    time_t      ttl) {
  unsigned int hv, slot;
  mm_cache_entry *p, *q;

#ifdef EACCELERATOR_USE_INODE
  hv = buf->st_dev + buf->st_ino;
#else
  hv = hash_mm(key, strlen(key));
#endif
  slot = hv & MM_HASH_MAX;

  EACCELERATOR_LOCK_RW();
  q = NULL;
  p = eaccelerator_mm_instance->hash[slot];
  while (p != NULL) {
#ifdef EACCELERATOR_USE_INODE
    if (p->st_dev == buf->st_dev && p->st_ino == buf->st_ino) {
      struct stat buf2;
      if ((eaccelerator_check_mtime &&
          (buf->st_mtime != p->mtime || buf->st_size != p->filesize)) ||
          (strcmp(p->realfilename, key) != 0 &&
           (stat(p->realfilename,&buf2) != 0 ||
           buf2.st_dev != buf->st_dev ||
           buf2.st_ino != buf->st_ino))) {
#else
    if ((p->hv == hv) && (strcmp(p->realfilename, key) == 0)) {
      if (eaccelerator_check_mtime &&
          (buf->st_mtime != p->mtime || buf->st_size != p->filesize)) {
#endif
        /* key is invalid. Remove it. */
        *nreloads = p->nreloads+1;
        if (q == NULL) {
          eaccelerator_mm_instance->hash[slot] = p->next;
        } else {
          q->next = p->next;
        }
        eaccelerator_mm_instance->hash_cnt--;
        if (p->use_cnt > 0) {
          /* key is used by other process/thred. Shedule it to remove */
          p->removed = 1;
          p->next = eaccelerator_mm_instance->removed;
          eaccelerator_mm_instance->removed = p;
          eaccelerator_mm_instance->rem_cnt++;
          EACCELERATOR_UNLOCK_RW();
          return NULL;
        } else {
          /* key is unused. Remove it. */
          eaccelerator_free_nolock(p);
          EACCELERATOR_UNLOCK_RW();
          return NULL;
        }
      } else {
        /* key is valid */
        p->nhits++;
        p->use_cnt++;
        p->ttl = ttl;
        EACCELERATOR_UNLOCK_RW();
        return p;
      }
    }
    q = p;
    p = p->next;
  }
  EACCELERATOR_UNLOCK_RW();
  return NULL;
}

static void hash_add_mm(mm_cache_entry *x) {
  mm_cache_entry *p,*q;
  unsigned int slot;
#ifdef EACCELERATOR_USE_INODE
  slot = (x->st_dev + x->st_ino) & MM_HASH_MAX;
#else
  x->hv = hash_mm(x->realfilename, strlen(x->realfilename));
  slot = x->hv & MM_HASH_MAX;
#endif

  EACCELERATOR_LOCK_RW();
  x->next = eaccelerator_mm_instance->hash[slot];
  eaccelerator_mm_instance->hash[slot] = x;
  eaccelerator_mm_instance->hash_cnt++;
  q = x;
  p = x->next;
  while (p != NULL) {
#ifdef EACCELERATOR_USE_INODE
    if ((p->st_dev == x->st_dev) && (p->st_ino == x->st_ino)) {
#else
    if ((p->hv == x->hv) &&
        (strcmp(p->realfilename, x->realfilename) == 0)) {
#endif
      q->next = p->next;
      eaccelerator_mm_instance->hash_cnt--;
      eaccelerator_mm_instance->hash[slot]->nreloads += p->nreloads;
      if (p->use_cnt > 0) {
        /* key is used by other process/thred. Shedule it to remove */
        p->removed = 1;
        p->next = eaccelerator_mm_instance->removed;
        eaccelerator_mm_instance->removed = p;
        eaccelerator_mm_instance->rem_cnt++;
        EACCELERATOR_UNLOCK_RW();
        return;
      } else {
        /* key is unused. Remove it. */
        eaccelerator_free_nolock(p);
        EACCELERATOR_UNLOCK_RW();
        return;
      }
    }
    q = p;
    p = p->next;
  }
  EACCELERATOR_UNLOCK_RW();
}

static int init_mm(TSRMLS_D) {
  pid_t  owner = getpid();
  MM     *mm;
  size_t total;
  char   mm_path[MAXPATHLEN];

/*  if (getppid() != 1) return SUCCESS; */ /*???*/
#ifdef ZEND_WIN32
    snprintf(mm_path, MAXPATHLEN, "%s.%s", EACCELERATOR_MM_FILE, sapi_module.name);
#else
    snprintf(mm_path, MAXPATHLEN, "%s.%s%d", EACCELERATOR_MM_FILE, sapi_module.name, getpid());
#endif
/*  snprintf(mm_path, MAXPATHLEN, "%s.%s%d", EACCELERATOR_MM_FILE, sapi_module.name, geteuid());*/
  if ((eaccelerator_mm_instance = (eaccelerator_mm*)mm_attach(eaccelerator_shm_size*1024*1024, mm_path)) != NULL) {
#ifdef ZTS
    mm_mutex = tsrm_mutex_alloc();
#endif
    return SUCCESS;
  }
  mm = mm_create(eaccelerator_shm_size*1024*1024, mm_path);
  if (!mm) {
    return FAILURE;
  }
#ifdef DEBUG
#ifdef ZEND_WIN32
  fprintf(F_fp, "init_mm [%d]\n", getpid());
#else
  fprintf(F_fp, "init_mm [%d,%d]\n", getpid(), getppid());
#endif
  fflush(F_fp);
#endif
#ifdef ZTS
  mm_mutex = tsrm_mutex_alloc();
#endif
  total = mm_available(mm);
  eaccelerator_mm_instance = mm_malloc(mm, sizeof(*eaccelerator_mm_instance));
  if (!eaccelerator_mm_instance) {
    return FAILURE;
  }
  mm_set_attach(mm, eaccelerator_mm_instance);
  memset(eaccelerator_mm_instance, 0, sizeof(*eaccelerator_mm_instance));
  eaccelerator_mm_instance->owner = owner;
  eaccelerator_mm_instance->mm    = mm;
  eaccelerator_mm_instance->total = total;
  eaccelerator_mm_instance->hash_cnt = 0;
  eaccelerator_mm_instance->rem_cnt  = 0;
  eaccelerator_mm_instance->enabled = 1;
  eaccelerator_mm_instance->optimizer_enabled = 1;
  eaccelerator_mm_instance->removed = NULL;
  eaccelerator_mm_instance->locks = NULL;
  eaccelerator_mm_instance->user_hash_cnt = 0;
  eaccelerator_mm_instance->last_prune = time(0);
  EACCELERATOR_PROTECT();
  return SUCCESS;
}

static void shutdown_mm(TSRMLS_D) {
  if (eaccelerator_mm_instance) {
#ifdef ZEND_WIN32
    if (eaccelerator_mm_instance->owner == getpid()) {
#else
    if (getpgrp() == getpid()) {
#endif
      MM *mm = eaccelerator_mm_instance->mm;
#ifdef DEBUG
#ifdef ZEND_WIN32
      fprintf(F_fp, "shutdown_mm [%d]\n", getpid());
#else
      fprintf(F_fp, "shutdown_mm [%d,%d]\n", getpid(), getppid());
#endif
      fflush(F_fp);
#endif
#ifdef ZTS
      tsrm_mutex_free(mm_mutex);
#endif
      if (mm) {
        mm_destroy(mm);
      }
      eaccelerator_mm_instance = NULL;
    }
  }
}

static void debug_printf(char *format, ...) {
  char output_buf[512];
  va_list args;

  va_start(args, format);
  vsnprintf(output_buf, sizeof(output_buf), format, args);
  va_end(args);

#ifdef ZEND_WIN32
  OutputDebugString(output_buf);
/*  zend_printf("EACCELERATOR: %s<br>\n",output_buf);*/
#else
  fputs(output_buf, stderr);
#endif
}

/******************************************************************************/

#define FIXUP(x) if((x)!=NULL) {(x) = (void*)(((char*)(x)) + ((long)(MMCG(mem))));}

static void fixup_zval(zval* z TSRMLS_DC);

typedef void (*fixup_bucket_t)(void* TSRMLS_DC);

#define fixup_zval_hash(from) \
  fixup_hash(from, (fixup_bucket_t)fixup_zval TSRMLS_CC)

#ifdef ZEND_ENGINE_2
static void fixup_property_info(zend_property_info* from TSRMLS_DC) {
  FIXUP(from->name);
}
#endif

static void fixup_hash(HashTable* source, fixup_bucket_t fixup_bucket TSRMLS_DC) {
  unsigned int i;
  Bucket *p;

  if (source->nNumOfElements > 0) {
    if (!MMCG(compress)) {
      if (source->arBuckets != NULL) {
        FIXUP(source->arBuckets);
        for (i = 0; i < source->nTableSize; i++) {
          FIXUP(source->arBuckets[i]);
        }
      }
    }
    FIXUP(source->pListHead);
    FIXUP(source->pListTail);

    p = source->pListHead;
    while (p) {
      FIXUP(p->pNext);
      FIXUP(p->pLast);
      FIXUP(p->pData);
      FIXUP(p->pDataPtr);
      FIXUP(p->pListLast);
      FIXUP(p->pListNext);
      if (p->pDataPtr) {
        fixup_bucket(p->pDataPtr TSRMLS_CC);
        p->pData = &p->pDataPtr;
      } else {
        fixup_bucket(p->pData TSRMLS_CC);
      }
      p = p->pListNext;
    }
    source->pInternalPointer = source->pListHead;
  }
}

static void fixup_zval(zval* zv TSRMLS_DC) {
  switch (zv->type) {
    case IS_CONSTANT:
    case IS_STRING:
      if (zv->value.str.val == NULL ||
         zv->value.str.len == 0) {
        zv->value.str.val = empty_string;
        zv->value.str.len = 0;
      } else {
        FIXUP(zv->value.str.val);
      }
      break;
    case IS_ARRAY:
    case IS_CONSTANT_ARRAY:
      if (zv->value.ht == NULL || zv->value.ht == &EG(symbol_table)) {
      } else {
        FIXUP(zv->value.ht);
        fixup_zval_hash(zv->value.ht);
      }
      break;
    case IS_OBJECT:
      if (!MMCG(compress)) {
        return;
      }
#ifndef ZEND_ENGINE_2
      FIXUP(zv->value.obj.ce);
      if (zv->value.obj.properties != NULL) {
        FIXUP(zv->value.obj.properties);
        fixup_zval_hash(zv->value.obj.properties);
      }
#endif
    default:
      break;
  }
}

static void fixup_op_array(eaccelerator_op_array* from TSRMLS_DC) {
  zend_op *opline;
  zend_op *end;

#ifdef ZEND_ENGINE_2
  if (from->num_args > 0) {
    zend_uint i;
    FIXUP(from->arg_info);
    for (i = 0; i < from->num_args; i++) {
      FIXUP(from->arg_info[i].name);
      FIXUP(from->arg_info[i].class_name);
    }
  }
#else
  FIXUP(from->arg_types);
#endif
  FIXUP(from->function_name);
#ifdef ZEND_ENGINE_2
  FIXUP(from->scope_name);
#endif
  if (from->type == ZEND_INTERNAL_FUNCTION) {
    return;
  }

  if (from->opcodes != NULL) {
    FIXUP(from->opcodes);

    opline = from->opcodes;
    end = opline + from->last;
    MMCG(compress) = 0;
    for (;opline < end; opline++) {
/*
      if (opline->result.op_type == IS_CONST) fixup_zval(&opline->result.u.constant  TSRMLS_CC);
*/
      if (opline->op1.op_type    == IS_CONST) fixup_zval(&opline->op1.u.constant TSRMLS_CC);
      if (opline->op2.op_type    == IS_CONST) fixup_zval(&opline->op2.u.constant TSRMLS_CC);
#ifdef ZEND_ENGINE_2
      switch (opline->opcode) {
        case ZEND_JMP:
          FIXUP(opline->op1.u.jmp_addr);
          break;
        case ZEND_JMPZ:
        case ZEND_JMPNZ:
        case ZEND_JMPZ_EX:
        case ZEND_JMPNZ_EX:
          FIXUP(opline->op2.u.jmp_addr);
          break;
      }
      opline->handler = get_opcode_handler(opline->opcode TSRMLS_CC);
#endif
    }
    MMCG(compress) = 1;
  }
  FIXUP(from->brk_cont_array);
#ifdef ZEND_ENGINE_2
	/* HOESH: try & catch support */
	FIXUP(from->try_catch_array);
#endif
  if (from->static_variables != NULL) {
    FIXUP(from->static_variables);
    fixup_zval_hash(from->static_variables);
  }
  FIXUP(from->filename);
#ifdef ZEND_ENGINE_2
  FIXUP(from->doc_comment);
#endif
}

static void fixup_class_entry(eaccelerator_class_entry* from TSRMLS_DC) {
  FIXUP(from->name);
  FIXUP(from->parent);
#ifdef ZEND_ENGINE_2
  FIXUP(from->filename);
  FIXUP(from->doc_comment);
  fixup_zval_hash(&from->constants_table);
  fixup_zval_hash(&from->default_properties);
  fixup_hash(&from->properties_info, (fixup_bucket_t)fixup_property_info TSRMLS_CC);
  if (from->static_members != NULL) {
    FIXUP(from->static_members);
    fixup_zval_hash(from->static_members);
  }
#else
  fixup_zval_hash(&from->default_properties);
#endif
  fixup_hash(&from->function_table, (fixup_bucket_t)fixup_op_array TSRMLS_CC);
}

static void eaccelerator_fixup(mm_cache_entry *p TSRMLS_DC) {
  mm_fc_entry* q;

  MMCG(mem) = (char*)((long)p - (long)p->next);
  MMCG(compress) = 1;
  p->next        = NULL;
  FIXUP(p->op_array);
  FIXUP(p->f_head);
  FIXUP(p->c_head);
  fixup_op_array(p->op_array TSRMLS_CC);
  q = p->f_head;
  while (q != NULL) {
    FIXUP(q->fc);
    fixup_op_array((eaccelerator_op_array*)q->fc TSRMLS_CC);
    FIXUP(q->next);
    q = q->next;
  }
  q = p->c_head;
  while (q != NULL) {
    FIXUP(q->fc);
    fixup_class_entry((eaccelerator_class_entry*)q->fc TSRMLS_CC);
    FIXUP(q->next);
    q = q->next;
  }
}

/******************************************************************************/

static int encode_version(const char *s) {
  unsigned int v1 = 0;
  unsigned int v2 = 0;
  unsigned int v3 = 0;
  unsigned int c;
  char m = '.';
  sscanf(s, "%u.%u%c%u",&v1,&v2,&m,&v3);
  switch (m) {
    case  'a': c = 0; break;
    case  'b': c = 1; break;
    case  '.': c = 2; break;
    case  's': c = 15; break;
    default: c = 2;
  }
  return ((v1 & 0xf) << 20) |
         ((v2 & 0xff) << 12) |
         ((c & 0xf) << 8) |
         (v3 & 0xff);
}

static void decode_version(char *version, int v) {
  int t = (v & 0x000f00) >> 8;
  char c;
  switch (t) {
    case  0: c = 'a'; break;
    case  1: c = 'b'; break;
    case  2: c = '.'; break;
    case 15: c = 's'; break;
    default: c = '.';
  }
  snprintf(version, 16, "%d.%d%c%d", (v & 0xf00000) >> 20,
                                     (v & 0x0ff000) >> 12,
                                     c,
                                     (v & 0x0000ff));
}

#ifdef EACCELERATOR_USE_INODE
static int eaccelerator_inode_key(char* s, dev_t dev, ino_t ino TSRMLS_DC) {
  int n;
  strncpy(s, MMCG(cache_dir), MAXPATHLEN-1);
  strlcat(s, "/eaccelerator-", MAXPATHLEN-1);
  n = strlen(s);
  while (dev > 0) {
    if (n >= MAXPATHLEN) return 0;
    s[n++] = (dev % 10) +'0';
    dev /= 10;
  }
  if (n >= MAXPATHLEN) return 0;
  s[n++] = '.';
  while (ino > 0) {
    if (n >= MAXPATHLEN) return 0;
    s[n++] = (ino % 10) +'0';
    ino /= 10;
  }
  if (n >= MAXPATHLEN) return 0;
  s[n++] = '\000';
  return 1;
}
#endif

static int eaccelerator_md5(char* s, const char* prefix, const char* key TSRMLS_DC) {
#if defined(PHP_MAJOR_VERSION) && defined(PHP_MINOR_VERSION) && \
    ((PHP_MAJOR_VERSION > 4) || (PHP_MAJOR_VERSION == 4 && PHP_MINOR_VERSION > 1))
  char md5str[33];
  PHP_MD5_CTX context;
  unsigned char digest[16];

  md5str[0] = '\0';
  PHP_MD5Init(&context);
  PHP_MD5Update(&context, key, strlen(key));
  PHP_MD5Final(digest, &context);
  make_digest(md5str, digest);
  snprintf(s, MAXPATHLEN-1, "%s%s%s", MMCG(cache_dir), prefix, md5str);
  return 1;
#else
  zval retval;
  zval md5;
  zval param;
  zval *params[1];

  ZVAL_STRING(&md5, "md5", 0);
  INIT_ZVAL(param);
  params[0] = &param;
  ZVAL_STRING(params[0], (char*)key, 0);
  if (call_user_function(CG(function_table), (zval**)NULL, &md5, &retval, 1, params TSRMLS_CC) == SUCCESS &&
      retval.type == IS_STRING &&
      retval.value.str.len == 32) {
    strncpy(s, MMCG(cache_dir), MAXPATHLEN-1);
    strlcat(s, prefix, MAXPATHLEN);
    strlcat(s, retval.value.str.val, MAXPATHLEN);
    zval_dtor(&retval);
    return 1;
  }
  s[0] ='\0';
#endif
  return 0;
}

static void eaccelerator_prune(time_t t) {
  unsigned int i;

  EACCELERATOR_LOCK_RW();
  eaccelerator_mm_instance->last_prune = t;
  for (i = 0; i < MM_HASH_SIZE; i++) {
    mm_cache_entry **p = &eaccelerator_mm_instance->hash[i];
    while (*p != NULL) {
      struct stat buf;
      if (((*p)->ttl != 0 && (*p)->ttl < t && (*p)->use_cnt <= 0) ||
          stat((*p)->realfilename,&buf) != 0 ||
#ifdef EACCELERATOR_USE_INODE
          (*p)->st_dev != buf.st_dev ||
          (*p)->st_ino != buf.st_ino ||
#endif
          (*p)->mtime != buf.st_mtime ||
          (*p)->filesize != buf.st_size) {
        mm_cache_entry *r = *p;
        *p = (*p)->next;
        eaccelerator_mm_instance->hash_cnt--;
        eaccelerator_free_nolock(r);
      } else {
        p = &(*p)->next;
      }
    }
  }
  EACCELERATOR_UNLOCK_RW();
}

static void* eaccelerator_malloc2(size_t size TSRMLS_DC) {
  void *p = NULL;
  time_t t;

  if (eaccelerator_gc(TSRMLS_C) > 0) {
    p = eaccelerator_malloc(size);
    if (p != NULL) {
      return p;
    }
  }
  if (eaccelerator_shm_prune_period > 0) {
    t = time(0);
    if (t - eaccelerator_mm_instance->last_prune > eaccelerator_shm_prune_period) {
      eaccelerator_prune(t);
      p = eaccelerator_malloc(size);
    }
  }
  return p;
}

#define EACCELERATOR_CRC32(crc, ch)   (crc = (crc >> 8) ^ crc32tab[(crc ^ (ch)) & 0xff])

static const unsigned int crc32tab[256] = {
  0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
  0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
  0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
  0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
  0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
  0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
  0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
  0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
  0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
  0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
  0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
  0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
  0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
  0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
  0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
  0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
  0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
  0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
  0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
  0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
  0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
  0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
  0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
  0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
  0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
  0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
  0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
  0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
  0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
  0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
  0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
  0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
  0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
  0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
  0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
  0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
  0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
  0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
  0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
  0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
  0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
  0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
  0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
  0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
  0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
  0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
  0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
  0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
  0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
  0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
  0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
  0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
  0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
  0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
  0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
  0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
  0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
  0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
  0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
  0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
  0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
  0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
  0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
  0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d,
};

unsigned int eaccelerator_crc32(const char *p, size_t n) {
  unsigned int crc = ~0;
  for (; n--; ++p) {
    EACCELERATOR_CRC32(crc, *p);
  }
  return ~crc;
}

static mm_cache_entry* hash_find_file(const char  *key,
                                      struct stat *buf TSRMLS_DC) {
  int f;
  char s[MAXPATHLEN];
  mm_file_header hdr;
  mm_cache_entry *p;
  int use_shm = 1;

#ifdef EACCELERATOR_USE_INODE
  struct stat buf2;

  if (!eaccelerator_inode_key(s, buf->st_dev, buf->st_ino TSRMLS_CC)) {
    return NULL;
  }
#else
  if (!eaccelerator_md5(s, "/eaccelerator-", key TSRMLS_CC)) {
    return NULL;
  }
#endif

  if ((f = open(s, O_RDONLY | O_BINARY)) > 0) {
    EACCELERATOR_FLOCK(f, LOCK_SH);
    if (read(f, &hdr, sizeof(hdr)) != sizeof(hdr)) {
      EACCELERATOR_FLOCK(f, LOCK_UN);
      close(f);
      return NULL;
    }
    if (strncmp(hdr.magic,"EACCELERATOR",8) != 0 ||
        hdr.eaccelerator_version != binary_eaccelerator_version ||
        hdr.zend_version != binary_zend_version ||
        hdr.php_version != binary_php_version) {
      EACCELERATOR_FLOCK(f, LOCK_UN);
      close(f);
      unlink(s);
      return NULL;
    }
    p = eaccelerator_malloc(hdr.size);
    if (p == NULL) {
      p = eaccelerator_malloc2(hdr.size TSRMLS_CC);
    }
    if (p == NULL) {
      p = emalloc(hdr.size);
      use_shm = 0;
    }
    if (p == NULL) {
      EACCELERATOR_FLOCK(f, LOCK_UN);
      close(f);
      return NULL;
    }
    if (read(f, p, hdr.size) != hdr.size ||
        p->size != hdr.size ||
        hdr.crc32 != eaccelerator_crc32((const char*)p,p->size)) {
      EACCELERATOR_FLOCK(f, LOCK_UN);
      close(f);
      unlink(s);
      if (use_shm) eaccelerator_free(p); else efree(p);
      return NULL;
    }
    EACCELERATOR_FLOCK(f, LOCK_UN);
    close(f);
#ifdef EACCELERATOR_USE_INODE
    if (p->st_dev != buf->st_dev || p->st_ino != buf->st_ino) {
#else
    if (strcmp(key,p->realfilename) != 0) {
#endif
      if (use_shm) eaccelerator_free(p); else efree(p);
      return NULL;
    }
    if ((eaccelerator_check_mtime &&
        (buf->st_mtime != p->mtime || buf->st_size != p->filesize))
#ifdef EACCELERATOR_USE_INODE
        ||
        (strcmp(p->realfilename, key) != 0 &&
         (stat(p->realfilename,&buf2) != 0 ||
         buf2.st_dev != buf->st_dev ||
         buf2.st_ino != buf->st_ino))
#endif
       ) {
      /* key is invalid. Remove it. */
      if (use_shm) eaccelerator_free(p); else efree(p);
      unlink(s);
      return NULL;
    }
    eaccelerator_fixup(p TSRMLS_CC);
    if (use_shm) {
      p->nhits    = 1;
      p->nreloads = 1;
      p->use_cnt  = 1;
      p->removed  = 0;
      if (eaccelerator_shm_ttl > 0) {
        p->ttl = time(0) + eaccelerator_shm_ttl;
      } else {
        p->ttl = 0;
      }
      hash_add_mm(p);
    } else {
      p->use_cnt  = 0;
      p->removed  = 1;
    }
    return p;
  }
  return NULL;
}

static int hash_add_file(mm_cache_entry *p TSRMLS_DC) {
  int f;
  int ret = 0;
  char s[MAXPATHLEN];
  mm_file_header hdr;

#ifdef EACCELERATOR_USE_INODE
  if (!eaccelerator_inode_key(s, p->st_dev, p->st_ino TSRMLS_CC)) {
    return 0;
  }
#else
  if (!eaccelerator_md5(s, "/eaccelerator-", p->realfilename TSRMLS_CC)) {
    return 0;
  }
#endif

  unlink(s);
  f = open(s, O_CREAT | O_WRONLY | O_EXCL | O_BINARY, S_IRUSR | S_IWUSR);
  if (f > 0) {
    EACCELERATOR_FLOCK(f, LOCK_EX);
    strcpy(hdr.magic,"EACCELERATOR");
    hdr.eaccelerator_version = binary_eaccelerator_version;
    hdr.zend_version    = binary_zend_version;
    hdr.php_version     = binary_php_version;
    hdr.size  = p->size;
    hdr.mtime = p->mtime;
    p->next = p;
    hdr.crc32 = eaccelerator_crc32((const char*)p,p->size);
    ret = (write(f, &hdr, sizeof(hdr)) == sizeof(hdr));
    if (ret) ret = (write(f, p, p->size) == p->size);
    EACCELERATOR_FLOCK(f, LOCK_UN);
    close(f);
  }
  return ret;
}

typedef union align_union {
  double d;
  void *v;
  int (*func)(int);
  long l;
} align_union;

#if (defined (__GNUC__) && __GNUC__ >= 2)
#define EACCELERATOR_PLATFORM_ALIGNMENT (__alignof__ (align_test))
#else
#define EACCELERATOR_PLATFORM_ALIGNMENT (sizeof(align_union))
#endif

#define EACCELERATOR_ALIGN(n) (n) = (void*)((((size_t)(n)-1) & ~(EACCELERATOR_PLATFORM_ALIGNMENT-1)) + EACCELERATOR_PLATFORM_ALIGNMENT)

/******************************************************************************/

#ifndef DEBUG
inline
#endif
static
void calc_string(char* str, int len TSRMLS_DC) {
  if (len > MAX_DUP_STR_LEN || zend_hash_add(&MMCG(strings), str, len, &str, sizeof(char*), NULL) == SUCCESS) {
    EACCELERATOR_ALIGN(MMCG(mem));
    MMCG(mem) += len;
  }
}

static void calc_zval(zval* z TSRMLS_DC);
static void calc_class_entry(zend_class_entry* from TSRMLS_DC);

typedef void (*calc_bucket_t)(void* TSRMLS_DC);

#define calc_hash_ex(from, start, calc_bucket) \
  calc_hash_int(from, start, calc_bucket TSRMLS_CC)

#define calc_hash(from, calc_bucket) \
  calc_hash_ex(from, (from)->pListHead, calc_bucket)

#define calc_zval_hash(from) \
  calc_hash(from, (calc_bucket_t)calc_zval_ptr)

#define calc_zval_hash_ex(from, start) \
  calc_hash_ex(from, start, (calc_bucket_t)calc_zval_ptr)


static void calc_zval_ptr(zval** from TSRMLS_DC) {
  EACCELERATOR_ALIGN(MMCG(mem));
  MMCG(mem) += sizeof(zval);
  calc_zval(*from TSRMLS_CC);
}

#ifdef ZEND_ENGINE_2
static void calc_property_info(zend_property_info* from TSRMLS_DC) {
  EACCELERATOR_ALIGN(MMCG(mem));
  MMCG(mem) += sizeof(zend_property_info);
  calc_string(from->name, from->name_length+1 TSRMLS_CC);
}

static void calc_class_entry_ptr(zend_class_entry** from TSRMLS_DC) {
  calc_class_entry(*from TSRMLS_CC);
}
#endif

static void calc_hash_int(HashTable* source, Bucket* start, calc_bucket_t calc_bucket TSRMLS_DC) {
  Bucket* p;

  if (source->nNumOfElements > 0) {
    if (!MMCG(compress)) {
      EACCELERATOR_ALIGN(MMCG(mem));
      MMCG(mem) += source->nTableSize * sizeof(Bucket*);
    }
    p = start;
    while (p) {
      EACCELERATOR_ALIGN(MMCG(mem));
      MMCG(mem) += offsetof(Bucket,arKey)+p->nKeyLength;
      calc_bucket(p->pData TSRMLS_CC);
      p = p->pListNext;
    }
  }
}

static void calc_zval(zval* zv TSRMLS_DC) {
  switch (zv->type & ~IS_CONSTANT_INDEX) {
    case IS_CONSTANT:
    case IS_STRING:
      if (zv->value.str.val == NULL ||
          zv->value.str.val == empty_string ||
          zv->value.str.len == 0) {
      } else {
        calc_string(zv->value.str.val, zv->value.str.len+1 TSRMLS_CC);
      }
      break;
    case IS_ARRAY:
    case IS_CONSTANT_ARRAY:
      if (zv->value.ht == NULL || zv->value.ht == &EG(symbol_table)) {
      } else {
        EACCELERATOR_ALIGN(MMCG(mem));
        MMCG(mem) += sizeof(HashTable);
        calc_zval_hash(zv->value.ht);
      }
      break;
    case IS_OBJECT:
#ifndef ZEND_ENGINE_2
      if (zv->value.obj.ce != NULL) {
        zend_class_entry *ce = zv->value.obj.ce;
        if (!MMCG(compress)) {
          debug_printf("[%d] EACCELERATOR can't cache objects\n", getpid());
          zend_bailout();
        }
        while (ce != NULL) {
          if (ce->type !=  ZEND_USER_CLASS && strcmp(ce->name,"stdClass") != 0) {
            debug_printf("[%d] EACCELERATOR can't cache objects\n", getpid());
            zend_bailout();
          }
          ce = ce->parent;
        }
        calc_string(zv->value.obj.ce->name, zv->value.obj.ce->name_length+1 TSRMLS_CC);
      }
      if (zv->value.obj.properties != NULL) {
        EACCELERATOR_ALIGN(MMCG(mem));
        MMCG(mem) += sizeof(HashTable);
        calc_zval_hash(zv->value.obj.properties);
      }
#endif
      return;
    case IS_RESOURCE:
      debug_printf("[%d] EACCELERATOR can't cache resources\n", getpid());
      zend_bailout();
    default:
      break;
  }
}

static void calc_op_array(zend_op_array* from TSRMLS_DC) {
  zend_op *opline;
  zend_op *end;

  if (from->type == ZEND_INTERNAL_FUNCTION) {
    EACCELERATOR_ALIGN(MMCG(mem));
    MMCG(mem) += sizeof(zend_internal_function);
  } else if (from->type == ZEND_USER_FUNCTION) {
    EACCELERATOR_ALIGN(MMCG(mem));
    MMCG(mem) += sizeof(eaccelerator_op_array);
  } else {
    debug_printf("[%d] EACCELERATOR can't cache function \"%s\"\n", getpid(), from->function_name);
    zend_bailout();
  }
#ifdef ZEND_ENGINE_2
  if (from->num_args > 0) {
    zend_uint i;
    EACCELERATOR_ALIGN(MMCG(mem));
    MMCG(mem) += from->num_args * sizeof(zend_arg_info);
    for (i = 0; i < from->num_args; i++) {
      if (from->arg_info[i].name) {
        calc_string(from->arg_info[i].name,from->arg_info[i].name_len+1 TSRMLS_CC);
      }
      if (from->arg_info[i].class_name) {
        calc_string(from->arg_info[i].class_name,from->arg_info[i].class_name_len+1 TSRMLS_CC);
      }
    }
  }
#else
  if (from->arg_types != NULL) {
    calc_string((char*)from->arg_types, (from->arg_types[0]+1) * sizeof(zend_uchar) TSRMLS_CC);
  }
#endif
  if (from->function_name != NULL) {
    calc_string(from->function_name, strlen(from->function_name)+1 TSRMLS_CC);
  }
#ifdef ZEND_ENGINE_2
  if (from->scope != NULL)
  {
    // HOESH: the same problem?
    Bucket* q = CG(class_table)->pListHead;
    while (q != NULL)
	{
      if (*(zend_class_entry**)q->pData == from->scope)
	  {
        calc_string(q->arKey, q->nKeyLength TSRMLS_CC);
        break;
      }
      q = q->pListNext;
    }
  }
#endif
  if (from->type == ZEND_INTERNAL_FUNCTION) {
    return;
  }

  if (from->opcodes != NULL) {
    EACCELERATOR_ALIGN(MMCG(mem));
    MMCG(mem) += from->last * sizeof(zend_op);

    opline = from->opcodes;
    end = opline + from->last;
    MMCG(compress) = 0;
    for (;opline < end; opline++) {
/*
      if (opline->result.op_type == IS_CONST) calc_zval(&opline->result.u.constant  TSRMLS_CC);
*/
      if (opline->op1.op_type    == IS_CONST) calc_zval(&opline->op1.u.constant TSRMLS_CC);
      if (opline->op2.op_type    == IS_CONST) calc_zval(&opline->op2.u.constant TSRMLS_CC);
    }
    MMCG(compress) = 1;
  }
  if (from->brk_cont_array != NULL) {
    EACCELERATOR_ALIGN(MMCG(mem));
    MMCG(mem) += sizeof(zend_brk_cont_element) * from->last_brk_cont;
  }
#ifdef ZEND_ENGINE_2
	/* HOESH: try & catch support */
	if (from->try_catch_array != NULL)
	{
		EACCELERATOR_ALIGN(MMCG(mem));
		MMCG(mem) += sizeof(zend_try_catch_element) * from->last_try_catch;
	}
#endif
  if (from->static_variables != NULL) {
    EACCELERATOR_ALIGN(MMCG(mem));
    MMCG(mem) += sizeof(HashTable);
    calc_zval_hash(from->static_variables);
  }
  if (from->filename != NULL) {
    calc_string(from->filename, strlen(from->filename)+1 TSRMLS_CC);
  }
#ifdef ZEND_ENGINE_2
  if (from->doc_comment != NULL) {
    calc_string(from->doc_comment, from->doc_comment_len+1 TSRMLS_CC);
  }
#endif
}

static void calc_class_entry(zend_class_entry* from TSRMLS_DC) {
  int i;

  if (from->type != ZEND_USER_CLASS) {
    debug_printf("[%d] EACCELERATOR can't cache internal class \"%s\"\n", getpid(), from->name);
    zend_bailout();
  }
/*
  if (from->builtin_functions) {
    debug_printf("[%d] EACCELERATOR can't cache class \"%s\" because of it has "
        "some builtin_functions\n", getpid(), from->name);
    zend_bailout();
  }
*/
  EACCELERATOR_ALIGN(MMCG(mem));
  MMCG(mem) += sizeof(eaccelerator_class_entry);

  if (from->name != NULL) {
    calc_string(from->name, from->name_length+1 TSRMLS_CC);
  }
  if (from->parent != NULL && from->parent->name) {
    calc_string(from->parent->name, from->parent->name_length + 1  TSRMLS_CC);
  }
#ifdef ZEND_ENGINE_2
#if 0
  // what's problem. why from->interfaces[i] == 0x5a5a5a5a ?
  for (i=0; i<from->num_interfaces; i++) {
    if (from->interfaces[i]) {
      calc_string(from->interfaces[i]->name, from->interfaces[i]->name_length);
    }
  }
#endif
  if (from->filename != NULL) {
    calc_string(from->filename, strlen(from->filename)+1 TSRMLS_CC);
  }
  if (from->doc_comment != NULL) {
    calc_string(from->doc_comment, from->doc_comment_len+1 TSRMLS_CC);
  }

  calc_zval_hash(&from->constants_table);
  calc_zval_hash(&from->default_properties);
  calc_hash(&from->properties_info, (calc_bucket_t)calc_property_info);
  if (from->static_members != NULL) {
    EACCELERATOR_ALIGN(MMCG(mem));
    MMCG(mem) += sizeof(HashTable);
    calc_zval_hash(from->static_members);
  }
#else
  calc_zval_hash(&from->default_properties);
#endif
  calc_hash(&from->function_table, (calc_bucket_t)calc_op_array);
}

static int calc_size(char* key, zend_op_array* op_array,
                     Bucket* f, Bucket *c TSRMLS_DC) {
  Bucket *b;
  char   *x;
  int len = strlen(key);
  MMCG(compress) = 1;
  MMCG(mem) = NULL;

  zend_hash_init(&MMCG(strings), 0, NULL, NULL, 0);
  MMCG(mem) += offsetof(mm_cache_entry,realfilename)+len+1;
  zend_hash_add(&MMCG(strings), key, len+1, &key, sizeof(char*), NULL);
  b = c;
  while (b != NULL) {
    EACCELERATOR_ALIGN(MMCG(mem));
    MMCG(mem) += offsetof(mm_fc_entry,htabkey)+b->nKeyLength;
    x = b->arKey;
    zend_hash_add(&MMCG(strings), b->arKey, b->nKeyLength, &x, sizeof(char*), NULL);
    b = b->pListNext;
  }
  b = f;
  while (b != NULL) {
    EACCELERATOR_ALIGN(MMCG(mem));
    MMCG(mem) += offsetof(mm_fc_entry,htabkey)+b->nKeyLength;
    x = b->arKey;
    zend_hash_add(&MMCG(strings), b->arKey, b->nKeyLength, &x, sizeof(char*), NULL);
    b = b->pListNext;
  }
  while (c != NULL) {
#ifdef ZEND_ENGINE_2
    calc_class_entry(*(zend_class_entry**)c->pData TSRMLS_CC);
#else
    calc_class_entry((zend_class_entry*)c->pData TSRMLS_CC);
#endif
    c = c->pListNext;
  }
  while (f != NULL) {
    calc_op_array((zend_op_array*)f->pData TSRMLS_CC);
    f = f->pListNext;
  }
  calc_op_array(op_array TSRMLS_CC);
  EACCELERATOR_ALIGN(MMCG(mem));
  zend_hash_destroy(&MMCG(strings));
  return (long)MMCG(mem);
}

/******************************************************************************/

static inline char* store_string(char* str, int len TSRMLS_DC) {
  char *p;
  if (len > MAX_DUP_STR_LEN) {
    EACCELERATOR_ALIGN(MMCG(mem));
    p = (char*)MMCG(mem);
    MMCG(mem) += len;
    memcpy(p, str, len);
  } else if (zend_hash_find(&MMCG(strings), str, len, (void*)&p) == SUCCESS) {
    p = *(char**)p;
  } else {
    EACCELERATOR_ALIGN(MMCG(mem));
    p = (char*)MMCG(mem);
    MMCG(mem) += len;
    memcpy(p, str, len);
    zend_hash_add(&MMCG(strings), str, len, (void*)&p, sizeof(char*), NULL);
  }
  return p;
}

static void store_zval(zval* z TSRMLS_DC);
static eaccelerator_class_entry* store_class_entry(zend_class_entry* from TSRMLS_DC);

typedef void* (*store_bucket_t)(void* TSRMLS_DC);

#define store_hash_ex(to, from, start, store_bucket) \
  store_hash_int(to, from, start, store_bucket TSRMLS_CC)

#define store_hash(to, from, store_bucket) \
  store_hash_ex(to, from, (from)->pListHead, store_bucket)

#define store_zval_hash(to, from) \
  store_hash(to, from, (store_bucket_t)store_zval_ptr)

#define store_zval_hash_ex(to, from, start) \
  store_hash_ex(to, from, start, (store_bucket_t)store_zval_ptr)

static zval* store_zval_ptr(zval* from TSRMLS_DC) {
  zval* to;
  EACCELERATOR_ALIGN(MMCG(mem));
  to = (zval*)MMCG(mem);
  MMCG(mem) += sizeof(zval);
  memcpy(to, from, sizeof(zval));
  store_zval(to TSRMLS_CC);
  return to;
}

#ifdef ZEND_ENGINE_2
static zend_property_info* store_property_info(zend_property_info* from TSRMLS_DC) {
  zend_property_info* to;
  EACCELERATOR_ALIGN(MMCG(mem));
  to = (zend_property_info*)MMCG(mem);
  MMCG(mem) += sizeof(zend_property_info);
  memcpy(to, from, sizeof(zend_property_info));
  to->name = store_string(from->name, from->name_length+1 TSRMLS_CC);
  return to;
}

static eaccelerator_class_entry* store_class_entry_ptr(zend_class_entry** from TSRMLS_DC) {
  return store_class_entry(*from TSRMLS_CC);
}
#endif

static void store_hash_int(HashTable* target, HashTable* source, Bucket* start, store_bucket_t copy_bucket TSRMLS_DC) {
  Bucket *p, *np, *prev_p;

  memcpy(target, source, sizeof(HashTable));

  if (source->nNumOfElements > 0) {
    if (!MMCG(compress)) {
      EACCELERATOR_ALIGN(MMCG(mem));
      target->arBuckets = (Bucket **)MMCG(mem);
      MMCG(mem) += target->nTableSize * sizeof(Bucket*);
      memset(target->arBuckets, 0, target->nTableSize * sizeof(Bucket*));
    }

    target->pDestructor = NULL;
    target->persistent  = 1;
    target->pListHead   = NULL;
    target->pListTail   = NULL;

    p = start;
    prev_p = NULL;
    np = NULL;
    while (p) {
      EACCELERATOR_ALIGN(MMCG(mem));
      np = (Bucket*)MMCG(mem);
      MMCG(mem) += offsetof(Bucket,arKey)+p->nKeyLength;

      if (!MMCG(compress)) {
        int nIndex = p->h % source->nTableSize;
        if(target->arBuckets[nIndex]) {
          np->pNext = target->arBuckets[nIndex];
          np->pLast = NULL;
          np->pNext->pLast = np;
        } else {
          np->pNext = NULL;
          np->pLast = NULL;
        }
        target->arBuckets[nIndex] = np;
      }
      np->h = p->h;
      np->nKeyLength = p->nKeyLength;

      if (p->pDataPtr == NULL) {
        np->pData    = copy_bucket(p->pData TSRMLS_CC);
        np->pDataPtr = NULL;
      } else {
        np->pDataPtr = copy_bucket(p->pDataPtr TSRMLS_CC);
        np->pData    = &np->pDataPtr;
      }

      np->pListLast = prev_p;
      np->pListNext = NULL;

      memcpy(np->arKey, p->arKey, p->nKeyLength);

      if (prev_p) {
        prev_p->pListNext = np;
      } else {
        target->pListHead = np;
      }
      prev_p = np;
      p = p->pListNext;
    }
    target->pListTail = np;
    target->pInternalPointer = target->pListHead;
  }
}

static void store_zval(zval* zv TSRMLS_DC) {
  switch (zv->type & ~IS_CONSTANT_INDEX) {
    case IS_CONSTANT:
    case IS_STRING:
      if (zv->value.str.val == NULL ||
          zv->value.str.val == empty_string ||
          zv->value.str.len == 0) {
        zv->value.str.val = empty_string;
        zv->value.str.len = 0;
      } else {
        zv->value.str.val = store_string(zv->value.str.val, zv->value.str.len+1 TSRMLS_CC);
      }
      break;
    case IS_ARRAY:
    case IS_CONSTANT_ARRAY:
      if (zv->value.ht == NULL || zv->value.ht == &EG(symbol_table)) {
      } else {
        HashTable* p;
        EACCELERATOR_ALIGN(MMCG(mem));
        p = (HashTable*)MMCG(mem);
        MMCG(mem) += sizeof(HashTable);
        store_zval_hash(p, zv->value.ht);
        zv->value.ht = p;
      }
      break;
    case IS_OBJECT:
      if (!MMCG(compress)) {
        return;
      }
#ifndef ZEND_ENGINE_2
      if (zv->value.obj.ce != NULL) {
        char *s = store_string(zv->value.obj.ce->name, zv->value.obj.ce->name_length+1 TSRMLS_CC);
        zend_str_tolower(s, zv->value.obj.ce->name_length);
        zv->value.obj.ce = (zend_class_entry*)s;
      }
      if (zv->value.obj.properties != NULL) {
        HashTable* p;
        EACCELERATOR_ALIGN(MMCG(mem));
        p = (HashTable*)MMCG(mem);
        MMCG(mem) += sizeof(HashTable);
        store_zval_hash(p, zv->value.obj.properties);
        zv->value.obj.properties = p;
      }
#endif
    default:
      break;
  }
}

static eaccelerator_op_array* store_op_array(zend_op_array* from TSRMLS_DC) {
  eaccelerator_op_array *to;
  zend_op *opline;
  zend_op *end;

#ifdef DEBUG
  pad(TSRMLS_C);
  fprintf(F_fp, "[%d] store_op_array: %s [scope=%s]\n", getpid(),
    from->function_name ? from->function_name : "(top)", from->scope ? from->scope->name : "NULL");
  fflush(F_fp);
#endif

  if (from->type == ZEND_INTERNAL_FUNCTION) {
    EACCELERATOR_ALIGN(MMCG(mem));
    to = (eaccelerator_op_array*)MMCG(mem);
    MMCG(mem) += offsetof(eaccelerator_op_array,opcodes);
  } else if (from->type == ZEND_USER_FUNCTION) {
    EACCELERATOR_ALIGN(MMCG(mem));
    to = (eaccelerator_op_array*)MMCG(mem);
    MMCG(mem) += sizeof(eaccelerator_op_array);
  } else {
    return NULL;
  }

  to->type = from->type;
#ifdef ZEND_ENGINE_2
  to->num_args = from->num_args;
  to->required_num_args = from->required_num_args;
  if (from->num_args > 0) {
    zend_uint i;
    EACCELERATOR_ALIGN(MMCG(mem));
    to->arg_info = (zend_arg_info*)MMCG(mem);
    MMCG(mem) += from->num_args * sizeof(zend_arg_info);
    for (i = 0; i < from->num_args; i++) {
      if (from->arg_info[i].name) {
        to->arg_info[i].name = store_string(from->arg_info[i].name,from->arg_info[i].name_len+1 TSRMLS_CC);
        to->arg_info[i].name_len = from->arg_info[i].name_len;
      }
      if (from->arg_info[i].class_name) {
        to->arg_info[i].class_name = store_string(from->arg_info[i].class_name,from->arg_info[i].class_name_len+1 TSRMLS_CC);
        to->arg_info[i].class_name_len = from->arg_info[i].class_name_len;
      }
      to->arg_info[i].allow_null        = from->arg_info[i].allow_null;
      to->arg_info[i].pass_by_reference = from->arg_info[i].pass_by_reference;
      to->arg_info[i].return_reference = from->arg_info[i].return_reference;
    }
  }
  to->pass_rest_by_reference = from->pass_rest_by_reference;
#else
  if (from->arg_types != NULL) {
    to->arg_types = (unsigned char*)
      store_string((char*)from->arg_types, (from->arg_types[0]+1) * sizeof(zend_uchar) TSRMLS_CC);
  }
#endif
  if (from->function_name != NULL)
  {
	/*
	 * HOESH: converting to lowercase. this helps to find 
	 * class methods in function_table without each time tolower...
	 */
	to->function_name = store_string(from->function_name, strlen(from->function_name)+1  TSRMLS_CC);
	// XXX: revert
    // zend_str_tolower(to->function_name, strlen(from->function_name));
  }
#ifdef ZEND_ENGINE_2
  to->fn_flags         = from->fn_flags;
  to->scope_name = NULL;
  to->scope_name_len = 0;
  if (from->scope != NULL)
  {
	// HOESH: why? use from->scope->name & from->scope->name_len instead.
	// Keep internal class behavior on mind!
    Bucket* q = CG(class_table)->pListHead;
    while (q != NULL)
	{
      if (*(zend_class_entry**)q->pData == from->scope)
	  {
        to->scope_name = store_string(q->arKey, q->nKeyLength TSRMLS_CC);
        to->scope_name_len = q->nKeyLength - 1;
#ifdef DEBUG
        pad(TSRMLS_C); fprintf(F_fp, "[%d]                 find scope '%s' in CG(class_table) save hashkey '%s' [%08x] as to->scope_name\n",
            getpid(), from->scope->name ? from->scope->name : "NULL", q->arKey, to->scope_name); fflush(F_fp);
#endif
        break;
      }
      q = q->pListNext;
    }
    if (to->scope_name == NULL)
    {
#ifdef DEBUG
      pad(TSRMLS_C); fprintf(F_fp, "[%d]                 could not find scope '%s' in CG(class_table), saving it to NULL\n", getpid(), from->scope->name ? from->scope->name : "NULL"); fflush(F_fp);
#endif
    }
  }
#endif
  if (from->type == ZEND_INTERNAL_FUNCTION) {
    return to;
  }
  to->opcodes          = from->opcodes;
  to->last             = from->last;
  to->T                = from->T;
  to->brk_cont_array   = from->brk_cont_array;
  to->last_brk_cont    = from->last_brk_cont;
#ifdef ZEND_ENGINE_2
	/* HOESH: try & catch support */
	to->try_catch_array   = from->try_catch_array;
	to->last_try_catch    = from->last_try_catch;
  to->uses_this        = from->uses_this;
#else
  to->uses_globals     = from->uses_globals;
#endif
  to->static_variables = from->static_variables;
  to->return_reference = from->return_reference;
  to->filename         = from->filename;

  if (from->opcodes != NULL) {
    EACCELERATOR_ALIGN(MMCG(mem));
    to->opcodes = (zend_op*)MMCG(mem);
    MMCG(mem) += from->last * sizeof(zend_op);
    memcpy(to->opcodes, from->opcodes, from->last * sizeof(zend_op));

    opline = to->opcodes;
    end = opline + to->last;
    MMCG(compress) = 0;
    for (;opline < end; opline++) {
/*
      if (opline->result.op_type == IS_CONST) store_zval(&opline->result.u.constant  TSRMLS_CC);
*/
      if (opline->op1.op_type    == IS_CONST) store_zval(&opline->op1.u.constant TSRMLS_CC);
      if (opline->op2.op_type    == IS_CONST) store_zval(&opline->op2.u.constant TSRMLS_CC);
#ifdef ZEND_ENGINE_2
      switch (opline->opcode) {
        case ZEND_JMP:
          opline->op1.u.jmp_addr = to->opcodes + (opline->op1.u.jmp_addr - from->opcodes);
          break;
        case ZEND_JMPZ:
        case ZEND_JMPNZ:
        case ZEND_JMPZ_EX:
        case ZEND_JMPNZ_EX:
          opline->op2.u.jmp_addr = to->opcodes + (opline->op2.u.jmp_addr - from->opcodes);
          break;
      }
#endif
    }
    MMCG(compress) = 1;
  }
  if (from->brk_cont_array != NULL) {
    EACCELERATOR_ALIGN(MMCG(mem));
    to->brk_cont_array = (zend_brk_cont_element*)MMCG(mem);
    MMCG(mem) += sizeof(zend_brk_cont_element) * from->last_brk_cont;
    memcpy(to->brk_cont_array, from->brk_cont_array,
           sizeof(zend_brk_cont_element) * from->last_brk_cont);
  } else {
    to->last_brk_cont  = 0;
  }
#ifdef ZEND_ENGINE_2
	/* HOESH: try & catch support */
	if (from->try_catch_array != NULL)
	{
		EACCELERATOR_ALIGN(MMCG(mem));
		to->try_catch_array = (zend_try_catch_element*)MMCG(mem);
		MMCG(mem) += sizeof(zend_try_catch_element) * from->last_try_catch;
		memcpy(to->try_catch_array, from->try_catch_array,
			sizeof(zend_try_catch_element) * from->last_try_catch);
	}
	else
	{
		to->last_try_catch  = 0;
	}
#endif
  if (from->static_variables != NULL) {
    EACCELERATOR_ALIGN(MMCG(mem));
    to->static_variables = (HashTable*)MMCG(mem);
    MMCG(mem) += sizeof(HashTable);
    store_zval_hash(to->static_variables, from->static_variables);
  }
  if (from->filename != NULL) {
    to->filename =
      store_string(to->filename, strlen(from->filename)+1 TSRMLS_CC);
  }
#ifdef ZEND_ENGINE_2
  to->line_start      = from->line_start;
  to->line_end        = from->line_end;
  to->doc_comment_len = from->doc_comment_len;
  if (from->doc_comment != NULL) {
    to->doc_comment = store_string(from->doc_comment, from->doc_comment_len+1 TSRMLS_CC);
  }
#endif
  return to;
}

static eaccelerator_class_entry* store_class_entry(zend_class_entry* from TSRMLS_DC) {
  eaccelerator_class_entry *to;
  int i;

  EACCELERATOR_ALIGN(MMCG(mem));
  to = (eaccelerator_class_entry*)MMCG(mem);
  MMCG(mem) += sizeof(eaccelerator_class_entry);
  to->type        = from->type;
  to->name        = NULL;
  to->name_length = from->name_length;
  to->parent      = NULL;
#ifdef ZEND_ENGINE_2
  to->ce_flags    = from->ce_flags;
  to->static_members = NULL;
  to->num_interfaces = from->num_interfaces;

#if 0
  // i need to check more. why this field is null.
  //
  for (i=0; i<from->num_interfaces; i++) {
    if (from->interfaces[i]) {
      to->interfaces[i] = store_string(from->interfaces[i]->name, from->interfaces[i]->name_length);
    }
  }
#endif

#endif

#ifdef DEBUG
  pad(TSRMLS_C);
  fprintf(F_fp, "[%d] store_class_entry: %s parent was '%s'\n", getpid(), from->name? from->name : "(top)", from->parent ? from->parent->name : "NULL");
  fflush(F_fp);
  MMCG(xpad)++;
#endif

  if (from->name != NULL) {
    to->name = store_string(from->name, from->name_length+1 TSRMLS_CC);
	//XXX: revert
    // zend_str_tolower(to->name, from->name_length);
  }
  if (from->parent != NULL && from->parent->name) {
    to->parent = store_string(from->parent->name, from->parent->name_length+1 TSRMLS_CC);
    //XXX: revert
    // zend_str_tolower(to->parent, from->parent->name_length);
  }

/*
  if (!from->constants_updated) {
    zend_hash_apply_with_argument(&from->default_properties, (apply_func_arg_t) zval_update_constant, (void *) 1 TSRMLS_CC);
    to->constants_updated = 1;
  }
*/
#ifdef ZEND_ENGINE_2
  to->line_start                 = from->line_start;
  to->line_end                   = from->line_end;
  to->doc_comment_len            = from->doc_comment_len;
  to->iterator_funcs             = from->iterator_funcs;
  to->create_object              = from->create_object;
  to->get_iterator               = from->get_iterator;
  to->interface_gets_implemented = from->interface_gets_implemented;

  if (from->filename != NULL) {
    to->filename = store_string(from->filename, strlen(from->filename)+1 TSRMLS_CC);
  }
  if (from->doc_comment != NULL) {
    to->doc_comment = store_string(from->doc_comment, from->doc_comment_len+1 TSRMLS_CC);
  }

  store_zval_hash(&to->constants_table, &from->constants_table);
  store_zval_hash(&to->default_properties, &from->default_properties);
  store_hash(&to->properties_info, &from->properties_info, (store_bucket_t)store_property_info);
  if (from->static_members != NULL) {
    EACCELERATOR_ALIGN(MMCG(mem));
    to->static_members = (HashTable*)MMCG(mem);
    MMCG(mem) += sizeof(HashTable);
    store_zval_hash(to->static_members, from->static_members);
  }
#else
  store_zval_hash(&to->default_properties, &from->default_properties);
#endif
  store_hash(&to->function_table, &from->function_table, (store_bucket_t)store_op_array);

#ifdef DEBUG
  MMCG(xpad)--;
#endif

  return to;
}

static mm_cache_entry* eaccelerator_store_int(
                         char* key, int len,
                         zend_op_array* op_array,
                         Bucket* f, Bucket *c TSRMLS_DC) {
  mm_cache_entry *p;
  mm_fc_entry    *fc;
  mm_fc_entry    *q;
  char *x;

#ifdef DEBUG
  pad(TSRMLS_C); fprintf(F_fp, "[%d] eaccelerator_store_int: key='%s'\n", getpid(), key); fflush(F_fp);
#endif

  MMCG(compress) = 1;
  zend_hash_init(&MMCG(strings), 0, NULL, NULL, 0);
  p = (mm_cache_entry*)MMCG(mem);
  MMCG(mem) += offsetof(mm_cache_entry,realfilename)+len+1;

  p->nhits    = 0;
  p->use_cnt  = 0;
  p->removed  = 0;
  p->f_head   = NULL;
  p->c_head   = NULL;
  memcpy(p->realfilename, key, len+1);
  x = p->realfilename;
  zend_hash_add(&MMCG(strings), key, len+1, &x, sizeof(char*), NULL);

  q = NULL;
  while (c != NULL) {
#ifdef DEBUG
  pad(TSRMLS_C); fprintf(F_fp, "[%d] eaccelerator_store_int:     class hashkey=", getpid()); binary_print(c->arKey, c->nKeyLength); fflush(F_fp);
#endif
    EACCELERATOR_ALIGN(MMCG(mem));
    fc = (mm_fc_entry*)MMCG(mem);
    MMCG(mem) += offsetof(mm_fc_entry,htabkey)+c->nKeyLength;
    memcpy(fc->htabkey, c->arKey, c->nKeyLength);
    fc->htablen = c->nKeyLength;
    fc->next = NULL;
#ifdef ZEND_ENGINE_2
    fc->fc = *(zend_class_entry**)c->pData;
#else
    fc->fc = c->pData;
#endif
    c = c->pListNext;
    x = fc->htabkey;
    zend_hash_add(&MMCG(strings), fc->htabkey, fc->htablen, &x, sizeof(char*), NULL);
    if (q == NULL) {
      p->c_head = fc;
    } else {
      q->next = fc;
    }
    q = fc;
  }

  q = NULL;
  while (f != NULL) {
#ifdef DEBUG
  pad(TSRMLS_C); fprintf(F_fp, "[%d] eaccelerator_store_int:     function hashkey='%s'\n", getpid(), f->arKey); fflush(F_fp);
#endif
    EACCELERATOR_ALIGN(MMCG(mem));
    fc = (mm_fc_entry*)MMCG(mem);
    MMCG(mem) += offsetof(mm_fc_entry,htabkey)+f->nKeyLength;
    memcpy(fc->htabkey, f->arKey, f->nKeyLength);
    fc->htablen = f->nKeyLength;
    fc->next = NULL;
    fc->fc = f->pData;
    f = f->pListNext;
    x = fc->htabkey;
    zend_hash_add(&MMCG(strings), fc->htabkey, fc->htablen, &x, sizeof(char*), NULL);
    if (q == NULL) {
      p->f_head = fc;
    } else {
      q->next = fc;
    }
    q = fc;
  }

  q = p->c_head;
  while (q != NULL) {
#ifdef ZEND_ENGINE_2
    q->fc = store_class_entry((zend_class_entry*)q->fc TSRMLS_CC);
#else
    q->fc = store_class_entry((zend_class_entry*)q->fc TSRMLS_CC);
#endif
    q = q->next;
  }

  q = p->f_head;
  while (q != NULL) {
    q->fc = store_op_array((zend_op_array*)q->fc TSRMLS_CC);
    q = q->next;
  }
  p->op_array = store_op_array(op_array TSRMLS_CC);

  zend_hash_destroy(&MMCG(strings));
  return p;
}

/* called after succesful compilation, from eaccelerator_compile file */
static int eaccelerator_store(char* key, struct stat *buf, int nreloads,
                         zend_op_array* op_array,
                         Bucket* f, Bucket *c TSRMLS_DC) {
  mm_cache_entry *p;
  int len = strlen(key);
  int use_shm = 1;
  int ret = 0;
  int size = 0;

  zend_try {
    size = calc_size(key, op_array, f, c TSRMLS_CC);
  } zend_catch {
    size =  0;
  } zend_end_try();
  if (size == 0) {
    return 0;
  }
  EACCELERATOR_UNPROTECT();
  MMCG(mem) = eaccelerator_malloc(size);
  if (MMCG(mem) == NULL) {
    MMCG(mem) = eaccelerator_malloc2(size TSRMLS_CC);
  }
  if (!MMCG(mem) && !eaccelerator_scripts_shm_only) {
    EACCELERATOR_PROTECT();
    MMCG(mem) = emalloc(size);
    use_shm = 0;
  }
  if (MMCG(mem)) {
    memset(MMCG(mem), 0, size);
    p = eaccelerator_store_int(key, len, op_array, f, c TSRMLS_CC);
    p->mtime    = buf->st_mtime;
    p->filesize = buf->st_size;
    p->size     = size;
    p->nreloads = nreloads;
#ifdef EACCELERATOR_USE_INODE
    p->st_dev   = buf->st_dev;
    p->st_ino   = buf->st_ino;
#endif
    if (use_shm) {
      if (eaccelerator_shm_ttl > 0) {
        p->ttl = time(0) + eaccelerator_shm_ttl;
      } else {
        p->ttl = 0;
      }
      if (!eaccelerator_scripts_shm_only) {
        hash_add_file(p TSRMLS_CC);
      }
      hash_add_mm(p);
      EACCELERATOR_PROTECT();
      ret = 1;
    } else {
      ret =  hash_add_file(p TSRMLS_CC);
      efree(p);
    }
  }
  return ret;
}

/******************************************************************************/

static void restore_zval(zval * TSRMLS_DC);
static zend_class_entry* restore_class_entry(zend_class_entry* to, eaccelerator_class_entry *from TSRMLS_DC);

typedef void* (*restore_bucket_t)(void* TSRMLS_DC);

#define restore_zval_hash(target, source) \
  restore_hash(target, source, (restore_bucket_t)restore_zval_ptr TSRMLS_CC)

static zval* restore_zval_ptr(zval* from TSRMLS_DC) {
  zval* p;
  ALLOC_ZVAL(p);
  memcpy(p, from, sizeof(zval));
  restore_zval(p TSRMLS_CC);
  return p;
}

#ifdef ZEND_ENGINE_2
static zend_property_info* restore_property_info(zend_property_info* from TSRMLS_DC) {
  zend_property_info* to = emalloc(sizeof(zend_property_info));
  memcpy(to, from, sizeof(zend_property_info));
  to->name = emalloc(from->name_length+1);
  memcpy(to->name, from->name, from->name_length+1);
  return to;
}

static zend_class_entry* restore_class_entry_ptr(eaccelerator_class_entry *from TSRMLS_DC) {
  return restore_class_entry(NULL, from TSRMLS_CC);
}
#endif

static HashTable* restore_hash(HashTable *target, HashTable *source, restore_bucket_t copy_bucket TSRMLS_DC)
{
  Bucket *p, *np, *prev_p;
  int nIndex;

  if (target == NULL) {
    ALLOC_HASHTABLE(target);
  }
  memcpy(target, source, sizeof(HashTable));
  target->arBuckets = (Bucket **) emalloc(target->nTableSize * sizeof(Bucket*));
  memset(target->arBuckets, 0, target->nTableSize * sizeof(Bucket*));
  target->pDestructor = NULL;
  target->persistent  = 0;
  target->pListHead   = NULL;
  target->pListTail   = NULL;

  p = source->pListHead;
  prev_p = NULL;
  np = NULL;
  while (p) {
    np = (Bucket *) emalloc(offsetof(Bucket,arKey) + p->nKeyLength);
/*    np = (Bucket *) emalloc(sizeof(Bucket) + p->nKeyLength);*/
    nIndex = p->h % source->nTableSize;
    if(target->arBuckets[nIndex]) {
      np->pNext = target->arBuckets[nIndex];
      np->pLast = NULL;
      np->pNext->pLast = np;
    } else {
      np->pNext = NULL;
      np->pLast = NULL;
    }
    target->arBuckets[nIndex] = np;
    np->h = p->h;
    np->nKeyLength = p->nKeyLength;

    if (p->pDataPtr == NULL) {
      np->pData    = copy_bucket(p->pData TSRMLS_CC);
      np->pDataPtr = NULL;
    } else {
      np->pDataPtr = copy_bucket(p->pDataPtr TSRMLS_CC);
      np->pData    = &np->pDataPtr;
    }
    np->pListLast = prev_p;
    np->pListNext = NULL;

    memcpy(np->arKey, p->arKey, p->nKeyLength);

    if (prev_p) {
      prev_p->pListNext = np;
    } else {
      target->pListHead = np;
    }
    prev_p = np;
    p = p->pListNext;
  }
  target->pListTail = np;
  target->pInternalPointer = target->pListHead;
  return target;
}

static void restore_zval(zval *zv TSRMLS_DC)
{
  switch (zv->type & ~IS_CONSTANT_INDEX) {
    case IS_CONSTANT:
    case IS_STRING:
      if (zv->value.str.val == NULL || zv->value.str.val == empty_string || zv->value.str.len == 0) {
        zv->value.str.val = empty_string;
        return;
      } else {
        char *p = emalloc(zv->value.str.len+1);
        memcpy(p, zv->value.str.val, zv->value.str.len+1);
        zv->value.str.val = p;
      }
      return;

    case IS_ARRAY:
    case IS_CONSTANT_ARRAY:
      if (zv->value.ht != NULL && zv->value.ht != &EG(symbol_table)) {
        zv->value.ht = restore_zval_hash(NULL, zv->value.ht);
        zv->value.ht->pDestructor = ZVAL_PTR_DTOR;
      }
      return;
    case IS_OBJECT: {
#ifndef ZEND_ENGINE_2
      zend_bool incomplete_class = 0;
      char* class_name = (char*)zv->value.obj.ce;
      int   name_len = 0;
      if (!MMCG(compress)) {
        return;
      }
      if (class_name != NULL) {
        zend_class_entry *ce = NULL;
        name_len = strlen(class_name);
        if (zend_hash_find(CG(class_table), (void*)class_name, name_len+1,
            (void **)&ce) != SUCCESS) {
          char *lowercase_name = estrndup(INCOMPLETE_CLASS, sizeof(INCOMPLETE_CLASS));
          zend_str_tolower(lowercase_name, sizeof(INCOMPLETE_CLASS));
          if (zend_hash_find(CG(class_table),lowercase_name, sizeof(INCOMPLETE_CLASS),
            (void **)&ce) != SUCCESS) {
            efree(lowercase_name);
            zend_error(E_ERROR, "EACCELERATOR can't restore object's class \"%s\"", class_name);
          } else {
            efree(lowercase_name);
            zv->value.obj.ce = ce;
            incomplete_class = 1;
          }
        } else {
          zv->value.obj.ce = ce;
        }
      }
      if (zv->value.obj.properties != NULL) {
        zv->value.obj.properties = restore_zval_hash(NULL, zv->value.obj.properties);
        zv->value.obj.properties->pDestructor = ZVAL_PTR_DTOR;
        /* Clearing references */
        {
          Bucket* p = zv->value.obj.properties->pListHead;
          while (p != NULL) {
            ((zval*)(p->pDataPtr))->refcount = 1;
            p = p->pListNext;
          }
        }
      }
      if (incomplete_class && class_name != NULL) {
        zval *val;
        MAKE_STD_ZVAL(val);
        Z_TYPE_P(val)   = IS_STRING;
        Z_STRVAL_P(val) = estrndup(class_name, name_len);
        Z_STRLEN_P(val) = name_len;
        zend_hash_update(Z_OBJPROP_P(zv), MAGIC_MEMBER, sizeof(MAGIC_MEMBER), &val, sizeof(val), NULL);
      }
#endif
      return;
    }
  }
}

static void call_op_array_ctor_handler(zend_extension *extension, zend_op_array *op_array TSRMLS_DC) {
  if (extension->op_array_ctor) {
    extension->op_array_ctor(op_array);
  }
}

static zend_op_array* restore_op_array(zend_op_array *to, eaccelerator_op_array *from TSRMLS_DC) {
  zend_function* function;

#ifdef DEBUG
  pad(TSRMLS_C);
  fprintf(F_fp, "[%d] restore_op_array: %s\n", getpid(),
    from->function_name? from->function_name : "(top)");
  fflush(F_fp);
#endif

  if (from->type == ZEND_INTERNAL_FUNCTION) {
    if (to == NULL) {
      to = emalloc(sizeof(zend_internal_function));
    }
    memset(to, 0, sizeof(zend_internal_function));
  } else {
    if (to == NULL) {
      to = emalloc(sizeof(zend_op_array));
    }
    memset(to, 0, sizeof(zend_op_array));
    if (ZendOptimizer) {
      zend_llist_apply_with_argument(&zend_extensions, (llist_apply_with_arg_func_t) call_op_array_ctor_handler, to TSRMLS_CC);
    }
  }
  to->type             = from->type;
#ifdef ZEND_ENGINE_2
  /* this is internal function's case
   * struct zend_internal_function
	zend_uchar type;
	char *function_name;		
	zend_class_entry *scope;
	zend_uint fn_flags;	
	union _zend_function *prototype;
	zend_uint num_args;
	zend_uint required_num_args;
	zend_arg_info *arg_info;
	zend_bool pass_rest_by_reference;
	unsigned char return_reference;
    */
  /*
   * typedef struct _zend_internal_function {
   * // Common elements
   *    zend_uchar type;
   *    char *function_name;		
   *    zend_class_entry *scope;
   *    zend_uint fn_flags;	
   *    union _zend_function *prototype;
   *    zend_uint num_args;
   *    zend_uint required_num_args;
   *    zend_arg_info *arg_info;
   *    zend_bool pass_rest_by_reference;
   *    unsigned char return_reference;
   * // END of common elements
   *
   *    void (*handler)(INTERNAL_FUNCTION_PARAMETERS);
   * } zend_internal_function;
   */
  to->num_args = from->num_args;
  to->required_num_args      = from->required_num_args;
  to->arg_info = from->arg_info;
  to->pass_rest_by_reference = from->pass_rest_by_reference;
#else
  to->arg_types        = from->arg_types;
#endif
  to->function_name    = from->function_name;

  int    fname_len;
  char  *fname_lc;

  if (to->function_name)
  {
    fname_len = strlen(to->function_name);
    fname_lc  = zend_str_tolower_dup(to->function_name, fname_len);
  }
#ifdef ZEND_ENGINE_2
  to->fn_flags         = from->fn_flags;

  /* segv74:
   * to->scope = MMCG(class_entry)
   *
   * if  from->scope_name == NULL,
   *     ; MMCG(class) == NULL  : we are in function or outside function.
   *     ; MMCG(class) != NULL  : inherited method not defined in current file, should have to find.
   *                              just LINK (zend_op_array *) to to original entry in parent,
   *                              but, with what? !!! I don't know PARENT CLASS NAME !!!!
   *
   *
   * if  from->scope_name != NULL,
   *     ; we are in class member function 
   *
   *     ; we have to find appropriate (zend_class_entry*) to->scope for name from->scope_name
   *     ; if we find in CG(class_table), link to it.
   *     ; if fail, it should be MMCG(class_entry)
   *    
   * am I right here ? ;-(
   */
  if (from->scope_name != NULL)
  {
	char  *from_scope_lc = zend_str_tolower_dup(from->scope_name, from->scope_name_len);
	if (zend_hash_find(CG(class_table), (void *)from_scope_lc, from->scope_name_len+1, (void **)&to->scope) != SUCCESS)
    {
#ifdef DEBUG
      pad(TSRMLS_C); fprintf(F_fp, "[%d]                   can't find '%s' in hash. use MMCG(class_entry).\n", getpid(), from_scope_lc); fflush(F_fp);
#endif
	  to->scope = MMCG(class_entry);
    }
    else
    {
#ifdef DEBUG
      pad(TSRMLS_C); fprintf(F_fp, "[%d]                   found '%s' in hash\n", getpid(), from_scope_lc); fflush(F_fp);
#endif
      to->scope = *(zend_class_entry **)(to->scope);
    }
    efree(from_scope_lc);
  }
  else
  {
#ifdef DEBUG
      pad(TSRMLS_C); fprintf(F_fp, "[%d]                   from is NULL\n", getpid()); fflush(F_fp);
#endif
    if (MMCG(class_entry))
    {
      zend_class_entry *p;

      for (p = MMCG(class_entry)->parent; p; p = p->parent)
      {
#ifdef DEBUG
      pad(TSRMLS_C); fprintf(F_fp, "[%d]                   checking parent '%s' have '%s'\n", getpid(), p->name, fname_lc); fflush(F_fp);
#endif
		if (zend_hash_find(&p->function_table, fname_lc, fname_len+1, (void **) &function)==SUCCESS)
        {
#ifdef DEBUG
      pad(TSRMLS_C); fprintf(F_fp, "[%d]                                   '%s' has '%s' of scope '%s'\n", getpid(), p->name, fname_lc, function->common.scope->name); fflush(F_fp);
#endif
          to->scope = function->common.scope;
          break;
        }
      }
    }
    else
      to->scope = NULL;
  }

#ifdef DEBUG
  pad(TSRMLS_C);
  fprintf(F_fp, "[%d]                   %s's scope is '%s'\n", getpid(),
    from->function_name ? from->function_name : "(top)", to->scope ? to->scope->name : "NULL");
  fflush(F_fp);
#endif
#if 0
	if (to->scope != NULL)
	{
		unsigned int len = strlen(to->function_name);
		char *lcname = zend_str_tolower_dup(to->function_name, len);
		char *lc_to_name = zend_str_tolower_dup(to->scope->name, to->scope->name_length);
		/*
		 * HOESH: this one probably the old style constructor,
		 * so we set this as the constructor for the scope if
		 * 0) it doesn't exists yet,
		 * or, if the constructor is inherited from the parent:
		 * A) the constructor is internal function
		 * B) the constructor's scope name doesn't match the oparray's scope name
		 *
		 * remember lcname & len can be used as scope name info after the match!
		 */

    /* segv74: I got a question.
     *
     *         I think that, this code is reconstructing zend_class_entry thing.
     *         is it right doing this here?
     *
     *         IMHO, we can do this job in restore_class_entry().
     *         it's not good for readablity, and dosen't have performace gain.
     */
#ifdef DEBUG
  pad(TSRMLS_C);
  fprintf(F_fp, "        scope: %s[%d], method name: %s[%d] (%d) : to->scope->constructor=0x%08x\n",
      lcname, to->scope->name_length, lc_to_name, len, memcmp(lc_to_name, lcname, len), to->scope->constructor);
  fflush(F_fp);
#endif
		if  (
				to->scope->name_length == len &&
				memcmp(lc_to_name, lcname, len) == 0 &&
				(
					to->scope->constructor == NULL || // case 0)
					to->scope->constructor->type == ZEND_INTERNAL_FUNCTION || // case A)
					to->scope->constructor->op_array.scope->name_length != len || // case B)
					memcmp(to->scope->constructor->op_array.scope->name, lcname, len) != 0
				)
			)
		{
#ifdef DEBUG
  pad(TSRMLS_C);
  fprintf(F_fp, "------>    matched\n");
  fflush(F_fp);
#endif
			to->scope->constructor = (zend_function*)to;
		}
		/* HOESH: To avoid unnecessary lookups */
		else if (*lcname == '_' && *(lcname+1) == '_')
		{
			if (len == sizeof(ZEND_CONSTRUCTOR_FUNC_NAME)-1 &&
				memcmp(lcname, ZEND_CONSTRUCTOR_FUNC_NAME, sizeof(ZEND_CONSTRUCTOR_FUNC_NAME)) == 0)
			{
				to->scope->constructor = (zend_function*)to;
			}
			else if (len == sizeof(ZEND_DESTRUCTOR_FUNC_NAME)-1 &&
				memcmp(lcname, ZEND_DESTRUCTOR_FUNC_NAME, sizeof(ZEND_DESTRUCTOR_FUNC_NAME)) == 0)
			{
				to->scope->destructor = (zend_function*)to;
			}
			else if (len == sizeof(ZEND_CLONE_FUNC_NAME)-1 &&
				memcmp(lcname, ZEND_CLONE_FUNC_NAME, sizeof(ZEND_CLONE_FUNC_NAME)) == 0)
			{
				to->scope->clone = (zend_function*)to;
			}
			else if (len == sizeof(ZEND_GET_FUNC_NAME)-1 &&
				memcmp(lcname, ZEND_GET_FUNC_NAME, sizeof(ZEND_GET_FUNC_NAME)) == 0)
			{
				to->scope->__get = (zend_function*)to;
			}
			else if (len == sizeof(ZEND_SET_FUNC_NAME)-1 &&
				memcmp(lcname, ZEND_SET_FUNC_NAME, sizeof(ZEND_SET_FUNC_NAME)) == 0)
			{
				to->scope->__set = (zend_function*)to;
			}
			else if (len == sizeof(ZEND_CALL_FUNC_NAME)-1 &&
				memcmp(lcname, ZEND_CALL_FUNC_NAME, sizeof(ZEND_CALL_FUNC_NAME)) == 0)
			{
				to->scope->__call = (zend_function*)to;
			}
		}
		efree(lcname);
        efree(lc_to_name);
	}
#endif
#endif
  if (from->type == ZEND_INTERNAL_FUNCTION)
  {
	zend_class_entry* ce = MMCG(class_entry);
#ifdef DEBUG
    pad(TSRMLS_C); fprintf(F_fp, "[%d]                   [internal function from=%08x,to=%08x] ce='%s' [%08x]\n", getpid(), from, to, ce->name, ce); fflush(F_fp);
    if (ce)
    {
      pad(TSRMLS_C); fprintf(F_fp, "[%d]                                       ce->parent='%s' [%08x]\n", getpid(), ce->parent->name, ce->parent); fflush(F_fp);
    }
#endif
    if (ce != NULL &&
		ce->parent != NULL &&
		zend_hash_find(&ce->parent->function_table,
			fname_lc, fname_len+1,
			(void **) &function)==SUCCESS &&
		function->type == ZEND_INTERNAL_FUNCTION)
	{
#ifdef DEBUG
      pad(TSRMLS_C); fprintf(F_fp, "[%d]                                       found in function table\n", getpid()); fflush(F_fp);
#endif
		((zend_internal_function*)(to))->handler = ((zend_internal_function*) function)->handler;
    }
	else
	{
      /*??? FIXME. I don't know how to fix handler. */
		/*
		 * HOESH TODO: must solve this somehow, to avoid returnin
		 * damaged structure...
		 */
#ifdef DEBUG
      pad(TSRMLS_C); fprintf(F_fp, "[%d]                                       can't find\n", getpid()); fflush(F_fp);
#endif
    }
    return to;
  }
  to->opcodes          = from->opcodes;
  to->last = to->size  = from->last;
  to->T                = from->T;
  to->brk_cont_array   = from->brk_cont_array;
  to->last_brk_cont    = from->last_brk_cont;
/*
  to->current_brk_cont = -1;
  to->static_variables = from->static_variables;
  to->start_op         = to->opcodes;
  to->backpatch_count  = 0;
*/
  to->return_reference = from->return_reference;
  to->done_pass_two    = 1;
  to->filename         = from->filename;
#ifdef ZEND_ENGINE_2
	/* HOESH: try & catch support */
	to->try_catch_array   = from->try_catch_array;
	to->last_try_catch    = from->last_try_catch;
  to->uses_this        = from->uses_this;

  to->line_start      = from->line_start;
  to->line_end        = from->line_end;
  to->doc_comment_len = from->doc_comment_len;
  to->doc_comment     = from->doc_comment;
/*???
  if (from->doc_comment != NULL) {
    to->doc_comment = emalloc(from->doc_comment_len+1);
    memcpy(to->doc_comment, from->doc_comment, from->doc_comment_len+1);
  }
*/
#else
  to->uses_globals     = from->uses_globals;
#endif
  if (from->static_variables) {
    to->static_variables = restore_zval_hash(NULL, from->static_variables);
    to->static_variables->pDestructor = ZVAL_PTR_DTOR;
#ifndef ZEND_ENGINE_2
    if (MMCG(class_entry) != NULL) {
      Bucket* p = to->static_variables->pListHead;
      while (p != NULL) {
        ((zval*)(p->pDataPtr))->refcount = 1;
        p = p->pListNext;
      }
    }
#endif
  }

  /* disable deletion in destroy_op_array */
  ++MMCG(refcount_helper);
  to->refcount = &MMCG(refcount_helper);

  return to;
}

static zend_op_array* restore_op_array_ptr(eaccelerator_op_array *from TSRMLS_DC) {
  return restore_op_array(NULL, from TSRMLS_CC);
}

static zend_class_entry* restore_class_entry(zend_class_entry* to, eaccelerator_class_entry *from TSRMLS_DC)
{
  zend_class_entry *old;
  zend_function     *f;
  int   fname_len;
  char *fname_lc;

#ifdef DEBUG
  pad(TSRMLS_C);
  fprintf(F_fp, "[%d] retore_class_entry: %s\n", getpid(), from->name? from->name : "(top)");
  fflush(F_fp);
  MMCG(xpad)++;
#endif
  if (to == NULL) {
    to = emalloc(sizeof(zend_class_entry));
  }
  memset(to, 0, sizeof(zend_class_entry));
  to->type        = from->type;
/*
  to->name        = NULL;
  to->name_length = from->name_length;
  to->constants_updated = 0;
  to->parent      = NULL;
*/
#ifdef ZEND_ENGINE_2
  to->ce_flags    = from->ce_flags;
/*
  to->static_members = NULL;
*/
  to->num_interfaces = from->num_interfaces;
  //to->num_interfaces = 0;
  if (to->num_interfaces > 0) {
    to->interfaces = (zend_class_entry **) emalloc(sizeof(zend_class_entry *)*to->num_interfaces);
    // XXX:
    //
    // should find out class entry. what the hell !!!
    memset(to->interfaces, 0, sizeof(zend_class_entry *)*to->num_interfaces);
  } else {
    to->interfaces = NULL;
  }

  to->iterator_funcs             = from->iterator_funcs;
  to->create_object              = from->create_object;
  to->get_iterator               = from->get_iterator;
  to->interface_gets_implemented = from->interface_gets_implemented;
/*
  to->create_object = NULL;
*/
#endif

  if (from->name != NULL) {
    to->name_length = from->name_length;
    to->name = emalloc(from->name_length+1);
    memcpy(to->name, from->name, from->name_length+1);
  }

  if (from->parent != NULL)
  {
    int   name_len = strlen(from->parent);
    char *name_lc  = zend_str_tolower_dup(from->parent, name_len);

    if (zend_hash_find(CG(class_table), (void *)name_lc, name_len+1, (void **)&to->parent) != SUCCESS)
	{
      debug_printf("[%d] EACCELERATOR can't restore parent class "
          "\"%s\" of class \"%s\"\n", getpid(), (char*)from->parent, to->name);
      to->parent = NULL;
    }
	else
	{
#ifdef ZEND_ENGINE_2
	  /*
	   * HOESH: not sure this is a clean solution on kwestlake's
	   * problem... Fact: zend_hash_find returned **, but we
	   * expected * - I've tested this against autoload and
	   * everything seems to be oK.
	   */
	  to->parent = *(zend_class_entry**)to->parent;
	  /*
	   * HOESH: ahh, this is really needed. ;)
	   */
	  to->constructor  = to->parent->constructor;
	  to->destructor  = to->parent->destructor;
	  to->clone  = to->parent->clone;
	  to->__get  = to->parent->__get;
      to->__set  = to->parent->__set;
      to->__call = to->parent->__call;
	  to->create_object = to->parent->create_object;
#else
	  to->handle_property_get  = to->parent->handle_property_get;
      to->handle_property_set  = to->parent->handle_property_set;
      to->handle_function_call = to->parent->handle_function_call;
#endif
    }
    efree(name_lc);
  }
  else
  {
#ifdef DEBUG
    pad(TSRMLS_C); fprintf(F_fp, "[%d] parent = NULL\n", getpid()); fflush(F_fp);
#endif
    to->parent = NULL;
  }

  old = MMCG(class_entry);
  MMCG(class_entry) = to;

#ifdef ZEND_ENGINE_2
  to->refcount = 1;

  to->line_start      = from->line_start;
  to->line_end        = from->line_end;
  to->doc_comment_len = from->doc_comment_len;
  if (from->filename != NULL) {
    size_t len = strlen(from->filename)+1;
    to->filename = emalloc(len);
    memcpy(to->filename, from->filename, len);
  }
  if (from->doc_comment != NULL) {
    to->doc_comment = emalloc(from->doc_comment_len+1);
    memcpy(to->doc_comment, from->doc_comment, from->doc_comment_len+1);
  }

  restore_zval_hash(&to->constants_table, &from->constants_table);
  to->constants_table.pDestructor = ZVAL_PTR_DTOR;
  restore_zval_hash(&to->default_properties, &from->default_properties);
  to->default_properties.pDestructor = ZVAL_PTR_DTOR;
  restore_hash(&to->properties_info, &from->properties_info, (restore_bucket_t)restore_property_info TSRMLS_CC);
  if (from->static_members != NULL) {
    ALLOC_HASHTABLE(to->static_members);
    restore_zval_hash(to->static_members, from->static_members);
    to->static_members->pDestructor = ZVAL_PTR_DTOR;
/*
  } else {
    ALLOC_HASHTABLE(to->static_members);
    zend_hash_init_ex(to->static_members, 0, NULL, ZVAL_PTR_DTOR, 0, 0);
*/
  }
/*??? FIXME
    to->properties_info.pDestructor = (dtor_func_t) zend_destroy_property_info;
*/
#else
  to->refcount = emalloc(sizeof(*to->refcount));
  *to->refcount = 1;

  restore_zval_hash(&to->default_properties, &from->default_properties);
  to->default_properties.pDestructor = ZVAL_PTR_DTOR;
  /* Clearing references */
  {
    Bucket* p = to->default_properties.pListHead;
    while (p != NULL) {
      ((zval*)(p->pDataPtr))->refcount = 1;
      p = p->pListNext;
    }
  }
#endif
  restore_hash(&to->function_table, &from->function_table, (restore_bucket_t)restore_op_array_ptr TSRMLS_CC);
  to->function_table.pDestructor = ZEND_FUNCTION_DTOR;

#ifdef ZEND_ENGINE_2
  int   cname_len = to->name_length;
  char *cname_lc  = zend_str_tolower_dup(to->name, cname_len);

  Bucket *p = to->function_table.pListHead;
  while (p != NULL) {
    f         = p->pData;
    fname_len = strlen(f->common.function_name);
    fname_lc  = zend_str_tolower_dup(f->common.function_name, fname_len);

    if (fname_len == cname_len && !memcmp(fname_lc, cname_lc, fname_len))
      to->constructor = (zend_function*)f;
    else if (fname_lc[0] == '_' && fname_lc[1] == '_')
    {
      if (fname_len == sizeof(ZEND_CONSTRUCTOR_FUNC_NAME)-1 && memcmp(fname_lc, ZEND_CONSTRUCTOR_FUNC_NAME, sizeof(ZEND_CONSTRUCTOR_FUNC_NAME)) == 0)
        to->constructor = (zend_function*)f;
      else if (fname_len == sizeof(ZEND_DESTRUCTOR_FUNC_NAME)-1 && memcmp(fname_lc, ZEND_DESTRUCTOR_FUNC_NAME, sizeof(ZEND_DESTRUCTOR_FUNC_NAME)) == 0)
        to->destructor = (zend_function*)f;
      else if (fname_len == sizeof(ZEND_CLONE_FUNC_NAME)-1 && memcmp(fname_lc, ZEND_CLONE_FUNC_NAME, sizeof(ZEND_CLONE_FUNC_NAME)) == 0)
        to->clone = (zend_function*)f;
      else if (fname_len == sizeof(ZEND_GET_FUNC_NAME)-1 && memcmp(fname_lc, ZEND_GET_FUNC_NAME, sizeof(ZEND_GET_FUNC_NAME)) == 0)
        to->__get = (zend_function*)f;
      else if (fname_len == sizeof(ZEND_SET_FUNC_NAME)-1 && memcmp(fname_lc, ZEND_SET_FUNC_NAME, sizeof(ZEND_SET_FUNC_NAME)) == 0)
        to->__set = (zend_function*)f;
      else if (fname_len == sizeof(ZEND_CALL_FUNC_NAME)-1 && memcmp(fname_lc, ZEND_CALL_FUNC_NAME, sizeof(ZEND_CALL_FUNC_NAME)) == 0)
        to->__call = (zend_function*)f;
    }
    efree(fname_lc);
    p = p->pListNext;
  }
  efree(cname_lc);

#endif
  MMCG(class_entry) = old;

#ifdef DEBUG
  MMCG(xpad)--;
#endif

  return to;
}

static void restore_function(mm_fc_entry *p TSRMLS_DC) {
  zend_op_array op_array;

  if ((p->htabkey[0] == '\000') &&
      zend_hash_exists(CG(function_table), p->htabkey, p->htablen)) {
    return;
  }
  if (restore_op_array(&op_array, (eaccelerator_op_array *)p->fc TSRMLS_CC) != NULL) {
    if (zend_hash_add(CG(function_table), p->htabkey, p->htablen,
        &op_array, sizeof(zend_op_array), NULL) == FAILURE) {
      CG(in_compilation) = 1;
      CG(compiled_filename) = MMCG(mem);
#ifdef ZEND_ENGINE_2
      CG(zend_lineno) = op_array.line_start;
#else
      CG(zend_lineno) = op_array.opcodes[0].lineno;
#endif
      zend_error(E_ERROR, "Cannot redeclare %s()", p->htabkey);
    }
  }
}

/*
 * Class handling.
 */
static void restore_class(mm_fc_entry *p TSRMLS_DC) {
#ifdef ZEND_ENGINE_2
  zend_class_entry *ce;
#else
  zend_class_entry ce;
#endif

  if ((p->htabkey[0] == '\000') &&
      zend_hash_exists(CG(class_table), p->htabkey, p->htablen)) {
    return;
  }
#ifdef ZEND_ENGINE_2
  ce = restore_class_entry(NULL, (eaccelerator_class_entry *)p->fc TSRMLS_CC);
  if (ce != NULL)
#else
  if (restore_class_entry(&ce, (eaccelerator_class_entry *)p->fc TSRMLS_CC) != NULL)
#endif
  {
#ifdef ZEND_ENGINE_2
    if (zend_hash_add(CG(class_table), p->htabkey, p->htablen,
                      &ce, sizeof(zend_class_entry*), NULL) == FAILURE)
#else
    if (zend_hash_add(CG(class_table), p->htabkey, p->htablen,
                      &ce, sizeof(zend_class_entry), NULL) == FAILURE)
#endif
    {
      CG(in_compilation) = 1;
      CG(compiled_filename) = MMCG(mem);
#ifdef ZEND_ENGINE_2
      CG(zend_lineno) = ce->line_start;
#else
      CG(zend_lineno) = 0;
#endif
      zend_error(E_ERROR, "Cannot redeclare class %s", p->htabkey);
    }
  }

}

/*
 * Try to restore a file from the cache.
 */
static zend_op_array* eaccelerator_restore(char *realname, struct stat *buf,
                                      int *nreloads, time_t compile_time TSRMLS_DC) {
  mm_cache_entry *p;
  zend_op_array *op_array = NULL;

  *nreloads = 1;
  EACCELERATOR_UNPROTECT();
  p = hash_find_mm(realname, buf, nreloads,
                   ((eaccelerator_shm_ttl > 0)?(compile_time + eaccelerator_shm_ttl):0));
  if (p == NULL && !eaccelerator_scripts_shm_only) {
    p = hash_find_file(realname, buf TSRMLS_CC);
  }
  EACCELERATOR_PROTECT();
  if (p != NULL && p->op_array != NULL) {
    MMCG(class_entry) = NULL;
    op_array = restore_op_array(NULL, p->op_array TSRMLS_CC);
    if (op_array != NULL) {
      mm_fc_entry *e;
      mm_used_entry *used = emalloc(sizeof(mm_used_entry));
      used->entry  = p;
      used->next   = (mm_used_entry*)MMCG(used_entries);
      MMCG(used_entries) = (void*)used;
      MMCG(mem) = op_array->filename;
      for (e = p->c_head; e!=NULL; e = e->next) {
        restore_class(e TSRMLS_CC);
      }
      for (e = p->f_head; e!=NULL; e = e->next) {
        restore_function(e TSRMLS_CC);
      }
      MMCG(mem) = p->realfilename;
    }
  }
  return op_array;
}

/*
 * Only files matching user specified conditions should be cached.
 *
 * TODO - check the algorithm (fl)
 */

static int match(const char* name, const char* pat) {
  char p,k;
  int ok, neg;

  while (1) {
    p = *pat++;
    if (p == '\0') {
      return (*name == '\0');
    } else if (p == '*') {
      if (*pat == '\0') {
        return 1;
      }
      do {
        if (match(name, pat)) {
          return 1;
        }
      } while (*name++ != '\0');
      return 0;
    } else if (p == '?') {
      if (*name++ == '\0') {
        return 0;
      }
    } else if (p == '[') {
      ok = 0;
      if ((k = *name++) == '\0') {
        return 0;
      }
      if ((neg = (*pat == '!')) != '\0') {
        ++pat;
      }
      while ((p = *pat++) != ']') {
        if (*pat == '-') {
          if (p <= k && k <= pat[1]) {
            ok = 1;
          }
          pat += 2;
        } else {
          if (p == '\\') {
            p = *pat++;
            if (p == '\0') {
              p ='\\';
              pat--;
            }
          }
          if (p == k) {
            ok = 1;
          }
        }
      }
      if (ok == neg) {
        return 0;
      }
    } else {
      if (p == '\\') {
        p = *pat++;
        if (p == '\0') {
          p ='\\';
          pat--;
        }
      }
      if (*name++ != p) {
        return 0;
      }
    }
  }
  return (*name == '\0');
}

static int eaccelerator_ok_to_cache(char *realname TSRMLS_DC) {
  mm_cond_entry *p;
  int ok;

  if (MMCG(cond_list) == NULL) {
    return 1;
  }

  /* if "realname" matches to any pattern started with "!" then ignore it */
  for (p = MMCG(cond_list); p != NULL; p = p->next) {
    if (p->not && match(realname, p->str)) {
      return 0;
    }
  }

  /* else if it matches to any pattern not started with "!" then accept it */
  ok = 1;
  for (p = MMCG(cond_list); p != NULL; p = p->next) {
    if (!p->not) {
      ok = 0;
      if (match(realname, p->str)) {
        return 1;
      }
    }
  }
  return ok;
}

/******************************************************************************/
static char* eaccelerator_realpath(const char* name, char* realname TSRMLS_DC) {
/* ???TODO it is possibe to cache name->realname mapping to avoid lstat() calls */
#if ZEND_MODULE_API_NO >= 20001222
  return VCWD_REALPATH(name, realname);
#else
  return V_REALPATH(name, realname);
#endif
}

static int eaccelerator_stat(zend_file_handle *file_handle,
                        char* realname, struct stat* buf TSRMLS_DC) {
#ifdef EACCELERATOR_USE_INODE
#ifndef ZEND_WIN32
  if (file_handle->type == ZEND_HANDLE_FP && file_handle->handle.fp != NULL) {
    if (fstat(fileno(file_handle->handle.fp), buf) == 0 &&
       S_ISREG(buf->st_mode)) {
      if (file_handle->opened_path != NULL) {
        strcpy(realname,file_handle->opened_path);
      }
      return 0;
    }
  } else
#endif
  if (file_handle->opened_path != NULL) {
    if (stat(file_handle->opened_path, buf) == 0 &&
        S_ISREG(buf->st_mode)) {
       strcpy(realname,file_handle->opened_path);
       return 0;
    }
  } else if (PG(include_path) == NULL ||
             file_handle->filename[0] == '.' ||
             IS_SLASH(file_handle->filename[0]) ||
             IS_ABSOLUTE_PATH(file_handle->filename,strlen(file_handle->filename))) {
    if (stat(file_handle->filename, buf) == 0 &&
        S_ISREG(buf->st_mode)) {
       return 0;
    }
  } else {
    char* ptr = PG(include_path);
    char* end;
    int   len;
    char  tryname[MAXPATHLEN];
    int   filename_len = strlen(file_handle->filename);

    while (ptr && *ptr) {
      end = strchr(ptr, DEFAULT_DIR_SEPARATOR);
      if (end != NULL) {
        len = end-ptr;
        end++;
      } else {
        len = strlen(ptr);
        end = ptr+len;
      }
      if (len+filename_len+2 < MAXPATHLEN) {
        memcpy(tryname, ptr, len);
        tryname[len] = '/';
        memcpy(tryname+len+1, file_handle->filename, filename_len);
        tryname[len+filename_len+1] = '\0';
        if (stat(tryname, buf) == 0 &&
            S_ISREG(buf->st_mode)) {
          return 0;
        }
      }
      ptr = end;
    }
  }
  return -1;
#else
  if (file_handle->opened_path != NULL) {
    strcpy(realname,file_handle->opened_path);
#ifndef ZEND_WIN32
    if (file_handle->type == ZEND_HANDLE_FP && file_handle->handle.fp != NULL) {
      if (!eaccelerator_check_mtime) {
        return 0;
      } else if (fstat(fileno(file_handle->handle.fp), buf) == 0 &&
                 S_ISREG(buf->st_mode)) {
        return 0;
      } else {
        return -1;
      }
    } else {
      if (!eaccelerator_check_mtime) {
        return 0;
      } else if (stat(realname, buf) == 0 &&
                 S_ISREG(buf->st_mode)) {
        return 0;
      } else {
        return -1;
      }
    }
#else
    if (!eaccelerator_check_mtime) {
      return 0;
    } else if (stat(realname, buf) == 0 &&
               S_ISREG(buf->st_mode)) {
      return 0;
    } else {
      return -1;
    }
#endif
  } else if (file_handle->filename == NULL) {
    return -1;
  } else if (PG(include_path) == NULL ||
             file_handle->filename[0] == '.' ||
             IS_SLASH(file_handle->filename[0]) ||
             IS_ABSOLUTE_PATH(file_handle->filename,strlen(file_handle->filename))) {
    if (eaccelerator_realpath(file_handle->filename, realname TSRMLS_CC)) {
      if (!eaccelerator_check_mtime) {
        return 0;
      } else if (stat(realname, buf) == 0 &&
                 S_ISREG(buf->st_mode)) {
        return 0;
      } else {
        return -1;
      }
    }
  } else {
    char* ptr = PG(include_path);
    char* end;
    int   len;
    char  tryname[MAXPATHLEN];
    int   filename_len = strlen(file_handle->filename);

    while (ptr && *ptr) {
      end = strchr(ptr, DEFAULT_DIR_SEPARATOR);
      if (end != NULL) {
        len = end-ptr;
        end++;
      } else {
        len = strlen(ptr);
        end = ptr+len;
      }
      if (len+filename_len+2 < MAXPATHLEN) {
        memcpy(tryname, ptr, len);
        tryname[len] = '/';
        memcpy(tryname+len+1, file_handle->filename, filename_len);
        tryname[len+filename_len+1] = '\0';
        if (eaccelerator_realpath(tryname, realname TSRMLS_CC)) {
#ifdef ZEND_WIN32
          if (stat(realname, buf) == 0 &&
              S_ISREG(buf->st_mode)) {
            return 0;
          }
#else
          if (!eaccelerator_check_mtime) {
            return 0;
          } else if (stat(realname, buf) == 0 &&
                     S_ISREG(buf->st_mode)) {
            return 0;
          } else {
            return -1;
          }
#endif
        }
      }
      ptr = end;
    }
  }
  return -1;
#endif
}

/*
 * Intercept compilation of PHP file.  If we already have the file in
 * our cache, restore it.  Otherwise call the original Zend compilation
 * function and store the compiled zend_op_array in out cache.
 * This function is called again for each PHP file included in the
 * main PHP file.
 */
ZEND_DLEXPORT zend_op_array* eaccelerator_compile_file(zend_file_handle *file_handle, int type TSRMLS_DC) {
  zend_op_array *t;
  struct stat buf;
  char  realname[MAXPATHLEN];
  int   nreloads;
  time_t compile_time;

#ifdef EACCELERATOR_USE_INODE
  realname[0] = '\000';
#endif
#if defined(DEBUG) || defined(TEST_PERFORMANCE)
#ifdef TEST_PERFORMANCE
  struct timeval tv_start;
  fprintf(F_fp, "[%d] Enter COMPILE\n",getpid()); fflush(F_fp);
  start_time(&tv_start);
#endif
  fprintf(F_fp, "[%d] Enter COMPILE\n",getpid()); fflush(F_fp);
  fprintf(F_fp, "[%d] compile_file: \"%s\"\n",getpid(), file_handle->filename); fflush(F_fp);
  MMCG(xpad)+=2;
#endif
  if (!MMCG(enabled) ||
      (eaccelerator_mm_instance == NULL) ||
      !eaccelerator_mm_instance->enabled ||
      file_handle == NULL ||
      file_handle->filename == NULL ||
      eaccelerator_stat(file_handle, realname, &buf TSRMLS_CC) != 0 ||
      buf.st_mtime >= (compile_time = time(0)) ||
#ifdef EACCELERATOR_USE_INODE
      0) {
#else
      !eaccelerator_ok_to_cache(realname TSRMLS_CC)) {
#endif
#if defined(DEBUG) || defined(TEST_PERFORMANCE)
    fprintf(F_fp, "\t[%d] compile_file: compiling\n",getpid()); fflush(F_fp);
#endif
    t = mm_saved_zend_compile_file(file_handle, type TSRMLS_CC);
#if defined(DEBUG) || defined(TEST_PERFORMANCE)
#ifdef TEST_PERFORMANCE
    fprintf(F_fp, "\t[%d] compile_file: end (%ld)\n",getpid(),elapsed_time(&tv_start)); fflush(F_fp);
#else
    fprintf(F_fp, "\t[%d] compile_file: end\n",getpid()); fflush(F_fp);
#endif
    MMCG(xpad)-=2;
#endif
#if defined(DEBUG)
    fprintf(F_fp, "[%d] Leave COMPILE\n",getpid()); fflush(F_fp);
#endif
    return t;
  }

  t = eaccelerator_restore(realname, &buf, &nreloads, compile_time TSRMLS_CC);

// segv74: really cheap work around to auto_global problem.
//         it makes just in time to every time.
#ifdef ZEND_ENGINE_2
  zend_is_auto_global("_GET", sizeof("_SERVER")-1 TSRMLS_CC);
  zend_is_auto_global("_POST", sizeof("_SERVER")-1 TSRMLS_CC);
  zend_is_auto_global("_COOKIE", sizeof("_SERVER")-1 TSRMLS_CC);
  zend_is_auto_global("_SERVER", sizeof("_SERVER")-1 TSRMLS_CC);
  zend_is_auto_global("_ENV", sizeof("_ENV")-1 TSRMLS_CC);
  zend_is_auto_global("_REQUEST", sizeof("_REQUEST")-1 TSRMLS_CC);
  zend_is_auto_global("_FILES", sizeof("_SERVER")-1 TSRMLS_CC);
#endif
  if (t != NULL) {
    if (eaccelerator_debug > 0) {
      debug_printf("[%d] EACCELERATOR hit: \"%s\"\n", getpid(), t->filename);
    }
    /* restored from cache */

    zend_llist_add_element(&CG(open_files), file_handle);
#ifdef ZEND_ENGINE_2
    if (file_handle->opened_path == NULL && file_handle->type != ZEND_HANDLE_STREAM) {
      file_handle->handle.stream.handle = (void*)1;
#else
    if (file_handle->opened_path == NULL && file_handle->type != ZEND_HANDLE_FP) {
      int dummy = 1;
      file_handle->opened_path = MMCG(mem);
      zend_hash_add(&EG(included_files), file_handle->opened_path, strlen(file_handle->opened_path)+1, (void *)&dummy, sizeof(int), NULL);
      file_handle->handle.fp = NULL;
#endif
/*??? I don't understud way estrdup is not need
      file_handle->opened_path = estrdup(MMCG(mem));
*/
    }
#if defined(DEBUG) || defined(TEST_PERFORMANCE)
#ifdef TEST_PERFORMANCE
    fprintf(F_fp, "\t[%d] compile_file: restored (%ld)\n",getpid(),elapsed_time(&tv_start)); fflush(F_fp);
#else
    fprintf(F_fp, "\t[%d] compile_file: restored\n",getpid()); fflush(F_fp);
#endif
    MMCG(xpad)-=2;
#endif
#if defined(DEBUG)
    fprintf(F_fp, "[%d] Leave COMPILE\n",getpid()); fflush(F_fp);
    //dprint_compiler_retval(t, 1);
#endif
    return t;
  } else {
    /* not in cache or must be recompiled */
    Bucket *function_table_tail;
    Bucket *class_table_tail;
    HashTable* orig_function_table;
    HashTable* orig_class_table;
    HashTable* orig_eg_class_table;
    HashTable tmp_function_table;
    HashTable tmp_class_table;
    zend_function tmp_func;
    zend_class_entry tmp_class;
    int bailout;

#if defined(DEBUG) || defined(TEST_PERFORMANCE)
    fprintf(F_fp, "\t[%d] compile_file: marking\n",getpid()); fflush(F_fp);
    if (CG(class_table) != EG(class_table))
    {
      fprintf(F_fp, "\t[%d] oops, CG(class_table)[%08x] != EG(class_table)[%08x]\n", getpid(), CG(class_table), EG(class_table));
      //log_hashkeys("CG(class_table)\n", CG(class_table));
      //log_hashkeys("EG(class_table)\n", EG(class_table));
    }
    else
      fprintf(F_fp, "\t[%d] OKAY. That what I thought, CG(class_table)[%08x] == EG(class_table)[%08x]\n", getpid(), CG(class_table), EG(class_table));
      //log_hashkeys("CG(class_table)\n", CG(class_table));
#endif

    zend_hash_init_ex(&tmp_function_table, 100, NULL, ZEND_FUNCTION_DTOR, 1, 0);
    zend_hash_copy(&tmp_function_table, &eaccelerator_global_function_table, NULL, &tmp_func, sizeof(zend_function));
    orig_function_table = CG(function_table);
    CG(function_table) = &tmp_function_table;

    zend_hash_init_ex(&tmp_class_table, 10, NULL, ZEND_CLASS_DTOR, 1, 0);
    zend_hash_copy(&tmp_class_table, &eaccelerator_global_class_table, NULL, &tmp_class, sizeof(zend_class_entry));

    orig_class_table = CG(class_table);;
    CG(class_table) = &tmp_class_table;
#ifdef ZEND_ENGINE_2
    orig_eg_class_table = EG(class_table);;
    EG(class_table) = &tmp_class_table;
#endif

    /* Storing global pre-compiled functions and classes */
    function_table_tail = CG(function_table)->pListTail;
    class_table_tail = CG(class_table)->pListTail;

#if defined(DEBUG) || defined(TEST_PERFORMANCE)
#ifdef TEST_PERFORMANCE
    fprintf(F_fp, "\t[%d] compile_file: compiling (%ld)\n",getpid(),elapsed_time(&tv_start)); fflush(F_fp);
#else
    fprintf(F_fp, "\t[%d] compile_file: compiling tmp_class_table=%d class_table=%d\n", getpid(), tmp_class_table.nNumOfElements, orig_class_table->nNumOfElements); fflush(F_fp);
#endif
#endif
    if (MMCG(optimizer_enabled) && eaccelerator_mm_instance->optimizer_enabled) {
      MMCG(compiler) = 1;
    }

    bailout = 0;
    zend_try {
      t = mm_saved_zend_compile_file(file_handle, type TSRMLS_CC);
    } zend_catch {
      CG(function_table) = orig_function_table;
      CG(class_table) = orig_class_table;
#ifdef ZEND_ENGINE_2
      EG(class_table) = orig_eg_class_table;
#endif
      bailout = 1;
    } zend_end_try();
    if (bailout) {
      zend_bailout();
    }
#if defined(DEBUG)
    //log_hashkeys("class_table\n", CG(class_table));
#endif

/*???
    if (file_handle->opened_path == NULL && t != NULL) {
      file_handle->opened_path = t->filename;
    }
*/
    MMCG(compiler) = 0;
    if (t != NULL &&
        file_handle->opened_path != NULL &&
#ifdef EACCELERATOR_USE_INODE
        eaccelerator_ok_to_cache(file_handle->opened_path TSRMLS_CC)) {
#else
        (eaccelerator_check_mtime ||
         ((stat(file_handle->opened_path, &buf) == 0) && S_ISREG(buf.st_mode)))) {
#endif
#if defined(DEBUG) || defined(TEST_PERFORMANCE)
#ifdef TEST_PERFORMANCE
      fprintf(F_fp, "\t[%d] compile_file: storing in cache (%ld)\n",getpid(),elapsed_time(&tv_start)); fflush(F_fp);
#else
      fprintf(F_fp, "\t[%d] compile_file: storing in cache\n",getpid()); fflush(F_fp);
#endif
#endif
#ifdef WITH_EACCELERATOR_LOADER
      if (t->last >= 3 &&
          t->opcodes[0].opcode == ZEND_SEND_VAL &&
          t->opcodes[1].opcode == ZEND_DO_FCALL &&
          t->opcodes[2].opcode == ZEND_RETURN &&
          t->opcodes[1].op1.op_type == IS_CONST &&
          t->opcodes[1].op1.u.constant.type == IS_STRING &&
          t->opcodes[1].op1.u.constant.value.str.len == sizeof("eaccelerator_load")-1 &&
          (memcmp(t->opcodes[1].op1.u.constant.value.str.val, "eaccelerator_load", sizeof("eaccelerator_load")-1) == 0) &&
          t->opcodes[0].op1.op_type == IS_CONST &&
          t->opcodes[0].op1.u.constant.type == IS_STRING) {
        zend_op_array* new_t;
        zend_bool old_in_compilation = CG(in_compilation);
        char* old_filename = CG(compiled_filename);
        int old_lineno = CG(zend_lineno);

        CG(in_compilation) = 1;
        zend_set_compiled_filename(t->filename TSRMLS_CC);
        CG(zend_lineno) = t->opcodes[1].lineno;
        new_t = eaccelerator_load(
          t->opcodes[0].op1.u.constant.value.str.val,
          t->opcodes[0].op1.u.constant.value.str.len TSRMLS_CC);
        CG(in_compilation) = old_in_compilation;
        CG(compiled_filename) = old_filename;
        CG(zend_lineno) = old_lineno;
        if (new_t != NULL) {
#ifdef ZEND_ENGINE_2
          destroy_op_array(t TSRMLS_CC);
#else
          destroy_op_array(t);
#endif
          efree(t);
          t = new_t;
        }
      }
#endif
      function_table_tail = function_table_tail?function_table_tail->pListNext:
                                                CG(function_table)->pListHead;
      class_table_tail = class_table_tail?class_table_tail->pListNext:
                                          CG(class_table)->pListHead;
      if (eaccelerator_store(file_handle->opened_path, &buf, nreloads, t,
                        function_table_tail, class_table_tail TSRMLS_CC)) {
        if (eaccelerator_debug > 0) {
          debug_printf("[%d] EACCELERATOR %s: \"%s\"\n", getpid(),
              (nreloads == 1) ? "cached" : "re-cached", file_handle->opened_path);
        }
      } else {
        if (eaccelerator_debug > 0) {
          debug_printf("[%d] EACCELERATOR cann't cache: \"%s\"\n", getpid(), file_handle->opened_path);
        }
      }
    } else {
      function_table_tail = function_table_tail?function_table_tail->pListNext:
                                                CG(function_table)->pListHead;
      class_table_tail = class_table_tail?class_table_tail->pListNext:
                                          CG(class_table)->pListHead;
    }
    CG(function_table) = orig_function_table;
    CG(class_table) = orig_class_table;
#ifdef ZEND_ENGINE_2
    EG(class_table) = orig_eg_class_table;
#ifdef DEBUG
    fprintf(F_fp, "\t[%d] restoring CG(class_table)[%08x] != EG(class_table)[%08x]\n", getpid(), CG(class_table), EG(class_table));
#endif
#endif
    while (function_table_tail != NULL) {
      zend_op_array *op_array = (zend_op_array*)function_table_tail->pData;
      if (op_array->type == ZEND_USER_FUNCTION) {
        if (zend_hash_add(CG(function_table),
                          function_table_tail->arKey,
                          function_table_tail->nKeyLength,
                          op_array, sizeof(zend_op_array), NULL) == FAILURE &&
            function_table_tail->arKey[0] != '\000') {
          CG(in_compilation) = 1;
          CG(compiled_filename) = file_handle->opened_path;
#ifdef ZEND_ENGINE_2
          CG(zend_lineno) = op_array->line_start;
#else
          CG(zend_lineno) = op_array->opcodes[0].lineno;
#endif
          zend_error(E_ERROR, "Cannot redeclare %s()", function_table_tail->arKey);
        }
      }
      function_table_tail = function_table_tail->pListNext;
    }
    while (class_table_tail != NULL) {
#ifdef ZEND_ENGINE_2
      zend_class_entry **ce = (zend_class_entry**)class_table_tail->pData;
      if ((*ce)->type == ZEND_USER_CLASS) {
        if (zend_hash_add(CG(class_table),
                          class_table_tail->arKey,
                          class_table_tail->nKeyLength,
                          ce, sizeof(zend_class_entry*), NULL) == FAILURE &&
            class_table_tail->arKey[0] != '\000') {
          CG(in_compilation) = 1;
          CG(compiled_filename) = file_handle->opened_path;
          CG(zend_lineno) = (*ce)->line_start;
#else
      zend_class_entry *ce = (zend_class_entry*)class_table_tail->pData;
      if (ce->type == ZEND_USER_CLASS) {
        if (ce->parent != NULL) {
          if (zend_hash_find(CG(class_table), (void*)ce->parent->name, ce->parent->name_length+1, (void **)&ce->parent) != SUCCESS)
		  {
            ce->parent = NULL;
          }
        }
        if (zend_hash_add(CG(class_table),
                          class_table_tail->arKey,
                          class_table_tail->nKeyLength,
                          ce, sizeof(zend_class_entry), NULL) == FAILURE &&
            class_table_tail->arKey[0] != '\000') {
          CG(in_compilation) = 1;
          CG(compiled_filename) = file_handle->opened_path;
          CG(zend_lineno) = 0;
#endif
          zend_error(E_ERROR, "Cannot redeclare class %s", class_table_tail->arKey);
        }
      }
      class_table_tail = class_table_tail->pListNext;
    }
    tmp_function_table.pDestructor = NULL;
    tmp_class_table.pDestructor = NULL;
    zend_hash_destroy(&tmp_function_table);
    zend_hash_destroy(&tmp_class_table);
  }
#if defined(DEBUG) || defined(TEST_PERFORMANCE)
#ifdef TEST_PERFORMANCE
  fprintf(F_fp, "\t[%d] compile_file: end (%ld)\n",getpid(),elapsed_time(&tv_start)); fflush(F_fp);
#else
  fprintf(F_fp, "\t[%d] compile_file: end\n",getpid()); fflush(F_fp);
#endif
  MMCG(xpad)-=2;
  fflush(F_fp);
#endif
#if defined(DEBUG)
  fprintf(F_fp, "[%d] Leave COMPILE\n",getpid()); fflush(F_fp);
  //dprint_compiler_retval(t, 0);
#endif
  return t;
}

#ifdef PROFILE_OPCODES
static void profile_execute(zend_op_array *op_array TSRMLS_DC)
{
  int i;
  struct timeval tv_start;
  long usec;

  for (i=0;i<MMCG(profile_level);i++)
    fputs("  ", F_fp);
  fprintf(F_fp,"enter: %s:%s\n", op_array->filename, op_array->function_name);
  fflush(F_fp);
  start_time(&tv_start);
  MMCG(self_time)[MMCG(profile_level)] = 0;
  MMCG(profile_level)++;
#ifdef WITH_EACCELERATOR_EXECUTOR
  eaccelerator_execute(op_array TSRMLS_CC);
#else
  mm_saved_zend_execute(op_array TSRMLS_CC);
#endif
  usec = elapsed_time(&tv_start);
  MMCG(profile_level)--;
  if (MMCG(profile_level) > 0)
    MMCG(self_time)[MMCG(profile_level)-1] += usec;
  for (i=0;i<MMCG(profile_level);i++)
    fputs("  ", F_fp);
  fprintf(F_fp,"leave: %s:%s (%ld,%ld)\n", op_array->filename, op_array->function_name, usec, usec-MMCG(self_time)[MMCG(profile_level)]);
  fflush(F_fp);
}

ZEND_DLEXPORT zend_op_array* profile_compile_file(zend_file_handle *file_handle, int type TSRMLS_DC) {
  zend_op_array *t;
  int i;
  struct timeval tv_start;
  long usec;

  start_time(&tv_start);
  MMCG(self_time)[MMCG(profile_level)] = 0;
  t = eaccelerator_compile_file(file_handle, type TSRMLS_CC);
  usec = elapsed_time(&tv_start);
  if (MMCG(profile_level) > 0)
    MMCG(self_time)[MMCG(profile_level)-1] += usec;
  for (i=0;i<MMCG(profile_level);i++)
    fputs("  ", F_fp);
  fprintf(F_fp,"compile: %s (%ld)\n", file_handle->filename, usec);
  fflush(F_fp);
  return t;
}

#endif  /* #ifdef PROFILE_OPCODES */

/* Format Bytes */
static void format_size(char* s, unsigned int size, int legend) {
  unsigned int i = 0;
  unsigned int n = 0;
  char ch;
  do {
    if ((n != 0) && (n % 3 == 0)) {
      s[i++] = ',';
    }
    s[i++] = (char)((int)'0' + (size % 10));
    n++;
    size = size / 10;
  } while (size != 0);
  s[i] = '\0';
  n = 0; i--;
  while (n < i) {
    ch = s[n];
    s[n] = s[i];
    s[i] = ch;
    n++, i--;
  }
  if (legend) {
    strcat(s, " Bytes");
  }
}

PHP_MINFO_FUNCTION(eaccelerator) {
  char s[32];

  php_info_print_table_start();
  php_info_print_table_header(2, "eAccelerator support", "enabled");
  php_info_print_table_row(2, "Version", EACCELERATOR_VERSION);
  php_info_print_table_row(2, "Caching Enabled", (MMCG(enabled) && (eaccelerator_mm_instance != NULL) && eaccelerator_mm_instance->enabled)?"true":"false");
  php_info_print_table_row(2, "Optimizer Enabled", (MMCG(optimizer_enabled) && (eaccelerator_mm_instance != NULL) && eaccelerator_mm_instance->optimizer_enabled)?"true":"false");
  if (eaccelerator_mm_instance != NULL) {
    size_t available;
    EACCELERATOR_UNPROTECT();
    available = mm_available(eaccelerator_mm_instance->mm);
    EACCELERATOR_LOCK_RD();
    EACCELERATOR_PROTECT();
    format_size(s, eaccelerator_mm_instance->total, 1);
    php_info_print_table_row(2, "Memory Size", s);
    format_size(s, available, 1);
    php_info_print_table_row(2, "Memory Available", s);
    format_size(s, eaccelerator_mm_instance->total - available, 1);
    php_info_print_table_row(2, "Memory Allocated", s);
    snprintf(s, 32, "%u", eaccelerator_mm_instance->hash_cnt);
    php_info_print_table_row(2, "Cached Scripts", s);
    snprintf(s, 32, "%u", eaccelerator_mm_instance->rem_cnt);
    php_info_print_table_row(2, "Removed Scripts", s);
    snprintf(s, 32, "%u", eaccelerator_mm_instance->user_hash_cnt);
    php_info_print_table_row(2, "Cached Keys", s);
    EACCELERATOR_UNPROTECT();
    EACCELERATOR_UNLOCK_RD();
    EACCELERATOR_PROTECT();
  }
  php_info_print_table_end();

  DISPLAY_INI_ENTRIES();
}

/* User Cache Routines (put, get, rm, gc) */

static char* build_key(const char* key, int key_len, int *xlen TSRMLS_DC) {
  int len = strlen(MMCG(hostname));
  if (len > 0) {
    char* xkey;
    *xlen = len + key_len + 1;
    xkey = emalloc((*xlen)+1);
    memcpy(xkey, MMCG(hostname), len);
    xkey[len] = ':';
    memcpy(xkey+len+1, key, key_len+1);
    return xkey;
  } else {
    *xlen = key_len;
    return (char*)key;
  }
}

static int eaccelerator_lock(const char* key, int key_len TSRMLS_DC) {
  int xlen;
  char* xkey;
  mm_lock_entry* x;
  mm_lock_entry** p;
  int ok = 0;

  if (eaccelerator_mm_instance == NULL) {
    return 0;
  }
  xkey = build_key(key, key_len, &xlen TSRMLS_CC);
  EACCELERATOR_UNPROTECT();
  x = eaccelerator_malloc(offsetof(mm_lock_entry,key)+xlen+1);
  if (x == NULL) {
    EACCELERATOR_PROTECT();
    if (xlen != key_len) {efree(xkey);}
    return 0;
  }
  x->pid = getpid();
#ifdef ZTS
  x->thread = tsrm_thread_id();
#endif
  x->next = NULL;
  memcpy(x->key, xkey, xlen+1);
  while (1) {
    EACCELERATOR_LOCK_RW();
    p = &eaccelerator_mm_instance->locks;
    while ((*p) != NULL) {
      if (strcmp((*p)->key,x->key) == 0) {
#ifdef ZTS
        if (x->pid == (*p)->pid && x->thread == (*p)->thread) {
#else
        if (x->pid == (*p)->pid) {
#endif
          ok = 1;
          eaccelerator_free_nolock(x);
        }
        break;
      }
      p = &(*p)->next;
    }
    if ((*p) == NULL) {
      *p = x;
      ok = 1;
    }
    EACCELERATOR_UNLOCK_RW();
    if (ok) {
      break;
    } else {
#ifdef ZEND_WIN32
      Sleep(100);
/*???
#elif defined(HAVE_SCHED_YIELD)
      sched_yield();
*/
#else
      struct timeval t;
      t.tv_sec = 0;
      t.tv_usec = 100;
      select(0, NULL, NULL, NULL, &t);
#endif
    }
  }
  EACCELERATOR_PROTECT();
  if (xlen != key_len) {efree(xkey);}
  return 1;
}

static int eaccelerator_unlock(const char* key, int key_len TSRMLS_DC) {
  int xlen;
  char* xkey;
  mm_lock_entry** p;

  if (eaccelerator_mm_instance == NULL) {
    return 0;
  }
  xkey = build_key(key, key_len, &xlen TSRMLS_CC);
  EACCELERATOR_UNPROTECT();
  EACCELERATOR_LOCK_RW();
  p = &eaccelerator_mm_instance->locks;
  while ((*p) != NULL) {
    if (strcmp((*p)->key,xkey) == 0) {
#ifdef ZTS
      if ((*p)->pid == getpid() && (*p)->thread == tsrm_thread_id()) {
#else
      if ((*p)->pid == getpid()) {
#endif
         mm_lock_entry *x = (*p);
        *p = (*p)->next;
        eaccelerator_free_nolock(x);
      } else {
        EACCELERATOR_UNLOCK_RW();
        EACCELERATOR_PROTECT();
        if (xlen != key_len) {efree(xkey);}
        return 0;
      }
      break;
    }
    p = &(*p)->next;
  }
  EACCELERATOR_UNLOCK_RW();
  EACCELERATOR_PROTECT();
  if (xlen != key_len) {efree(xkey);}
  return 1;
}

int eaccelerator_put(const char* key, int key_len, zval* val, time_t ttl, eaccelerator_cache_place where TSRMLS_DC) {
  mm_user_cache_entry *p, *q;
  unsigned int slot;
  long size;
  int use_shm = 1;
  int ret = 0;
  char s[MAXPATHLEN];
  int xlen;
  char* xkey;

  xkey = build_key(key, key_len, &xlen TSRMLS_CC);
  MMCG(compress) = 1;
  MMCG(mem) = NULL;
  zend_hash_init(&MMCG(strings), 0, NULL, NULL, 0);
  EACCELERATOR_ALIGN(MMCG(mem));
  MMCG(mem) += offsetof(mm_user_cache_entry, key)+xlen+1;
  calc_zval(val TSRMLS_CC);
  zend_hash_destroy(&MMCG(strings));

  size = (long)MMCG(mem);

  MMCG(mem) = NULL;
  if (eaccelerator_mm_instance != NULL &&
      (where == eaccelerator_shm_and_disk ||
       where == eaccelerator_shm ||
       where == eaccelerator_shm_only)) {
    EACCELERATOR_UNPROTECT();
    if (eaccelerator_shm_max == 0 || size <= eaccelerator_shm_max) {
      MMCG(mem) = eaccelerator_malloc(size);
      if (MMCG(mem) == NULL) {
        MMCG(mem) = eaccelerator_malloc2(size TSRMLS_CC);
      }
    }
    if (MMCG(mem) == NULL) {
      EACCELERATOR_PROTECT();
    }
  }
  if (MMCG(mem) == NULL &&
      (where == eaccelerator_shm_and_disk ||
       where == eaccelerator_shm ||
       where == eaccelerator_disk_only)) {
    use_shm = 0;
    MMCG(mem) = emalloc(size);
  }
  if (MMCG(mem)) {
    zend_hash_init(&MMCG(strings), 0, NULL, NULL, 0);
    EACCELERATOR_ALIGN(MMCG(mem));
    q = (mm_user_cache_entry*)MMCG(mem);
    q->size = size;
    MMCG(mem) += offsetof(mm_user_cache_entry,key)+xlen+1;
    q->hv = hash_mm(xkey, xlen);;
    memcpy(q->key, xkey, xlen+1);
    memcpy(&q->value, val, sizeof(zval));
    q->ttl = ttl?time(0)+ttl:0;
    store_zval(&q->value TSRMLS_CC);
    zend_hash_destroy(&MMCG(strings));

    /* storing to file */
    if ((where == eaccelerator_shm_and_disk ||
         ((where == eaccelerator_shm) && !use_shm) ||
         where == eaccelerator_disk_only) &&
        eaccelerator_md5(s, "/eaccelerator-user-", q->key TSRMLS_CC)) {
      int f;
      unlink(s);
      f = open(s, O_CREAT | O_WRONLY | O_EXCL | O_BINARY, S_IRUSR | S_IWUSR);
      if (f > 0) {
        mm_file_header hdr;
        EACCELERATOR_FLOCK(f, LOCK_EX);
        strcpy(hdr.magic,"EACCELERATOR");
        hdr.eaccelerator_version = binary_eaccelerator_version;
        hdr.zend_version    = binary_zend_version;
        hdr.php_version     = binary_php_version;
        hdr.size  = q->size;
        hdr.mtime = q->ttl;
        q->next = q;
        hdr.crc32 = eaccelerator_crc32((const char*)q,q->size);
        if (write(f, &hdr, sizeof(hdr)) == sizeof(hdr)) {
          write(f, q, q->size);
          EACCELERATOR_FLOCK(f, LOCK_UN);
          close(f);
          ret = 1;
        } else {
          EACCELERATOR_FLOCK(f, LOCK_UN);
          close(f);
          unlink(s);
        }
      }
      if (!use_shm) {
        efree(q);
      }
    }

    if ((where == eaccelerator_shm_and_disk ||
         where == eaccelerator_shm ||
         where == eaccelerator_shm_only) && use_shm) {
      /* storing to shared memory */
      slot = q->hv & MM_USER_HASH_MAX;
      EACCELERATOR_LOCK_RW();
      eaccelerator_mm_instance->user_hash_cnt++;
      q->next = eaccelerator_mm_instance->user_hash[slot];
      eaccelerator_mm_instance->user_hash[slot] = q;
      p = q->next;
      while (p != NULL) {
        if ((p->hv == q->hv) && (strcmp(p->key, xkey) == 0)) {
          eaccelerator_mm_instance->user_hash_cnt--;
          q->next = p->next;
          eaccelerator_free_nolock(p);
          break;
        }
        q = p;
        p = p->next;
      }
      EACCELERATOR_UNLOCK_RW();
      EACCELERATOR_PROTECT();
      ret = 1;
    }
  }
  if (xlen != key_len) {efree(xkey);}
  return ret;
}

int eaccelerator_get(const char* key, int key_len, zval* return_value, eaccelerator_cache_place where  TSRMLS_DC) {
  unsigned int hv, slot;
  char s[MAXPATHLEN];
  int xlen;
  char* xkey;

  xkey = build_key(key, key_len, &xlen TSRMLS_CC);
  hv = hash_mm(xkey,xlen);
  slot = hv & MM_USER_HASH_MAX;

  if (eaccelerator_mm_instance != NULL &&
      (where == eaccelerator_shm_and_disk ||
       where == eaccelerator_shm ||
       where == eaccelerator_shm_only)) {
    mm_user_cache_entry *p, *q;
    mm_user_cache_entry *x = NULL;
    EACCELERATOR_UNPROTECT();
    EACCELERATOR_LOCK_RW();
    q = NULL;
    p = eaccelerator_mm_instance->user_hash[slot];
    while (p != NULL) {
      if ((p->hv == hv) && (strcmp(p->key, xkey) == 0)) {
        x = p;
        if (p->ttl != 0 && p->ttl < time(0)) {
          if (q == NULL) {
            eaccelerator_mm_instance->user_hash[slot] = p->next;
          } else {
            q->next = p->next;
          }
          eaccelerator_mm_instance->user_hash_cnt--;
          eaccelerator_free_nolock(x);
          x = NULL;
        }
        break;
      }
      q = p;
      p = p->next;
    }
    EACCELERATOR_UNLOCK_RW();
    EACCELERATOR_PROTECT();
    if (x) {
      memcpy(return_value, &x->value, sizeof(zval));
      restore_zval(return_value TSRMLS_CC);
      if (xlen != key_len) {efree(xkey);}
      return 1;
    }
  }

  /* key is not found in shared memory try to load it from file */
  if ((where == eaccelerator_shm_and_disk ||
       where == eaccelerator_shm ||
       where == eaccelerator_disk_only) &&
      eaccelerator_md5(s, "/eaccelerator-user-", xkey TSRMLS_CC)) {
    time_t t = time(0);
    int use_shm = 1;
    int ret = 0;
    int f;

    if ((f = open(s, O_RDONLY | O_BINARY)) > 0) {
      mm_file_header hdr;

      EACCELERATOR_FLOCK(f, LOCK_SH);
      if (read(f, &hdr, sizeof(hdr)) != sizeof(hdr) ||
          strncmp(hdr.magic,"EACCELERATOR",8) != 0 ||
          hdr.eaccelerator_version != binary_eaccelerator_version ||
          hdr.zend_version != binary_zend_version ||
          hdr.php_version != binary_php_version) {
        EACCELERATOR_FLOCK(f, LOCK_UN);
        close(f);
        unlink(s);
        if (xlen != key_len) {efree(xkey);}
        return 0;
      }
      if (hdr.mtime == 0 || hdr.mtime > t) {
        /* try to put it into shared memory */
        mm_user_cache_entry *p = NULL;
        if (eaccelerator_mm_instance != NULL &&
            (where == eaccelerator_shm_and_disk ||
             where == eaccelerator_shm)) {
          if (eaccelerator_shm_max == 0 || hdr.size <= eaccelerator_shm_max) {
            EACCELERATOR_UNPROTECT();
            p = eaccelerator_malloc(hdr.size);
            if (p == NULL) {
              p = eaccelerator_malloc2(hdr.size TSRMLS_CC);
            }
            if (p == NULL) {
              EACCELERATOR_PROTECT();
            }
          }
        }
        if (p == NULL) {
          p = emalloc(hdr.size);
          use_shm = 0;
        }
        if (p != NULL) {
          if (read(f, p, hdr.size) == hdr.size &&
              hdr.size == p->size &&
              hdr.crc32 == eaccelerator_crc32((const char*)p,p->size)) {
            MMCG(mem) = (char*)((long)p - (long)p->next);
            MMCG(compress) = 1;
            fixup_zval(&p->value TSRMLS_CC);

            if (strcmp(xkey,p->key) != 0) {
              if (use_shm) {
                eaccelerator_free(p);
              } else {
                efree(p);
              }
              EACCELERATOR_FLOCK(f, LOCK_UN);
              close(f);
              unlink(s);
              if (use_shm) EACCELERATOR_PROTECT();
              if (xlen != key_len) {efree(xkey);}
              return 0;
            }

            memcpy(return_value, &p->value, sizeof(zval));
            restore_zval(return_value TSRMLS_CC);
            ret = 1;
            if (use_shm) {
              /* put it into shared memory */
              mm_user_cache_entry *q,*prev;

              p->hv = hv;
              EACCELERATOR_LOCK_RW();
              p->next = eaccelerator_mm_instance->user_hash[slot];
              eaccelerator_mm_instance->user_hash[slot] = p;
              eaccelerator_mm_instance->user_hash_cnt++;
              prev = p;
              q = p->next;
              while (q != NULL) {
                if ((q->hv == hv) && (strcmp(q->key, xkey) == 0)) {
                  prev->next = q->next;
                  eaccelerator_mm_instance->user_hash_cnt--;
                  eaccelerator_free_nolock(q);
                  break;
                }
                prev = q;
                q = q->next;
              }
              EACCELERATOR_UNLOCK_RW();
            } else {
              efree(p);
            }
            EACCELERATOR_FLOCK(f, LOCK_UN);
            close(f);
          } else {
            if (use_shm) {
              eaccelerator_free(p);
            } else {
              efree(p);
            }
            EACCELERATOR_FLOCK(f, LOCK_UN);
            close(f);
            unlink(s);
          }
        }
        if (use_shm) EACCELERATOR_PROTECT();
      } else {
        EACCELERATOR_FLOCK(f, LOCK_UN);
        close(f);
        unlink(s);
      }
      if (xlen != key_len) {efree(xkey);}
      return ret;
    }
  }
  if (xlen != key_len) {efree(xkey);}
  return 0;
}

int eaccelerator_rm(const char* key, int key_len, eaccelerator_cache_place where  TSRMLS_DC) {
  unsigned int hv, slot;
  mm_user_cache_entry *p, *q;
  char s[MAXPATHLEN];
  int xlen;
  char* xkey;

  xkey = build_key(key, key_len, &xlen TSRMLS_CC);
  /* removing file */
  if ((where == eaccelerator_shm_and_disk ||
       where == eaccelerator_shm ||
       where == eaccelerator_disk_only) &&
      eaccelerator_md5(s, "/eaccelerator-user-", xkey TSRMLS_CC)) {
    unlink(s);
  }

  /* removing from shared memory */
  if (eaccelerator_mm_instance != NULL &&
      (where == eaccelerator_shm_and_disk ||
       where == eaccelerator_shm ||
       where == eaccelerator_shm_only)) {
    hv = hash_mm(xkey, xlen);
    slot = hv & MM_USER_HASH_MAX;

    EACCELERATOR_UNPROTECT();
    EACCELERATOR_LOCK_RW();
    q = NULL;
    p = eaccelerator_mm_instance->user_hash[slot];
    while (p != NULL) {
      if ((p->hv == hv) && (strcmp(p->key, xkey) == 0)) {
        if (q == NULL) {
          eaccelerator_mm_instance->user_hash[slot] = p->next;
        } else {
          q->next = p->next;
        }
        eaccelerator_mm_instance->user_hash_cnt--;
        eaccelerator_free_nolock(p);
        break;
      }
      q = p;
      p = p->next;
    }
    EACCELERATOR_UNLOCK_RW();
    EACCELERATOR_PROTECT();
  }
  if (xlen != key_len) {efree(xkey);}
  return 1;
}

size_t eaccelerator_gc(TSRMLS_D) {
  size_t size = 0;
  unsigned int i;
  time_t t = time(0);

  if (eaccelerator_mm_instance == NULL) {
    return 0;
  }
  EACCELERATOR_UNPROTECT();
  EACCELERATOR_LOCK_RW();
  for (i = 0; i < MM_USER_HASH_SIZE; i++) {
    mm_user_cache_entry** p = &eaccelerator_mm_instance->user_hash[i];
    while (*p != NULL) {
      if ((*p)->ttl != 0 && (*p)->ttl < t) {
        mm_user_cache_entry *r = *p;
        *p = (*p)->next;
        eaccelerator_mm_instance->user_hash_cnt--;
        size += r->size;
        eaccelerator_free_nolock(r);
      } else {
        p = &(*p)->next;
      }
    }
  }
  EACCELERATOR_UNLOCK_RW();
  EACCELERATOR_PROTECT();
  return size;
}

PHP_FUNCTION(eaccelerator_lock) {
  char *key;
  int  key_len;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
                          "s", &key, &key_len) == FAILURE) {
    return;
  }
  if (eaccelerator_lock(key, key_len TSRMLS_CC)) {
    RETURN_TRUE;
  } else {
    RETURN_FALSE;
  }
}

PHP_FUNCTION(eaccelerator_unlock) {
  char *key;
  int  key_len;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
                          "s", &key, &key_len) == FAILURE) {
    return;
  }
  if (eaccelerator_unlock(key, key_len TSRMLS_CC)) {
    RETURN_TRUE;
  } else {
    RETURN_FALSE;
  }
}

PHP_FUNCTION(eaccelerator_put) {
  char   *key;
  int    key_len;
  zval   *val;
  time_t ttl = 0;
  long   where = eaccelerator_keys_cache_place;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
                          "sz|ll", &key, &key_len, &val, &ttl, &where) == FAILURE) {
    return;
  }
  if (eaccelerator_put(key, key_len, val, ttl, where TSRMLS_CC)) {
    RETURN_TRUE;
  } else {
    RETURN_FALSE;
  }
}

PHP_FUNCTION(eaccelerator_get) {
  char *key;
  int  key_len;
  long where = eaccelerator_keys_cache_place;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
                          "s|l", &key, &key_len, &where) == FAILURE) {
    return;
  }
  if (eaccelerator_get(key, key_len, return_value, where TSRMLS_CC)) {
    return;
  } else {
    RETURN_NULL();
  }
}

PHP_FUNCTION(eaccelerator_rm) {
  char *key;
  int  key_len;
  long where = eaccelerator_keys_cache_place;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
                          "s|l", &key, &key_len, &where) == FAILURE) {
    return;
  }
  if (eaccelerator_rm(key, key_len, where TSRMLS_CC)) {
    RETURN_TRUE;
  } else {
    RETURN_FALSE;
  }
}

PHP_FUNCTION(eaccelerator_gc) {
  if(ZEND_NUM_ARGS() != 0) {
    WRONG_PARAM_COUNT;
  }
  eaccelerator_gc(TSRMLS_C);
  RETURN_TRUE;
}

#ifdef WITH_EACCELERATOR_SESSIONS

static int do_session_unlock(TSRMLS_D) {
  if (MMCG(session) != NULL) {
    eaccelerator_unlock(MMCG(session),strlen(MMCG(session)) TSRMLS_CC);
    efree(MMCG(session));
    MMCG(session) = NULL;
  }
  return 1;
}

static int do_session_lock(const char* sess_name TSRMLS_DC) {
  if (MMCG(session) != NULL) {
    if (strcmp(MMCG(session),sess_name) == 0) {
      return 1;
    } else {
      do_session_unlock(TSRMLS_C);
    }
  }
  if (eaccelerator_lock(sess_name, strlen(sess_name) TSRMLS_CC)) {
    MMCG(session) = estrdup(sess_name);
    return 1;
  } else {
    return 0;
  }
}

#ifdef HAVE_PHP_SESSIONS_SUPPORT

PS_OPEN_FUNC(eaccelerator) {
  if (eaccelerator_mm_instance == NULL) {
    return FAILURE;
  }
  PS_SET_MOD_DATA((void *)1);
  return SUCCESS;
}

PS_CLOSE_FUNC(eaccelerator) {
  if (eaccelerator_mm_instance == NULL) {
    return FAILURE;
  }
  do_session_unlock(TSRMLS_C);
  return SUCCESS;
}

PS_READ_FUNC(eaccelerator) {
  char *skey;
  int  len;
  zval ret;

  len = sizeof("sess_") + strlen(key);
  skey = do_alloca(len + 1);
  strcpy(skey,"sess_");
  strcat(skey,key);
  do_session_lock(skey TSRMLS_CC);
  if (eaccelerator_get(skey, len, &ret, eaccelerator_sessions_cache_place TSRMLS_CC) &&
      ret.type == IS_STRING) {
    *val = estrdup(ret.value.str.val);
    *vallen = ret.value.str.len;
    zval_dtor(&ret);
  } else {
    *val = emalloc(1);
    (*val)[0] = '\0';
    *vallen = 0;
  }
  free_alloca(skey);
  return SUCCESS;
}

PS_WRITE_FUNC(eaccelerator) {
  char *skey;
  int  len;
  char *tmp;
  time_t ttl;
  zval sval;

  len = sizeof("sess_") + strlen(key);
  skey = do_alloca(len + 1);
  strcpy(skey,"sess_");
  strcat(skey,key);
  if (cfg_get_string("session.gc_maxlifetime", &tmp)==FAILURE) {
    ttl = 1440;
  } else {
    ttl = atoi(tmp);
  }
  sval.type = IS_STRING;
  sval.value.str.val = (char*)val;
  sval.value.str.len = vallen;

  do_session_lock(skey TSRMLS_CC);
  if (eaccelerator_put(skey, len, &sval, ttl, eaccelerator_sessions_cache_place TSRMLS_CC)) {
    free_alloca(skey);
    return SUCCESS;
  } else {
    free_alloca(skey);
    return FAILURE;
  }
}

PS_DESTROY_FUNC(eaccelerator) {
  char *skey;
  int  len;

  len = sizeof("sess_") + strlen(key);
  skey = do_alloca(len + 1);
  strcpy(skey,"sess_");
  strcat(skey,key);
  if (eaccelerator_rm(skey, len, eaccelerator_sessions_cache_place TSRMLS_CC)) {
    free_alloca(skey);
    return SUCCESS;
  } else {
    free_alloca(skey);
    return FAILURE;
  }
}

PS_GC_FUNC(eaccelerator) {
  if (eaccelerator_mm_instance == NULL) {
    return FAILURE;
  }
  eaccelerator_gc(TSRMLS_C);
  return SUCCESS;
}

#ifdef PS_CREATE_SID_ARGS
PS_CREATE_SID_FUNC(eaccelerator) {
  static char hexconvtab[] = "0123456789abcdef";
  PHP_MD5_CTX context;
  unsigned char digest[16];
  char buf[256];
  struct timeval tv;
  int i;
  int j = 0;
  unsigned char c;

  long entropy_length;
  char *entropy_file;

  if (cfg_get_string("session.entropy_length", &entropy_file)==FAILURE) {
    entropy_length = 0;
  } else {
    entropy_length = atoi(entropy_file);
  }
  if (cfg_get_string("session.entropy_file", &entropy_file)==FAILURE) {
    entropy_file = empty_string;
  }

  gettimeofday(&tv, NULL);
  PHP_MD5Init(&context);

  sprintf(buf, "%ld%ld%0.8f", tv.tv_sec, tv.tv_usec, php_combined_lcg(TSRMLS_C) * 10);
  PHP_MD5Update(&context, buf, strlen(buf));

  if (entropy_length > 0) {
    int fd;

    fd = VCWD_OPEN(entropy_file, O_RDONLY);
    if (fd >= 0) {
      unsigned char buf[2048];
      int n;
      int to_read = entropy_length;

      while (to_read > 0) {
        n = read(fd, buf, MIN(to_read, sizeof(buf)));
        if (n <= 0) break;
        PHP_MD5Update(&context, buf, n);
        to_read -= n;
      }
      close(fd);
    }
  }

  PHP_MD5Final(digest, &context);

  for (i = 0; i < 16; i++) {
    c = digest[i];
    buf[j++] = hexconvtab[c >> 4];
    buf[j++] = hexconvtab[c & 15];
  }
  buf[j] = '\0';

  if (newlen)
    *newlen = j;
  return estrdup(buf);
}
#endif

static ps_module ps_mod_eaccelerator = {
#ifdef PS_CREATE_SID_ARGS
  PS_MOD_SID(eaccelerator)
#else
  PS_MOD(eaccelerator)
#endif
};

#else

PHP_FUNCTION(_eaccelerator_session_open) {
  if (eaccelerator_mm_instance == NULL) {
    RETURN_FALSE;
  }
  RETURN_TRUE;
}

PHP_FUNCTION(_eaccelerator_session_close) {
  if (eaccelerator_mm_instance == NULL) {
    RETURN_FALSE;
  }
  do_session_unlock(TSRMLS_C);
  RETURN_TRUE;
}

PHP_FUNCTION(_eaccelerator_session_read) {
  zval **arg_key;
  char *key;
  int  len;

  if (ZEND_NUM_ARGS() != 1 || zend_get_parameters_ex(1, &arg_key) == FAILURE) {
    WRONG_PARAM_COUNT;
  }
  len = sizeof("sess_") + (*arg_key)->value.str.len;
  key = do_alloca(len + 1);
  strcpy(key,"sess_");
  strcat(key,(*arg_key)->value.str.val);
  do_session_lock(key TSRMLS_CC);
  if (eaccelerator_get(key, len, return_value, eaccelerator_sessions_cache_place TSRMLS_CC)) {
    free_alloca(key);
    return;
  } else {
    free_alloca(key);
    RETURN_EMPTY_STRING();
  }
}

PHP_FUNCTION(_eaccelerator_session_write) {
  zval **arg_key, **arg_val;
  char *key;
  int  len;
  char *tmp;
  time_t ttl;

  if (ZEND_NUM_ARGS() != 2 || zend_get_parameters_ex(2, &arg_key, &arg_val) == FAILURE) {
    WRONG_PARAM_COUNT;
  }
  len = sizeof("sess_") + (*arg_key)->value.str.len;
  key = do_alloca(len + 1);
  strcpy(key,"sess_");
  strcat(key,(*arg_key)->value.str.val);
  if (cfg_get_string("session.gc_maxlifetime", &tmp)==FAILURE) {
    ttl = 1440;
  } else {
    ttl = atoi(tmp);
  }
  do_session_lock(key TSRMLS_CC);
  if (eaccelerator_put(key, len, *arg_val, ttl, eaccelerator_sessions_cache_place TSRMLS_CC)) {
    free_alloca(key);
    RETURN_TRUE;
  } else {
    free_alloca(key);
    RETURN_FALSE;
  }
}

PHP_FUNCTION(_eaccelerator_session_destroy) {
  zval **arg_key;
  char *key;
  int  len;

  if (ZEND_NUM_ARGS() != 1 || zend_get_parameters_ex(1, &arg_key) == FAILURE) {
    WRONG_PARAM_COUNT;
  }
  len = sizeof("sess_") + (*arg_key)->value.str.len;
  key = do_alloca(len + 1);
  strcpy(key,"sess_");
  strcat(key,(*arg_key)->value.str.val);
  if (eaccelerator_rm(key, len, eaccelerator_sessions_cache_place TSRMLS_CC)) {
    free_alloca(key);
    RETURN_TRUE;
  } else {
    free_alloca(key);
    RETURN_FALSE;
  }
}

PHP_FUNCTION(_eaccelerator_session_gc) {
  if (eaccelerator_mm_instance == NULL) {
    RETURN_FALSE;
  }
  eaccelerator_gc(TSRMLS_C);
  RETURN_TRUE;
}
#endif /* HAVE_PHP_SESSIONS_SUPPORT */


static int eaccelerator_set_session_handlers(TSRMLS_D) {
  zval func;
  zval retval;
  int ret = 1;
#ifdef HAVE_PHP_SESSIONS_SUPPORT
  zval param;
  zval *params[1];
/*
  if (php_session_register_module(&ps_mod_eaccelerator) != 0) {
    return 0;
  }
*/
  if (eaccelerator_sessions_cache_place == eaccelerator_none) {
    return 0;
  }
  ZVAL_STRING(&func, "session_module_name", 0);
  INIT_ZVAL(param);
  params[0] = &param;
  ZVAL_STRING(params[0], "eaccelerator", 0);
  if (call_user_function(EG(function_table), NULL, &func, &retval,
        1, params TSRMLS_CC) == FAILURE) {
    ret = 0;
  }
  zval_dtor(&retval);
  return ret;
#else
  zval *params[6];
  int i;

  if (eaccelerator_sessions_cache_place == eaccelerator_none) {
    return 0;
  }
  if (eaccelerator_mm_instance == NULL) {
    return 0;
  }
  if (!zend_hash_exists(EG(function_table), "session_set_save_handler", sizeof("session_set_save_handler"))) {
    return 0;
  }

  ZVAL_STRING(&func, "session_set_save_handler", 0);
  MAKE_STD_ZVAL(params[0]);
  ZVAL_STRING(params[0], "_eaccelerator_session_open", 1);
  MAKE_STD_ZVAL(params[1]);
  ZVAL_STRING(params[1], "_eaccelerator_session_close", 1);
  MAKE_STD_ZVAL(params[2]);
  ZVAL_STRING(params[2], "_eaccelerator_session_read", 1);
  MAKE_STD_ZVAL(params[3]);
  ZVAL_STRING(params[3], "_eaccelerator_session_write", 1);
  MAKE_STD_ZVAL(params[4]);
  ZVAL_STRING(params[4], "_eaccelerator_session_destroy", 1);
  MAKE_STD_ZVAL(params[5]);
  ZVAL_STRING(params[5], "_eaccelerator_session_gc", 1);
  if (call_user_function(EG(function_table), NULL, &func, &retval,
        6, params TSRMLS_CC) == FAILURE) {
    ret = 0;
  }
  zval_dtor(&retval);
  for (i = 0; i < 6; i++) zval_ptr_dtor(&params[i]);
  return ret;
#endif
}

PHP_FUNCTION(eaccelerator_set_session_handlers) {
  if (eaccelerator_set_session_handlers(TSRMLS_C)) {
    RETURN_TRUE;
  } else {
    RETURN_FALSE;
  }
}

#endif

#ifdef WITH_EACCELERATOR_CRASH
PHP_FUNCTION(eaccelerator_crash) {
  char *x = NULL;
  strcpy(x,"Hello");
}
#endif

PHP_FUNCTION(eaccelerator);

/******************************************************************************/
/*
 * Begin of dynamic loadable module interfaces.
 * There are two interfaces:
 *  - standard php module,
 *  - zend extension.
 */
PHP_INI_MH(eaccelerator_filter) {
  mm_cond_entry *p, *q;
  char *s = new_value;
  char *ss;
  int  not;
  for (p = MMCG(cond_list); p != NULL; p = q) {
    q = p->next;
    if (p->str) {
      free(p->str);
    }
    free(p);
  }
  MMCG(cond_list) = NULL;
  while (*s) {
    for (; *s == ' ' || *s == '\t'; s++)
      ;
    if (*s == 0)
      break;
    if (*s == '!') {
      s++;
      not = 1;
    } else {
      not = 0;
    }
    ss = s;
    for (; *s && *s != ' ' && *s != '\t'; s++)
      ;
    if ((s > ss) && *ss) {
      p = (mm_cond_entry *)malloc(sizeof(mm_cond_entry));
      if (p == NULL)
        break;
      p->not = not;
      p->len = s-ss;
      p->str = malloc(p->len+1);
      memcpy(p->str, ss, p->len);
      p->str[p->len] = 0;
      p->next = MMCG(cond_list);
      MMCG(cond_list) = p;
    }
  }
  return SUCCESS;
}

static PHP_INI_MH(eaccelerator_OnUpdateLong) {
  long *p = (long*)mh_arg1;
  *p = zend_atoi(new_value, new_value_length);
  return SUCCESS;
}

static PHP_INI_MH(eaccelerator_OnUpdateBool) {
  zend_bool *p = (zend_bool*)mh_arg1;
  if (strncasecmp("on", new_value, sizeof("on"))) {
    *p = (zend_bool) atoi(new_value);
  } else {
    *p = (zend_bool) 1;
  }
  return SUCCESS;
}

static PHP_INI_MH(eaccelerator_OnUpdateCachePlace) {
  eaccelerator_cache_place *p = (eaccelerator_cache_place*)mh_arg1;
  if (strncasecmp("shm_and_disk", new_value, sizeof("shm_and_disk")) == 0) {
    *p = eaccelerator_shm_and_disk;
  } else if (strncasecmp("shm", new_value, sizeof("shm")) == 0) {
    *p = eaccelerator_shm;
  } else if (strncasecmp("shm_only", new_value, sizeof("shm_only")) == 0) {
    *p = eaccelerator_shm_only;
  } else if (strncasecmp("disk_only", new_value, sizeof("disk_only")) == 0) {
    *p = eaccelerator_disk_only;
  } else if (strncasecmp("none", new_value, sizeof("none")) == 0) {
    *p = eaccelerator_none;
  }
  return SUCCESS;
}

#ifndef ZEND_ENGINE_2
#define OnUpdateLong OnUpdateInt
#endif

PHP_INI_BEGIN()
STD_PHP_INI_ENTRY("eaccelerator.enable",         "1", PHP_INI_ALL, OnUpdateBool, enabled, zend_eaccelerator_globals, eaccelerator_globals)
STD_PHP_INI_ENTRY("eaccelerator.optimizer",      "1", PHP_INI_ALL, OnUpdateBool, optimizer_enabled, zend_eaccelerator_globals, eaccelerator_globals)
STD_PHP_INI_ENTRY("eaccelerator.compress",       "1", PHP_INI_ALL, OnUpdateBool, compression_enabled, zend_eaccelerator_globals, eaccelerator_globals)
STD_PHP_INI_ENTRY("eaccelerator.compress_level", "9", PHP_INI_ALL, OnUpdateLong, compress_level, zend_eaccelerator_globals, eaccelerator_globals)                  
ZEND_INI_ENTRY1("eaccelerator.shm_size",         "0", PHP_INI_SYSTEM, eaccelerator_OnUpdateLong, &eaccelerator_shm_size)
ZEND_INI_ENTRY1("eaccelerator.shm_max",          "0", PHP_INI_SYSTEM, eaccelerator_OnUpdateLong, &eaccelerator_shm_max)
ZEND_INI_ENTRY1("eaccelerator.shm_ttl",          "0", PHP_INI_SYSTEM, eaccelerator_OnUpdateLong, &eaccelerator_shm_ttl)
ZEND_INI_ENTRY1("eaccelerator.shm_prune_period", "0", PHP_INI_SYSTEM, eaccelerator_OnUpdateLong, &eaccelerator_shm_prune_period)
ZEND_INI_ENTRY1("eaccelerator.debug",            "0", PHP_INI_SYSTEM, eaccelerator_OnUpdateLong, &eaccelerator_debug)
ZEND_INI_ENTRY1("eaccelerator.check_mtime",      "1", PHP_INI_SYSTEM, eaccelerator_OnUpdateBool, &eaccelerator_check_mtime)
ZEND_INI_ENTRY1("eaccelerator.shm_only",         "0", PHP_INI_SYSTEM, eaccelerator_OnUpdateBool, &eaccelerator_scripts_shm_only)
ZEND_INI_ENTRY1("eaccelerator.keys",             "shm_and_disk", PHP_INI_SYSTEM, eaccelerator_OnUpdateCachePlace, &eaccelerator_keys_cache_place)
ZEND_INI_ENTRY1("eaccelerator.sessions",         "shm_and_disk", PHP_INI_SYSTEM, eaccelerator_OnUpdateCachePlace, &eaccelerator_sessions_cache_place)
ZEND_INI_ENTRY1("eaccelerator.content",          "shm_and_disk", PHP_INI_SYSTEM, eaccelerator_OnUpdateCachePlace, &eaccelerator_content_cache_place)
STD_PHP_INI_ENTRY("eaccelerator.cache_dir",      "/tmp/eaccelerator", PHP_INI_SYSTEM, OnUpdateString,
                  cache_dir, zend_eaccelerator_globals, eaccelerator_globals)
PHP_INI_ENTRY("eaccelerator.filter",             "",  PHP_INI_ALL, eaccelerator_filter)
PHP_INI_END()

static void eaccelerator_clean_request(TSRMLS_D) {
  mm_used_entry  *p = (mm_used_entry*)MMCG(used_entries);
  if (eaccelerator_mm_instance != NULL) {
    EACCELERATOR_UNPROTECT();
    mm_unlock(eaccelerator_mm_instance->mm);
    if (p != NULL || eaccelerator_mm_instance->locks != NULL) {
      EACCELERATOR_LOCK_RW();
      while (p != NULL) {
        p->entry->use_cnt--;
        if (p->entry->removed && p->entry->use_cnt <= 0) {
          if (eaccelerator_mm_instance->removed == p->entry) {
            eaccelerator_mm_instance->removed = p->entry->next;
            eaccelerator_mm_instance->rem_cnt--;
            eaccelerator_free_nolock(p->entry);
            p->entry = NULL;
          } else {
            mm_cache_entry *q = eaccelerator_mm_instance->removed;
            while (q != NULL && q->next != p->entry) {
              q = q->next;
            }
            if (q != NULL) {
              q->next = p->entry->next;
              eaccelerator_mm_instance->rem_cnt--;
              eaccelerator_free_nolock(p->entry);
              p->entry = NULL;
            }
          }
        }
        p = p->next;
      }
      if (eaccelerator_mm_instance->locks != NULL) {
        pid_t    pid    = getpid();
#ifdef ZTS
        THREAD_T thread = tsrm_thread_id();
#endif
        mm_lock_entry** p = &eaccelerator_mm_instance->locks;
        while ((*p) != NULL) {
#ifdef ZTS
          if ((*p)->pid == pid && (*p)->thread == thread) {
#else
          if ((*p)->pid == pid) {
#endif
            mm_lock_entry* x = *p;
            *p = (*p)->next;
            eaccelerator_free_nolock(x);
          } else {
            p = &(*p)->next;
          }
        }
      }
      EACCELERATOR_UNLOCK_RW();
    }
    EACCELERATOR_PROTECT();
    p = (mm_used_entry*)MMCG(used_entries);
    while (p != NULL) {
      mm_used_entry* r = p;
      p = p->next;
      if (r->entry != NULL && r->entry->use_cnt < 0) {
        efree(r->entry);
      }
      efree(r);
    }
  }
  MMCG(used_entries) = NULL;
  MMCG(in_request) = 0;
}

static void eaccelerator_clean_shutdown(void) {
  if (eaccelerator_mm_instance != NULL) {
    TSRMLS_FETCH();
    if (MMCG(in_request)) {
      fflush(stdout);
      fflush(stderr);
      eaccelerator_clean_request(TSRMLS_C);
      if (eaccelerator_debug > 0) {
        if (EG(active_op_array)) {
          fprintf(stderr, "[%d] EACCELERATOR: PHP unclean shutdown on opline %ld of %s() at %s:%u\n\n",
            getpid(),
            (long)(active_opline-EG(active_op_array)->opcodes),
            get_active_function_name(TSRMLS_C),
            zend_get_executed_filename(TSRMLS_C),
            zend_get_executed_lineno(TSRMLS_C));
        }  else {
          fprintf(stderr, "[%d] EACCELERATOR: PHP unclean shutdown\n\n",getpid());
        }
      }
    }
  }
}

#ifdef WITH_EACCELERATOR_CRASH_DETECTION
static void eaccelerator_crash_handler(int dummy) {
  TSRMLS_FETCH();
  fflush(stdout);
  fflush(stderr);
#ifdef SIGSEGV
  if (MMCG(original_sigsegv_handler) != eaccelerator_crash_handler) {
    signal(SIGSEGV, MMCG(original_sigsegv_handler));
  } else {
    signal(SIGSEGV, SIG_DFL);
  }
#endif
#ifdef SIGFPE
  if (MMCG(original_sigfpe_handler) != eaccelerator_crash_handler) {
    signal(SIGFPE, MMCG(original_sigfpe_handler));
  } else {
    signal(SIGFPE, SIG_DFL);
  }
#endif
#ifdef SIGBUS
  if (MMCG(original_sigbus_handler) != eaccelerator_crash_handler) {
    signal(SIGBUS, MMCG(original_sigbus_handler));
  } else {
    signal(SIGBUS, SIG_DFL);
  }
#endif
#ifdef SIGILL
  if (MMCG(original_sigill_handler) != eaccelerator_crash_handler) {
    signal(SIGILL, MMCG(original_sigill_handler));
  } else {
    signal(SIGILL, SIG_DFL);
  }
#endif
#ifdef SIGABRT
  if (MMCG(original_sigabrt_handler) != eaccelerator_crash_handler) {
    signal(SIGABRT, MMCG(original_sigabrt_handler));
  } else {
    signal(SIGABRT, SIG_DFL);
  }
#endif
  eaccelerator_clean_request(TSRMLS_C);
  if (EG(active_op_array)) {
    fprintf(stderr, "[%d] EACCELERATOR: PHP crashed on opline %ld of %s() at %s:%u\n\n",
      getpid(),
      (long)(active_opline-EG(active_op_array)->opcodes),
      get_active_function_name(TSRMLS_C),
      zend_get_executed_filename(TSRMLS_C),
      zend_get_executed_lineno(TSRMLS_C));
  } else {
    fprintf(stderr, "[%d] EACCELERATOR: PHP crashed\n\n",getpid());
  }
#if !defined(WIN32) && !defined(NETWARE)
  kill(getpid(), dummy);
#else
  raise(dummy);
#endif
}
#endif

static void eaccelerator_init_globals(zend_eaccelerator_globals *eaccelerator_globals)
{
  eaccelerator_globals->used_entries      = NULL;
  eaccelerator_globals->enabled           = 1;
  eaccelerator_globals->cache_dir         = NULL;
  eaccelerator_globals->optimizer_enabled = 1;
  eaccelerator_globals->compiler          = 0;
  eaccelerator_globals->encoder           = 0;
  eaccelerator_globals->cond_list         = NULL;
  eaccelerator_globals->content_headers   = NULL;
#ifdef WITH_EACCELERATOR_SESSIONS
  eaccelerator_globals->session           = NULL;
#endif
  eaccelerator_globals->hostname[0]       = '\000';
  eaccelerator_globals->in_request        = 0;
}

static void eaccelerator_globals_dtor(zend_eaccelerator_globals *eaccelerator_globals)
{
  mm_cond_entry *p, *q;

  for (p = eaccelerator_globals->cond_list; p != NULL; p = q) {
    q = p->next;
    if (p->str) {
      free(p->str);
    }
    free(p);
  }
  eaccelerator_globals->cond_list = NULL;
}

static void register_eaccelerator_as_zend_extension();
static int eaccelerator_set_session_handlers();

static int eaccelerator_check_php_version(TSRMLS_D) {
  zval v;
  int ret = 0;
  if (zend_get_constant("PHP_VERSION", sizeof("PHP_VERSION")-1, &v TSRMLS_CC)) {
    if (Z_TYPE(v) == IS_STRING &&
        Z_STRLEN(v) == sizeof(PHP_VERSION)-1 &&
        strcmp(Z_STRVAL(v),PHP_VERSION) == 0) {
      ret = 1;
    } else {
      zend_error(E_CORE_WARNING,"[%s] This build of \"%s\" was compiled for PHP version %s. Rebuild it for your PHP version (%s) or download precompiled binaries.\n", EACCELERATOR_EXTENSION_NAME,EACCELERATOR_EXTENSION_NAME,PHP_VERSION,Z_STRVAL(v));
    }
    zval_dtor(&v);
  } else {
    zend_error(E_CORE_WARNING,"[%s] This build of \"%s\" was compiled for PHP version %s. Rebuild it for your PHP version.\n", EACCELERATOR_EXTENSION_NAME,EACCELERATOR_EXTENSION_NAME,PHP_VERSION);
  }
  return ret;
}

PHP_MINIT_FUNCTION(eaccelerator) {
  if (type == MODULE_PERSISTENT) {
#ifndef ZEND_WIN32
    if (strcmp(sapi_module.name,"apache") == 0) {
      /* Is the parent process - init */
/*
      sleep(1);
      if (getppid() != 1) {
*/
      if (getpid() != getpgrp()) {
        return SUCCESS;
      }
    }
#endif
#ifdef WITH_EACCELERATOR_LOADER
    if (zend_hash_exists(&module_registry, EACCELERATOR_LOADER_EXTENSION_NAME, sizeof(EACCELERATOR_LOADER_EXTENSION_NAME))) {
      zend_error(E_CORE_WARNING,"Extension \"%s\" is not need with \"%s\". Remove it from php.ini\n", EACCELERATOR_LOADER_EXTENSION_NAME, EACCELERATOR_EXTENSION_NAME);
      zend_hash_del(&module_registry, EACCELERATOR_LOADER_EXTENSION_NAME, sizeof(EACCELERATOR_LOADER_EXTENSION_NAME));
    }
#endif
  }
  if (!eaccelerator_check_php_version(TSRMLS_C)) {
    return FAILURE;
  }
/*??? FIXME
  ZEND_INIT_MODULE_GLOBALS(eaccelerator, eaccelerator_init_globals, eaccelerator_globals_dtor);
*/
  ZEND_INIT_MODULE_GLOBALS(eaccelerator, eaccelerator_init_globals, NULL);
  REGISTER_INI_ENTRIES();
  REGISTER_STRING_CONSTANT("EACCELERATOR_VERSION", EACCELERATOR_VERSION, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("EACCELERATOR_SHM_AND_DISK", eaccelerator_shm_and_disk, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("EACCELERATOR_SHM", eaccelerator_shm, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("EACCELERATOR_SHM_ONLY", eaccelerator_shm_only, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("EACCELERATOR_DISK_ONLY", eaccelerator_disk_only, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("EACCELERATOR_NONE", eaccelerator_none, CONST_CS | CONST_PERSISTENT);
  binary_eaccelerator_version = encode_version(EACCELERATOR_VERSION);
  binary_php_version = encode_version(PHP_VERSION);
  binary_zend_version = encode_version(ZEND_VERSION);
  eaccelerator_is_extension = 1;
  if (type == MODULE_PERSISTENT &&
      strcmp(sapi_module.name, "cgi") != 0 &&
      strcmp(sapi_module.name, "cli") != 0) {
#if defined(DEBUG) || defined(TEST_PERFORMANCE) || defined(PROFILE_OPCODES)
    F_fp = fopen(DEBUG_LOGFILE, "a");
    if (!F_fp) {
      F_fp = fopen(DEBUG_LOGFILE_CGI, "a");
      if (!F_fp) {
        fprintf(stderr, "Cann't open log file '%s'.", DEBUG_LOGFILE);
      }
      chmod(DEBUG_LOGFILE_CGI, 0777);
    }
    fputs("\n=======================================\n", F_fp);
    fprintf(F_fp, "[%d] EACCELERATOR STARTED\n", getpid());
    fputs("=======================================\n", F_fp);
    fflush(F_fp);
#endif

    if (init_mm(TSRMLS_C) == FAILURE) {
      zend_error(E_CORE_WARNING,"[%s] Can not create shared memory area\n", EACCELERATOR_EXTENSION_NAME);
    }

    mm_saved_zend_compile_file = zend_compile_file;
#ifdef PROFILE_OPCODES
    zend_compile_file = profile_compile_file;
    mm_saved_zend_execute = zend_execute;
    zend_execute = profile_execute;
#else
    zend_compile_file = eaccelerator_compile_file;
#ifdef WITH_EACCELERATOR_EXECUTOR
    mm_saved_zend_execute = zend_execute;
    zend_execute = eaccelerator_execute;
#endif
#endif
    atexit(eaccelerator_clean_shutdown);
  }
#if defined(WITH_EACCELERATOR_SESSIONS) && defined(HAVE_PHP_SESSIONS_SUPPORT)
    if (eaccelerator_sessions_cache_place != eaccelerator_none &&
        eaccelerator_sessions_registered == 0) {
      php_session_register_module(&ps_mod_eaccelerator);
      eaccelerator_sessions_registered = 1;
    }
#endif
#ifdef WITH_EACCELERATOR_CONTENT_CACHING
    eaccelerator_content_cache_startup();
#endif
  if (!eaccelerator_is_zend_extension) {
    register_eaccelerator_as_zend_extension();
  }
  return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(eaccelerator) {
  if (eaccelerator_mm_instance == NULL || !eaccelerator_is_extension) {
    return SUCCESS;
  }
  zend_compile_file = mm_saved_zend_compile_file;
#if defined(PROFILE_OPCODES) || defined(WITH_EACCELERATOR_EXECUTOR)
  zend_execute = mm_saved_zend_execute;
#endif
#ifdef WITH_EACCELERATOR_CONTENT_CACHING
  eaccelerator_content_cache_shutdown();
#endif
  shutdown_mm(TSRMLS_C);
#if defined(DEBUG) || defined(TEST_PERFORMANCE) || defined(PROFILE_OPCODES)
  fputs("========================================\n", F_fp);
  fprintf(F_fp, "[%d] EACCELERATOR STOPPED\n", getpid());
  fputs("========================================\n\n", F_fp);
  fclose(F_fp);
  F_fp = NULL;
#endif
  UNREGISTER_INI_ENTRIES();
#ifndef ZTS
  eaccelerator_globals_dtor(&eaccelerator_globals TSRMLS_CC);
#endif
  eaccelerator_is_zend_extension = 0;
  eaccelerator_is_extension = 0;
  return SUCCESS;
}

PHP_RINIT_FUNCTION(eaccelerator)
{
	if (eaccelerator_mm_instance == NULL)
	{
		return SUCCESS;
	}
	/*
	 * HOESH: Initialization on first call,
	 * came from eaccelerator_zend_startup().
	 */
	if (eaccelerator_global_function_table.nTableSize == 0)
	{
		zend_function tmp_func;
		zend_class_entry tmp_class;

		// Don't need this, as the context given by function argument.
		// TSRMLS_FETCH();

		zend_hash_init_ex(&eaccelerator_global_function_table, 100, NULL, NULL, 1, 0);
		zend_hash_copy(&eaccelerator_global_function_table, CG(function_table), NULL, &tmp_func, sizeof(zend_function));
		
		zend_hash_init_ex(&eaccelerator_global_class_table, 10, NULL, NULL, 1, 0);
		zend_hash_copy(&eaccelerator_global_class_table, CG(class_table), NULL, &tmp_class, sizeof(zend_class_entry));
	}
#if defined(DEBUG)
	fprintf(F_fp, "[%d] Enter RINIT\n",getpid()); fflush(F_fp);
#endif
#ifdef PROFILE_OPCODES
	fputs("\n========================================\n", F_fp);
	fflush(F_fp);
#endif
	MMCG(in_request) = 1;
	MMCG(used_entries) = NULL;
	MMCG(compiler) = 0;
	MMCG(encoder) = 0;
	MMCG(refcount_helper) = 1;
	MMCG(compress_content) = 1;
	MMCG(content_headers) = NULL;
	/* Storing Host Name */
	MMCG(hostname)[0] = '\000';
	{
		zval  **server_vars, **hostname;

		if (zend_hash_find(&EG(symbol_table), "_SERVER", sizeof("_SERVER"), (void **) &server_vars) == SUCCESS &&
			Z_TYPE_PP(server_vars) == IS_ARRAY &&
			zend_hash_find(Z_ARRVAL_PP(server_vars), "SERVER_NAME", sizeof("SERVER_NAME"), (void **) &hostname)==SUCCESS &&
			Z_TYPE_PP(hostname) == IS_STRING &&
			Z_STRLEN_PP(hostname) > 0)
		{
			if (sizeof(MMCG(hostname)) > Z_STRLEN_PP(hostname))
			{
				memcpy(MMCG(hostname),Z_STRVAL_PP(hostname),Z_STRLEN_PP(hostname)+1);
			}
			else
			{
				memcpy(MMCG(hostname),Z_STRVAL_PP(hostname),sizeof(MMCG(hostname))-1);
				MMCG(hostname)[sizeof(MMCG(hostname))-1] = '\000';
			}
		}
	}
#if defined(DEBUG)
	fprintf(F_fp, "[%d] Leave RINIT\n",getpid()); fflush(F_fp);
#endif
#if defined(DEBUG) || defined(TEST_PERFORMANCE)  || defined(PROFILE_OPCODES)
	MMCG(xpad) = 0;
#endif
#ifdef PROFILE_OPCODES
	MMCG(profile_level) = 0;
#endif
#ifdef WITH_EACCELERATOR_CRASH_DETECTION
#ifdef SIGSEGV
	MMCG(original_sigsegv_handler) = signal(SIGSEGV, eaccelerator_crash_handler);
#endif
#ifdef SIGFPE
	MMCG(original_sigfpe_handler) = signal(SIGFPE, eaccelerator_crash_handler);
#endif
#ifdef SIGBUS
	MMCG(original_sigbus_handler) = signal(SIGBUS, eaccelerator_crash_handler);
#endif
#ifdef SIGILL
	MMCG(original_sigill_handler) = signal(SIGILL, eaccelerator_crash_handler);
#endif
#ifdef SIGABRT
	MMCG(original_sigabrt_handler) = signal(SIGABRT, eaccelerator_crash_handler);
#endif
#endif
	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(eaccelerator)
{
	if (eaccelerator_mm_instance == NULL)
	{
		return SUCCESS;
	}
#ifdef WITH_EACCELERATOR_CRASH_DETECTION
#ifdef SIGSEGV
	if (MMCG(original_sigsegv_handler) != eaccelerator_crash_handler)
	{
		signal(SIGSEGV, MMCG(original_sigsegv_handler));
	}
	else
	{
		signal(SIGSEGV, SIG_DFL);
	}
#endif
#ifdef SIGFPE
	if (MMCG(original_sigfpe_handler) != eaccelerator_crash_handler)
	{
		signal(SIGFPE, MMCG(original_sigfpe_handler));
	}
	else
	{
		signal(SIGFPE, SIG_DFL);
	}
#endif
#ifdef SIGBUS
	if (MMCG(original_sigbus_handler) != eaccelerator_crash_handler)
	{
		signal(SIGBUS, MMCG(original_sigbus_handler));
	}
	else
	{
		signal(SIGBUS, SIG_DFL);
	}
#endif
#ifdef SIGILL
	if (MMCG(original_sigill_handler) != eaccelerator_crash_handler)
	{
		signal(SIGILL, MMCG(original_sigill_handler));
	}
	else
	{
		signal(SIGILL, SIG_DFL);
	}
#endif
#ifdef SIGABRT
	if (MMCG(original_sigabrt_handler) != eaccelerator_crash_handler)
	{
		signal(SIGABRT, MMCG(original_sigabrt_handler));
	}
	else
	{
		signal(SIGABRT, SIG_DFL);
	}
#endif
#endif
#if defined(DEBUG)
	fprintf(F_fp, "[%d] Enter RSHUTDOWN\n",getpid()); fflush(F_fp);
#endif
	eaccelerator_clean_request(TSRMLS_C);
#if defined(DEBUG)
	fprintf(F_fp, "[%d] Leave RSHUTDOWN\n",getpid()); fflush(F_fp);
#endif
	return SUCCESS;
}

#ifdef ZEND_ENGINE_2
ZEND_BEGIN_ARG_INFO(eaccelerator_second_arg_force_ref, 0)
  ZEND_ARG_PASS_INFO(0)
  ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();
#else
static unsigned char eaccelerator_second_arg_force_ref[] = {2, BYREF_NONE, BYREF_FORCE};
#endif

function_entry eaccelerator_functions[] = {
  PHP_FE(eaccelerator, NULL)
  PHP_FE(eaccelerator_put, NULL)
  PHP_FE(eaccelerator_get, NULL)
  PHP_FE(eaccelerator_rm, NULL)
  PHP_FE(eaccelerator_gc, NULL)
  PHP_FE(eaccelerator_lock, NULL)
  PHP_FE(eaccelerator_unlock, NULL)
#ifdef WITH_EACCELERATOR_ENCODER
  PHP_FE(eaccelerator_encode, eaccelerator_second_arg_force_ref)
#endif
#ifdef WITH_EACCELERATOR_LOADER
  PHP_FE(eaccelerator_load, NULL)
  PHP_FE(_eaccelerator_loader_file, NULL)
  PHP_FE(_eaccelerator_loader_line, NULL)
#endif
#ifdef WITH_EACCELERATOR_SESSIONS
#ifndef HAVE_PHP_SESSIONS_SUPPORT
  PHP_FE(_eaccelerator_session_open, NULL)
  PHP_FE(_eaccelerator_session_close, NULL)
  PHP_FE(_eaccelerator_session_read, NULL)
  PHP_FE(_eaccelerator_session_write, NULL)
  PHP_FE(_eaccelerator_session_destroy, NULL)
  PHP_FE(_eaccelerator_session_gc, NULL)
#endif
  PHP_FE(eaccelerator_set_session_handlers, NULL)
#endif
#ifdef WITH_EACCELERATOR_CONTENT_CACHING
  PHP_FE(_eaccelerator_output_handler, NULL)
  PHP_FE(eaccelerator_cache_page, NULL)
  PHP_FE(eaccelerator_rm_page, NULL)
  PHP_FE(eaccelerator_cache_output, NULL)
  PHP_FE(eaccelerator_cache_result, NULL)
#endif
#ifdef WITH_EACCELERATOR_CRASH
  PHP_FE(eaccelerator_crash, NULL)
#endif
  {NULL, NULL, NULL}
};

zend_module_entry eaccelerator_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
  STANDARD_MODULE_HEADER,
#endif
  EACCELERATOR_EXTENSION_NAME,
  eaccelerator_functions,
  PHP_MINIT(eaccelerator),
  PHP_MSHUTDOWN(eaccelerator),
  PHP_RINIT(eaccelerator),
  PHP_RSHUTDOWN(eaccelerator),
  PHP_MINFO(eaccelerator),
#if ZEND_MODULE_API_NO >= 20010901
  EACCELERATOR_VERSION,          /* extension version number (string) */
#endif
  STANDARD_MODULE_PROPERTIES
};

#if defined(COMPILE_DL_EACCELERATOR)
ZEND_GET_MODULE(eaccelerator)
#endif

static startup_func_t last_startup;
static zend_llist_element *eaccelerator_el;

#define EACCELERATOR_VERSION_GUID   "PHPE8EDA1B6-806A-4851-B1C8-A6B4712F44FB"
#define EACCELERATOR_LOGO_GUID      "PHPE6F78DE9-13E4-4dee-8518-5FA2DACEA803"

#define EACCELERATOR_VERSION_STRING ("eAccelerator " EACCELERATOR_VERSION " (PHP " PHP_VERSION ")")

static const unsigned char eaccelerator_logo[] = {
      71, 73, 70, 56, 57, 97, 88,  0, 31,  0,247,  0,  0,255,255,255,141,
     144,192,219,  2, 17,125,128,172,  0,  0,  0,194,196,216,245,188,192,
     229, 69, 80,250,221,223,238,137,144,220,221,233,248,204,207,238,238,
     244,253,238,239, 43, 44, 54,233,103,112,162,165,204,160,162,194,154,
     156,185,172,173,192,134,136,178, 86, 88,109,156,159,201,224, 36, 49,
     241,154,160,174,176,194,229,230,238,151,153,189,134,137,183,152,155,
     198,213,213,222,246,247,249,118,121,161,171,173,208,233,233,233,251,
     251,251,137,139,186,212,213,227,140,143,190,226, 53, 65,140,143,177,
      97, 99,122,138,141,188,187,188,217,122,124,165,146,148,188,155,158,
     200,119,121,150,117,119,158,177,179,205,203,204,222,176,177,196,129,
     131,163, 76, 77, 95,142,145,183,164,165,183,236,120,128,145,146,151,
     166,168,197,148,151,196,254,254,254,169,169,171,151,154,198,142,145,
     188,186,187,211,149,152,197, 65, 66, 82,178,178,178,153,156,199,130,
     133,178,144,147,194, 54, 55, 68,140,141,164,186,187,205,154,157,199,
     221, 19, 33,142,145,193,167,169,206,209,209,209,135,138,184, 11, 11,
      14,108,110,136, 32, 33, 41,139,140,157, 22, 22, 27,147,150,195,231,
      86, 96,214,215,224,190,192,218,124,127,169,161,164,203,145,148,194,
     142,145,185,115,117,157,171,173,205,252,252,253,139,142,190,139,141,
     170,247,247,247,193,194,211,151,154,190,200,201,225,180,181,201,229,
     229,229,223,223,223,216,217,226,109,112,149,158,161,202,125,128,170,
     159,162,202,114,116,155,143,146,193,146,149,195,224,224,224,150,153,
     193,138,141,187,103,105,140,183,185,215,236,237,241,168,170,200,243,
     171,176,219,220,230,145,146,173,227,227,227,128,131,175,109,111,148,
     107,109,145,190,190,190,133,136,181,195,196,222,100,102,136,224,225,
     238,161,161,162,178,180,200,242,242,242,174,176,210,222,222,234,168,
     171,207,184,186,213,234,234,239,180,182,209,110,112,150,192,194,220,
     245,245,245,137,139,178,151,154,197,165,167,201,241,241,247,207,207,
     207,141,143,183,105,107,143,163,164,185,186,186,189,158,160,201,229,
     229,240,150,153,179,144,145,165,247,247,250,112,115,153,235,236,241,
     224,224,231,217,217,228,151,153,188,201,202,218,127,129,172,207,208,
     220,124,126,154,160,161,188,156,156,156,152,152,152,245,245,248,148,
     150,177,156,158,200,111,114,151,210,210,223,157,160,201,163,165,204,
     132,135,180,236,236,236,149,149,154,175,176,200,166,167,188,132,134,
     179,190,191,210,223,224,230,149,152,187,155,158,198,144,146,181,165,
     167,199,198,200,222,143,144,170,224,225,232,177,179,199,179,181,207,
     153,153,157,234,234,243,235,235,244,237,194,197,252,252,254,160,162,
     202,158,160,189,168,169,189,211,211,225,122,124,142,149,151,170,207,
     208,227,167,168,190,154,156,193,167,169,193,168,170,199,170,172,194,
     171,172,198,192,192,192,199,200,214,183,183,183,254,254,255,214,215,
     232,106,108,142,173,175,200,201,203,223,197,199,223,206,207,220,210,
     211,230,187,189,212,189,190,213,129,130,150,198,199,202,159,161,195,
     150,153,197,181,183,213,176,178,208,176,178,212,181,182,207,232,174,
     178,124,126,146,163,164,188,212,213,228,144,145,171,134,136,165,148,
     149,172,132,134,168,239,239,244,129,132,176,162,163,193,139,142,189,
     162,164,204,155,156,171,166,168,204,147,149,177,108,110,146,206,206,
     218,135,137,172,145,147,176,126,128,159,245,245,247,192,193,218, 33,
     249,  4,  0,  0,  0,  0,  0, 44,  0,  0,  0,  0, 88,  0, 31,  0, 64,
       8,255,  0,  3,  8, 28, 72,176,160,193,131,  8, 19, 42, 92,200,144,
     225,191, 60,125,104,  0, 80,  5, 76, 77,171, 80,165,  0,116,  1, 96,
      39,137,178, 81,  0, 38,100,168,181, 17, 88,  6,  0,206,  0,204,200,
     144,161, 21,128, 62,125,186,149,202, 50,  7,145,171, 12, 87,244, 73,
       3, 16,207,149, 30, 51, 94, 10, 90,112, 64,192, 65,141,162, 22, 42,
      16,160, 82,  3, 10,  1, 20, 16,  8, 16, 16,114,132, 64,  5, 31,  2,
     199,209, 58, 36, 16,128,215,175, 96,195,138, 29, 75,182,236,216, 47,
     143,246,136, 75,118,234,  5, 10, 56, 10,141,160,120, 65,134,137, 64,
      20, 83,132,253,137, 35,134,  7,128,174, 10,  6, 40,240, 58,  0,  8,
     128,192, 50,  2,151,144, 81, 24, 64,129,  1, 31,  0,  0,177,193, 32,
       2,133, 15, 27, 32,123, 53, 32,224,129,128,  4, 23, 14, 28,  0,144,
     160,179,104,207,  9,188, 30, 16, 32,246,  1,134,  5,  2, 14, 32,136,
     221,192, 10, 14,  0,  2, 46, 52, 56,193, 26,195,103,216, 15,254,  6,
      48, 75,188,184,241,178,195, 86, 40, 65,184, 69,137, 11,184,  7, 89,
     173, 32, 22,118,160, 10, 91,242, 72,  4,  0,  3, 40, 11,  8, 16,164,
      56, 20,255,201, 82,132,  3, 27, 54,242, 88,192,128,193,130, 15, 31,
      14, 28,  6,176,  0,177,158,133,247, 46, 32,178,112, 48, 65, 98,  0,
     125, 16, 69,252,208,130, 40, 94, 56,194,213, 64, 62, 40, 69, 69, 20,
      66, 16, 16,197, 11,  4,212,208,  1,132, 53, 12, 85,212, 11, 82, 64,
     129,130, 17,  2,185,195,  9, 47,  2, 49,160,192,  6, 27, 40,160,193,
      99, 12,  0, 16,195,  0,  0, 68, 96,131,  2, 37, 80, 16,  1,  0, 27,
      80, 80,194, 99,  5, 32,182,162, 97,  0, 52, 96,128,  1, 13,  0,176,
     128,  1, 11,  0,128,192,143,155, 25,128,  0, 88, 71, 22,201,195,  8,
      98, 24, 34,203, 25,123,196,129,198,149,113,156, 33, 66, 58,197,136,
     241,133, 95, 99,117,117,220,152, 96,249,216,192,145, 66, 22,137,230,
       2, 69, 42,201, 36,145, 95,249,136,192,144, 94, 29,185, 36,  0,112,
     210,233,213,144,119,138, 73,230,159,128,134,117,205, 36,131,148, 81,
      71, 34,175,236, 64,208, 14,175, 36, 82, 71, 25,131, 76, 82, 86, 67,
     148, 86,106,208, 27, 85,116,208, 70, 34, 88,120, 19, 72, 36,  4, 85,
     225,195, 14, 46, 60, 23, 64, 84, 14, 88, 96, 65, 16,  2,  5, 49,170,
       5, 29,112,255, 72,206, 10,129,148,147,200, 64, 88,132, 66,135, 31,
     173,204,  0, 64, 40, 26,  0,128,207, 37, 33,209,161,138, 63,130,  0,
     112,133,  7, 87,108,100, 73, 59,  0, 16,187,236, 21,155,  0,144, 70,
     176,171,184, 17, 11, 34,159,120,176,200, 34,249, 36,145,135, 41,167,
     108,179,  5, 65,114, 77,165,132,133, 16, 72, 65,  0,  4,107,184, 11,
      65, 10,  4,164, 16,  4,170, 22,  8,244,  3, 51,209, 88,234,111,165,
     152,250,176, 70, 19,135,160, 51,141, 28,  2,105,  1,  5, 21,109,104,
     161, 69,  7, 76, 16,  1,  1, 17, 17, 79, 76,131,186, 16,180,  1,106,
       0,144,168,131,196, 60,247,  8,164, 64,  1, 36,147,204, 64,  1, 41,
     142, 44, 98,201, 37,124,117,114,142, 94,193,252,  1,201,129,122,245,
     164, 33,123, 56,129, 73, 15,246,188,195,207, 46, 92,252, 96,130,  9,
      63, 84,130, 66, 24, 72, 76,145,  3, 33,214, 56,113,198, 35, 95,128,
      37,230,  0,  5,120, 21,193,  6, 94,201,  8,192,  0, 50, 14, 32,216,
      99,193,126,245,152,215, 50,124,197,153,  0, 56, 52, 32, 64,108,164,
     173,141,  7, 30,107,167,  6,128,111, 39, 24,224, 89,112, 66,194, 89,
     167,155,184,201,255,102,  0, 88, 72,122,213, 21, 16, 44,122,181,162,
     215,134, 61,102,248,  0, 37, 96,230, 53,  5,193, 82, 13,128,  6,  3,
      96,141,167,  0,  6, 92, 32,  0,  6,162,181, 61,155,  0,179,201, 77,
     183,  1, 86,116,198,217,  3,176,201, 70,219,  5, 39,244, 13,192,106,
     115,127,214, 64,  2, 69,254,107,251,237,184, 51, 84,243,238,188,247,
     238,103,239,192,139,245,133, 38,142, 52,113,110, 67,112, 52,225,136,
      38,198,136,149,251,243,  3,189,177, 69, 16,172,192,114, 78, 32,216,
     112,115,140, 66, 74, 80,229,130, 66,107, 96, 81,198, 10,135,192, 50,
     144, 35, 46, 51,146,126, 39,128, 52,  3,128, 45,138,116,  2, 86, 17,
      96,181,  0, 86, 48,245,147, 96,194, 32, 95,125,  3,192, 51,114,  0,
       0, 59,192,209,139,229, 12,100,  7, 47,168, 64, 20,142,112,  4, 20,
      32,208, 42, 21,112, 64, 10,136, 64,132, 20, 40,208, 40,117,  9, 64,
       7, 24, 81,135,  3,  5,  0, 11, 95, 73,  2,  0,144,160,  6, 22,140,
      34, 13,160,248,197, 39,124,225,149,126,  0,192,  3, 30, 24,131, 27,
     148,  5,  0, 92, 36, 11,134, 87,112, 73, 62,186, 48,134, 52,116,  1,
      16,220,242, 22, 40,255,178, 81, 13, 59,152, 66, 15,183,232, 64, 65,
      20,198,176,120,189,171, 65, 41,112, 65, 85,104,112, 49, 33, 40,129,
      12, 72, 17, 72, 11, 46, 17, 20,129,200,  1, 25,158,104, 15, 41,246,
       1,140, 66,168,161, 17, 48, 40,132, 37,186,128, 12, 61,248,193, 12,
      51, 16,137, 39,180,225,  6, 16, 72, 96, 21,208,200,192,  4, 38, 64,
       3, 56,206,128,  6, 37, 36,129, 36, 10,177, 71, 65,184, 33, 23,208,
       8, 67, 52,202,144,137, 69, 69,193, 42, 62,192,162, 20,240,133,175,
     163,188, 32,  8,143,148,144, 64, 42,193,  9, 29, 12,196,  4, 96,160,
     135,  9,  4, 98,  2, 21, 60,129,  4, 96,216,206, 28,192, 80, 74, 21,
     168,128,  4,240, 33,129, 43,135,246,202, 39,192,231,148,182,124,130,
      10, 70,105,130, 57,216,146,  4, 76,200,212, 26, 66,160,  8, 69,232,
      98, 32, 22,170,  0, 20,160, 48, 65,165,164, 32, 65,245,138,138, 20,
     142, 66,  5, 26, 40,145, 99,225,216,132, 36,160,  7, 61,233,  5, 65,
       9,245,  8,129, 57,104,177, 14, 81,164,235,  8,223, 75, 72, 16,232,
     117, 21,130,192, 99, 25, 83, 56,197, 61,234,241,187,224,  1,143,  7,
      98, 16,129,206,110, 32,255,129, 22, 44,196,  8, 85, 96, 66, 37,194,
      48,133, 89, 12,193,  9, 34,232,  4,152,  4, 50, 54,175,217,160,  4,
     130,113, 12,100, 26, 58,128, 24, 76,142,  2, 94,139,168,228, 50, 83,
      54,222, 61,233, 17, 34, 64,  3, 37,254, 48,132, 30, 16,  2, 21,169,
      72, 41, 42,  8,209,131, 33, 80,131, 18,113,144,133,151,156, 55, 28,
       0,104, 77, 69,133,139,192,140, 54, 48,163, 15, 80,224, 14,148,171,
     154, 99, 24,  0,209, 59, 84,212,108,107,195,  0,  0, 52, 55,154,210,
     128,238,115,114,  3,  0, 14,214, 38,128, 19,  4,169, 56,108, 51, 75,
      87,130,234, 21, 27,204,104,107, 40, 27,128, 13, 48,218, 50,197,129,
     133,112,153,185,  3, 82, 67,195,153,208,180,237,  2,  9,  0,205,103,
     188, 82,186, 59,209, 78,170, 84, 77,141,103,214,118, 27,  1, 44, 97,
     109,163,217,205,218,150,176,164,174,128,141, 48, 15,205, 12,  3, 24,
     163,  0,159, 82, 64,162, 49,120,204,140,188, 42, 81,161,114,198, 55,
     166,105,234,230,146, 58, 87,  0,212,181, 71, 86,192,131, 84,151,240,
     215,  3,112, 38,170,174,131,157, 83,  1, 43,156,201, 13,230, 43, 55,
      10,155,136,188, 34,162, 20, 50,125, 64,  6, 50,136,204, 97, 82,116,
     152,215,114,198,110,160,235, 92,105,150,122,  1,220, 68,117,170,107,
     179, 42,  2,254,234,215,214,237,117,174,108,131,157, 96,147,218, 90,
     123, 90,247,186, 82, 11,  8,  0, 59};

static int eaccelerator_last_startup(zend_extension *extension) {
  int ret;
  extension->startup = last_startup;
  ret = extension->startup(extension);
  zend_extensions.count++;
  eaccelerator_el->next = zend_extensions.head;
  eaccelerator_el->prev = NULL;
  zend_extensions.head->prev = eaccelerator_el;
  zend_extensions.head = eaccelerator_el;
  if (ZendOptimizer) {
    if ((ZendOptimizer = zend_get_extension("Zend Optimizer")) != NULL) {
      ZendOptimizer->op_array_handler = NULL;
    }
  }
  return ret;
}

/*
static int eaccelerator_ioncube_startup(zend_extension *extension) {
  int ret;
  zend_extension* last_ext = (zend_extension*)zend_extensions.tail->data;
  extension->startup = last_startup;
  ret = extension->startup(extension);
  last_startup = last_ext->startup;
  last_ext->startup = eaccelerator_last_startup;
  return ret;
}
*/

ZEND_DLEXPORT int eaccelerator_zend_startup(zend_extension *extension) {
 eaccelerator_is_zend_extension = 1;
  eaccelerator_el   = NULL;
  last_startup = NULL;

  if (!eaccelerator_is_extension) {
    if (zend_startup_module(&eaccelerator_module_entry) != SUCCESS) {
      return FAILURE;
    }
  }

  if (zend_llist_count(&zend_extensions) > 1) {
    zend_llist_element *p = zend_extensions.head;
    while (p != NULL) {
      zend_extension* ext = (zend_extension*)(p->data);
      if (strcmp(ext->name, EACCELERATOR_EXTENSION_NAME) == 0) {
        /* temporary removing eAccelerator extension */
        zend_extension* last_ext = (zend_extension*)zend_extensions.tail->data;
        if (eaccelerator_el != NULL) {
          zend_error(E_CORE_ERROR,"[%s] %s %s can not be loaded twich",
                   EACCELERATOR_EXTENSION_NAME,
                   EACCELERATOR_EXTENSION_NAME,
                   EACCELERATOR_VERSION);
          exit(1);
        }
        if (last_ext != ext) {
          eaccelerator_el = p;
          last_startup = last_ext->startup;
          last_ext->startup = eaccelerator_last_startup;
          zend_extensions.count--;
          if (p->prev != NULL) {
            p->prev->next = p->next;
          } else {
            zend_extensions.head = p->next;
          }
          if (p->next != NULL) {
            p->next->prev = p->prev;
          } else {
            zend_extensions.tail = p->prev;
          }
        }
      } else if (strcmp(ext->name, "pcntl") == 0) {
      } else if (strcmp(ext->name, "DBG") == 0) {
      } else if (strcmp(ext->name, "Xdebug") == 0) {
      } else if (strcmp(ext->name, "Advanced PHP Debugger (APD)") == 0) {
      } else if (strcmp(ext->name, "Zend Extension Manager") == 0 ||
                 strcmp(ext->name, "Zend Optimizer") == 0) {
        /* Disable ZendOptimizer Optimizations */
        ZendOptimizer = ext;
        ext->op_array_handler = NULL;
/*???
        } else if (strcmp(ext->name, "the ionCube PHP Loader") == 0) {
          zend_extension* last_ext = (zend_extension*)zend_extensions.tail->data;
          if (ext != last_ext) {
            last_ext->startup  = last_startup;
            last_startup = ext->startup;
            ext->startup = eaccelerator_ioncube_startup;
          }
*/
      } else {
        zend_error(E_CORE_ERROR,"[%s] %s %s is incompatible with %s %s",
                   EACCELERATOR_EXTENSION_NAME,
                   EACCELERATOR_EXTENSION_NAME,
                   EACCELERATOR_VERSION,
                   ext->name,
                   ext->version);
        exit(1);
      }
      p = p->next;
    }
  }

  php_register_info_logo(EACCELERATOR_VERSION_GUID, "text/plain", (unsigned char*)EACCELERATOR_VERSION_STRING, sizeof(EACCELERATOR_VERSION_STRING));
  php_register_info_logo(EACCELERATOR_LOGO_GUID,    "image/gif",  (unsigned char*)eaccelerator_logo, sizeof(eaccelerator_logo));

  /*
   * HOESH: on apache restart there was some
   * problem with CG(class_table) in the latest PHP
   * versions. Initialization moved to eaccelerator_compile_file()
   * depends on the value below.
   */
  eaccelerator_global_function_table.nTableSize = 0;

  return SUCCESS;
}

#ifndef ZEND_EXT_API
#  define ZEND_EXT_API    ZEND_DLEXPORT
#endif

ZEND_EXTENSION();

ZEND_DLEXPORT zend_extension zend_extension_entry = {
  EACCELERATOR_EXTENSION_NAME,
  EACCELERATOR_VERSION,
  "eAccelerator",
  "http://eaccelerator.sourceforge.net",
  "Copyright (c) 2004-2005 eAccelerator",
  eaccelerator_zend_startup,
  NULL,
  NULL,   /* void (*activate)() */
  NULL,   /* void (*deactivate)() */
  NULL,   /* void (*message_handle)(int message, void *arg) */
#ifdef WITH_EACCELERATOR_OPTIMIZER
  eaccelerator_optimize,   /* void (*op_array_handler)(zend_op_array *o_a); */
#else
  NULL,   /* void (*op_array_handler)(zend_op_array *o_a); */
#endif
  NULL,   /* void (*statement_handler)(zend_op_array *o_a); */
  NULL,   /* void (*fcall_begin_handler)(zend_op_array *o_a); */
  NULL,   /* void (*fcall_end_handler)(zend_op_array *o_a); */
  NULL,   /* void (*op_array_ctor)(zend_op_array *o_a); */
  NULL,   /* void (*op_array_dtor)(zend_op_array *o_a); */
#ifdef COMPAT_ZEND_EXTENSION_PROPERTIES
  NULL,   /* api_no_check */
  COMPAT_ZEND_EXTENSION_PROPERTIES
#else
  STANDARD_ZEND_EXTENSION_PROPERTIES
#endif
};

static zend_extension eaccelerator_extension_entry = {
  EACCELERATOR_EXTENSION_NAME,
  EACCELERATOR_VERSION,
  "eAccelerator",
  "http://eaccelerator.sourceforge.net",
  "Copyright (c) 2004-2004 eAccelerator",
  eaccelerator_zend_startup,
  NULL,
  NULL,   /* void (*activate)() */
  NULL,   /* void (*deactivate)() */
  NULL,   /* void (*message_handle)(int message, void *arg) */
#ifdef WITH_EACCELERATOR_OPTIMIZER
  eaccelerator_optimize,   /* void (*op_array_handler)(zend_op_array *o_a); */
#else
  NULL,   /* void (*op_array_handler)(zend_op_array *o_a); */
#endif
  NULL,   /* void (*statement_handler)(zend_op_array *o_a); */
  NULL,   /* void (*fcall_begin_handler)(zend_op_array *o_a); */
  NULL,   /* void (*fcall_end_handler)(zend_op_array *o_a); */
  NULL,   /* void (*op_array_ctor)(zend_op_array *o_a); */
  NULL,   /* void (*op_array_dtor)(zend_op_array *o_a); */
#ifdef COMPAT_ZEND_EXTENSION_PROPERTIES
  NULL,   /* api_no_check */
  COMPAT_ZEND_EXTENSION_PROPERTIES
#else
  STANDARD_ZEND_EXTENSION_PROPERTIES
#endif
};

static void register_eaccelerator_as_zend_extension() {
  zend_extension extension = eaccelerator_extension_entry;
  extension.handle = 0;
  zend_llist_prepend_element(&zend_extensions, &extension);
}

/******************************************************************************/

#ifdef WITH_EACCELERATOR_DISASSEMBLER

static const char* extopnames_declare[] = {
  "",                          /* 0 */
  "DECLARE_CLASS",             /* 1 */
  "DECLARE_FUNCTION",          /* 2 */
  "DECLARE_INHERITED_CLASS"    /* 3 */
};

static const char* extopnames_cast[] = {
  "IS_NULL",                   /* 0 */
  "IS_LONG",                   /* 1 */
  "IS_DOUBLE",                 /* 2 */
  "IS_STRING",                 /* 3 */
  "IS_ARRAY",                  /* 4 */
  "IS_OBJECT",                 /* 5 */
  "IS_BOOL",                   /* 6 */
  "IS_RESOURCE",               /* 7 */
  "IS_CONSTANT",               /* 8 */
  "IS_CONSTANT_ARRAY"          /* 9 */
};

static const char* extopnames_fetch[] = {
  "FETCH_STANDARD",            /* 0 */
  "FETCH_ADD_LOCK"             /* 1 */
};

static const char* extopnames_fetch_class[] = {
  "FETCH_CLASS_DEFAULT",       /* 0 */
  "FETCH_CLASS_SELF",          /* 1 */
  "FETCH_CLASS_PARENT",        /* 2 */
  "FETCH_CLASS_MAIN",          /* 3 */
  "FETCH_CLASS_GLOBAL",        /* 4 */
  "FETCH_CLASS_AUTO"           /* 5 */
};

static const char* extopnames_init_fcall[] = {
  "&nbsp;",                    /* 0 */
  "MEMBER_FUNC_CALL",          /* 1 */
  "CTOR_CALL",                 /* 2 */
  "CTOR_CALL"                  /* 3 */
};

static const char* extopnames_sendnoref[] = {
  "&nbsp;",                    /* 0 */
  "ARG_SEND_BY_REF",           /* 1 */
  "ARG_COMPILE_TIME_BOUND",    /* 2 */
  "ARG_SEND_BY_REF | ZEND_ARG_COMPILE_TIME_BOUND" /* 3 */
};

static const char* fetchtypename[] = {
  "FETCH_GLOBAL",             /* 0 */
  "FETCH_LOCAL",              /* 1 */
  "FETCH_STATIC"              /* 2 */
#ifdef ZEND_ENGINE_2
  ,
  "FETCH_STATIC_MEMBER"       /* 3 */
#endif
};

static void dump_write(const char* s, uint len) {
  uint i = 0;
  while (i < len) {
    if (!s[i]) ZEND_PUTS("\\000");
    else if (s[i] == '\n')      ZEND_PUTS("\\n");
    else if (s[i] == '\r') ZEND_PUTS("\\r");
    else if (s[i] < ' ')   zend_printf("\\%03o",(unsigned char)s[i]);
    else if (s[i] == '<')  ZEND_PUTS("&lt;");
    else if (s[i] == '>')  ZEND_PUTS("&gt;");
    else if (s[i] == '&')  ZEND_PUTS("&amp;");
    else if (s[i] == '\'') ZEND_PUTS("\\'");
    else if (s[i] == '\\') ZEND_PUTS("\\\\");
    else zend_write(&s[i], 1);
    ++i;
  }
}

static void dump_zval(zval* v, int compress) {
  switch(v->type & ~IS_CONSTANT_INDEX) {
    case IS_NULL:
      ZEND_PUTS("null");
      break;
    case IS_LONG:
      zend_printf("long(%ld)", v->value.lval);
      break;
    case IS_DOUBLE:
      zend_printf("double(%e)", v->value.dval);
/*
      zend_printf("double(%.*G)", v->value.dval);
*/
      break;
    case IS_STRING:
      ZEND_PUTS("string('");
string_dump:
      dump_write(v->value.str.val, v->value.str.len);
      ZEND_PUTS("')");
      break;
    case IS_BOOL:
      zend_printf("bool(%s)", v->value.lval?"true":"false");
      break;
    case IS_ARRAY:
      ZEND_PUTS("array(");
array_dump:
      {
        Bucket* p = v->value.ht->pListHead;
        while (p != NULL) {
          if (p->nKeyLength == 0) {
            zend_printf("%lu",p->h);
          } else {
            int is_const = 0;
            if (((zval*)p->pDataPtr)->type & IS_CONSTANT_INDEX) {
              is_const = 1;
            }
            if (is_const) {
              ZEND_PUTS("constant(");
            }
            ZEND_PUTS(p->arKey);
            if (is_const) {
              ZEND_PUTS(")");
            }
          }
          ZEND_PUTS(" => ");
          dump_zval((zval*)p->pDataPtr, 1);
          p = p->pListNext;
          if (p != NULL) ZEND_PUTS(", ");
        }
      }
      ZEND_PUTS(")");
      break;
    case IS_OBJECT:
#ifdef ZEND_ENGINE_2
      ZEND_PUTS("object(?)");
#else
      ZEND_PUTS("object(");
      if (v->value.obj.ce != NULL) {
        zend_printf("class: '%s' properties(", v->value.obj.ce);
      } else {
        ZEND_PUTS("class: ? properties(");
      }
      if (v->value.obj.properties != NULL) {
        Bucket* p = v->value.obj.properties->pListHead;
        while (p != NULL) {
          if (p->nKeyLength == 0) {
            zend_printf("%lu",p->h);
          } else {
            int is_const = 0;
            if ((compress && (((zval*)p->pData)->type & IS_CONSTANT_INDEX)) ||
                (!compress && ((*(zval**)p->pData)->type & IS_CONSTANT_INDEX))) {
              is_const = 1;
            }
            if (is_const) {
              ZEND_PUTS("constant(");
            }
            ZEND_PUTS(p->arKey);
            if (is_const) {
              ZEND_PUTS(")");
            }
          }
          ZEND_PUTS(" => ");
          dump_zval((zval*)p->pDataPtr, 1);
          p = p->pListNext;
          if (p != NULL) ZEND_PUTS(", ");
        }
      }
      ZEND_PUTS("))");
#endif
      break;
    case IS_RESOURCE:
      ZEND_PUTS("resource(?)");
      break;
    case IS_CONSTANT:
      ZEND_PUTS("constant('");
      goto string_dump;
    case IS_CONSTANT_ARRAY:
      ZEND_PUTS("constatnt_array(");
      goto array_dump;
    default:
      zend_printf("unknown(%d)",v->type);
  }
}

static void dump_op_array(eaccelerator_op_array* p TSRMLS_DC) {
  zend_op *opline;
  zend_op *end;

#ifdef ZEND_ENGINE_2
  zend_printf("T = %u, size = %u\n, brk_count = %u<br>\n", p->T, p->last, p->last_brk_cont);
#else
  zend_printf("T = %u, size = %u\n, uses_globals = %d, brk_count = %u<br>\n", p->T, p->last ,p->uses_globals, p->last_brk_cont);
#endif

  if (p->static_variables) {
    Bucket *q = p->static_variables->pListHead;

    ZEND_PUTS("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
    ZEND_PUTS("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"200\">Static variable</th><th width=\"400\">Value</th></tr></thead>\n");
    ZEND_PUTS("<tbody valign=\"top\" bgcolor=\"#cccccc\" style=\"word-break:break-all\">\n");

    while (q) {
      zend_printf("<tr><td bgcolor=\"#ccccff\">$%s</td><td>", q->arKey);
      dump_zval((zval*)q->pDataPtr, 1);
      ZEND_PUTS("&nbsp;</td></tr>\n");
      q = q->pListNext;
    }
    ZEND_PUTS("<tbody></table><br>\n");
  }

  if (p->opcodes) {
    int n = 0;
    opline = p->opcodes;
    end = opline + p->last;

    ZEND_PUTS("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"900\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
    ZEND_PUTS("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"40\">N</th><th width=\"160\">OPCODE</th><th width=\"160\">EXTENDED_VALUE</th><th width=\"220\">OP1</th><th width=\"220\">OP2</th><th width=\"80\">RESULT</th></tr></thead>\n");
    ZEND_PUTS("<tbody valign=\"top\" bgcolor=\"#cccccc\" style=\"word-break:break-all; font-size: x-small\">\n");
    for (;opline < end; opline++) {
      const opcode_dsc* op = get_opcode_dsc(opline->opcode);
      if (op != NULL) {
        zend_printf("<tr><td>%d </td><td>%s </td>",n, op->opname);
        if ((op->ops & EXT_MASK) == EXT_OPLINE) {
          zend_printf("<td>opline(%lu) </td>",opline->extended_value);
        } else if ((op->ops & EXT_MASK) == EXT_FCALL) {
          zend_printf("<td>args(%lu) </td>",opline->extended_value);
        } else if ((op->ops & EXT_MASK) == EXT_ARG) {
          zend_printf("<td>arg(%lu) </td>",opline->extended_value);
        } else if ((op->ops & EXT_MASK) == EXT_SEND) {
          zend_printf("<td>%s </td>", get_opcode_dsc(opline->extended_value)->opname);
        } else if ((op->ops & EXT_MASK) == EXT_CAST) {
          zend_printf("<td>%s </td>", extopnames_cast[opline->extended_value]);
        } else if ((op->ops & EXT_MASK) == EXT_INIT_FCALL) {
          zend_printf("<td>%s </td>", extopnames_init_fcall[opline->extended_value]);
        } else if ((op->ops & EXT_MASK) == EXT_FETCH) {
          zend_printf("<td>%s </td>", extopnames_fetch[opline->extended_value]);
        } else if ((op->ops & EXT_MASK) == EXT_DECLARE) {
          zend_printf("<td>%s </td>", extopnames_declare[opline->extended_value]);
        } else if ((op->ops & EXT_MASK) == EXT_SEND_NOREF) {
          zend_printf("<td>%s </td>", extopnames_sendnoref[opline->extended_value]);
        } else if ((op->ops & EXT_MASK) == EXT_FCLASS) {
          zend_printf("<td>%s </td>", extopnames_fetch_class[opline->extended_value]);
        } else if ((op->ops & EXT_MASK) == EXT_IFACE) {
          zend_printf("<td>interface(%lu) </td>",opline->extended_value);
        } else if ((op->ops & EXT_MASK) == EXT_CLASS) {
          zend_printf("<td>$class%u </td>",VAR_NUM(opline->extended_value));
        } else if ((op->ops & EXT_MASK) == EXT_BIT) {
          zend_printf("<td>%d </td>",opline->extended_value?1:0);
        } else if ((op->ops & EXT_MASK) == EXT_ISSET) {
          if (opline->extended_value == ZEND_ISSET) {
            ZEND_PUTS("<td>ZEND_ISSET </td>");
          } else if (opline->extended_value == ZEND_ISEMPTY) {
            ZEND_PUTS("<td>ZEND_ISEMPTY </td>");
          } else {
            ZEND_PUTS("<td>&nbsp; </td>");
          }
#ifdef ZEND_ENGINE_2
        } else if ((op->ops & EXT_MASK) == EXT_ASSIGN) {
          if (opline->extended_value == ZEND_ASSIGN_OBJ) {
            ZEND_PUTS("<td>ZEND_ASSIGN_OBJ </td>");
          } else if (opline->extended_value == ZEND_ASSIGN_DIM) {
            ZEND_PUTS("<td>ZEND_ASSIGN_DIM </td>");
          } else {
            ZEND_PUTS("<td>&nbsp; </td>");
          }
        } else if (opline->opcode == ZEND_UNSET_DIM_OBJ) {
          if (opline->extended_value == ZEND_UNSET_DIM) {
            ZEND_PUTS("<td>ZEND_UNSET_DIM </td>");
          } else if (opline->extended_value == ZEND_UNSET_OBJ) {
            ZEND_PUTS("<td>ZEND_UNSET_OBJ </td>");
          } else {
            ZEND_PUTS("<td>&nbsp; </td>");
          }
#endif
        } else if (opline->extended_value != 0) {
          zend_printf("<td>%ld </td>",opline->extended_value);
        } else {
          ZEND_PUTS("<td>&nbsp;</td>");
        }
      } else {
        zend_printf("<tr><td>%d </td><td>UNKNOWN_OPCODE %d </td><td>%lu </td>", n, opline->opcode,opline->extended_value);
        op = get_opcode_dsc(0);
      }

      if ((op->ops & OP1_MASK) == OP1_OPLINE) {
        zend_printf("<td>opline(%d) </td>",opline->op1.u.opline_num);
#ifdef ZEND_ENGINE_2
      } else if ((op->ops & OP1_MASK) == OP1_JMPADDR) {
        zend_printf("<td>opline(%u) </td>",(unsigned int)(opline->op1.u.jmp_addr - p->opcodes));
      } else if ((op->ops & OP1_MASK) == OP1_CLASS) {
        zend_printf("<td>$class%u </td>",VAR_NUM(opline->op1.u.var));
      } else if ((op->ops & OP1_MASK) == OP1_UCLASS) {
        if (opline->op1.op_type == IS_UNUSED) {
          zend_printf("<td>&nbsp; </td>");
        } else {
          zend_printf("<td>$class%u </td>",VAR_NUM(opline->op1.u.var));
        }
#endif
      } else if ((op->ops & OP1_MASK) == OP1_BRK) {
        if (opline->op1.u.opline_num != -1 &&
            opline->op2.op_type == IS_CONST &&
            opline->op2.u.constant.type == IS_LONG) {
          int level  = opline->op2.u.constant.value.lval;
          zend_uint offset = opline->op1.u.opline_num;
          zend_brk_cont_element *jmp_to;
          do {
            if (offset >= p->last_brk_cont) {
              goto brk_failed;
            }
            jmp_to = &p->brk_cont_array[offset];
            offset = jmp_to->parent;
          } while (--level > 0);
          zend_printf("<td>opline(%d) </td>",jmp_to->brk);
        } else {
brk_failed:
          zend_printf("<td>brk_cont(%u) </td>",opline->op1.u.opline_num);
        }
      } else if ((op->ops & OP1_MASK) == OP1_CONT) {
        if (opline->op1.u.opline_num != -1 &&
            opline->op2.op_type == IS_CONST &&
            opline->op2.u.constant.type == IS_LONG) {
          int level  = opline->op2.u.constant.value.lval;
          zend_uint offset = opline->op1.u.opline_num;
          zend_brk_cont_element *jmp_to;
          do {
            if (offset >= p->last_brk_cont) {
              goto cont_failed;
            }
            jmp_to = &p->brk_cont_array[offset];
            offset = jmp_to->parent;
          } while (--level > 0);
          zend_printf("<td>opline(%d) </td>",jmp_to->cont);
        } else {
cont_failed:
          zend_printf("<td>brk_cont(%u) </td>",opline->op1.u.opline_num);
        }
      } else if ((op->ops & OP1_MASK) == OP1_ARG) {
        zend_printf("<td>arg(%ld) </td>",opline->op1.u.constant.value.lval);
      } else if ((op->ops & OP1_MASK) == OP1_VAR) {
        zend_printf("<td>$var%u </td>",VAR_NUM(opline->op1.u.var));
      } else if ((op->ops & OP1_MASK) == OP1_TMP) {
        zend_printf("<td>$tmp%u </td>",VAR_NUM(opline->op1.u.var));
      } else {
        if (opline->op1.op_type == IS_CONST) {
          ZEND_PUTS("<td>");
          dump_zval(&opline->op1.u.constant, 0);
          ZEND_PUTS(" </td>");
        } else if (opline->op1.op_type == IS_TMP_VAR) {
          zend_printf("<td>$tmp%u </td>",VAR_NUM(opline->op1.u.var));
        } else if (opline->op1.op_type == IS_VAR) {
          zend_printf("<td>$var%u </td>",VAR_NUM(opline->op1.u.var));
        } else if (opline->op1.op_type == IS_UNUSED) {
          ZEND_PUTS("<td>&nbsp;</td>");
        } else {
          zend_printf("<td>UNKNOWN NODE %d </td>",opline->op1.op_type);
        }
      }

      if ((op->ops & OP2_MASK) == OP2_OPLINE) {
        zend_printf("<td>opline(%d) </td>",opline->op2.u.opline_num);
#ifdef ZEND_ENGINE_2
      } else if ((op->ops & OP2_MASK) == OP2_JMPADDR) {
        zend_printf("<td>opline(%u) </td>",(unsigned int)(opline->op2.u.jmp_addr - p->opcodes));
      } else if ((op->ops & OP2_MASK) == OP2_CLASS) {
          zend_printf("<td>$class%u </td>",VAR_NUM(opline->op2.u.var));
#endif
      } else if ((op->ops & OP2_MASK) == OP2_VAR) {
          zend_printf("<td>$var%u </td>",VAR_NUM(opline->op2.u.var));
      } else if ((op->ops & OP2_MASK) == OP2_FETCH) {
#ifdef ZEND_ENGINE_2
        if (opline->op2.u.EA.type == ZEND_FETCH_STATIC_MEMBER) {
          zend_printf("<td>%s $class%u</td>",fetchtypename[opline->op2.u.EA.type],VAR_NUM(opline->op2.u.var));
        } else {
          zend_printf("<td>%s </td>",fetchtypename[opline->op2.u.EA.type]);
        }
#else
        zend_printf("<td>%s </td>",fetchtypename[opline->op2.u.fetch_type]);
#endif
      } else if ((op->ops & OP2_MASK) == OP2_INCLUDE) {
        if (opline->op2.u.constant.value.lval == ZEND_EVAL) {
          ZEND_PUTS("<td>ZEND_EVAL </td>");
        } else if (opline->op2.u.constant.value.lval == ZEND_INCLUDE) {
          ZEND_PUTS("<td>ZEND_INCLUDE </td>");
        } else if (opline->op2.u.constant.value.lval == ZEND_INCLUDE_ONCE) {
          ZEND_PUTS("<td>ZEND_INCLUDE_ONCE </td>");
        } else if (opline->op2.u.constant.value.lval == ZEND_REQUIRE) {
          ZEND_PUTS("<td>ZEND_REQUIRE </td>");
        } else if (opline->op2.u.constant.value.lval == ZEND_REQUIRE_ONCE) {
          ZEND_PUTS("<td>ZEND_REQUIRE_ONCE </td>");
        } else {
          ZEND_PUTS("<td>&nbsp;</td>");
        }
      } else if ((op->ops & OP2_MASK) == OP2_ARG) {
        zend_printf("<td>arg(%u) </td>",opline->op2.u.opline_num);
      } else if ((op->ops & OP2_MASK) == OP2_ISSET) {
        if (opline->op2.u.constant.value.lval == ZEND_ISSET) {
          ZEND_PUTS("<td>ZEND_ISSET </td>");
        } else if (opline->op2.u.constant.value.lval == ZEND_ISEMPTY) {
          ZEND_PUTS("<td>ZEND_ISEMPTY </td>");
        } else {
          ZEND_PUTS("<td>&nbsp; </td>");
        }
      } else {
        if (opline->op2.op_type == IS_CONST) {
          ZEND_PUTS("<td>");
          dump_zval(&opline->op2.u.constant, 0);
          ZEND_PUTS(" </td>");
        } else if (opline->op2.op_type == IS_TMP_VAR) {
          zend_printf("<td>$tmp%u </td>",VAR_NUM(opline->op2.u.var));
        } else if (opline->op2.op_type == IS_VAR) {
          zend_printf("<td>$var%u </td>",VAR_NUM(opline->op2.u.var));
        } else if (opline->op2.op_type == IS_UNUSED) {
          ZEND_PUTS("<td>&nbsp; </td>");
        } else {
          zend_printf("<td>UNKNOWN NODE %d </td>",opline->op2.op_type);
        }
      }

      switch (op->ops & RES_MASK) {
        case RES_STD:
          if (opline->result.op_type == IS_CONST) {
            ZEND_PUTS("<td>");
            dump_zval(&opline->result.u.constant, 0);
            ZEND_PUTS("</td>");
          } else if (opline->result.op_type == IS_TMP_VAR) {
            zend_printf("<td>$tmp%u</td>",VAR_NUM(opline->result.u.var));
          } else if (opline->result.op_type == IS_VAR) {
            if ((opline->result.u.EA.type & EXT_TYPE_UNUSED) != 0)
              zend_printf("<td>$var%u <small>(unused)</small></td>",VAR_NUM(opline->result.u.var));
            else
              zend_printf("<td>$var%u</td>",VAR_NUM(opline->result.u.var));
          } else if (opline->result.op_type == IS_UNUSED) {
            ZEND_PUTS("<td>&nbsp;</td>");
          } else {
            zend_printf("<td>UNKNOWN NODE %d</td>",opline->result.op_type);
          }
          break;
        case RES_CLASS:
          zend_printf("<td>$class%u</td>",VAR_NUM(opline->result.u.var));
          break;
        case RES_TMP:
          zend_printf("<td>$tmp%u</td>",VAR_NUM(opline->result.u.var));
          break;
        case RES_VAR:
          if ((opline->result.u.EA.type & EXT_TYPE_UNUSED) != 0) {
            zend_printf("<td>$var%u <small>(unused)</small></td>",VAR_NUM(opline->result.u.var));
          } else {
            zend_printf("<td>$var%u</td>",VAR_NUM(opline->result.u.var));
          }
          break;
        case RES_UNUSED:
          ZEND_PUTS("<td>&nbsp;</td>");
          break;
        default:
          zend_printf("<td>UNKNOWN NODE %d</td>",opline->result.op_type);
          break;
      }
      ZEND_PUTS("</tr>\n");
      n++;
    }
    ZEND_PUTS("</tbody></table>\n");
  }
}

static void dump_cache_entry(mm_cache_entry *p TSRMLS_DC) {
  mm_fc_entry *fc = p->c_head;
  if (fc != NULL) {
    ZEND_PUTS("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
    ZEND_PUTS("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"600\">Classes</th></tr></thead>\n");
    ZEND_PUTS("<tbody valign=\"top\" bgcolor=\"#ccccff\" style=\"word-break:break-all\">\n");
    while (fc != NULL) {
      eaccelerator_class_entry* x = (eaccelerator_class_entry*)fc->fc;
      char class[1024];
      memcpy(class, fc->htabkey, fc->htablen);
      class[fc->htablen] = '\0';
      if (class[0] == '\000') class[0] = '-';
      if (x->type == ZEND_USER_CLASS) {
#ifdef ZEND_ENGINE_2
        zend_printf("<tr><td><a href=\"?file=%s&class=%s\"&Horde=22c8f7474b79194f32569fc1af447f5b>%s</a> [\n",
        p->realfilename, class, class);
        if (x->ce_flags & ZEND_ACC_FINAL_CLASS) {
          ZEND_PUTS("final ");
        }
        if (x->ce_flags & ZEND_ACC_IMPLICIT_ABSTRACT_CLASS) {
          ZEND_PUTS("implicit abstract ");
        }
        if (x->ce_flags & ZEND_ACC_EXPLICIT_ABSTRACT_CLASS) {
          ZEND_PUTS("explicit abstract ");
        }
        if (x->ce_flags & ZEND_ACC_INTERFACE) {
          ZEND_PUTS("interface");
        } else {
          ZEND_PUTS("class ");
        }
        ZEND_PUTS("]</td></tr>");
#else
        zend_printf("<tr><td><a href=\"?file=%s&class=%s\"&Horde=22c8f7474b79194f32569fc1af447f5b>%s</a></td></tr>\n",
          p->realfilename, class, class);
#endif
      } else {
        zend_printf("<tr><td>%s [internal]</td></tr>\n", class);
      }
      fc = fc->next;
    }
    ZEND_PUTS("</tbody></table><br>\n");
  }
  fc = p->f_head;
  if (fc != NULL) {
    ZEND_PUTS("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
    ZEND_PUTS("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"600\">Functions</th></tr></thead>\n");
    ZEND_PUTS("<tbody valign=\"top\" bgcolor=\"#ccccff\" style=\"word-break:break-all\">\n");
    while (fc != NULL) {
      char func[1024];
      memcpy(func, fc->htabkey, fc->htablen);
      func[fc->htablen] = '\0';
      if (func[0] == '\000' && fc->htablen > 0) func[0] = '-';
      if (((zend_function*)(fc->fc))->type == ZEND_USER_FUNCTION) {
        zend_printf("<tr><td><a href=\"?file=%s&func=%s\"&Horde=22c8f7474b79194f32569fc1af447f5b>%s</a></td></tr>\n",
          p->realfilename, func, func);
      } else {
        zend_printf("<tr><td>%s [internal]</td></tr>\n", func);
      }
      fc = fc->next;
    }
    ZEND_PUTS("</tbody></table><br>\n");
  }
  if (p->op_array) {
    dump_op_array(p->op_array TSRMLS_CC);
  }
}

static void dump_class(mm_cache_entry *p, char* class TSRMLS_DC) {
  mm_fc_entry *fc = p->c_head;
  int len;
  if (class[0] == '-') {
    len = strlen(class);
    class[0] = '\0';
  } else {
    len = strlen(class)+1;
  }
  while (fc != NULL) {
    if (len == fc->htablen && memcmp(fc->htabkey,class,fc->htablen)==0) {
      break;
    }
    fc = fc->next;
  }
  if (class[0] == '\0') class[0] = '-';
  if (fc && fc->fc) {
    eaccelerator_class_entry* c = (eaccelerator_class_entry*)fc->fc;
    Bucket *q;
    if (c->parent) {
      zend_printf("<h4>extends: %s</h4>\n", (const char*)c->parent);
    }

#ifdef ZEND_ENGINE_2
    if (c->properties_info.nNumOfElements > 0) {
      q = c->properties_info.pListHead;
      ZEND_PUTS("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
      ZEND_PUTS("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"200\">Property</th><th width=\"400\">Value</th></tr></thead>\n");
      ZEND_PUTS("<tbody valign=\"top\" bgcolor=\"#cccccc\" style=\"word-break:break-all\">\n");
      while (q) {
        zend_property_info* x = (zend_property_info*)q->pData;
        Bucket* y = NULL;

        zend_printf("<tr><td bgcolor=\"#ccccff\">$%s [", q->arKey);
        if (x->flags & ZEND_ACC_STATIC) {
          ZEND_PUTS("static ");
        }
        if ((x->flags & ZEND_ACC_PPP_MASK) == ZEND_ACC_PRIVATE) {
          ZEND_PUTS("private ");
        } else if ((x->flags & ZEND_ACC_PPP_MASK) == ZEND_ACC_PROTECTED) {
          ZEND_PUTS("protected ");
        } else if ((x->flags & ZEND_ACC_PPP_MASK) == ZEND_ACC_PUBLIC) {
          ZEND_PUTS("public ");
        }
        if (x->flags & ZEND_ACC_FINAL) {
          ZEND_PUTS("final ");
        }
        ZEND_PUTS("]</td><td>");
        if ((x->flags & ZEND_ACC_STATIC) && c->static_members != NULL && c->static_members->nNumOfElements > 0) {
          y = c->static_members->pListHead;
        } else if ((x->flags & ZEND_ACC_STATIC) == 0 && c->default_properties.nNumOfElements > 0) {
          y = c->default_properties.pListHead;
        }
        while (y) {
          if (y->h == x->h &&
              (int)y->nKeyLength == x->name_length+1 &&
              memcmp(y->arKey, x->name, x->name_length+1) == 0) {
            dump_zval((zval*)y->pDataPtr, 1);
            break;
          }
          y = y->pListNext;
        }
        ZEND_PUTS("&nbsp;</td></tr>\n");
        q = q->pListNext;
      }
      ZEND_PUTS("<tbody></table><br>\n");
    }

    if (c->constants_table.nNumOfElements > 0) {
      q = c->constants_table.pListHead;
      ZEND_PUTS("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
      ZEND_PUTS("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"200\">Constant</th><th width=\"400\">Value</th></tr></thead>\n");
      ZEND_PUTS("<tbody valign=\"top\" bgcolor=\"#cccccc\" style=\"word-break:break-all\">\n");
      while (q) {
        zend_printf("<tr><td bgcolor=\"#ccccff\">%s</td><td>", q->arKey);
        dump_zval((zval*)q->pDataPtr, 1);
        ZEND_PUTS("&nbsp;</td></tr>\n");
        q = q->pListNext;
      }
      ZEND_PUTS("<tbody></table><br>\n");
    }
#else
    if (c->default_properties.nNumOfElements > 0) {
      q = c->default_properties.pListHead;
      ZEND_PUTS("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
      ZEND_PUTS("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"200\">Property</th><th width=\"400\">Value</th></tr></thead>\n");
      ZEND_PUTS("<tbody valign=\"top\" bgcolor=\"#cccccc\" style=\"word-break:break-all\">\n");
      while (q) {
        zend_printf("<tr><td bgcolor=\"#ccccff\">$%s</td><td>", q->arKey);
        dump_zval((zval*)q->pDataPtr, 1);
        ZEND_PUTS("&nbsp;</td></tr>\n");
        q = q->pListNext;
      }
      ZEND_PUTS("<tbody></table><br>\n");
    }
#endif

    q = c->function_table.pListHead;
    ZEND_PUTS("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
    ZEND_PUTS("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"600\">Methods</th></tr></thead>\n");
    ZEND_PUTS("<tbody valign=\"top\" bgcolor=\"#ccccff\" style=\"word-break:break-all\">\n");
    while (q) {
      eaccelerator_op_array* x = (eaccelerator_op_array*)q->pData;
      if (x->type == ZEND_USER_FUNCTION) {
#ifdef ZEND_ENGINE_2
        zend_printf("<tr><td><a href=\"?file=%s&class=%s&func=%s\"&Horde=22c8f7474b79194f32569fc1af447f5b>%s</a> [", p->realfilename, class, q->arKey, q->arKey);
        if (x->fn_flags & ZEND_ACC_STATIC) {
          ZEND_PUTS("static ");
        }
        if ((x->fn_flags & ZEND_ACC_PPP_MASK) == ZEND_ACC_PRIVATE) {
          ZEND_PUTS("private ");
        } else if ((x->fn_flags & ZEND_ACC_PPP_MASK) == ZEND_ACC_PROTECTED) {
          ZEND_PUTS("protected ");
        } else if ((x->fn_flags & ZEND_ACC_PPP_MASK) == ZEND_ACC_PUBLIC) {
          ZEND_PUTS("public ");
        }
        if (x->fn_flags & ZEND_ACC_ABSTRACT) {
          ZEND_PUTS("abstract ");
        }
        if (x->fn_flags & ZEND_ACC_FINAL) {
          ZEND_PUTS("final ");
        }
        ZEND_PUTS("]</td></tr>");
#else
        zend_printf("<tr><td><a href=\"?file=%s&class=%s&func=%s\"&Horde=22c8f7474b79194f32569fc1af447f5b>%s</a></td></tr>\n", p->realfilename, class, q->arKey, q->arKey);
#endif
      } else {
        zend_printf("<tr><td>%s [internal]</td></tr>\n", q->arKey);
      }
      q = q->pListNext;
    }
    ZEND_PUTS("</tbody></table><br>\n");
    return;
  }
  ZEND_PUTS("<h5>NOT FOUND</h5>\n");
}

static void dump_method(mm_cache_entry *p, char* class, const char* func TSRMLS_DC) {
  mm_fc_entry *fc = p->c_head;
  int len;
  if (class[0] == '-') {
    len = strlen(class);
    class[0] = '\0';
  } else {
    len = strlen(class)+1;
  }
  while (fc != NULL) {
    if (len == fc->htablen && memcmp(fc->htabkey,class,fc->htablen)==0) {
      break;
    }
    fc = fc->next;
  }
  if (class[0] == '\0') class[0] = '-';
  if (fc && fc->fc) {
    unsigned int len = strlen(func)+1;
    eaccelerator_class_entry* c = (eaccelerator_class_entry*)fc->fc;
    Bucket *q = c->function_table.pListHead;
    while (q != NULL) {
      if (len == q->nKeyLength && memcmp(func, q->arKey, len) == 0) {
              dump_op_array((eaccelerator_op_array*)q->pData TSRMLS_CC);
        return;
      }
      q = q->pListNext;
    }
  }
  ZEND_PUTS("<h5>NOT FOUND</h5>\n");
}

static void dump_function(mm_cache_entry *p, char* func TSRMLS_DC) {
  mm_fc_entry *fc = p->f_head;
  int len;
  if (func[0] == '-') {
    len = strlen(func);
    func[0] = '\0';
  } else {
    len = strlen(func)+1;
  }
  while (fc != NULL) {
    if (len == fc->htablen && memcmp(fc->htabkey,func,fc->htablen)==0) {
      break;
    }
    fc = fc->next;
  }
  if (func[0] == '\0') func[0] = '-';
  if (fc && fc->fc) {
    dump_op_array((eaccelerator_op_array*)fc->fc TSRMLS_CC);
    return;
  }
  ZEND_PUTS("<h5>NOT FOUND</h5>\n");
}

static int eaccelerator_dump(char* file, char* func, char* class TSRMLS_DC)
{
  unsigned int slot;
  mm_cache_entry *p;

  if (file != NULL) {
    EACCELERATOR_UNPROTECT();
    EACCELERATOR_LOCK_RD();
    EACCELERATOR_PROTECT();
    for (slot=0; slot<MM_HASH_SIZE; slot++) {
      p = eaccelerator_mm_instance->hash[slot];
      while (p != NULL) {
        if (strcmp(p->realfilename, file) == 0) {
          goto found;
        }
        p = p->next;
      }
    }
found:
    zend_printf("<h2>FILE: %s</h2>\n", file);
    if (!p) {
      EACCELERATOR_UNPROTECT();
      EACCELERATOR_UNLOCK_RD();
      EACCELERATOR_PROTECT();
      ZEND_PUTS("<h5>NOT FOUND</h5>\n");
      return 0;
    }
    if (class != NULL) {
      if (func != NULL) {
        zend_printf("<h3>CLASS: %s</h3>\n", class);
        zend_printf("<h4>METHOD: %s</h4>\n", func);
        dump_method(p,class,func TSRMLS_CC);
      } else {
        zend_printf("<h3>CLASS: %s</h3>\n", class);
        dump_class(p,class TSRMLS_CC);
      }
    } else if (func != NULL) {
      zend_printf("<h3>FUNCTION: %s</h3>\n", func);
      dump_function(p,func TSRMLS_CC);
    } else {
      dump_cache_entry(p TSRMLS_CC);
    }
    EACCELERATOR_UNPROTECT();
    EACCELERATOR_UNLOCK_RD();
    EACCELERATOR_PROTECT();
    return 1;
  }
  return 0;
}

static int eaccelerator_dump_all(TSRMLS_D)
{
  unsigned int i;
  mm_cache_entry* p;
  char s[1024];

  if (eaccelerator_mm_instance == NULL) {
    return 0;
  }
  EACCELERATOR_UNPROTECT();
  EACCELERATOR_LOCK_RD();
  EACCELERATOR_PROTECT();
  for (i = 0; i < MM_HASH_SIZE; i++) {
    p = eaccelerator_mm_instance->hash[i];
    while (p != NULL) {
      mm_fc_entry *fc = p->c_head;
      zend_printf("<h2>FILE: %s</h2>\n", p->realfilename);
      if (p->op_array);
        dump_op_array(p->op_array TSRMLS_CC);

      while (fc != NULL) {
        eaccelerator_class_entry* c = (eaccelerator_class_entry*)fc->fc;
        Bucket *q = c->function_table.pListHead;
        memcpy(s, fc->htabkey, fc->htablen);
        s[fc->htablen] = '\0';
        if (s[0] == '\000') s[0] = '-';
        zend_printf("<h3>CLASS: %s</h3>\n", s);
        while (q) {
          zend_printf("<h4>METHOD: %s</h4>\n", q->arKey);
          dump_op_array((eaccelerator_op_array*)q->pData TSRMLS_CC);
          q = q->pListNext;
        }
        fc = fc->next;
      }

      fc = p->f_head;
      while (fc != NULL) {
        memcpy(s, fc->htabkey, fc->htablen);
        s[fc->htablen] = '\0';
        if (s[0] == '\000') s[0] = '-';
        zend_printf("<h3>FUNCTION: %s</h3>\n", s);
        dump_op_array((eaccelerator_op_array*)fc->fc TSRMLS_CC);
        fc = fc->next;
      }
      p = p->next;
    }
  }
  EACCELERATOR_UNPROTECT();
  EACCELERATOR_UNLOCK_RD();
  EACCELERATOR_PROTECT();
  return 1;
}

static void eaccelerator_purge()
{
  if (eaccelerator_mm_instance != NULL) {
    mm_cache_entry *p, *q;
    EACCELERATOR_UNPROTECT();
    EACCELERATOR_LOCK_RW();
    p = eaccelerator_mm_instance->removed;
    eaccelerator_mm_instance->rem_cnt = 0;
    eaccelerator_mm_instance->removed = NULL;
    while (p != NULL) {
      q = p->next;
      eaccelerator_free_nolock(p);
      p = q;
    }
    EACCELERATOR_UNLOCK_RW();
    EACCELERATOR_PROTECT();
  }
}
#endif

static void eaccelerator_clean(TSRMLS_D) {
  time_t t;

  t = time(0);
  /* Remove expired scripts from shared memory */
  eaccelerator_prune(t);

  /* Remove expired keys (session data, content) from disk cache */
#ifndef ZEND_WIN32
  /* clear file cache */
  {
    DIR           *dp;
    struct dirent *entry;
    char          s[MAXPATHLEN];

    if ((dp = opendir(MMCG(cache_dir))) != NULL) {
      while ((entry = readdir(dp)) != NULL) {
        if (strstr(entry->d_name,"eaccelerator-user") == entry->d_name) {
          int f;
          strncpy(s, MMCG(cache_dir), MAXPATHLEN-1);
          strlcat(s, "/", MAXPATHLEN);
          strlcat(s, entry->d_name, MAXPATHLEN);
          if ((f = open(s, O_RDONLY | O_BINARY)) > 0) {
            mm_file_header hdr;
            EACCELERATOR_FLOCK(f, LOCK_SH);
            if (read(f, &hdr, sizeof(hdr)) != sizeof(hdr) ||
                strncmp(hdr.magic,"EACCELERATOR",8) != 0 ||
                (hdr.mtime != 0 && hdr.mtime < t)) {
              EACCELERATOR_FLOCK(f, LOCK_UN);
              close(f);
              unlink(s);
            } else {
              EACCELERATOR_FLOCK(f, LOCK_UN);
              close(f);
            }
          }
        }
      }
      closedir(dp);
    }
  }
#else
  {
    HANDLE          hList;
    TCHAR           szDir[MAXPATHLEN];
    WIN32_FIND_DATA FileData;
    char            s[MAXPATHLEN];

    snprintf(szDir, MAXPATHLEN, "%s\\eaccelerator-user*", MMCG(cache_dir));

    if ((hList = FindFirstFile(szDir, &FileData)) != INVALID_HANDLE_VALUE) {
      do {
        int f;
        strncpy(s, MMCG(cache_dir), MAXPATHLEN-1);
        strlcat(s, "\\", MAXPATHLEN);
        strlcat(s, FileData.cFileName, MAXPATHLEN);
        if ((f = open(s, O_RDONLY | O_BINARY)) > 0) {
          mm_file_header hdr;
          EACCELERATOR_FLOCK(f, LOCK_SH);
          if (read(f, &hdr, sizeof(hdr)) != sizeof(hdr) ||
              strncmp(hdr.magic,"EACCELERATOR",8) != 0 ||
              (hdr.mtime != 0 && hdr.mtime < t)) {
            EACCELERATOR_FLOCK(f, LOCK_UN);
            close(f);
            unlink(s);
          } else {
            EACCELERATOR_FLOCK(f, LOCK_UN);
            close(f);
          }
        }
      } while (FindNextFile(hList, &FileData));
    }

    FindClose(hList);
  }
#endif
  /* Remove expired keys (session data, content) from shared memory */
  eaccelerator_gc(TSRMLS_C);
}

static void eaccelerator_clear(TSRMLS_D) {
  unsigned int i;
  mm_cache_entry *p;

  EACCELERATOR_UNPROTECT();
  EACCELERATOR_LOCK_RW();
  for (i = 0; i < MM_HASH_SIZE; i++) {
    p = eaccelerator_mm_instance->hash[i];
    while (p != NULL) {
      mm_cache_entry *r = p;
      p = p->next;
      eaccelerator_mm_instance->hash_cnt--;
      if (r->use_cnt <= 0) {
        eaccelerator_free_nolock(r);
      } else {
        r->removed = 1;
        r->next = eaccelerator_mm_instance->removed;
        eaccelerator_mm_instance->removed = r;
        eaccelerator_mm_instance->rem_cnt++;
      }
    }
    eaccelerator_mm_instance->hash[i] = NULL;
  }
  for (i = 0; i < MM_USER_HASH_SIZE; i++) {
    mm_user_cache_entry* p = eaccelerator_mm_instance->user_hash[i];
    while (p != NULL) {
      mm_user_cache_entry *r = p;
      p = p->next;
      eaccelerator_mm_instance->user_hash_cnt--;
      eaccelerator_free_nolock(r);
    }
    eaccelerator_mm_instance->user_hash[i] = NULL;
  }
  EACCELERATOR_UNLOCK_RW();
  EACCELERATOR_PROTECT();
#ifndef ZEND_WIN32
  /* clear file cache */
  {
    DIR           *dp;
    struct dirent *entry;
    char          s[MAXPATHLEN];

    if ((dp = opendir(MMCG(cache_dir))) != NULL) {
      while ((entry = readdir(dp)) != NULL) {
        if (strstr(entry->d_name,"eaccelerator") == entry->d_name) {
          strncpy(s, MMCG(cache_dir), MAXPATHLEN-1);
          strlcat(s, "/", MAXPATHLEN);
          strlcat(s, entry->d_name, MAXPATHLEN);
          unlink(s);
        }
      }
      closedir(dp);
    }
  }
#else
  {
    HANDLE          hList;
    TCHAR           szDir[MAXPATHLEN];
    WIN32_FIND_DATA FileData;
    char            s[MAXPATHLEN];

    snprintf(szDir, MAXPATHLEN, "%s\\eaccelerator*", MMCG(cache_dir));

    if ((hList = FindFirstFile(szDir, &FileData)) != INVALID_HANDLE_VALUE) {
      do {
        strncpy(s, MMCG(cache_dir), MAXPATHLEN-1);
        strlcat(s, "\\", MAXPATHLEN);
        strlcat(s, FileData.cFileName, MAXPATHLEN);
        unlink(s);
      } while (FindNextFile(hList, &FileData));
    }

    FindClose(hList);
  }
#endif
}

static int cache_entry_compare(const void* p, const void* q) {
  return strcmp((*((mm_cache_entry**)p))->realfilename,(*((mm_cache_entry**)q))->realfilename);
}

static int eaccelerator_login(TSRMLS_D) {
  zval** http_vars = NULL;
  zval** name = NULL;
  zval** pass = NULL;
  char*  admin_name;
  char*  admin_password;

  if (cfg_get_string("eaccelerator.admin.name", &admin_name)==FAILURE || *admin_name == '\0') {
    admin_name = NULL;
  }
  if (cfg_get_string("eaccelerator.admin.password", &admin_password)==FAILURE || *admin_password == '\0') {
    admin_password = NULL;
  }
  if (admin_name == NULL && admin_password == NULL) {
    return 1;
  }
  if (zend_hash_find(&EG(symbol_table), "_SERVER", sizeof("_SERVER"), (void **) &http_vars) != FAILURE && (*http_vars)->type==IS_ARRAY) {
    if (zend_hash_find((*http_vars)->value.ht, "PHP_AUTH_USER", sizeof("PHP_AUTH_USER"), (void **) &name) == FAILURE || (*name)->type!=IS_STRING) {
      name = NULL;
    }
    if (zend_hash_find((*http_vars)->value.ht, "PHP_AUTH_PW", sizeof("PHP_AUTH_PW"), (void **) &pass) == FAILURE || (*pass)->type!=IS_STRING) {
      pass = NULL;
    }
  }
  if (name != NULL && pass != NULL) {
    if (admin_name == NULL || strcmp(admin_name,Z_STRVAL_PP(name)) == 0) {
      if (admin_password != NULL) {
        zval retval;
        zval crypt;
        zval param1;
        zval *params[2];

        ZVAL_STRING(&crypt, "crypt", 0);
        params[0] = *pass;
        INIT_ZVAL(param1);
        params[1] = &param1;
        ZVAL_STRING(params[1], admin_password, 0);
        if (call_user_function(CG(function_table), (zval**)NULL, &crypt, &retval, 2, params TSRMLS_CC) == SUCCESS &&
            retval.type == IS_STRING &&
            Z_STRLEN(retval) == Z_STRLEN_P(params[1]) &&
            strcmp(Z_STRVAL(retval),Z_STRVAL_P(params[1])) == 0) {
          zval_dtor(&retval);
          return 1;
        }
        zval_dtor(&retval);
      } else {
        return 1;
      }
    }
  }
  sapi_add_header_ex("WWW-authenticate: basic realm='eAccelerator'",
                     sizeof("WWW-authenticate: basic realm='eAccelerator'")-1,
                     1, 1 TSRMLS_CC);
  sapi_add_header_ex("HTTP/1.0 401 Unauthorized",
                     sizeof("HTTP/1.0 401 Unauthorized")-1, 1, 1 TSRMLS_CC);
  ZEND_PUTS("You must enter a valid login ID and password to access this resource\n");
  return 0;
}

static void eaccelerator_disable_caching(TSRMLS_D) {
  struct tm tmbuf;
  time_t curtime;
  char s[256];

  time(&curtime);
  strftime(s, 255, "Last-Modified: %a, %d %b %Y %H:%M:%S GMT", php_gmtime_r(&curtime, &tmbuf));

  sapi_add_header_ex("Expires: Thu, 19 Nov 1981 08:52:00 GMT", sizeof("Expires: Thu, 19 Nov 1981 08:52:00 GMT")-1, 1, 1 TSRMLS_CC);
  sapi_add_header_ex(s, strlen(s), 1, 1 TSRMLS_CC);
  sapi_add_header_ex("Cache-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0", sizeof("Cache-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0")-1, 1, 1 TSRMLS_CC);
  sapi_add_header_ex("Pragma: no-cache", sizeof("Pragma: no-cache")-1, 1, 1 TSRMLS_CC);
}

static void eaccelerator_puts_filename(const char* s) {
  int i = 0;
  while (s[i] != '\0') {
    ZEND_PUTC(s[i]);
    if (s[i] == '/' || s[i] == '\\') {
      ZEND_PUTS("<wbr>");
    }
    i++;
  }
}

PHP_FUNCTION(eaccelerator) {
  unsigned int i, j;
  unsigned int available;
  mm_cache_entry* p;
  mm_cache_entry** slots;
  char s[MAXPATHLEN];
  zval** php_self = NULL;
  zval** serv_soft = NULL;
  zval** http_vars = NULL;

  eaccelerator_disable_caching(TSRMLS_C);
  if (eaccelerator_mm_instance == NULL) {
    ZEND_PUTS("eAccelerator ");
    ZEND_PUTS(EACCELERATOR_VERSION);
    ZEND_PUTS(" is not active!\nIt doesn't work in CGI or command line mode!\n\n");
    RETURN_NULL();
  }
  if (!eaccelerator_login(TSRMLS_C)) {
    RETURN_NULL();
  }

  if (zend_hash_find(&EG(symbol_table), "_SERVER", sizeof("_SERVER"), (void **) &http_vars) != FAILURE && (*http_vars)->type==IS_ARRAY) {
    if (zend_hash_find((*http_vars)->value.ht, "PHP_SELF", sizeof("PHP_SELF"), (void **) &php_self) == FAILURE || (*php_self)->type!=IS_STRING) {
      php_self = NULL;
    }
    if (zend_hash_find((*http_vars)->value.ht, "SERVER_SOFTWARE", sizeof("SERVER_SOFTWARE"), (void **) &serv_soft) == FAILURE || (*serv_soft)->type!=IS_STRING) {
      serv_soft = NULL;
    }
  }
  if (zend_hash_find(&EG(symbol_table), "_POST", sizeof("_POST"), (void **) &http_vars) != FAILURE && (*http_vars)->type==IS_ARRAY) {
    if (zend_hash_exists((*http_vars)->value.ht, "enable", sizeof("enable"))) {
      EACCELERATOR_UNPROTECT();
      eaccelerator_mm_instance->enabled = 1;
      EACCELERATOR_PROTECT();
      snprintf(s, MAXPATHLEN, "Location: %s",php_self?(*php_self)->value.str.val:"eaccelerator.php");
      sapi_add_header_ex(s, strlen(s), 1, 1 TSRMLS_CC);
      RETURN_NULL();
    } else if (zend_hash_exists((*http_vars)->value.ht, "disable", sizeof("disable"))) {
      EACCELERATOR_UNPROTECT();
      eaccelerator_mm_instance->enabled = 0;
      EACCELERATOR_PROTECT();
      snprintf(s, MAXPATHLEN, "Location: %s",php_self?(*php_self)->value.str.val:"eaccelerator.php");
      sapi_add_header_ex(s, strlen(s), 1, 1 TSRMLS_CC);
      RETURN_NULL();
#ifdef WITH_EACCELERATOR_OPTIMIZER
    } else if (zend_hash_exists((*http_vars)->value.ht, "enable_opt", sizeof("enable_opt"))) {
      EACCELERATOR_UNPROTECT();
      eaccelerator_mm_instance->optimizer_enabled = 1;
      EACCELERATOR_PROTECT();
      snprintf(s, MAXPATHLEN, "Location: %s",php_self?(*php_self)->value.str.val:"eaccelerator.php");
      sapi_add_header_ex(s, strlen(s), 1, 1 TSRMLS_CC);
      RETURN_NULL();
    } else if (zend_hash_exists((*http_vars)->value.ht, "disable_opt", sizeof("disable_opt"))) {
      EACCELERATOR_UNPROTECT();
      eaccelerator_mm_instance->optimizer_enabled = 0;
      EACCELERATOR_PROTECT();
      snprintf(s, MAXPATHLEN, "Location: %s",php_self?(*php_self)->value.str.val:"eaccelerator.php");
      sapi_add_header_ex(s, strlen(s), 1, 1 TSRMLS_CC);
      RETURN_NULL();
#endif
    } else if (zend_hash_exists((*http_vars)->value.ht, "clear", sizeof("clear"))) {
      eaccelerator_clear(TSRMLS_C);
      snprintf(s, MAXPATHLEN, "Location: %s",php_self?(*php_self)->value.str.val:"eaccelerator.php");
      sapi_add_header_ex(s, strlen(s), 1, 1 TSRMLS_CC);
      RETURN_NULL();
    } else if (zend_hash_exists((*http_vars)->value.ht, "clean", sizeof("clean"))) {
      eaccelerator_clean(TSRMLS_C);
      snprintf(s, MAXPATHLEN, "Location: %s",php_self?(*php_self)->value.str.val:"eaccelerator.php");
      sapi_add_header_ex(s, strlen(s), 1, 1 TSRMLS_CC);
      RETURN_NULL();
#ifdef WITH_EACCELERATOR_DISASSEMBLER
    } else if (zend_hash_exists((*http_vars)->value.ht, "purge", sizeof("purge"))) {
      eaccelerator_purge();
      snprintf(s, MAXPATHLEN, "Location: %s",php_self?(*php_self)->value.str.val:"eaccelerator.php");
      sapi_add_header_ex(s, strlen(s), 1, 1 TSRMLS_CC);
      RETURN_NULL();
    } else if (zend_hash_exists((*http_vars)->value.ht, "dump", sizeof("dump"))) {
      snprintf(s, MAXPATHLEN, "Location: %s?dump=",php_self?(*php_self)->value.str.val:"eaccelerator.php");
      sapi_add_header_ex(s, strlen(s), 1, 1 TSRMLS_CC);
      RETURN_NULL();
#endif
    }
  }

  ZEND_PUTS("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">\n<html>\n<head>\n  <title>eAccelerator</title>\n</head>\n<body>\n");
  ZEND_PUTS("<h1 align=\"center\">eAccelerator ");
  ZEND_PUTS(EACCELERATOR_VERSION);
  ZEND_PUTS("</h1>\n");

#ifdef WITH_EACCELERATOR_DISASSEMBLER
  if (zend_hash_find(&EG(symbol_table), "_GET", sizeof("_GET"), (void **) &http_vars) != FAILURE && (*http_vars)->type==IS_ARRAY) {
    if (zend_hash_exists((*http_vars)->value.ht, "dump", sizeof("dump"))) {
      eaccelerator_dump_all(TSRMLS_C);
      ZEND_PUTS("</body></html>");
      RETURN_NULL();
    } else {
      zval** data;
      char* file  = NULL;
      char* func  = NULL;
      char* class = NULL;
      if (zend_hash_find((*http_vars)->value.ht, "file", sizeof("file"),(void**)&data) != FAILURE) {
        if (PG(magic_quotes_gpc)) {
          php_stripslashes((*data)->value.str.val, &(*data)->value.str.len TSRMLS_CC);
        }
        file = (*data)->value.str.val;
      }
      if (zend_hash_find((*http_vars)->value.ht, "func", sizeof("func"),(void**)&data) != FAILURE) {
        if (PG(magic_quotes_gpc)) {
          php_stripslashes((*data)->value.str.val, &(*data)->value.str.len TSRMLS_CC);
        }
        func = (*data)->value.str.val;
      }
      if (zend_hash_find((*http_vars)->value.ht, "class", sizeof("class"),(void**)&data) != FAILURE) {
        if (PG(magic_quotes_gpc)) {
          php_stripslashes((*data)->value.str.val, &(*data)->value.str.len TSRMLS_CC);
        }
        class = (*data)->value.str.val;
      }
      if (file != NULL) {
        eaccelerator_dump(file, func, class TSRMLS_CC);
        ZEND_PUTS("</body></html>");
        RETURN_NULL();
      }
    }
  }
#endif

  EACCELERATOR_UNPROTECT();
  available = mm_available(eaccelerator_mm_instance->mm);
  EACCELERATOR_LOCK_RD();
  EACCELERATOR_PROTECT();
  ZEND_PUTS("<form method=\"POST\"><input type=\"hidden\" name=\"Horde\" value=\"22c8f7474b79194f32569fc1af447f5b\" /><center>\n");
  if (MMCG(enabled) && eaccelerator_mm_instance->enabled) {
    ZEND_PUTS("<input type=\"submit\" name=\"disable\" value=\"Disable\" title=\"Disable caching of PHP scripts\" style=\"width:100px\">\n");
  } else {
    ZEND_PUTS("<input type=\"submit\" name=\"enable\" value=\"Enable\" title=\"Enable caching of PHP scripts\" style=\"width:100px\">\n");
  }
#ifdef WITH_EACCELERATOR_OPTIMIZER
  if (MMCG(optimizer_enabled) && eaccelerator_mm_instance->optimizer_enabled) {
    ZEND_PUTS("&nbsp;<input type=\"submit\" name=\"disable_opt\" value=\"Disable Opt.\" title=\"Disable optimization of cached PHP scripts\" style=\"width:100px\">\n");
  } else {
    ZEND_PUTS("&nbsp;<input type=\"submit\" name=\"enable_opt\" value=\"Enable Opt.\" title=\"Enable optimization of cached PHP scripts\" style=\"width:100px\">\n");
  }
#endif
  ZEND_PUTS("&nbsp;<input type=\"submit\" name=\"clear\" value=\"Clear\" title=\"Remove all unused scripts and data from shared memory and disk cache\" style=\"width:100px\" onclick=\"if (!window.confirm('Are you sure you want to delete all cached scripts, data, sessions data and content?')) {return false;}\">\n");
  ZEND_PUTS("&nbsp;<input type=\"submit\" name=\"clean\" value=\"Clean\" title=\"Remove all expired scripts and data from shared memory and disk cache\" style=\"width:100px\">\n");
#ifdef WITH_EACCELERATOR_DISASSEMBLER
  ZEND_PUTS("&nbsp;<input type=\"submit\" name=\"purge\" value=\"Purge\" title=\"Remove all 'removed' scripts from shared memory\" style=\"width:100px\" onclick=\"if (!window.confirm('Are you sure you want to delete all \\'removed\\' scripts? This action can cause PHP errors.')) {return false;}\">\n");
  ZEND_PUTS("&nbsp;<input type=\"submit\" name=\"dump\" value=\"Dump\" style=\"width:100px\">\n");
#endif
  ZEND_PUTS("</center></form>\n");

  ZEND_PUTS("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\">\n");
  ZEND_PUTS("<tr valign=\"middle\" bgcolor=\"#9999cc\"><th>eAccelerator support</th><th>enabled</th></tr>\n");
  zend_printf("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%s</td></tr>\n", "Caching Enabled", (MMCG(enabled) && (eaccelerator_mm_instance != NULL) && eaccelerator_mm_instance->enabled)?"true":"false");
  zend_printf("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%s</td></tr>\n", "Optimizer Enabled", (MMCG(optimizer_enabled) && (eaccelerator_mm_instance != NULL) && eaccelerator_mm_instance->optimizer_enabled)?"true":"false");

  format_size(s, eaccelerator_mm_instance->total, 1);
  zend_printf("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%s</td></tr>\n", "Memory Size", s);
  format_size(s, available, 1);
  zend_printf("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%s</td></tr>\n", "Memory Available", s);
  format_size(s, eaccelerator_mm_instance->total - available, 1);
  zend_printf("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%s</td></tr>\n", "Memory Allocated", s);
  zend_printf("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%u</td></tr>\n", "Cached Scripts", eaccelerator_mm_instance->hash_cnt);
  zend_printf("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%u</td></tr>\n", "Removed Scripts", eaccelerator_mm_instance->rem_cnt);
  zend_printf("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%u</td></tr>\n", "Cached Keys", eaccelerator_mm_instance->user_hash_cnt);
  ZEND_PUTS("</table><br>\n");

  slots = do_alloca(sizeof(mm_cache_entry*)*(eaccelerator_mm_instance->hash_cnt>eaccelerator_mm_instance->rem_cnt?eaccelerator_mm_instance->hash_cnt:eaccelerator_mm_instance->rem_cnt));
  j = 0;
  for (i = 0; i < MM_HASH_SIZE; i++) {
    p = eaccelerator_mm_instance->hash[i];
    while (p != NULL) {
      slots[j++] = p;
      p = p->next;
    }
  }
  qsort(slots, j, sizeof(mm_cache_entry*), cache_entry_compare);
  ZEND_PUTS("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"900\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed;word-break:break-all\">\n");
  ZEND_PUTS("<tr valign=\"middle\" bgcolor=\"#9999cc\"><th width=\"490\">Cached Script</th><th width=\"200\">MTime</th><th width=\"70\">Size</th><th width=\"70\">Reloads</th><th width=\"70\">Hits</th></tr>\n");
  for (i = 0; i < j; i++) {
    p = slots[i];
    format_size(s, p->size, 0);
#ifdef WITH_EACCELERATOR_DISASSEMBLER
    zend_printf("<tr valign=\"bottom\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\"><b><a href=\"%s?file=%s\"&Horde=22c8f7474b79194f32569fc1af447f5b>",
      php_self?(*php_self)->value.str.val:"",
      p->realfilename);
    eaccelerator_puts_filename(p->realfilename);
    zend_printf("</a></b></td><td>%s</td><td align=\"right\">%s</td><td align=\"right\">%d (%d)</td><td align=\"right\">%d</td></tr>\n",
      ctime(&p->mtime), s, p->nreloads, p->use_cnt, p->nhits);
#else
    ZEND_PUTS("<tr valign=\"bottom\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\"><b>");
    eaccelerator_puts_filename(p->realfilename);
    zend_printf("</b></td><td>%s</td><td align=\"right\">%s</td><td align=\"right\">%d</td><td align=\"right\">%d</td></tr>\n",
      ctime(&p->mtime), s, p->nreloads, p->nhits);
#endif
  }
  ZEND_PUTS("</table>\n<br>\n");

  j = 0;
  p = eaccelerator_mm_instance->removed;
  while (p != NULL) {
    slots[j++] = p;
    p = p->next;
  }
  qsort(slots, j, sizeof(mm_cache_entry*), cache_entry_compare);
  ZEND_PUTS("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"900\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed;word-break:break-all\">\n");
  ZEND_PUTS("<tr valign=\"middle\" bgcolor=\"#9999cc\"><th width=\"490\">Removed Script</th><th width=\"200\">MTime</th><th width=\"70\">Size</th><th width=\"70\">Reloads</th><th width=\"70\">Used</th></tr>\n");
  for (i = 0; i < j; i++) {
    p = slots[i];
    ZEND_PUTS("<tr valign=\"bottom\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>");
    eaccelerator_puts_filename(p->realfilename);
    zend_printf("</b></td><td>%s</td><td align=\"right\">%d</td><td align=\"right\">%d</td><td align=\"right\">%d</td></tr>\n",
      ctime(&p->mtime), p->size, p->nreloads, p->use_cnt);
  }
  ZEND_PUTS("</table>\n<br>\n");
#ifdef WITH_EACCELERATOR_DISASSEMBLER
  ZEND_PUTS("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"900\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed;word-break:break-all\">\n");
  ZEND_PUTS("<tr valign=\"middle\" bgcolor=\"#9999cc\"><th width=\"400\">Cached Key</th><th width=\"400\">Value</th><th width=\"100\">Expired</th></tr>\n");
  for (i = 0; i < MM_USER_HASH_SIZE; i++) {
    mm_user_cache_entry *p = eaccelerator_mm_instance->user_hash[i];
    while (p != NULL) {
      ZEND_PUTS("<tr valign=\"top\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>");
      ZEND_PUTS(p->key);
      ZEND_PUTS("</b></td><td>");
      dump_zval(&p->value, 1);
      if (p->ttl) {
        time_t t = time(0);
        if (p->ttl < t) {
          ZEND_PUTS("</td><td align=\"right\">expired</td></tr>\n");
        } else {
          unsigned long ttl = p->ttl - t;
          zend_printf("</td><td align=\"right\">%lu sec</td></tr>\n",ttl);
        }
      } else {
        ZEND_PUTS("</td><td align=\"right\">never</td></tr>\n");
      }
      p = p->next;
    }
  }
  ZEND_PUTS("</table>\n<br>\n");
#endif
  free_alloca(slots);
  EACCELERATOR_UNPROTECT();
  EACCELERATOR_UNLOCK_RD();
  EACCELERATOR_PROTECT();

  ZEND_PUTS("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"900\" align=\"center\" style=\"table-layout:fixed\"><tr><td align=\"center\"><hr><font size=\"1\">\n");
  zend_printf("<nobr>eAccelerator %s [shm:%s sem:%s],</nobr>\n<nobr>PHP %s [ZE %s",
              EACCELERATOR_VERSION, mm_shm_type(), mm_sem_type(), PHP_VERSION, ZEND_VERSION);
#if defined(ZEND_DEBUG) && ZEND_DEBUG
  ZEND_PUTS(" DEBUG");
#endif
#ifdef ZTS
  ZEND_PUTS(" TS");
#endif
  ZEND_PUTS("],</nobr>\n");
  if (serv_soft == NULL) {
    zend_printf("<nobr>%s,</nobr>\n", sapi_module.pretty_name);
  } else {
    zend_printf("<nobr>%s [%s],</nobr>\n", sapi_module.pretty_name, (*serv_soft)->value.str.val);
  }

  {
    char *s = php_get_uname();
    zend_printf("<nobr>%s</nobr>\n",s);
    efree(s);
  }
  ZEND_PUTS("<br>Produced by <a href=\"http://eaccelerator.sourceforge.net\">eAccelerator</a>.");
  if (PG(expose_php)) {
    ZEND_PUTS("<br><br><a href=\"http://eaccelerator.sourceforge.net\"><img border=\"0\" src=\"");
    if (SG(request_info).request_uri) {
      ZEND_PUTS(SG(request_info).request_uri);
    }
    ZEND_PUTS("?="EACCELERATOR_LOGO_GUID"\" align=\"middle\" alt=\"eAccelerator logo\" /></a>\n");
  }
  ZEND_PUTS("</font></td></tr></table></body></html>");
  RETURN_NULL();
}

#endif  /* #ifdef HAVE_EACCELERATOR */
