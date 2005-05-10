/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 - 2005 eAccelerator                               |
   | http://eaccelerator.net                                  			  |
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
   | Author(s): Dmitry Stogov <dstogov@users.sourceforge.net>             |
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

#include "webui.h"
#include "debug.h"
#include "shm.h"
#include "session.h"
#include "content.h"
#include "cache.h"

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

#define MAX_DUP_STR_LEN 256

/* Globals (different for each process/thread) */
ZEND_DECLARE_MODULE_GLOBALS(eaccelerator)

/* Globals (common for each process/thread) */
static long eaccelerator_shm_size = 0;
long eaccelerator_shm_max = 0;
static long eaccelerator_shm_ttl = 0;
static long eaccelerator_shm_prune_period = 0;
static long eaccelerator_debug = 0;
static zend_bool eaccelerator_check_mtime = 1;
static zend_bool eaccelerator_scripts_shm_only = 0;

eaccelerator_mm* eaccelerator_mm_instance = NULL;
static int eaccelerator_is_zend_extension = 0;
static int eaccelerator_is_extension      = 0;
static zend_extension* ZendOptimizer = NULL;

static HashTable eaccelerator_global_function_table;
static HashTable eaccelerator_global_class_table;

int binary_eaccelerator_version;
int binary_php_version;
int binary_zend_version;

FILE *F_fp;

/* pointer to the properties_info hashtable destructor */
static dtor_func_t properties_info_dtor = NULL;

/* saved original functions */
static zend_op_array *(*mm_saved_zend_compile_file)(zend_file_handle *file_handle, int type TSRMLS_DC);

#if defined(PROFILE_OPCODES) || defined(WITH_EACCELERATOR_EXECUTOR)
static void (*mm_saved_zend_execute)(zend_op_array *op_array TSRMLS_DC);
#endif

/* external declarations */
PHPAPI void php_stripslashes(char *str, int *len TSRMLS_DC);

ZEND_DLEXPORT zend_op_array* eaccelerator_compile_file(zend_file_handle *file_handle, int type TSRMLS_DC);

/******************************************************************************/
/* hash mm functions														  */
/* TODO: insert items sorted in buckets, so searching in buckets goes from 	  */
/*			O(n) to O(log n)
/******************************************************************************/

/* Create a key for the scripts hashtable. This is only used when eA can't use
   inodes. */
inline unsigned int hash_mm(const char *data, int len) {
  unsigned int h;
  const char *e = data + len;
  for (h = 2166136261U; data < e; ) {
    h *= 16777619;
    h ^= *data++;
  }
  return h;
}

/* Find a script entry with the given hash key */
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

/* Add a new entry to the hashtable */
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

/* Initialise the shared memory */
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
  eaccelerator_mm_instance = mm_malloc_lock(mm, sizeof(*eaccelerator_mm_instance));
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

/* Clean up the shared memory */
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

/******************************************************************************/
/* Prepare values to cache them												  */
/******************************************************************************/

#define FIXUP(x) if((x)!=NULL) {(x) = (void*)(((char*)(x)) + ((long)(MMCG(mem))));}

typedef void (*fixup_bucket_t)(void* TSRMLS_DC);

#define fixup_zval_hash(from) \
  fixup_hash(from, (fixup_bucket_t)fixup_zval TSRMLS_CC)

#ifdef ZEND_ENGINE_2
static void fixup_property_info(zend_property_info* from TSRMLS_DC) {
  FIXUP(from->name);
}
#endif

/* Prepare a zend HashTable for caching */
static void fixup_hash(HashTable* source, fixup_bucket_t fixup_bucket TSRMLS_DC) 
{
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

/* Prepare a zval for caching */
void fixup_zval(zval* zv TSRMLS_DC) {
  switch (zv->type & ~IS_CONSTANT_INDEX) {
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

/* Prepare an opcode array for caching */
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

/* Prepare a class entry for caching */
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

/* Prepare a cache entry for caching */
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

/* Function to create a hash key when filenames are used */
int eaccelerator_md5(char* s, const char* prefix, const char* key TSRMLS_DC) {
#if defined(PHP_MAJOR_VERSION) && defined(PHP_MINOR_VERSION) && \
    ((PHP_MAJOR_VERSION > 4) || (PHP_MAJOR_VERSION == 4 && PHP_MINOR_VERSION > 1))
  char md5str[33];
  PHP_MD5_CTX context;
  unsigned char digest[16];

  md5str[0] = '\0';
  PHP_MD5Init(&context);
  PHP_MD5Update(&context, (unsigned char*)key, strlen(key));
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

/* Remove expired keys, content and scripts from the cache */
void eaccelerator_prune(time_t t) {
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

/* Allocate a new cache chunk */
void* eaccelerator_malloc2(size_t size TSRMLS_DC) {
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

/******************************************************************************/
/* Cache file functions.													  */
/* TODO: create cache subdirectories -> speed improvement highly used servers */
/******************************************************************************/

/* Retrieve a cache entry from the cache directory */
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

/* Add a cache entry to the cache directory */
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

/******************************************************************************/
/* Functions to calculate the size of different structure that a compiled php */
/* script contains.															  */
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

/* Calculate the size of a point to a class entry */
static void calc_class_entry_ptr(zend_class_entry** from TSRMLS_DC) {
  calc_class_entry(*from TSRMLS_CC);
}
#endif

/* Calculate the size of an HashTable */
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

void calc_zval(zval* zv TSRMLS_DC) {
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

/* Calculate the size of an op_array */
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

/* Calculate the size of a class entry */
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

/* Calculate the size of a cache entry with its given op_array and function and
   class bucket */
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
/* Functions to store/cache data from the compiled script					  */
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

void store_zval(zval* zv TSRMLS_DC) {
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

/* Create a cache entry from the given op_array, functions and classes of a 
   script */
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
/* Adds the data from the compilation of the script to the cache */
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
/* Functions to restore a php script from shared memory						  */
/******************************************************************************/

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

void restore_zval(zval *zv TSRMLS_DC)
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

#ifdef ZEND_ENGINE_2
  int    fname_len;
  char  *fname_lc;
#endif

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

#ifdef ZEND_ENGINE_2

  if (to->function_name)
  {
    fname_len = strlen(to->function_name);
    fname_lc  = zend_str_tolower_dup(to->function_name, fname_len);
  }

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
#ifdef ZEND_ENGINE_2
			fname_lc, fname_len+1,
#else
			to->function_name, strlen(to->function_name)+1,
#endif
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
#ifdef ZEND_ENGINE_2
  int   cname_len;
  char *cname_lc;
  Bucket *p;
  union _zend_function *old_ctor;
#endif

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
#ifdef ZEND_ENGINE_2    
    char *name_lc  = zend_str_tolower_dup(from->parent, name_len);

    if (zend_hash_find(CG(class_table), (void *)name_lc, name_len+1, (void **)&to->parent) != SUCCESS)
#else
    if (zend_hash_find(CG(class_table), (void *)from->parent, name_len+1, (void **)&to->parent) != SUCCESS)
#endif
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
    }
    efree(name_lc);
#else
	  to->handle_property_get  = to->parent->handle_property_get;
      to->handle_property_set  = to->parent->handle_property_set;
      to->handle_function_call = to->parent->handle_function_call;
    }
#endif
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
  to->properties_info.pDestructor = properties_info_dtor;
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
  cname_len = to->name_length;
  cname_lc  = zend_str_tolower_dup(to->name, cname_len);
  old_ctor = to->constructor;

  p = to->function_table.pListHead;
  while (p != NULL) {
    f         = p->pData;
    fname_len = strlen(f->common.function_name);
    fname_lc  = zend_str_tolower_dup(f->common.function_name, fname_len);

    if (fname_len == cname_len && !memcmp(fname_lc, cname_lc, fname_len) 
    	&& to->constructor == old_ctor && f->common.scope != to->parent)
      to->constructor = (zend_function*)f;
    else if (fname_lc[0] == '_' && fname_lc[1] == '_' && f->common.scope != to->parent)
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

/* Try to restore a file from the cache. If the file isn't found in memory, the 
   the disk cache is checked */
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

/* Check if the file is ok to cache */
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
  zend_is_auto_global("_GET", sizeof("_GET")-1 TSRMLS_CC);
  zend_is_auto_global("_POST", sizeof("_POST")-1 TSRMLS_CC);
  zend_is_auto_global("_COOKIE", sizeof("_COOKIE")-1 TSRMLS_CC);
  zend_is_auto_global("_SERVER", sizeof("_SERVER")-1 TSRMLS_CC);
  zend_is_auto_global("_ENV", sizeof("_ENV")-1 TSRMLS_CC);
  zend_is_auto_global("_REQUEST", sizeof("_REQUEST")-1 TSRMLS_CC);
  zend_is_auto_global("_FILES", sizeof("_FILES")-1 TSRMLS_CC);
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
      file_handle->opened_path = MMCG(mem);
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

        zend_try {
          new_t = eaccelerator_load(
            t->opcodes[0].op1.u.constant.value.str.val,
            t->opcodes[0].op1.u.constant.value.str.len TSRMLS_CC);
        } zend_catch {
            CG(function_table)	= orig_function_table;
            CG(class_table)		= orig_class_table;
            bailout				= 1;
        } zend_end_try();
        if (bailout) {
          zend_bailout ();
        }
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
void format_size(char* s, unsigned int size, int legend) {
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

/* eAccelerator entry for phpinfo() */
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

/* let eaccelerator crash */
#ifdef WITH_EACCELERATOR_CRASH
PHP_FUNCTION(eaccelerator_crash) {
  char *x = NULL;
  strcpy(x,"Hello");
}
#endif

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
#ifdef WITH_EACCELERATOR_SHM
ZEND_INI_ENTRY("eaccelerator.keys",             "shm_and_disk", PHP_INI_SYSTEM, eaccelerator_OnUpdateKeysCachePlace)
#endif
#ifdef WITH_EACCELERATOR_SESSIONS
ZEND_INI_ENTRY("eaccelerator.sessions",         "shm_and_disk", PHP_INI_SYSTEM, eaccelerator_OnUpdateSessionCachePlace)
#endif
#ifdef WITH_EACCELERATOR_CONTENT_CACHING
ZEND_INI_ENTRY("eaccelerator.content",          "shm_and_disk", PHP_INI_SYSTEM, eaccelerator_OnUpdateContentCachePlace)
#endif
STD_PHP_INI_ENTRY("eaccelerator.cache_dir",      "/tmp/eaccelerator", PHP_INI_SYSTEM, OnUpdateString, cache_dir, zend_eaccelerator_globals, eaccelerator_globals)
PHP_INI_ENTRY("eaccelerator.filter",             "",  PHP_INI_ALL, eaccelerator_filter)
STD_PHP_INI_ENTRY("eaccelerator.name_space",      "", PHP_INI_SYSTEM, OnUpdateString, name_space, zend_eaccelerator_globals, eaccelerator_globals)
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

static void __attribute__((destructor)) eaccelerator_clean_shutdown(void) {
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

/* signal handlers */
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
  eaccelerator_globals->name_space        = '\000';
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

/* This function creates a dummy class entry to steal the pointer to the 
 * properties_info hashtable destructor because it's declared static */
static dtor_func_t get_zend_destroy_property_info(TSRMLS_D) {
  zend_class_entry dummy_class_entry;
  dummy_class_entry.type = ZEND_USER_CLASS; 

  zend_initialize_class_data(&dummy_class_entry, 1 TSRMLS_CC); 

  dtor_func_t property_dtor = dummy_class_entry.properties_info.pDestructor;

  zend_hash_destroy(&dummy_class_entry.default_properties);
  zend_hash_destroy(&dummy_class_entry.properties_info);
  zend_hash_destroy(dummy_class_entry.static_members);
  zend_hash_destroy(&dummy_class_entry.function_table);
  FREE_HASHTABLE(dummy_class_entry.static_members);
  zend_hash_destroy(&dummy_class_entry.constants_table);
  
  return property_dtor;
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
#ifndef HAS_ATTRIBUTE
    atexit(eaccelerator_clean_shutdown);
#endif
  }
#if defined(WITH_EACCELERATOR_SESSIONS) && defined(HAVE_PHP_SESSIONS_SUPPORT)
    if (!eaccelerator_session_registered()) {
      eaccelerator_register_session();
    }
#endif
#ifdef WITH_EACCELERATOR_CONTENT_CACHING
    eaccelerator_content_cache_startup();
#endif
  if (!eaccelerator_is_zend_extension) {
    register_eaccelerator_as_zend_extension();
  }
  
  /* cache the properties_info destructor */
  properties_info_dtor = get_zend_destroy_property_info(TSRMLS_C);
  
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
#ifdef WITH_EACCELERATOR_WEBUI
  PHP_FE(eaccelerator, NULL)
#endif
#ifdef WITH_EACCELERATOR_SHM
  PHP_FE(eaccelerator_put, NULL)
  PHP_FE(eaccelerator_get, NULL)
  PHP_FE(eaccelerator_rm, NULL)
  PHP_FE(eaccelerator_gc, NULL)
  PHP_FE(eaccelerator_lock, NULL)
  PHP_FE(eaccelerator_unlock, NULL)
#endif
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

static const unsigned char eaccelerator_logo[] = {
      71,  73,  70,  56,  57,  97,  88,   0,  31,   0, 
     213,   0,   0, 150, 153, 196, 237, 168,  86, 187, 
     206, 230,   4,   4,   4, 108, 110, 144,  99, 144, 
     199, 136, 138, 184,  87,  88, 109, 165, 165, 167, 
     163, 166, 202, 240, 151,  44, 149,  91,  21, 225, 
     225, 229,   4,  76, 164, 252, 215, 171, 255, 255, 
     255, 212, 212, 224, 241, 200, 149, 141, 144, 192, 
     216, 216, 226, 251, 230, 205, 192, 193, 218, 207, 
     221, 238, 181, 188, 216, 130, 132, 168, 150, 152, 
     185, 152, 152, 154, 180, 181, 198, 215, 184, 147, 
      40, 102, 177, 224, 232, 242, 244, 244, 244, 235, 
     236, 239, 118, 121, 157, 193, 193, 194, 146, 148, 
     174, 181, 143,  96, 154, 183, 219, 156, 159, 200, 
     126, 128, 170, 174, 175, 193,  65,  39,   7, 232, 
     214, 192, 254, 241, 226, 246, 246, 248, 108,  65, 
      13, 142, 144, 185, 252, 224, 189, 138, 171, 213, 
      69, 122, 188, 239, 244, 249,  48,  49,  60, 176, 
     178, 209, 200, 201, 222, 252, 252, 253, 251, 251, 
     251, 162, 163, 187, 208, 208, 213, 169, 171, 205, 
     241, 234, 225, 255, 252, 249, 254, 248, 241, 140, 
     142, 169, 249, 186, 111,  33, 249,   4,   0,   0, 
       0,   0,   0,  44,   0,   0,   0,   0,  88,   0, 
      31,   0,   0,   6, 255,  64, 137, 112,  72,  44, 
      26, 143, 200, 164, 114, 201,  92,  62, 158, 208, 
     168, 116,  74, 173,  90, 175, 216, 108,  85, 168, 
     237, 122, 191,  15,  25, 163, 118, 209, 153,   0, 
      68, 128,  73, 119, 169,  49, 100,  86,  46, 120, 
      78, 127, 216,  60,  21,  93,  83,   8, 208,  85, 
      60,  54,  83, 114, 117, 132,  89,  32,  23,  38, 
      73,  38, 103,  72,  38,  23,  32,  82, 123, 146, 
     147,  69, 106,   9,  52,  21,  19,  53, 137, 138, 
      51,  51, 156, 141,  21, 100,  52,   9, 148, 166, 
     123,   0, 106, 126,  16,  21, 104,  67, 169, 106, 
     140,   9,   3, 159, 140,  18, 176, 182,   0,  23, 
      21, 101,  67,  21,  80,  32,  52, 192,  52,  44, 
       6,  16,  15,   6,  23,  44,  81,  24,  81,  25, 
      81, 194,  80,  25,   6,  18,  12,  80,  23,  15, 
     169,  15, 172, 155, 105,  33,   7,   4, 158,  46, 
       0,  33,   3,   7,   7,  51,   7, 139, 231, 225, 
       7,  25, 124,  23,  23,  52, 190,  19,   4, 205, 
      44,  27,   4,   4,  19,  57,  15,  33,  15,  32, 
      54,  64, 168, 241,   0,   5,  10,  28, 255,  54, 
     160, 120, 128, 163, 160,  65,   2,  15, 244, 229, 
     200, 113, 162,  26,   4,  20, 252,  22, 130, 128, 
      48,  98, 131,  30,  34,  38, 102, 208,  58,  64, 
     203,   4,  73, 115,   3,   6, 184, 152,  69,  75, 
     228,   1,  87,  38,  80, 204,  19, 242, 235,   9, 
      54,  31,   4,  78, 212, 152, 192, 128,   1,   8, 
     255,  31,  79,  78, 108,  99, 245, 239,  24,   3, 
     136,  16,  32, 212, 139,  24,  34,  83,   8,   3, 
      62,  33, 128,  56,  90,   3,  68, 136,  17, 173, 
     138, 176,  76,  16, 114,  64,   2, 145,   4,   0, 
     136, 196, 128, 129,  22, 215, 146,  66,  50, 224, 
     248,  40,  33, 147,  62,   2,  11,  39, 120, 120, 
      48, 162,  33,  10,   2,  44,  62,  64, 156, 144, 
     244,  31, 129,  17,  12,  31, 240, 157, 240,  76, 
     238, 131,  12,   4, 160, 110,   4,   1, 130, 192, 
       6,   6,  33, 112, 212,  48, 226, 195, 220,  25, 
     145,  95, 189, 118,  77,  64,  50, 236, 172,  79, 
      66,  92, 140,  96,  11,   0,  71,  78,  12,  39, 
      12, 108, 200,  71,  32,  68, 190,  16,  38,  70, 
      56,  54, 120,  87,  71, 136,  16,  25,  70,  36, 
     160, 141,  65,  33,  10,  12,  57,  13,  36, 240, 
     173,  15,  64,   2,  31,  58, 186, 189,  34,  96, 
     238,  86,  73,  90, 198,  75, 146,  12,   1, 128, 
     249, 203, 208,  62,  74,   9,  49,  96,   0,   3, 
      53,   9,   6,  78, 220,  78, 141, 218, 251, 137, 
      19, 168, 199, 163, 230,  46, 254, 246, 120, 247, 
     169, 193, 183, 127, 234,  34,  67,   6,  63,  51, 
     249, 156,  12,  55, 160, 181,  57, 114, 255, 137, 
      52,   3, 127,  62,  12, 129,  65, 118,  74, 112, 
     247,  29, 120, 219,  81, 163, 224, 130, 224,  61, 
      40, 225, 130,  15, 222, 162,  74,  60, 160, 116, 
     181,  95,   6,  36, 189, 212, 255, 225, 103,  39, 
      97, 224,  74, 119, 186, 157,  98, 162,  18, 169, 
     172,  65,   3,  13,  40, 184, 178, 149,  14,   9, 
     160,  97,   2,  87,  18, 204, 104,  66,  89,  51, 
      24,  23, 163,  16,  62, 132, 224, 195,  90,  39, 
       6, 121, 132,  37,  58, 176, 136, 195,  59,  46, 
     152, 133,  98, 135, 174,   8, 129, 129, 143,  56, 
      36, 160,  93,  33,  84,  82,  97,   3,  11,  12, 
     136, 128,   3,  66, 227,  44,  49,   2, 110,  67, 
     248, 224, 131,   6,  34,  48, 176, 204,   3, 131, 
      84, 169, 230,  19,  44, 128, 144, 195,   6,  71, 
     190, 179,   4,  26,  46, 136, 169,   1,  10,  57, 
     236, 192,   2,  15,  79, 164, 185, 166, 154,  55, 
     124, 192, 192, 155,   8, 228,  54, 130,  11, 136, 
     134,  86, 167, 152,  99,  34, 176,  65,  14,  12, 
     124,  16,   8,  20, 126,  62, 176,   2,   5,  20, 
     188, 160, 233, 166, 155,  58, 224, 105,   4,  61, 
     252,  73,  69, 160,  59,  12,  42,   2,  10,   8, 
      32, 160, 193, 170, 171, 166, 138, 130,   8, 144, 
     238, 240,   1, 159, 145,  72,   0,   5,  15,  20, 
      56,  16,  65,   4,  63,   4, 224, 171,   2,   1, 
       0, 171, 192, 176, 195,   6,  32, 234,  21, 129, 
     126,  48,  85,  79, 204,  50,  43, 235,   7,  55, 
     196,  97, 171, 165, 159, 254, 240, 195, 176,  63, 
     188, 240,   0, 173,  80, 244, 192, 194, 153,   2, 
     192, 113, 236, 184, 114, 172,  96, 174, 185, 152, 
      82, 160, 255, 133,   7,  13,  20, 240, 132,  12, 
       5, 196,  80, 192,  92,  15, 192,  32, 175,   0, 
      15,  88,  16, 195, 190,  22, 148, 224, 238,  19, 
     250, 242,  27, 176, 188, 226, 198, 128, 205, 190, 
     252, 202,  96, 111,   1, 248,  62, 128, 112,   1, 
      22,  80, 106, 235,  11,  14,  80, 188, 107, 175, 
     190, 250, 106,  45, 198,   1, 144, 192, 193,  19, 
       5,  52, 208,  64,  24,  29, 116,  96, 111,   3, 
     240,  54,   0,  67, 200,   2,   8, 160,  50,  12, 
      30, 216,  11, 133, 203,  48, 192,  76,  51,  12, 
     237,  62, 112,  65, 187,  54, 216, 107, 178,   7, 
      49, 168,  28,  52, 190, 237, 194,  80, 178, 196, 
      43, 108,  28, 172,   2,  11, 120, 204, 193, 211, 
      17, 112, 176, 235, 211,  28, 236, 240,  68,   7, 
      33, 151, 128, 179, 184,  45,  55,  32,  64, 180, 
      22, 120, 224, 114, 203,  10, 199,  48, 179, 215, 
     225, 186,  12, 178, 217,  33, 163,  92, 175, 217, 
      22, 120, 125, 131,  13, 242,  62, 224, 117, 189, 
      35, 247,  41, 129,   3, 196,  50, 189,  64,  11, 
      36, 216,  48, 183,  29,  79,   4,  98, 195, 164, 
      46, 119,  80, 180, 217, 103,  75, 225, 178, 200, 
       2, 200, 252, 196, 227, 104, 139,  44, 242,   5, 
      50,  52, 160,  56,  12, 111,  63, 160, 246,  19, 
     246, 218,  96, 121, 206, 122, 191, 224, 247, 223, 
      45, 164, 238, 244, 174, 172,  63, 173, 238,   3, 
       5, 152,  28,  50, 206,  17,  43,  76, 154,  51, 
     232,  93,  67,  33, 185, 231, 121, 243,  46,  64, 
      12,  29,  60,  80, 130, 208, 193, 203, 204,  46, 
     231,  50,  96, 109, 119, 205,  13, 208, 139, 166, 
     173,  42, 144, 176,  64,  10, 212,  87, 159, 250, 
       2, 216, 103, 191, 192,  14, 129, 168, 252, 132, 
     202,  33,   7,  93, 188, 230, 138, 139, 221,   0, 
     191,  56, 239, 203, 240, 249,  49,  88, 160, 118, 
     220,  43, 155, 189, 115, 204, 140, 227, 172, 120, 
     243, 118, 135, 139, 245, 164, 149,  78,  74, 248, 
     220, 130, 187, 193, 224,  30,  16,  51, 113, 149, 
     160,   4,  54, 208,  90,   9, 102,  86,  51,  56, 
     196, 172, 102,  98, 171,  25,  12,  74, 240,  64, 
     152, 197, 204,  38,  53, 139,  88, 189,  90, 182, 
     192, 201, 213, 236,  76,  48, 179, 129,   5,  96, 
     166, 183, 113, 153,  80,  77,  66,  74, 161,  10, 
      41,  17,   4,   0,  59,   0 };
 
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

#endif  /* #ifdef HAVE_EACCELERATOR */
