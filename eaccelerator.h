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
   | Author(s): Dmitry Stogov <dstogov@users.sourceforge.net>             |
   +----------------------------------------------------------------------+
   $Id$
*/

#ifndef INCLUDED_EACCELERATOR_H
#define INCLUDED_EACCELERATOR_H

#include "php.h"
#include "zend.h"
#include "zend_API.h"
#include "zend_extensions.h"

#ifndef ZEND_WIN32
#  if ZEND_MODULE_API_NO >= 20001222
#    include "config.h"
#  else
#    include "php_config.h"
#  endif
#endif

#ifndef ZEND_WIN32
/* UnDefine if your filesystem doesn't support inodes */
#  define EACCELERATOR_USE_INODE
#endif

/* Define some of the following macros if you like to debug eAccelerator */
/*#define DEBUG*/
/*#define TEST_PERFORMANCE*/
/*#define PROFILE_OPCODES*/

#ifdef WITH_EACCELERATOR_CRASH_DETECTION
#  include <signal.h>
#endif

#if defined(DEBUG) || defined(TEST_PERFORMANCE)  || defined(PROFILE_OPCODES)
/* Here you can chage debuging log filename */
#define DEBUG_LOGFILE     "/var/log/httpd/eaccelerator_log"
#define DEBUG_LOGFILE_CGI "/tmp/eaccelerator_log"
#endif

#define EACCELERATOR_MM_FILE "/tmp/eaccelerator"

#ifdef HAVE_EACCELERATOR
/*
 * Where to cache
 */
typedef enum _eaccelerator_cache_place {
  eaccelerator_shm_and_disk, /* in shm and in disk */
  eaccelerator_shm,          /* in shm, but if it is not possible then on disk */
  eaccelerator_shm_only,     /* in shm only  */
  eaccelerator_disk_only,    /* on disk only */
  eaccelerator_none          /* don't cache  */
} eaccelerator_cache_place;

extern eaccelerator_cache_place eaccelerator_content_cache_place;

unsigned int eaccelerator_crc32(const char *p, size_t n);
int eaccelerator_put(const char* key, int key_len, zval* val, time_t ttl, eaccelerator_cache_place where TSRMLS_DC);
int eaccelerator_get(const char* key, int key_len, zval* return_value, eaccelerator_cache_place where  TSRMLS_DC);
int eaccelerator_rm(const char* key, int key_len, eaccelerator_cache_place where  TSRMLS_DC);
size_t eaccelerator_gc(TSRMLS_D);
#  ifdef WITH_EACCELERATOR_EXECUTOR
ZEND_DLEXPORT void eaccelerator_execute(zend_op_array *op_array TSRMLS_DC);
#  endif
#  ifdef WITH_EACCELERATOR_OPTIMIZER
void eaccelerator_optimize(zend_op_array *op_array);
#  endif
#ifdef WITH_EACCELERATOR_ENCODER
PHP_FUNCTION(eaccelerator_encode);
#endif
#ifdef WITH_EACCELERATOR_LOADER
zend_op_array* eaccelerator_load(char* src, int src_len TSRMLS_DC);
PHP_FUNCTION(eaccelerator_load);
PHP_FUNCTION(_eaccelerator_loader_file);
PHP_FUNCTION(_eaccelerator_loader_line);
#endif
#ifdef WITH_EACCELERATOR_CONTENT_CACHING
void eaccelerator_content_cache_startup();
void eaccelerator_content_cache_shutdown();

PHP_FUNCTION(_eaccelerator_output_handler);
PHP_FUNCTION(eaccelerator_cache_page);
PHP_FUNCTION(eaccelerator_rm_page);
PHP_FUNCTION(eaccelerator_cache_output);
PHP_FUNCTION(eaccelerator_cache_result);
#endif
#endif

/*
 * conditional filter
 */
typedef struct _mm_cond_entry {
  char     *str;
  int       len;
  zend_bool not;
  struct  _mm_cond_entry  *next;
} mm_cond_entry;

/*
 * Globals (different for each process/thread)
 */
ZEND_BEGIN_MODULE_GLOBALS(eaccelerator)
  void          *used_entries;     /* list of files which are used     */
                                   /* by process/thread                */
  zend_bool     enabled;
  zend_bool     optimizer_enabled;
  zend_bool     compression_enabled;
  zend_bool     compiler;
  zend_bool     encoder;
  zend_bool     compress;
  zend_bool     compress_content;
  zend_bool     in_request;
  zend_llist*   content_headers;
  long          compress_level;
  char          *cache_dir;
  char          *mem;
  HashTable     strings;
  zend_class_entry *class_entry;
  mm_cond_entry *cond_list;
  zend_uint      refcount_helper;
  char          hostname[32];
#ifdef WITH_EACCELERATOR_CRASH_DETECTION
#ifdef SIGSEGV
  void (*original_sigsegv_handler)(int);
#endif
#ifdef SIGFPE
  void (*original_sigfpe_handler)(int);
#endif
#ifdef SIGBUS
  void (*original_sigbus_handler)(int);
#endif
#ifdef SIGILL
  void (*original_sigill_handler)(int);
#endif
#ifdef SIGABRT
  void (*original_sigabrt_handler)(int);
#endif
#endif
#if defined(DEBUG) || defined(TEST_PERFORMANCE)  || defined(PROFILE_OPCODES)
  int  xpad;
#endif
#ifdef WITH_EACCELERATOR_SESSIONS
  char *session;
#endif
#ifdef PROFILE_OPCODES
  int  profile_level;
  long self_time[256];
#endif
ZEND_END_MODULE_GLOBALS(eaccelerator)

ZEND_EXTERN_MODULE_GLOBALS(eaccelerator)

#ifdef ZTS
#  define MMCG(v) TSRMG(eaccelerator_globals_id, zend_eaccelerator_globals*, v)
#else
#  define MMCG(v) (eaccelerator_globals.v)
#endif

#define EACCELERATOR_EXTENSION_NAME "eAccelerator"
#define EACCELERATOR_LOADER_EXTENSION_NAME "Turck Loader"

#endif /*#ifndef INCLUDED_EACCELERATOR_H*/
