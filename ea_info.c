/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 - 2012 eAccelerator                               |
   | http://eaccelerator.net                                  	          |
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
   $Id: ea_info.c 375 2010-01-19 15:49:13Z bart $
*/

#include "eaccelerator.h"
#include "eaccelerator_version.h"
#include "ea_info.h"
#include "mm.h"
#include "zend.h"
#include "fopen_wrappers.h"
#include "debug.h"
#include <fcntl.h>

#ifndef O_BINARY
#  define O_BINARY 0
#endif

#ifdef WITH_EACCELERATOR_INFO

#define NOT_ADMIN_WARNING "This script isn't in the allowed_admin_path setting!"

extern eaccelerator_mm *ea_mm_instance;

/* for checking if shm_only storage */
extern zend_bool ea_scripts_shm_only;

/* {{{ isAdminAllowed(): check if the admin functions are allowed for the calling script */
static int isAdminAllowed(TSRMLS_D)
{
    const char *filename = zend_get_executed_filename(TSRMLS_C);
    if (EAG(allowed_admin_path) && *EAG(allowed_admin_path)) {
        char *path;
        char *p;
        char *next;

        path = estrdup(EAG(allowed_admin_path));
        p = path;

        while (p && *p) {
            next = strchr(p, DEFAULT_DIR_SEPARATOR);
            if (next != NULL) {
                *next = '\0';
                ++next;
            }

            if (!php_check_specific_open_basedir(p, filename TSRMLS_CC)) {
                efree(path);
                return 1;
            }

            p = next;
        }
        efree(path);
        return 0;
    }
    return 0;
}
/* }}} */

/* {{{ clear_filecache(): Helper function to eaccelerator_clear which finds diskcache entries in the hashed dirs and removes them */
static void clear_filecache(const char* dir)
#ifndef ZEND_WIN32
{
    DIR *dp;
    struct dirent *entry;
    char s[MAXPATHLEN];
    struct stat dirstat;

    if ((dp = opendir(dir)) != NULL) {
        while ((entry = readdir(dp)) != NULL) {
            strncpy(s, dir, MAXPATHLEN - 1);
            strlcat(s, "/", MAXPATHLEN);
            strlcat(s, entry->d_name, MAXPATHLEN);
            if (strstr(entry->d_name, "eaccelerator") == entry->d_name) {
                unlink(s);
            }
            if (stat(s, &dirstat) != -1) {
                if (strcmp(entry->d_name, ".") == 0) {
                    continue;
                }
                if (strcmp(entry->d_name, "..") == 0) {
                    continue;
                }
                if (S_ISDIR(dirstat.st_mode)) {
                    clear_filecache(s);
                }
            }
        }
        closedir (dp);
    } else {
        ea_debug_error("[%s] Could not open cachedir %s\n", EACCELERATOR_EXTENSION_NAME, dir);
    }
}
#else
{
    HANDLE  hFind;
    WIN32_FIND_DATA wfd;
    char path[MAXPATHLEN];
    size_t dirlen = strlen(dir);

    memcpy(path, dir, dirlen);
    strcpy(path + dirlen++, "\\eaccelerator*");

    hFind = FindFirstFile(path, &wfd);
    if (hFind == INVALID_HANDLE_VALUE) {
        do {
            strcpy(path + dirlen, wfd.cFileName);
            if (FILE_ATTRIBUTE_DIRECTORY & wfd.dwFileAttributes) {
                clear_filecache(path);
            } else if (!DeleteFile(path)) {
                ea_debug_error("[%s] Can't delete file %s: error %d\n", EACCELERATOR_EXTENSION_NAME, path, GetLastError());
            }
        } while (FindNextFile(hFind, &wfd));
    }
    FindClose (hFind);
}
#endif
/* }}} */

/* {{{  clean_file: check if the given file is expired */
static inline void clean_file(char *file, time_t t)
{
    int f;

    if ((f = open(file, O_RDONLY | O_BINARY)) > 0) {
        ea_file_header hdr;
        EACCELERATOR_FLOCK (f, LOCK_SH);
        if (read(f, &hdr, sizeof(hdr)) != sizeof(hdr)
                || strncmp (hdr.magic, EA_MAGIC,	8) != 0
                || (hdr.mtime != 0 && hdr.mtime < t)) {
            EACCELERATOR_FLOCK (f, LOCK_UN);
            close (f);
            unlink (file);
        } else {
            EACCELERATOR_FLOCK (f, LOCK_UN);
            close (f);
        }
    }
}
/* }}} */

/* {{{ PHP_FUNCTION(eaccelerator_clean): remove all expired scripts and data from shared memory and disk cache */
PHP_FUNCTION(eaccelerator_clean)
{
    time_t t;

    if (ea_mm_instance == NULL) {
        RETURN_NULL();
    }

    if (!isAdminAllowed(TSRMLS_C)) {
        zend_error(E_WARNING, NOT_ADMIN_WARNING);
        RETURN_NULL();
    }

    t = time (NULL);

    /* Remove expired scripts from shared memory */
    eaccelerator_prune (t);
}
/* }}} */

/* {{{ PHP_FUNCTION(eaccelerator_check_mtime): enable or disable check_mtime */
PHP_FUNCTION(eaccelerator_check_mtime)
{
    zend_bool enable;

    if (ea_mm_instance == NULL) {
        RETURN_NULL();
    }

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "b", &enable) == FAILURE) {
        return;
    }

    if (isAdminAllowed(TSRMLS_C)) {
        if (enable) {
            ea_mm_instance->check_mtime_enabled = 1;
        } else {
            ea_mm_instance->check_mtime_enabled = 0;
        }
    } else {
        zend_error(E_WARNING, NOT_ADMIN_WARNING);
    }

    RETURN_NULL();
}
/* }}} */

/* {{{ PHP_FUNCTION(eaccelerator_optimizer): enable or disable optimizer */
#ifdef WITH_EACCELERATOR_OPTIMIZER
PHP_FUNCTION(eaccelerator_optimizer)
{
    zend_bool enable;

    if (ea_mm_instance == NULL) {
        RETURN_NULL();
    }

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "b", &enable) == FAILURE) {
        return;
    }

    if (isAdminAllowed(TSRMLS_C)) {
        if (enable) {
            ea_mm_instance->optimizer_enabled = 1;
        } else {
            ea_mm_instance->optimizer_enabled = 0;
        }
    } else {
        zend_error(E_WARNING, NOT_ADMIN_WARNING);
    }

    RETURN_NULL();
}
#endif
/* }}} */

/* {{{ PHP_FUNCTION(eaccelerator_caching): enable or disable caching */
PHP_FUNCTION(eaccelerator_caching)
{
    zend_bool enable;

    if (ea_mm_instance == NULL) {
        RETURN_NULL();
    }

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "b", &enable) == FAILURE) {
        return;
    }

    if (isAdminAllowed(TSRMLS_C)) {
        if (enable) {
            ea_mm_instance->enabled = 1;
        } else {
            ea_mm_instance->enabled = 0;
        }
    } else {
        zend_error(E_WARNING, NOT_ADMIN_WARNING);
    }

    RETURN_NULL();
}
/* }}} */

/* {{{ PHP_FUNCTION(eaccelerator_clear): remove all unused scripts and data from shared memory and disk cache */
PHP_FUNCTION(eaccelerator_clear)
{
    unsigned int i;
    ea_cache_entry *p;

    if (ea_mm_instance == NULL) {
        RETURN_NULL();
    }

    if (!isAdminAllowed(TSRMLS_C)) {
        zend_error(E_WARNING, NOT_ADMIN_WARNING);
        RETURN_NULL();
    }

    EACCELERATOR_LOCK_RW ();
    for (i = 0; i < EA_HASH_SIZE; i++) {
        p = ea_mm_instance->hash[i];
        while (p != NULL) {
            ea_cache_entry *r = p;
            p = p->next;
            ea_mm_instance->hash_cnt--;
            if (r->use_cnt <= 0) {
                eaccelerator_free_nolock (r);
            } else {
                r->removed = 1;
                r->next = ea_mm_instance->removed;
                ea_mm_instance->removed = r;
                ea_mm_instance->rem_cnt++;
            }
        }
        ea_mm_instance->hash[i] = NULL;
    }
    EACCELERATOR_UNLOCK_RW ();

    if(!ea_scripts_shm_only) {
        clear_filecache(EAG(cache_dir));
    }

    RETURN_NULL();
}
/* }}} */

/* {{{ PHP_FUNCTION(eaccelerator_purge): remove all 'removed' scripts from shared memory */
PHP_FUNCTION(eaccelerator_purge)
{

    if (!isAdminAllowed(TSRMLS_C)) {
        zend_error(E_WARNING, NOT_ADMIN_WARNING);
        RETURN_NULL();
    }

    if (ea_mm_instance != NULL) {
        ea_cache_entry *p, *q;
        EACCELERATOR_LOCK_RW();
        p = ea_mm_instance->removed;
        ea_mm_instance->rem_cnt = 0;
        ea_mm_instance->removed = NULL;
        while (p != NULL) {
            q = p->next;
            eaccelerator_free_nolock(p);
            p = q;
        }
        EACCELERATOR_UNLOCK_RW();
    }
    RETURN_NULL();
}
/* }}} */

/* {{{ PHP_FUNCTION(eaccelerator_info): get info about eaccelerator */
// returns info about eaccelerator as an array
// returhs the same as eaccelerator section in phpinfo
PHP_FUNCTION (eaccelerator_info)
{
    unsigned int available;
    char *shm, *sem;

    shm = (char *)mm_shm_type();
    sem = (char *)mm_sem_type();

    if (ea_mm_instance == NULL) {
        RETURN_NULL();
    }

    available = mm_available (ea_mm_instance->mm);

    // init return table
    array_init(return_value);

    // put eaccelerator information
    add_assoc_string(return_value, "version", EACCELERATOR_VERSION, 1);
    add_assoc_string(return_value, "shm_type", shm, 1);
    add_assoc_string(return_value, "sem_type", sem, 1);

#ifndef ZEND_ENGINE_2_5
    add_assoc_string(return_value, "logo", EACCELERATOR_LOGO_GUID, 1);
#endif

    add_assoc_bool(return_value, "cache", (EAG (enabled)
                                           && (ea_mm_instance != NULL)
                                           && ea_mm_instance->enabled) ? 1 : 0);
    add_assoc_bool(return_value, "optimizer", (EAG (optimizer_enabled)
                   && (ea_mm_instance != NULL)
                   && ea_mm_instance->optimizer_enabled) ? 1 : 0);
    add_assoc_bool(return_value, "check_mtime", (EAG (check_mtime_enabled)
                   && (ea_mm_instance != NULL)
                   && ea_mm_instance->check_mtime_enabled) ? 1 : 0);
    add_assoc_long(return_value, "memorySize", ea_mm_instance->total);
    add_assoc_long(return_value, "memoryAvailable", available);
    add_assoc_long(return_value, "memoryAllocated", ea_mm_instance->total - available);
    add_assoc_long(return_value, "cachedScripts", ea_mm_instance->hash_cnt);
    add_assoc_long(return_value, "removedScripts", ea_mm_instance->rem_cnt);

    return;
}
/* }}} */

/* {{{ PHP_FUNCTION(eaccelerator_cached_scripts): Get an array with information about all cached scripts */
PHP_FUNCTION(eaccelerator_cached_scripts)
{
    ea_cache_entry *p;
    int i;

    if (ea_mm_instance == NULL) {
        RETURN_NULL();
    }

    if (!isAdminAllowed(TSRMLS_C)) {
        zend_error(E_WARNING, NOT_ADMIN_WARNING);
        RETURN_NULL();
    }

    array_init(return_value);

    for (i = 0; i < EA_HASH_SIZE; i++) {
        p = ea_mm_instance->hash[i];
        while (p != NULL) {
            zval *script;
            MAKE_STD_ZVAL(script);
            array_init(script);
            add_assoc_string(script, "file", p->realfilename, 1);
            add_assoc_long(script, "mtime", p->mtime);
            add_assoc_long(script, "ts", p->ts);
            add_assoc_long(script, "ttl", p->ttl);
            add_assoc_long(script, "size", p->size);
            add_assoc_long(script, "reloads", p->nreloads);
            add_assoc_long(script, "usecount", p->use_cnt);
            add_assoc_long(script, "hits", p->nhits);
            add_next_index_zval(return_value, script);
            p = p->next;
        }
    }
    return;
}
/* }}} */

/* {{{ PHP_FUNCTION(eaccelerator_removed_scripts): Get a list of removed scripts */
PHP_FUNCTION(eaccelerator_removed_scripts)
{
    ea_cache_entry *p;
    zval *script;

    if (ea_mm_instance == NULL) {
        RETURN_NULL();
    }

    if (!isAdminAllowed(TSRMLS_C)) {
        zend_error(E_WARNING, NOT_ADMIN_WARNING);
        RETURN_NULL();
    }

    array_init(return_value);

    p = ea_mm_instance->removed;
    while (p != NULL) {
        MAKE_STD_ZVAL(script);
        array_init(script);
        add_assoc_string(script, "file", p->realfilename, 1);
        add_assoc_long(script, "mtime", p->mtime);
        add_assoc_long(script, "size", p->size);
        add_assoc_long(script, "reloads", p->nreloads);
        add_assoc_long(script, "usecount", p->use_cnt);
        add_assoc_long(script, "hits", p->nhits);
        add_next_index_zval(return_value, script);
        p = p->next;
    }
    return;
}
/* }}} */

#endif	/* WITH_EACCELERATOR_INFO */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: et sw=4 ts=4 fdm=marker
 * vim<600: et sw=4 ts=4
 */
