/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 - 2006 eAccelerator                               |
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
   $Id: $
*/

#include "eaccelerator.h"
#include "eaccelerator_version.h"
#include "ea_info.h"
#include "mm.h"
#include "cache.h"
#include "zend.h"
#include "fopen_wrappers.h"

#ifndef O_BINARY
#  define O_BINARY 0
#endif

#ifdef WITH_EACCELERATOR_INFO

#define NOT_ADMIN_WARNING "This script isn't in the allowed_admin_path setting!"

extern eaccelerator_mm *eaccelerator_mm_instance;

/* {{{ isAdminAllowed(): check if the admin functions are allowed for the calling script */
static int isAdminAllowed(TSRMLS_D) {
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

/* {{{ PHP_FUNCTION(eaccelerator_caching): enable or disable caching */
PHP_FUNCTION(eaccelerator_caching) 
{
    zend_bool enable;
    
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "b", &enable) == FAILURE)
		return;

    if (isAdminAllowed()) {
        EACCELERATOR_UNPROTECT();
        if (enable) {
            eaccelerator_mm_instance->enabled = 1;
        } else {
            eaccelerator_mm_instance->enabled = 0;
        }
        EACCELERATOR_PROTECT();
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
    
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "b", &enable) == FAILURE)
		return;

    if (isAdminAllowed()) {
        EACCELERATOR_UNPROTECT();
        if (enable) {
            eaccelerator_mm_instance->optimizer_enabled = 1;
        } else {
            eaccelerator_mm_instance->optimizer_enabled = 0;
        }
        EACCELERATOR_PROTECT();
    } else {
        zend_error(E_WARNING, NOT_ADMIN_WARNING);
    }
    
    RETURN_NULL();
}
#endif
/* }}} */

/* {{{ PHP_FUNCTION(eaccelerator_clean): remove all expired scripts and data from shared memory and disk cache */
PHP_FUNCTION(eaccelerator_clean)
{
	time_t t;

    if (!isAdminAllowed()) {
        zend_error(E_WARNING, NOT_ADMIN_WARNING);
        RETURN_NULL();
    }

	t = time (0);

	/* Remove expired scripts from shared memory */
	eaccelerator_prune (t);

	/* Remove expired keys (session data, content) from disk cache */
#ifndef ZEND_WIN32
	/* clear file cache */
	{
		DIR *dp;
		struct dirent *entry;
		char s[MAXPATHLEN];

		if ((dp = opendir (EAG (cache_dir))) != NULL) {
			while ((entry = readdir (dp)) != NULL) {
				if (strstr(entry->d_name, "eaccelerator-user") == entry->d_name) {
					int f;
					strncpy (s, EAG (cache_dir), MAXPATHLEN - 1);
					strlcat (s, "/", MAXPATHLEN);
					strlcat (s, entry->d_name, MAXPATHLEN);
					if ((f = open (s, O_RDONLY | O_BINARY)) > 0) {
						mm_file_header hdr;
						EACCELERATOR_FLOCK (f, LOCK_SH);
						if (read (f, &hdr, sizeof (hdr)) != sizeof (hdr) 
                                || strncmp (hdr.magic, "EACCELERATOR",	8) != 0 || (hdr.mtime != 0 && hdr.mtime < t)) {
							EACCELERATOR_FLOCK (f, LOCK_UN);
							close (f);
							unlink (s);
						} else {
							EACCELERATOR_FLOCK (f, LOCK_UN);
							close (f);
						}
					}
				}
			}
			closedir (dp);
		}
	}
#else
	{
		HANDLE hList;
		TCHAR szDir[MAXPATHLEN];
		WIN32_FIND_DATA FileData;
		char s[MAXPATHLEN];

		snprintf (szDir, MAXPATHLEN, "%s\\eaccelerator-user*", EAG (cache_dir));

		if ((hList = FindFirstFile (szDir, &FileData)) != INVALID_HANDLE_VALUE) {
			do {
				int f;
				strncpy (s, EAG (cache_dir), MAXPATHLEN - 1);
				strlcat (s, "\\", MAXPATHLEN);
				strlcat (s, FileData.cFileName, MAXPATHLEN);
				if ((f = open (s, O_RDONLY | O_BINARY)) > 0) {
					mm_file_header hdr;
					EACCELERATOR_FLOCK (f, LOCK_SH);
					if (read (f, &hdr, sizeof (hdr)) != sizeof (hdr) 
                            || strncmp (hdr.magic, "EACCELERATOR", 8) != 0 || (hdr.mtime != 0 && hdr.mtime < t)) {
						EACCELERATOR_FLOCK (f, LOCK_UN);
						close (f);
						unlink (s);
					} else {
						EACCELERATOR_FLOCK (f, LOCK_UN);
						close (f);
					}
				}
			}
			while (FindNextFile (hList, &FileData));
		}
		FindClose (hList);
	}
#endif
	/* Remove expired keys (session data, content) from shared memory */
	eaccelerator_gc (TSRMLS_C);
}
/* }}} */

/* {{{ PHP_FUNCTION(eaccelerator_clear): remove all unused scripts and data from shared memory and disk cache */
PHP_FUNCTION(eaccelerator_clear)
{
	unsigned int i;
	mm_cache_entry *p;

    if (!isAdminAllowed()) {
        zend_error(E_WARNING, NOT_ADMIN_WARNING);
        RETURN_NULL();
    }

	EACCELERATOR_UNPROTECT ();
	EACCELERATOR_LOCK_RW ();
	for (i = 0; i < MM_HASH_SIZE; i++) {
		p = eaccelerator_mm_instance->hash[i];
		while (p != NULL) {
			mm_cache_entry *r = p;
			p = p->next;
			eaccelerator_mm_instance->hash_cnt--;
			if (r->use_cnt <= 0) {
				eaccelerator_free_nolock (r);
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
		mm_user_cache_entry *p = eaccelerator_mm_instance->user_hash[i];
		while (p != NULL) {
			mm_user_cache_entry *r = p;
			p = p->next;
			eaccelerator_mm_instance->user_hash_cnt--;
			eaccelerator_free_nolock (r);
		}
		eaccelerator_mm_instance->user_hash[i] = NULL;
	}
	EACCELERATOR_UNLOCK_RW ();
	EACCELERATOR_PROTECT ();
#ifndef ZEND_WIN32
	/* clear file cache */
	{
		DIR *dp;
		struct dirent *entry;
		char s[MAXPATHLEN];

		if ((dp = opendir (EAG (cache_dir))) != NULL) {
			while ((entry = readdir (dp)) != NULL) {
				if (strstr (entry->d_name, "eaccelerator") == entry->d_name) {
					strncpy (s, EAG (cache_dir), MAXPATHLEN - 1);
					strlcat (s, "/", MAXPATHLEN);
					strlcat (s, entry->d_name, MAXPATHLEN);
					unlink (s);
				}
			}
			closedir (dp);
		}
	}
#else
	{
		HANDLE hList;
		TCHAR szDir[MAXPATHLEN];
		WIN32_FIND_DATA FileData;
		char s[MAXPATHLEN];

		snprintf (szDir, MAXPATHLEN, "%s\\eaccelerator*", EAG (cache_dir));

		if ((hList = FindFirstFile (szDir, &FileData)) != INVALID_HANDLE_VALUE) {
			do {
				strncpy (s, EAG (cache_dir), MAXPATHLEN - 1);
				strlcat (s, "\\", MAXPATHLEN);
				strlcat (s, FileData.cFileName, MAXPATHLEN);
				unlink (s);
			}
			while (FindNextFile (hList, &FileData));
		}

		FindClose (hList);
	}
#endif
    RETURN_NULL();
}
/* }}} */

/* {{{ PHP_FUNCTION(eaccelerator_purge): remove all 'removed' scripts from shared memory */
PHP_FUNCTION(eaccelerator_purge)
{

    if (!isAdminAllowed()) {
        zend_error(E_WARNING, NOT_ADMIN_WARNING);
        RETURN_NULL();
    }

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
	available = mm_available (eaccelerator_mm_instance->mm);

	// init return table
	array_init(return_value);
	
	// put eaccelerator information
	add_assoc_string(return_value, "version", EACCELERATOR_VERSION, 1);
	add_assoc_string(return_value, "shm_type", shm, 1);
    add_assoc_string(return_value, "sem_type", sem, 1);
    add_assoc_string(return_value, "logo", EACCELERATOR_LOGO_GUID, 1);
	add_assoc_bool(return_value, "cache", (EAG (enabled)
		&& (eaccelerator_mm_instance != NULL)
		&& eaccelerator_mm_instance->enabled) ? 1 : 0);
	add_assoc_bool(return_value, "optimizer", (EAG (optimizer_enabled)
		&& (eaccelerator_mm_instance != NULL)
		&& eaccelerator_mm_instance->optimizer_enabled) ? 1 : 0);
	add_assoc_long(return_value, "memorySize", eaccelerator_mm_instance->total);
	add_assoc_long(return_value, "memoryAvailable", available);
	add_assoc_long(return_value, "memoryAllocated", eaccelerator_mm_instance->total - available);
	add_assoc_long(return_value, "cachedScripts", eaccelerator_mm_instance->hash_cnt);
	add_assoc_long(return_value, "removedScripts", eaccelerator_mm_instance->rem_cnt);
    add_assoc_long(return_value, "cachedKeys", eaccelerator_mm_instance->user_hash_cnt);

	return;
}
/* }}} */

/* {{{ PHP_FUNCTION(eaccelerator_cached_scripts): Get an array with information about all cached scripts */
PHP_FUNCTION(eaccelerator_cached_scripts)
{
    mm_cache_entry *p;
    int i;

    if (!isAdminAllowed()) {
        zend_error(E_WARNING, NOT_ADMIN_WARNING);
        RETURN_NULL();
    }

    array_init(return_value);
    
    for (i = 0; i < MM_HASH_SIZE; i++) {
        p = eaccelerator_mm_instance->hash[i];
        while (p != NULL) {
            zval *script;
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
    }
    return;
}
/* }}} */

/* {{{ PHP_FUNCTION(eaccelerator_removed_scripts): Get a list of removed scripts */
PHP_FUNCTION(eaccelerator_removed_scripts)
{
    mm_cache_entry *p;
    zval *script;

    if (!isAdminAllowed()) {
        zend_error(E_WARNING, NOT_ADMIN_WARNING);
        RETURN_NULL();
    }

    MAKE_STD_ZVAL(script);
    array_init(return_value);

    p = eaccelerator_mm_instance->removed;
    while (p != NULL) {
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

/* {{{ PHP_FUNCTION(eaccelerator_list_keys): returns list of keys in shared memory that matches actual hostname or namespace */
PHP_FUNCTION(eaccelerator_list_keys)
{
	if (eaccelerator_list_keys(return_value TSRMLS_CC)) {
		return;
	} else {
    	RETURN_NULL ();
	}
}
/* }}} */

#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */

