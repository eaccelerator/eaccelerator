/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 - 2006 eAccelerator                               |
   | http://eaccelerator.net																						  |
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

#include "eaccelerator.h"
#include "eaccelerator_version.h"

#ifdef HAVE_EACCELERATOR

#include "opcodes.h"

#include "zend.h"
#include "zend_API.h"
#include "zend_extensions.h"

#include "debug.h"
#include "shm.h"
#include "session.h"
#include "content.h"
#include "cache.h"
#include "ea_store.h"
#include "ea_restore.h"
#include "ea_info.h"
#include "ea_dasm.h"

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

#include "php.h"
#include "php_ini.h"
#include "php_logos.h"
#include "main/fopen_wrappers.h"
#include "ext/standard/info.h"
#include "ext/standard/php_incomplete_class.h"
#include "ext/standard/md5.h"

#include "SAPI.h"

#define MAX_DUP_STR_LEN 256

/* Globals (different for each process/thread) */
ZEND_DECLARE_MODULE_GLOBALS(eaccelerator)

/* Globals (common for each process/thread) */
static long ea_shm_size = 0;
long ea_shm_max = 0;
static long ea_shm_ttl = 0;
static long ea_shm_prune_period = 0;
extern long eaccelerator_debug;
static zend_bool eaccelerator_check_mtime = 1;
zend_bool eaccelerator_scripts_shm_only = 0;

eaccelerator_mm* eaccelerator_mm_instance = NULL;
static int eaccelerator_is_zend_extension = 0;
static int eaccelerator_is_extension      = 0;
zend_extension* ZendOptimizer = NULL;

static HashTable eaccelerator_global_function_table;
static HashTable eaccelerator_global_class_table;

int binary_eaccelerator_version[2];
int binary_php_version[2];
int binary_zend_version[2];

#ifdef ZEND_ENGINE_2
/* pointer to the properties_info hashtable destructor */
extern dtor_func_t properties_info_dtor;
#endif

/* saved original functions */
static zend_op_array *(*mm_saved_zend_compile_file)(zend_file_handle *file_handle, int type TSRMLS_DC);

#ifdef DEBUG
static void (*mm_saved_zend_execute)(zend_op_array *op_array TSRMLS_DC);
#endif

/* external declarations */
PHPAPI void php_stripslashes(char *str, int *len TSRMLS_DC);

ZEND_DLEXPORT zend_op_array* eaccelerator_compile_file(zend_file_handle *file_handle, int type TSRMLS_DC);

/******************************************************************************/
/* hash mm functions                                                          */
/******************************************************************************/

/* Find a script entry with the given hash key */
static ea_cache_entry* hash_find_mm(const char  *key,
                                    struct stat *buf,
                                    int         *nreloads,
                                    time_t      ttl) {
  unsigned int hv, slot;
  ea_cache_entry *p, *q;

#ifdef EACCELERATOR_USE_INODE
  hv = buf->st_dev + buf->st_ino;
#else
  hv = zend_get_hash_value(key, strlen(key));
#endif
  slot = hv & EA_HASH_MAX;

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
static void hash_add_mm(ea_cache_entry *x) {
  ea_cache_entry *p,*q;
  unsigned int slot;
#ifdef EACCELERATOR_USE_INODE
  slot = (x->st_dev + x->st_ino) & EA_HASH_MAX;
#else
  x->hv = zend_get_hash_value(x->realfilename, strlen(x->realfilename));
  slot = x->hv & EA_HASH_MAX;
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

#ifdef ZEND_WIN32
    snprintf(mm_path, MAXPATHLEN, "%s.%s", EACCELERATOR_MM_FILE, sapi_module.name);
#else
    snprintf(mm_path, MAXPATHLEN, "%s.%s%d", EACCELERATOR_MM_FILE, sapi_module.name, owner);
#endif
/*  snprintf(mm_path, MAXPATHLEN, "%s.%s%d", EACCELERATOR_MM_FILE, sapi_module.name, geteuid());*/
  if ((eaccelerator_mm_instance = (eaccelerator_mm*)mm_attach(ea_shm_size*1024*1024, mm_path)) != NULL) {
#ifdef ZTS
    ea_mutex = tsrm_mutex_alloc();
#endif
    return SUCCESS;
  }
  mm = mm_create(ea_shm_size*1024*1024, mm_path);
  if (!mm) {
    return FAILURE;
  }
#ifdef ZEND_WIN32
  DBG(ea_debug_printf, (EA_DEBUG, "init_mm [%d]\n", owner));
#else
  DBG(ea_debug_printf, (EA_DEBUG, "init_mm [%d,%d]\n", owner, getppid()));
#endif
#ifdef ZTS
  ea_mutex = tsrm_mutex_alloc();
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
#ifdef ZEND_WIN32
      DBG(ea_debug_printf, (EA_DEBUG, "shutdown_mm [%d]\n", getpid()));
#else
      DBG(ea_debug_printf, (EA_DEBUG, "shutdown_mm [%d,%d]\n", getpid(), getppid()));
#endif
#ifdef ZTS
      tsrm_mutex_free(ea_mutex);
#endif
      if (mm) {
        mm_destroy(mm);
      }
      eaccelerator_mm_instance = NULL;
    }
  }
}

void encode_version(const char *str, int *version, int *extra)
{
    unsigned int a = 0;
    unsigned int b = 0;
    unsigned int c = 0;
    unsigned int d = 0;
    size_t len;
    char s[255];
    char buf[255];

    len = strlen(str);
    memcpy(buf, str, (len > 255) ? 255 : len);
    buf[255] = '\0';

    memset(s, 0, 255);
    sscanf(str, "%u.%u.%u%s", &a, &b, &c, s);

    if (s[0] == '.') {
        sscanf(s, ".%u-%s", &d, buf);
    } else if (s[0] == '-') {
        memcpy(buf, &s[1], 254);
    } else {
        memcpy(buf, s, 255);
    }

    *version = ((a & 0xff) << 24) | ((b & 0xff) << 16) | ((c & 0xff) << 8) | (d & 0xff);

    if (buf[0] == 0) {
        a = 0;
        b = 0;
    } else if (strncasecmp(buf, "rev", 3) == 0) {
        a = 1;
        sscanf(buf, "rev%u", &b);
    } else if (strncasecmp(buf, "rc", 2) == 0) {
        a = 2;
        sscanf(buf, "rc%u", &b);
    } else if (strncasecmp(buf, "beta", 4) == 0) {
        a = 3;
        sscanf(buf, "beta%u", &b);
    } else {
        a = 0xf;
        // just encode the first 4 bytes
        b = ((buf[0] & 0x7f) << 21) | ((buf[1] & 0x7f) << 14) | ((buf[2] & 0x7f) << 7) | (buf[3] & 0x7f);
    }

    *extra = ((a & 0xf) << 28) | (0x0fffffff & b);
}

static void decode_version(int version, int extra, char *str, size_t len)
{
    int number;

    if ((version & 0xff) == 0) {
        number = snprintf(str, len, "%u.%u.%u", (version >> 24), ((version >> 16) & 0xff), ((version >> 8) & 0xff));
    } else {
        number = snprintf(str, len, "%u.%u.%u.%u", (version >> 24), ((version >> 16) & 0xff), ((version >> 8) & 0xff), (version & 0xff));
    }

    if (extra != 0) {
        unsigned int type = ((extra >> 28) & 0xf);
        extra = (extra & 0x0fffffff);
        switch (type) {
            case 1:
                snprintf(&str[number], len, "-rev%u", extra);
                break;
            case 2:
                snprintf(&str[number], len, "-rc%u", extra);
                break;
            case 3:
                snprintf(&str[number], len, "-beta%u", extra);
                break;
            case 15:
                if (len >= number + 5) {
                    str[number] = '-';
                    str[number + 1] = (extra >> 21) & 0x7f;
                    str[number + 2] = (extra >> 14) & 0x7f;
                    str[number + 3] = (extra >> 7) & 0x7f;
                    str[number + 4] = extra & 0x7f;
                    str[number + 5] = '\0';
                }
                break;
            default:
                break;
        }
    }
}

static char num2hex[] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};

#ifdef EACCELERATOR_USE_INODE
static int eaccelerator_inode_key(char* s, dev_t dev, ino_t ino TSRMLS_DC) {
  int n, i;
  snprintf(s, MAXPATHLEN-1, "%s/", EAG(cache_dir));
  n = strlen(s);
  for (i = 1; i <= EACCELERATOR_HASH_LEVEL && n < MAXPATHLEN - 1; i++) {
    s[n++] = num2hex[(ino >> (i*4)) & 0xf];
    s[n++] = '/';
  }
  s[n] = 0;
  strlcat(s, "eaccelerator-", MAXPATHLEN-1);
  n += sizeof("eaccelerator-") - 1;
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
  int i;
  int n;

  md5str[0] = '\0';
  PHP_MD5Init(&context);
  PHP_MD5Update(&context, (unsigned char*)key, strlen(key));
  PHP_MD5Final(digest, &context);
  make_digest(md5str, digest);
  snprintf(s, MAXPATHLEN-1, "%s/", EAG(cache_dir));
  n = strlen(s);
  for (i = 0; i < EACCELERATOR_HASH_LEVEL && n < MAXPATHLEN - 1; i++) {
    s[n++] = md5str[i];
    s[n++] = '/';
  }
  s[n] = 0;
  snprintf(s, MAXPATHLEN-1, "%s%s%s", s, prefix, md5str);
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
      Z_TYPE(retval) == IS_STRING && Z_STRLEN(retval) == 32) {
    strncpy(s, EAG(cache_dir), MAXPATHLEN-1);
    strlcat(s, prefix, MAXPATHLEN);
    strlcat(s, Z_STRVAL(retval), MAXPATHLEN);
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
  for (i = 0; i < EA_HASH_SIZE; i++) {
    ea_cache_entry **p = &eaccelerator_mm_instance->hash[i];
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
        ea_cache_entry *r = *p;
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
  if (ea_shm_prune_period > 0) {
    t = time(0);
    if (t - eaccelerator_mm_instance->last_prune > ea_shm_prune_period) {
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
/* Cache file functions.														*/
/******************************************************************************/

/* A function to check if the header of a cache file valid is.
 */
inline int check_header(ea_file_header *hdr)
{
#ifdef DEBUG
  char current[255];
  char cache[255];
#endif
	
  if (strncmp(hdr->magic, EA_MAGIC, 8) != 0) {
#ifdef DEBUG
    ea_debug_printf(EA_DEBUG, "Magic header mismatch.");
#endif
	return 0;	
  }
  if (hdr->eaccelerator_version[0] != binary_eaccelerator_version[0] 
      || hdr->eaccelerator_version[1] != binary_eaccelerator_version[1]) {
#ifdef DEBUG
    decode_version(hdr->eaccelerator_version[0], hdr->eaccelerator_version[1], cache, 255);
    decode_version(binary_eaccelerator_version[0], binary_eaccelerator_version[1], current, 255);
    ea_debug_printf(EA_DEBUG, "eAccelerator version mismatch, cache file %s and current version %s\n", cache, current);
#endif
    return 0;
  }
  if (hdr->zend_version[0] != binary_zend_version[0] 
      || hdr->zend_version[1] != binary_zend_version[1]) {
#ifdef DEBUG
    decode_version(hdr->zend_version[0], hdr->zend_version[1], cache, 255);
    decode_version(binary_zend_version[0], binary_zend_version[1], current, 255);
    ea_debug_printf(EA_DEBUG, "Zend version mismatch, cache file %s and current version %s\n", cache, current);
#endif
    return 0;
  }
  if (hdr->php_version[0] != binary_php_version[0] 
      || hdr->php_version[1] != binary_php_version[1]) {
#ifdef DEBUG
    decode_version(hdr->php_version[0], hdr->php_version[1], cache, 255);
    decode_version(binary_php_version[0], binary_php_version[1], current, 255);
    ea_debug_printf(EA_DEBUG, "PHP version mismatch, cache file %s and current version %s\n", cache, current);
#endif
    return 0;
  }
  return 1;
}

/* A function to create the header for a cache file.
 */
inline void init_header(ea_file_header *hdr)
{
  strncpy(hdr->magic, EA_MAGIC, 8);
  hdr->eaccelerator_version[0] = binary_eaccelerator_version[0];
  hdr->eaccelerator_version[1] = binary_eaccelerator_version[1];
  hdr->zend_version[0] = binary_zend_version[0];
  hdr->zend_version[1] = binary_zend_version[1];
  hdr->php_version[0] = binary_php_version[0];	
  hdr->php_version[1] = binary_php_version[1];
}
/* Retrieve a cache entry from the cache directory */
static ea_cache_entry* hash_find_file(const char  *key, struct stat *buf TSRMLS_DC) {
  int f;
  char s[MAXPATHLEN];
  ea_file_header hdr;
  ea_cache_entry *p;
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
    if (check_header(&hdr)) {
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
      if (ea_shm_ttl > 0) {
        p->ttl = time(0) + ea_shm_ttl;
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
static int hash_add_file(ea_cache_entry *p TSRMLS_DC) {
  int f;
  int ret = 0;
  char s[MAXPATHLEN];
  ea_file_header hdr;

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
    init_header(&hdr);
    hdr.size  = p->size;
    hdr.mtime = p->mtime;
    p->next = p;
    hdr.crc32 = eaccelerator_crc32((const char*)p,p->size);
    ret = (write(f, &hdr, sizeof(hdr)) == sizeof(hdr));
    if (ret) ret = (write(f, p, p->size) == p->size);
    EACCELERATOR_FLOCK(f, LOCK_UN);
    close(f);
  } else {
    ea_debug_log("EACCELERATOR: Open for write failed for \"%s\": %s\n", s, strerror(errno));
  }
  return ret;
}

/* called after succesful compilation, from eaccelerator_compile file */
/* Adds the data from the compilation of the script to the cache */
static int eaccelerator_store(char* key, struct stat *buf, int nreloads,
                         zend_op_array* op_array,
                         Bucket* f, Bucket *c TSRMLS_DC) {
  ea_cache_entry *p;
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
  DBG(ea_debug_printf, (EA_DEBUG, "[%d] eaccelerator_store: calc_size returned %d, mm=%x", getpid(), size, eaccelerator_mm_instance->mm));
  EACCELERATOR_UNPROTECT();
  EAG(mem) = eaccelerator_malloc(size);
  if (EAG(mem) == NULL) {
    EAG(mem) = eaccelerator_malloc2(size TSRMLS_CC);
  }
  if (!EAG(mem) && !eaccelerator_scripts_shm_only) {
    EACCELERATOR_PROTECT();
    EAG(mem) = emalloc(size);
    use_shm = 0;
  }
  if (EAG(mem)) {
    memset(EAG(mem), 0, size);
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
      if (ea_shm_ttl > 0) {
        p->ttl = time(0) + ea_shm_ttl;
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

/* Try to restore a file from the cache. If the file isn't found in memory, the 
   the disk cache is checked */
static zend_op_array* eaccelerator_restore(char *realname, struct stat *buf,
                                      int *nreloads, time_t compile_time TSRMLS_DC) {
  ea_cache_entry *p;
  zend_op_array *op_array = NULL;

  *nreloads = 1;
  EACCELERATOR_UNPROTECT();
  p = hash_find_mm(realname, buf, nreloads, ((ea_shm_ttl > 0)?(compile_time + ea_shm_ttl):0));
  if (p == NULL && !eaccelerator_scripts_shm_only) {
    p = hash_find_file(realname, buf TSRMLS_CC);
  }
  EACCELERATOR_PROTECT();
  if (p != NULL && p->op_array != NULL) {
    EAG(class_entry) = NULL;
    op_array = restore_op_array(NULL, p->op_array TSRMLS_CC);
    if (op_array != NULL) {
      ea_fc_entry *e;
      ea_used_entry *used = emalloc(sizeof(ea_used_entry));
      used->entry  = p;
      used->next   = (ea_used_entry*)EAG(used_entries);
      EAG(used_entries) = (void*)used;
      EAG(mem) = op_array->filename;
			/* only restore the classes and functions when we restore this script 
			 * for the first time. 
			 */
      if (!zend_hash_exists(&EAG(restored), p->realfilename, strlen(p->realfilename))) {
				for (e = p->c_head; e!=NULL; e = e->next) {
          restore_class(e TSRMLS_CC);
        }
        for (e = p->f_head; e!=NULL; e = e->next) {
          restore_function(e TSRMLS_CC);
        }
				zend_hash_add(&EAG(restored), p->realfilename, strlen(p->realfilename), NULL, 0, NULL);  
			}
			EAG(mem) = p->realfilename;
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
  ea_cond_entry *p;
  int ok;
  if (EAG(cond_list) == NULL) {
    return 1;
  }

  /* if "realname" matches to any pattern started with "!" then ignore it */
  for (p = EAG(cond_list); p != NULL; p = p->next) {
    if (p->not && match(realname, p->str)) {
      return 0;
    }
  }

  /* else if it matches to any pattern not started with "!" then accept it */
  ok = 1;
  for (p = EAG(cond_list); p != NULL; p = p->next) {
    if (!p->not) {
      ok = 0;
      if (match(realname, p->str)) {
        return 1;
      }
    }
  }
  return ok;
}

static int eaccelerator_stat(zend_file_handle *file_handle,
                        char* realname, struct stat* buf TSRMLS_DC) {
#ifdef EACCELERATOR_USE_INODE
#ifndef ZEND_WIN32
  if (file_handle->type == ZEND_HANDLE_FP && file_handle->handle.fp != NULL) {
    if (fstat(fileno(file_handle->handle.fp), buf) == 0 && S_ISREG(buf->st_mode)) {
      if (file_handle->opened_path != NULL) {
        strcpy(realname, file_handle->opened_path);
      }
      return 0;
    }
  } else
#endif
  if (file_handle->opened_path != NULL) {
    if (stat(file_handle->opened_path, buf) == 0 && S_ISREG(buf->st_mode)) {
       strcpy(realname,file_handle->opened_path);
       return 0;
    }
  } else if (PG(include_path) == NULL || 
				 file_handle->filename[0] == '.' ||
             IS_SLASH(file_handle->filename[0]) ||
             IS_ABSOLUTE_PATH(file_handle->filename,strlen(file_handle->filename))) {
    if (stat(file_handle->filename, buf) == 0 && S_ISREG(buf->st_mode)) {
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
        len = end - ptr;
        end++;
      } else {
        len = strlen(ptr);
        end = ptr + len;
      }
      if (len + filename_len + 2 < MAXPATHLEN) {
        memcpy(tryname, ptr, len);
        tryname[len] = '/';
        memcpy(tryname + len + 1, file_handle->filename, filename_len);
        tryname[len + filename_len + 1] = '\0';
        if (stat(tryname, buf) == 0 && S_ISREG(buf->st_mode)) {
          return 0;
        }
      }
      ptr = end;
    }

	if (zend_is_executing(TSRMLS_C)) {
        int tryname_length;
		strncpy(tryname, zend_get_executed_filename(TSRMLS_C), MAXPATHLEN);
		tryname[MAXPATHLEN - 1] = 0;
		tryname_length = strlen(tryname);

		while (tryname_length >= 0 && !IS_SLASH(tryname[tryname_length])) {
			tryname_length--;
		}
		if (tryname_length > 0 && tryname[0] != '[' // [no active file]
			&& tryname_length + filename_len + 1 < MAXPATHLEN)
		{
			strncpy(tryname + tryname_length + 1, file_handle->filename, filename_len + 1);
			if (stat(tryname, buf) == 0 && S_ISREG(buf->st_mode)) {
				return 0;
			}
		}
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
      } else if (fstat(fileno(file_handle->handle.fp), buf) == 0 && S_ISREG(buf->st_mode)) {
        return 0;
      } else {
        return -1;
      }
    } else {
      if (!eaccelerator_check_mtime) {
        return 0;
      } else if (stat(realname, buf) == 0 && S_ISREG(buf->st_mode)) {
        return 0;
      } else {
        return -1;
      }
    }
#else
    if (!eaccelerator_check_mtime) {
      return 0;
    } else if (stat(realname, buf) == 0 && S_ISREG(buf->st_mode)) {
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
    if (VCWD_REALPATH(file_handle->filename, realname)) {
      if (!eaccelerator_check_mtime) {
        return 0;
      } else if (stat(realname, buf) == 0 && S_ISREG(buf->st_mode)) {
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
        len = end - ptr;
        end++;
      } else {
        len = strlen(ptr);
        end = ptr + len;
      }
      if (len+filename_len+2 < MAXPATHLEN) {
        memcpy(tryname, ptr, len);
        tryname[len] = '/';
        memcpy(tryname + len + 1, file_handle->filename, filename_len);
        tryname[len + filename_len + 1] = '\0';
        if (VCWD_REALPATH(tryname, realname)) {
#ifdef ZEND_WIN32
          if (stat(realname, buf) == 0 && S_ISREG(buf->st_mode)) {
            return 0;
          }
#else
          if (!eaccelerator_check_mtime) {
            return 0;
          } else if (stat(realname, buf) == 0 && S_ISREG(buf->st_mode)) {
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
  int stat_result = 0;
#ifdef DEBUG
  struct timeval tv_start;
#endif
  int ok_to_cache = 0;

#ifdef EACCELERATOR_USE_INODE
  realname[0] = '\000';
#endif

  DBG(ea_debug_start_time, (&tv_start));
  DBG(ea_debug_printf, (EA_DEBUG, "[%d] Enter COMPILE\n",getpid()));
  DBG(ea_debug_printf, (EA_DEBUG, "[%d] compile_file: \"%s\"\n",getpid(), file_handle->filename));
#ifdef DEBUG
  EAG(xpad)+=2;
#endif

  compile_time = time(0);
  stat_result = eaccelerator_stat(file_handle, realname, &buf TSRMLS_CC);
  if (buf.st_mtime >= compile_time && eaccelerator_debug > 0) {
	ea_debug_log("EACCELERATOR: Warning: \"%s\" is cached but it's mtime is in the future.\n", file_handle->filename);
  }

  ok_to_cache = eaccelerator_ok_to_cache(file_handle->filename TSRMLS_CC);
 
  // eAccelerator isn't working, so just compile the file
  if (!EAG(enabled) || (eaccelerator_mm_instance == NULL) || 
      !eaccelerator_mm_instance->enabled || file_handle == NULL ||
      file_handle->filename == NULL || stat_result != 0 || !ok_to_cache) {
    DBG(ea_debug_printf, (EA_DEBUG, "\t[%d] compile_file: compiling\n", getpid()));
    t = mm_saved_zend_compile_file(file_handle, type TSRMLS_CC);
    DBG(ea_debug_printf, (EA_TEST_PERFORMANCE, "\t[%d] compile_file: end (%ld)\n", getpid(), ea_debug_elapsed_time(&tv_start)));
    DBG(ea_debug_printf, (EA_DEBUG, "\t[%d] compile_file: end\n", getpid()));
#ifdef DEBUG
    EAG(xpad)-=2;
#endif
    DBG(ea_debug_printf, (EA_DEBUG, "[%d] Leave COMPILE\n", getpid()));
    return t;
  }

  /* only restore file when open_basedir allows it */
  if (php_check_open_basedir(file_handle->filename TSRMLS_CC)) {
    zend_error(E_ERROR, "Can't load %s, open_basedir restriction.", file_handle->filename);
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

  if (t != NULL) { // restore from cache
#ifdef DEBUG
    ea_debug_log("[%d] EACCELERATOR hit: \"%s\"\n", getpid(), t->filename);
#else
    ea_debug_log("EACCELERATOR hit: \"%s\"\n", t->filename);
#endif

    zend_llist_add_element(&CG(open_files), file_handle);
#ifdef ZEND_ENGINE_2
    if (file_handle->opened_path == NULL && file_handle->type != ZEND_HANDLE_STREAM) {
      file_handle->handle.stream.handle = (void*)1;
      file_handle->opened_path = EAG(mem);
#else
    if (file_handle->opened_path == NULL && file_handle->type != ZEND_HANDLE_FP) {
      int dummy = 1;
      file_handle->opened_path = EAG(mem);
      zend_hash_add(&EG(included_files), file_handle->opened_path, strlen(file_handle->opened_path)+1, (void *)&dummy, sizeof(int), NULL);
      file_handle->handle.fp = NULL;
#endif
    }
    DBG(ea_debug_printf, (EA_TEST_PERFORMANCE, "\t[%d] compile_file: restored (%ld)\n", getpid(), ea_debug_elapsed_time(&tv_start)));
    DBG(ea_debug_printf, (EA_DEBUG, "\t[%d] compile_file: restored\n", getpid()));
#ifdef DEBUG
    EAG(xpad)-=2;
#endif
    DBG(ea_debug_printf, (EA_DEBUG, "[%d] Leave COMPILE\n", getpid()));
    return t;
  } else { // not in cache or must be recompiled
    Bucket *function_table_tail;
    Bucket *class_table_tail;
    int ea_bailout;

#ifdef DEBUG
    ea_debug_printf(EA_DEBUG, "\t[%d] compile_file: marking\n", getpid());
    if (CG(class_table) != EG(class_table)) {
      ea_debug_printf(EA_DEBUG, "\t[%d] oops, CG(class_table)[%08x] != EG(class_table)[%08x]\n", getpid(), CG(class_table), EG(class_table));
      ea_debug_log_hashkeys("CG(class_table)\n", CG(class_table));
      ea_debug_log_hashkeys("EG(class_table)\n", EG(class_table));
    } else {
      ea_debug_printf(EA_DEBUG, "\t[%d] OKAY. That what I thought, CG(class_table)[%08x] == EG(class_table)[%08x]\n", getpid(), CG(class_table), EG(class_table));
      ea_debug_log_hashkeys("CG(class_table)\n", CG(class_table));
    }
#endif

    /* Storing global pre-compiled functions and classes */
    function_table_tail = CG(function_table)->pListTail;
    class_table_tail = CG(class_table)->pListTail;

    DBG(ea_debug_printf, (EA_TEST_PERFORMANCE, "\t[%d] compile_file: compiling (%ld)\n", getpid(), ea_debug_elapsed_time(&tv_start)));
    
    if (EAG(optimizer_enabled) && eaccelerator_mm_instance->optimizer_enabled) {
      EAG(compiler) = 1;
    }

	/* try to compile the script */
    ea_bailout = 0;
    zend_try {
      t = mm_saved_zend_compile_file(file_handle, type TSRMLS_CC);
    } zend_catch {
      ea_bailout = 1;
    } zend_end_try();
    if (ea_bailout) {
      zend_bailout();
    }
    DBG(ea_debug_log_hashkeys, ("class_table\n", CG(class_table)));

    EAG(compiler) = 0;
    if (t != NULL && file_handle->opened_path != NULL && (eaccelerator_check_mtime ||
         ((stat(file_handle->opened_path, &buf) == 0) && S_ISREG(buf.st_mode)))) {
      DBG(ea_debug_printf, (EA_TEST_PERFORMANCE, "\t[%d] compile_file: storing in cache (%ld)\n", getpid(), ea_debug_elapsed_time(&tv_start)));
      DBG(ea_debug_printf, (EA_DEBUG, "\t[%d] compile_file: storing in cache\n", getpid()));
      function_table_tail = function_table_tail ? function_table_tail->pListNext : CG(function_table)->pListHead;
      class_table_tail = class_table_tail ? class_table_tail->pListNext : CG(class_table)->pListHead;
      if (eaccelerator_store(file_handle->opened_path, &buf, nreloads, t, function_table_tail, class_table_tail TSRMLS_CC)) {
#ifdef DEBUG
        ea_debug_log("[%d] EACCELERATOR %s: \"%s\"\n", getpid(), (nreloads == 1) ? "cached" : "re-cached", file_handle->opened_path);
#else
        ea_debug_log("EACCELERATOR %s: \"%s\"\n", (nreloads == 1) ? "cached" : "re-cached", file_handle->opened_path);
#endif
      } else {
#ifdef DEBUG
        ea_debug_log("[%d] EACCELERATOR can't cache: \"%s\"\n", getpid(), file_handle->opened_path);
#else
        ea_debug_log("EACCELERATOR can't cache: \"%s\"\n", file_handle->opened_path);
#endif
      }
    } else {
      function_table_tail = function_table_tail ? function_table_tail->pListNext : CG(function_table)->pListHead;
      class_table_tail = class_table_tail ? class_table_tail->pListNext : CG(class_table)->pListHead;
    }
  }
  DBG(ea_debug_printf, (EA_TEST_PERFORMANCE, "\t[%d] compile_file: end (%ld)\n", getpid(), ea_debug_elapsed_time(&tv_start)));
  DBG(ea_debug_printf, (EA_DEBUG, "\t[%d] compile_file: end\n", getpid()));
#ifdef DEBUG
  EAG(xpad)-=2;
#endif
  DBG(ea_debug_printf, (EA_DEBUG, "[%d] Leave COMPILE\n", getpid()));
  return t;
}

#ifdef DEBUG
static void profile_execute(zend_op_array *op_array TSRMLS_DC)
{
  int i;
  struct timeval tv_start;
  long usec;

  for (i=0;i<EAG(profile_level);i++)
    DBG(ea_debug_put, (EA_PROFILE_OPCODES, "  "));
  ea_debug_printf(EA_PROFILE_OPCODES, "enter profile_execute: %s:%s\n", op_array->filename, op_array->function_name);
  ea_debug_start_time(&tv_start);
  EAG(self_time)[EAG(profile_level)] = 0;
  EAG(profile_level)++;
  ea_debug_printf(EA_PROFILE_OPCODES, "About to enter zend_execute...\n");
  mm_saved_zend_execute(op_array TSRMLS_CC);
  ea_debug_printf(EA_PROFILE_OPCODES, "Finished zend_execute...\n");
  usec = ea_debug_elapsed_time(&tv_start);
  EAG(profile_level)--;
  if (EAG(profile_level) > 0)
    EAG(self_time)[EAG(profile_level)-1] += usec;
  for (i=0;i<EAG(profile_level);i++)
    DBG(ea_debug_put, (EA_PROFILE_OPCODES, "  "));
  ea_debug_printf(EA_PROFILE_OPCODES, "leave profile_execute: %s:%s (%ld,%ld)\n", op_array->filename, op_array->function_name, usec, usec-EAG(self_time)[EAG(profile_level)]);
}

ZEND_DLEXPORT zend_op_array* profile_compile_file(zend_file_handle *file_handle, int type TSRMLS_DC) {
  zend_op_array *t;
  int i;
  struct timeval tv_start;
  long usec;

  ea_debug_start_time(&tv_start);
  EAG(self_time)[EAG(profile_level)] = 0;
  t = eaccelerator_compile_file(file_handle, type TSRMLS_CC);
  usec = ea_debug_elapsed_time(&tv_start);
  if (EAG(profile_level) > 0)
    EAG(self_time)[EAG(profile_level)-1] += usec;
  for (i=0;i<EAG(profile_level);i++)
    DBG(ea_debug_put, (EA_PROFILE_OPCODES, "  "));
  ea_debug_printf(EA_DEBUG, "zend_op_array compile: %s (%ld)\n", file_handle->filename, usec);
  return t;
}

#endif  /* DEBUG */

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
  php_info_print_table_row(2, "Caching Enabled", (EAG(enabled) && (eaccelerator_mm_instance != NULL) && 
              eaccelerator_mm_instance->enabled)?"true":"false");
  php_info_print_table_row(2, "Optimizer Enabled", (EAG(optimizer_enabled) && 
              (eaccelerator_mm_instance != NULL) && eaccelerator_mm_instance->optimizer_enabled)?"true":"false");
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

/******************************************************************************/
/*
 * Begin of dynamic loadable module interfaces.
 * There are two interfaces:
 *  - standard php module,
 *  - zend extension.
 */
PHP_INI_MH(eaccelerator_filter) {
  ea_cond_entry *p, *q;
  char *s = new_value;
  char *ss;
  int  not;
  for (p = EAG(cond_list); p != NULL; p = q) {
    q = p->next;
    if (p->str) {
      free(p->str);
    }
    free(p);
  }
  EAG(cond_list) = NULL;
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
      p = (ea_cond_entry *)malloc(sizeof(ea_cond_entry));
      if (p == NULL)
        break;
      p->not = not;
      p->len = s-ss;
      p->str = malloc(p->len+1);
      memcpy(p->str, ss, p->len);
      p->str[p->len] = 0;
      p->next = EAG(cond_list);
      EAG(cond_list) = p;
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
ZEND_INI_ENTRY1("eaccelerator.shm_size",        "0", PHP_INI_SYSTEM, eaccelerator_OnUpdateLong, &ea_shm_size)
ZEND_INI_ENTRY1("eaccelerator.shm_max",         "0", PHP_INI_SYSTEM, eaccelerator_OnUpdateLong, &ea_shm_max)
ZEND_INI_ENTRY1("eaccelerator.shm_ttl",         "0", PHP_INI_SYSTEM, eaccelerator_OnUpdateLong, &ea_shm_ttl)
ZEND_INI_ENTRY1("eaccelerator.shm_prune_period", "0", PHP_INI_SYSTEM, eaccelerator_OnUpdateLong, &ea_shm_prune_period)
ZEND_INI_ENTRY1("eaccelerator.debug",           "1", PHP_INI_SYSTEM, eaccelerator_OnUpdateLong, &eaccelerator_debug)
STD_PHP_INI_ENTRY("eaccelerator.log_file",      "", PHP_INI_SYSTEM, OnUpdateString, eaccelerator_log_file, zend_eaccelerator_globals, eaccelerator_globals)
ZEND_INI_ENTRY1("eaccelerator.check_mtime",     "1", PHP_INI_SYSTEM, eaccelerator_OnUpdateBool, &eaccelerator_check_mtime)
ZEND_INI_ENTRY1("eaccelerator.shm_only",        "0", PHP_INI_SYSTEM, eaccelerator_OnUpdateBool, &eaccelerator_scripts_shm_only)
#ifdef WITH_EACCELERATOR_SHM
ZEND_INI_ENTRY("eaccelerator.keys",             "shm_and_disk", PHP_INI_SYSTEM, eaccelerator_OnUpdateKeysCachePlace)
#endif
#ifdef WITH_EACCELERATOR_SESSIONS
ZEND_INI_ENTRY("eaccelerator.sessions",         "shm_and_disk", PHP_INI_SYSTEM, eaccelerator_OnUpdateSessionCachePlace)
#endif
#ifdef WITH_EACCELERATOR_CONTENT_CACHING
ZEND_INI_ENTRY("eaccelerator.content",          "shm_and_disk", PHP_INI_SYSTEM, eaccelerator_OnUpdateContentCachePlace)
#endif
#ifdef WITH_EACCELERATOR_INFO
STD_PHP_INI_ENTRY("eaccelerator.allowed_admin_path",       "", PHP_INI_SYSTEM, OnUpdateString, allowed_admin_path, zend_eaccelerator_globals, eaccelerator_globals)
#endif
STD_PHP_INI_ENTRY("eaccelerator.cache_dir",      "/tmp/eaccelerator", PHP_INI_SYSTEM, OnUpdateString, cache_dir, zend_eaccelerator_globals, eaccelerator_globals)
PHP_INI_ENTRY("eaccelerator.filter",             "",  PHP_INI_ALL, eaccelerator_filter)
STD_PHP_INI_ENTRY("eaccelerator.name_space",      "", PHP_INI_SYSTEM, OnUpdateString, name_space, zend_eaccelerator_globals, eaccelerator_globals)
PHP_INI_END()

static void eaccelerator_clean_request(TSRMLS_D) {
  ea_used_entry  *p = (ea_used_entry*)EAG(used_entries);
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
            ea_cache_entry *q = eaccelerator_mm_instance->removed;
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
        ea_lock_entry** p = &eaccelerator_mm_instance->locks;
        while ((*p) != NULL) {
#ifdef ZTS
          if ((*p)->pid == pid && (*p)->thread == thread) {
#else
          if ((*p)->pid == pid) {
#endif
            ea_lock_entry* x = *p;
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
    p = (ea_used_entry*)EAG(used_entries);
    while (p != NULL) {
      ea_used_entry* r = p;
      p = p->next;
      if (r->entry != NULL && r->entry->use_cnt < 0) {
        eaccelerator_free(r->entry);
      }
      efree(r);
    }
  }
  EAG(used_entries) = NULL;
  EAG(in_request) = 0;
}

#if (__GNUC__ >= 3) || ((__GNUC__ == 2) && (__GNUC_MINOR__ >= 91))
static void __attribute__((destructor)) eaccelerator_clean_shutdown(void)
#else
void _fini(void)
#endif
{
  if (eaccelerator_mm_instance != NULL) {
    TSRMLS_FETCH();
    if (EAG(in_request)) {
      fflush(stdout);
      fflush(stderr);
      eaccelerator_clean_request(TSRMLS_C);
      if (EG(active_op_array)) {
        DBG(ea_debug_error, ("[%d] EACCELERATOR: PHP unclean shutdown on opline %ld of %s() at %s:%u\n\n",
          getpid(),
          (long)(active_opline-EG(active_op_array)->opcodes),
          get_active_function_name(TSRMLS_C),
          zend_get_executed_filename(TSRMLS_C),
          zend_get_executed_lineno(TSRMLS_C)));
      } else {
        DBG(ea_debug_error, ("[%d] EACCELERATOR: PHP unclean shutdown\n\n",getpid()));
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
  if (EAG(original_sigsegv_handler) != eaccelerator_crash_handler) {
    signal(SIGSEGV, EAG(original_sigsegv_handler));
  } else {
    signal(SIGSEGV, SIG_DFL);
  }
#endif
#ifdef SIGFPE
  if (EAG(original_sigfpe_handler) != eaccelerator_crash_handler) {
    signal(SIGFPE, EAG(original_sigfpe_handler));
  } else {
    signal(SIGFPE, SIG_DFL);
  }
#endif
#ifdef SIGBUS
  if (EAG(original_sigbus_handler) != eaccelerator_crash_handler) {
    signal(SIGBUS, EAG(original_sigbus_handler));
  } else {
    signal(SIGBUS, SIG_DFL);
  }
#endif
#ifdef SIGILL
  if (EAG(original_sigill_handler) != eaccelerator_crash_handler) {
    signal(SIGILL, EAG(original_sigill_handler));
  } else {
    signal(SIGILL, SIG_DFL);
  }
#endif
#ifdef SIGABRT
  if (EAG(original_sigabrt_handler) != eaccelerator_crash_handler) {
    signal(SIGABRT, EAG(original_sigabrt_handler));
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
  eaccelerator_globals->cond_list         = NULL;
  eaccelerator_globals->content_headers   = NULL;
#ifdef WITH_EACCELERATOR_SESSIONS
  eaccelerator_globals->session           = NULL;
#endif
  eaccelerator_globals->eaccelerator_log_file = '\000';
  eaccelerator_globals->name_space        = '\000';
  eaccelerator_globals->hostname[0]       = '\000';
  eaccelerator_globals->in_request        = 0;
  eaccelerator_globals->allowed_admin_path= NULL;
}

static void eaccelerator_globals_dtor(zend_eaccelerator_globals *eaccelerator_globals)
{
  ea_cond_entry *p, *q;

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

static void make_hash_dirs(char *fullpath, int lvl) {
  int j;
  int n = strlen(fullpath);
  mode_t old_umask = umask(0);
  
  if (lvl < 1)
    return;
  if (fullpath[n-1] != '/')
    fullpath[n++] = '/';
  
  for (j = 0; j < 16; j++) {
    fullpath[n] = num2hex[j];       
    fullpath[n+1] = 0;
    mkdir(fullpath, 0777);
    make_hash_dirs(fullpath, lvl-1);
  }
  fullpath[n+2] = 0;
  umask(old_umask);
}


PHP_MINIT_FUNCTION(eaccelerator) {
  char fullpath[MAXPATHLEN];

  if (type == MODULE_PERSISTENT) {
#ifndef ZEND_WIN32
    if (strcmp(sapi_module.name,"apache") == 0) {
      if (getpid() != getpgrp()) {
        return SUCCESS;
      }
    }
#endif
  }
  if (!eaccelerator_check_php_version(TSRMLS_C)) {
    return FAILURE;
  }
  ZEND_INIT_MODULE_GLOBALS(eaccelerator, eaccelerator_init_globals, NULL);
  REGISTER_INI_ENTRIES();
  REGISTER_STRING_CONSTANT("EACCELERATOR_VERSION", EACCELERATOR_VERSION, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("EACCELERATOR_SHM_AND_DISK", ea_shm_and_disk, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("EACCELERATOR_SHM", ea_shm, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("EACCELERATOR_SHM_ONLY", ea_shm_only, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("EACCELERATOR_DISK_ONLY", ea_disk_only, CONST_CS | CONST_PERSISTENT);
  REGISTER_LONG_CONSTANT("EACCELERATOR_NONE", ea_none, CONST_CS | CONST_PERSISTENT);
  encode_version(EACCELERATOR_VERSION, &binary_eaccelerator_version[0], &binary_eaccelerator_version[1]);
  encode_version(PHP_VERSION, &binary_php_version[0], &binary_php_version[1]);
  encode_version(ZEND_VERSION, &binary_zend_version[0], &binary_zend_version[1]);
  eaccelerator_is_extension = 1;

  ea_debug_init(TSRMLS_C);

  if(!eaccelerator_scripts_shm_only) {
    snprintf(fullpath, MAXPATHLEN-1, "%s/", EAG(cache_dir));
    make_hash_dirs(fullpath, EACCELERATOR_HASH_LEVEL);
  }

  if (type == MODULE_PERSISTENT &&
      strcmp(sapi_module.name, "cgi") != 0 &&
      strcmp(sapi_module.name, "cli") != 0) {
    DBG(ea_debug_put, (EA_DEBUG, "\n=======================================\n"));
    DBG(ea_debug_printf, (EA_DEBUG, "[%d] EACCELERATOR STARTED\n", getpid()));
    DBG(ea_debug_put, (EA_DEBUG, "=======================================\n"));

    if (init_mm(TSRMLS_C) == FAILURE) {
      zend_error(E_CORE_WARNING,"[%s] Can not create shared memory area", EACCELERATOR_EXTENSION_NAME);
      return FAILURE;
    }
    mm_saved_zend_compile_file = zend_compile_file;

#ifdef DEBUG
    zend_compile_file = profile_compile_file;
    mm_saved_zend_execute = zend_execute;
    zend_execute = profile_execute;
#else
    zend_compile_file = eaccelerator_compile_file;
#endif
  }
  
#ifdef WITH_EACCELERATOR_SESSIONS
  eaccelerator_register_session();
#endif
#ifdef WITH_EACCELERATOR_CONTENT_CACHING
  eaccelerator_content_cache_startup();
#endif
  if (!eaccelerator_is_zend_extension) {
    register_eaccelerator_as_zend_extension();
  }
  
#ifdef ZEND_ENGINE_2
  /* cache the properties_info destructor */
  properties_info_dtor = get_zend_destroy_property_info(TSRMLS_C);
#endif
  return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(eaccelerator) {
  if (eaccelerator_mm_instance == NULL || !eaccelerator_is_extension) {
    return SUCCESS;
  }
  zend_compile_file = mm_saved_zend_compile_file;
#ifdef WITH_EACCELERATOR_CONTENT_CACHING
  eaccelerator_content_cache_shutdown();
#endif
  shutdown_mm(TSRMLS_C);
  DBG(ea_debug_put, (EA_DEBUG, "========================================\n"));
  DBG(ea_debug_printf, (EA_DEBUG, "[%d] EACCELERATOR STOPPED\n", getpid()));
  DBG(ea_debug_put, (EA_DEBUG, "========================================\n\n"));
  ea_debug_shutdown();
  UNREGISTER_INI_ENTRIES();
#ifdef ZTS
  ts_free_id(eaccelerator_globals_id);
#else
  eaccelerator_globals_dtor(&eaccelerator_globals TSRMLS_CC);
#endif
  eaccelerator_is_zend_extension = 0;
  eaccelerator_is_extension = 0;
  return SUCCESS;
}

PHP_RINIT_FUNCTION(eaccelerator)
{
  union {
		zval **v;
    void *ptr;
  } server_vars, hostname;

	if (eaccelerator_mm_instance == NULL) {
		return SUCCESS;
	}

	/*
	 * Initialization on first call, comes from eaccelerator_zend_startup().
	 */
	if (eaccelerator_global_function_table.nTableSize == 0) {
		zend_function tmp_func;
		zend_class_entry tmp_class;

		zend_hash_init_ex(&eaccelerator_global_function_table, 100, NULL, NULL, 1, 0);
		zend_hash_copy(&eaccelerator_global_function_table, CG(function_table), NULL, 
			&tmp_func, sizeof(zend_function));
		
		zend_hash_init_ex(&eaccelerator_global_class_table, 10, NULL, NULL, 1, 0);
		zend_hash_copy(&eaccelerator_global_class_table, CG(class_table), NULL, 
			&tmp_class, sizeof(zend_class_entry));
	}

	DBG(ea_debug_printf, (EA_DEBUG, "[%d] Enter RINIT\n",getpid()));
	DBG(ea_debug_put, (EA_PROFILE_OPCODES, "\n========================================\n"));

	EAG(in_request) = 1;
	EAG(used_entries) = NULL;
	EAG(compiler) = 0;
	EAG(refcount_helper) = 1;
	EAG(compress_content) = 1;
	EAG(content_headers) = NULL;

	/* Storing Host Name */
	EAG(hostname)[0] = '\000';
  if (zend_hash_find(&EG(symbol_table), "_SERVER", sizeof("_SERVER"), &server_vars.ptr) == SUCCESS &&
			Z_TYPE_PP(server_vars.v) == IS_ARRAY &&
			zend_hash_find(Z_ARRVAL_PP(server_vars.v), "SERVER_NAME", sizeof("SERVER_NAME"), &hostname.ptr)==SUCCESS &&
			Z_TYPE_PP(hostname.v) == IS_STRING && Z_STRLEN_PP(hostname.v) > 0) {
		if (sizeof(EAG(hostname)) > Z_STRLEN_PP(hostname.v)) {
			memcpy(EAG(hostname),Z_STRVAL_PP(hostname.v),Z_STRLEN_PP(hostname.v)+1);
		} else {
			memcpy(EAG(hostname),Z_STRVAL_PP(hostname.v),sizeof(EAG(hostname))-1);
			EAG(hostname)[sizeof(EAG(hostname))-1] = '\000';
		}
  }

	zend_hash_init(&EAG(restored), 0, NULL, NULL, 0);

	DBG(ea_debug_printf, (EA_DEBUG, "[%d] Leave RINIT\n",getpid()));
#ifdef DEBUG
	EAG(xpad) = 0;
	EAG(profile_level) = 0;
#endif

#ifdef WITH_EACCELERATOR_CRASH_DETECTION
#ifdef SIGSEGV
	EAG(original_sigsegv_handler) = signal(SIGSEGV, eaccelerator_crash_handler);
#endif
#ifdef SIGFPE
	EAG(original_sigfpe_handler) = signal(SIGFPE, eaccelerator_crash_handler);
#endif
#ifdef SIGBUS
	EAG(original_sigbus_handler) = signal(SIGBUS, eaccelerator_crash_handler);
#endif
#ifdef SIGILL
	EAG(original_sigill_handler) = signal(SIGILL, eaccelerator_crash_handler);
#endif
#ifdef SIGABRT
	EAG(original_sigabrt_handler) = signal(SIGABRT, eaccelerator_crash_handler);
#endif
#endif
	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(eaccelerator)
{
	if (eaccelerator_mm_instance == NULL) {
		return SUCCESS;
	}
	zend_hash_destroy(&EAG(restored));
#ifdef WITH_EACCELERATOR_CRASH_DETECTION
#ifdef SIGSEGV
	if (EAG(original_sigsegv_handler) != eaccelerator_crash_handler) {
		signal(SIGSEGV, EAG(original_sigsegv_handler));
	} else {
		signal(SIGSEGV, SIG_DFL);
	}
#endif
#ifdef SIGFPE
	if (EAG(original_sigfpe_handler) != eaccelerator_crash_handler) {
		signal(SIGFPE, EAG(original_sigfpe_handler));
	} else {
		signal(SIGFPE, SIG_DFL);
	}
#endif
#ifdef SIGBUS
	if (EAG(original_sigbus_handler) != eaccelerator_crash_handler) {
		signal(SIGBUS, EAG(original_sigbus_handler));
	} else {
		signal(SIGBUS, SIG_DFL);
	}
#endif
#ifdef SIGILL
	if (EAG(original_sigill_handler) != eaccelerator_crash_handler) {
		signal(SIGILL, EAG(original_sigill_handler));
	} else {
		signal(SIGILL, SIG_DFL);
	}
#endif
#ifdef SIGABRT
	if (EAG(original_sigabrt_handler) != eaccelerator_crash_handler) {
		signal(SIGABRT, EAG(original_sigabrt_handler));
	} else {
		signal(SIGABRT, SIG_DFL);
	}
#endif
#endif
	DBG(ea_debug_printf, (EA_DEBUG, "[%d] Enter RSHUTDOWN\n",getpid()));
	eaccelerator_clean_request(TSRMLS_C);
	DBG(ea_debug_printf, (EA_DEBUG, "[%d] Leave RSHUTDOWN\n",getpid()));
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
#ifdef WITH_EACCELERATOR_SHM
  PHP_FE(eaccelerator_put, NULL)
  PHP_FE(eaccelerator_get, NULL)
  PHP_FE(eaccelerator_rm, NULL)
  PHP_FE(eaccelerator_gc, NULL)
  PHP_FE(eaccelerator_lock, NULL)
  PHP_FE(eaccelerator_unlock, NULL)
#endif
#ifdef WITH_EACCELERATOR_INFO
  PHP_FE(eaccelerator_caching, NULL)
  #ifdef WITH_EACCELERATOR_OPTIMIZER
  PHP_FE(eaccelerator_optimizer, NULL)
  #endif
  PHP_FE(eaccelerator_clear, NULL)
  PHP_FE(eaccelerator_clean, NULL)
  PHP_FE(eaccelerator_info, NULL)
  PHP_FE(eaccelerator_purge, NULL)
  PHP_FE(eaccelerator_cached_scripts, NULL)
  PHP_FE(eaccelerator_removed_scripts, NULL)
  PHP_FE(eaccelerator_list_keys, NULL)
#endif
#ifdef WITH_EACCELERATOR_CONTENT_CACHING
  PHP_FE(_eaccelerator_output_handler, NULL)
  PHP_FE(eaccelerator_cache_page, NULL)
  PHP_FE(eaccelerator_rm_page, NULL)
  PHP_FE(eaccelerator_cache_output, NULL)
  PHP_FE(eaccelerator_cache_result, NULL)
#endif
#ifdef WITH_EACCELERATOR_DISASSEMBLER
  PHP_FE(eaccelerator_dasm_file, NULL)
#endif
#ifdef ZEND_ENGINE_2
  {NULL, NULL, NULL, 0U, 0U}
#else
  {NULL, NULL, NULL}
#endif
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
          zend_error(E_CORE_ERROR,"[%s] %s %s can not be loaded twice",
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
      } else if (strcmp(ext->name, "Zend Extension Manager") == 0 ||
                 strcmp(ext->name, "Zend Optimizer") == 0) {
        /* Disable ZendOptimizer Optimizations */
        ZendOptimizer = ext;
        ext->op_array_handler = NULL;
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
  "http://eaccelerator.net",
  "Copyright (c) 2004-2006 eAccelerator",
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
  "http://eaccelerator.net",
  "Copyright (c) 2004-2006 eAccelerator",
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

/*
 * Local variables:
 * tab-width: 2
 * c-basic-offset: 2
 * End:
 * vim600: noet sw=2 ts=2 fdm=marker
 * vim<600: noet sw=2 ts=2
 */
