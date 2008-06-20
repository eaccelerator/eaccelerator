/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 - 2007 eAccelerator                               |
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

#include "eaccelerator.h"
#include "eaccelerator_version.h"

#ifdef HAVE_EACCELERATOR

#include "zend.h"
#include "zend_API.h"
#include "zend_extensions.h"
#include "ea_store.h"
#include "ea_restore.h"

#include <fcntl.h>

#ifndef O_BINARY
#  define O_BINARY 0
#endif

/* variables needed from eaccelerator.c */
extern long ea_shm_max;
extern eaccelerator_mm *eaccelerator_mm_instance;
extern int binary_eaccelerator_version;
extern int binary_php_version;
extern int binary_zend_version;

#if defined(WITH_EACCELERATOR_CONTENT_CACHING) || defined(WITH_EACCELERATOR_SESSIONS) || defined(WITH_EACCELERATOR_SHM)

static char *build_key(const char *key, int key_len, int *xlen TSRMLS_DC)
{
    int len;

    /*
     * namespace 
     */
    len = strlen(EAG(name_space));
    if (len > 0) {
        char *xkey;
        *xlen = len + key_len + 1;
        xkey = emalloc((*xlen) + 1);
        memcpy(xkey, EAG(name_space), len);
        xkey[len] = ':';
        memcpy(xkey + len + 1, key, key_len + 1);
        return xkey;
    }

    /*
     * hostname 
     */
    len = strlen(EAG(hostname));
    if (len > 0) {
        char *xkey;
        *xlen = len + key_len + 1;
        xkey = emalloc((*xlen) + 1);
        memcpy(xkey, EAG(hostname), len);
        xkey[len] = ':';
        memcpy(xkey + len + 1, key, key_len + 1);
        return xkey;
    } else {
        *xlen = key_len;
        return (char *) key;
    }
}

/* lock the key cache */
int eaccelerator_lock(const char *key, int key_len TSRMLS_DC)
{
    int xlen;
    char *xkey;
    ea_lock_entry *x;
    ea_lock_entry **p;
    int ok = 0;

    if (eaccelerator_mm_instance == NULL)
        return 0;

    xkey = build_key(key, key_len, &xlen TSRMLS_CC);
    EACCELERATOR_UNPROTECT();
    x = eaccelerator_malloc(offsetof(ea_lock_entry, key) + xlen + 1);
    if (x == NULL) {
        EACCELERATOR_PROTECT();
        if (xlen != key_len)
            efree(xkey);

        return 0;
    }
    x->pid = getpid();
#ifdef ZTS
    x->thread = tsrm_thread_id();
#endif
    x->next = NULL;
    memcpy(x->key, xkey, xlen + 1);
    while (1) {
        EACCELERATOR_LOCK_RW();
        p = &eaccelerator_mm_instance->locks;
        while ((*p) != NULL) {
            if (strcmp((*p)->key, x->key) == 0) {
#ifdef ZTS
                if (x->pid ==(*p)->pid && x->thread == (*p)->thread) {
#else
                if (x->pid ==(*p)->pid) {
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
#else
            struct timeval t;
            t.tv_sec = 0;
            t.tv_usec = 100;
            select(0, NULL, NULL, NULL, &t);
#endif
        }
    }
    EACCELERATOR_PROTECT();
    if (xlen != key_len)
        efree(xkey);

    return 1;
}

/* unlock to key cache */
int eaccelerator_unlock(const char *key, int key_len TSRMLS_DC)
{
    int xlen;
    char *xkey;
    ea_lock_entry **p;

    if (eaccelerator_mm_instance == NULL)
        return 0;

    xkey = build_key(key, key_len, &xlen TSRMLS_CC);
    EACCELERATOR_UNPROTECT();
    EACCELERATOR_LOCK_RW();
    p = &eaccelerator_mm_instance->locks;
    while ((*p) != NULL) {
        if (strcmp((*p)->key, xkey) == 0) {
#ifdef ZTS
            if ((*p)->pid == getpid() && (*p)->thread == tsrm_thread_id()) {
#else
            if ((*p)->pid == getpid()) {
#endif
                ea_lock_entry *x = (*p);
                *p = (*p)->next;
                eaccelerator_free_nolock(x);
            } else {
                EACCELERATOR_UNLOCK_RW();
                EACCELERATOR_PROTECT();
                if (xlen != key_len)
                    efree(xkey);

                return 0;
            }
            break;
        }
        p = &(*p)->next;
    }
    EACCELERATOR_UNLOCK_RW();
    EACCELERATOR_PROTECT();
    if (xlen != key_len)
        efree(xkey);

    return 1;
}

/* put a key in the cache (shm or disk) */
int eaccelerator_put(const char *key, int key_len, zval * val, time_t ttl, ea_cache_place where TSRMLS_DC)
{
    ea_user_cache_entry *p, *q;
    unsigned int slot, hv;
    long size;
    int use_shm = 1;
    int ret = 0;
    char s[MAXPATHLEN];
    int xlen;
    char *xkey;

    xkey = build_key(key, key_len, &xlen TSRMLS_CC);
    EAG(compress) = 1;
    EAG(mem) = NULL;
    zend_hash_init(&EAG(strings), 0, NULL, NULL, 0);
    EACCELERATOR_ALIGN(EAG(mem));
    size = offsetof(ea_user_cache_entry, key) + xlen + 1;
    size += calc_zval(val TSRMLS_CC);
    zend_hash_destroy(&EAG(strings));

    EAG(mem) = NULL;
    if (eaccelerator_mm_instance != NULL && (where == ea_shm_and_disk || where == ea_shm || where == ea_shm_only)) {
        EACCELERATOR_UNPROTECT();
        if (ea_shm_max == 0 || size <= ea_shm_max) {
            EAG(mem) = eaccelerator_malloc(size);
            if (EAG(mem) == NULL) {
                EAG(mem) = eaccelerator_malloc2(size TSRMLS_CC);
            }
        }
        if (EAG(mem) == NULL) {
            EACCELERATOR_PROTECT();
        }
    }
    if (EAG(mem) == NULL && (where == ea_shm_and_disk || where == ea_shm || where == ea_disk_only)) {
        use_shm = 0;
        EAG(mem) = emalloc(size);
    }
    if (EAG(mem)) {
        zend_hash_init(&EAG(strings), 0, NULL, NULL, 0);
        EACCELERATOR_ALIGN(EAG(mem));
        q = (ea_user_cache_entry *) EAG(mem);
        q->size = size;
        EAG(mem) += offsetof(ea_user_cache_entry, key) + xlen + 1;
        q->hv = zend_get_hash_value(xkey, xlen);
        memcpy(q->key, xkey, xlen + 1);
        memcpy(&q->value, val, sizeof(zval));
        q->ttl = ttl ? EAG(req_start) + ttl : 0;
        q->create = EAG(req_start);
        /* set the refcount to 1 */
        RESET_PZVAL_REFCOUNT(&q->value);
        store_zval(&EAG(mem), &q->value TSRMLS_CC);
        zend_hash_destroy(&EAG(strings));

        /*
         * storing to file 
         */
        if ((where == ea_shm_and_disk || ((where == ea_shm) && !use_shm) || where == ea_disk_only) && 
        		eaccelerator_md5(s, "/eaccelerator-user-", q->key TSRMLS_CC)) {
            int f;
            unlink(s);
            f = open(s, O_CREAT | O_WRONLY | O_EXCL | O_BINARY, S_IRUSR | S_IWUSR);
            if (f > 0) {
                ea_file_header hdr;
                EACCELERATOR_FLOCK(f, LOCK_EX);
                init_header(&hdr);
                hdr.size = q->size;
                hdr.mtime = q->ttl;
                q->next = q;
                hdr.crc32 = eaccelerator_crc32((const char *) q, q->size);
                if (write(f, &hdr, sizeof(hdr)) == sizeof(hdr)) {
                    ssize_t result = 0;
                    result = write(f, q, q->size);
                    EACCELERATOR_FLOCK(f, LOCK_UN);
                    close(f);
                    ret = 1;
                } else {
                    EACCELERATOR_FLOCK(f, LOCK_UN);
                    close(f);
                    unlink(s);
                }
            }
            if (!use_shm)
                efree(q);
        }

        if ((where == ea_shm_and_disk || where == ea_shm || where == ea_shm_only) && use_shm) {
            /*
             * storing to shared memory 
             */
            slot = q->hv & EA_USER_HASH_MAX;
            hv = q->hv;
            EACCELERATOR_LOCK_RW();
            eaccelerator_mm_instance->user_hash_cnt++;
            q->next = eaccelerator_mm_instance->user_hash[slot];
            eaccelerator_mm_instance->user_hash[slot] = q;
            p = q->next;
            while (p != NULL) {
                if ((p->hv == hv) && (strcmp(p->key, xkey) == 0)) {
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
    if (xlen != key_len)
        efree(xkey);

    return ret;
}

/* get a key from the cache */
int eaccelerator_get(const char *key, int key_len, zval * return_value, ea_cache_place where TSRMLS_DC)
{
    unsigned int hv, slot;
    char s[MAXPATHLEN];
    int xlen;
    char *xkey;

    xkey = build_key(key, key_len, &xlen TSRMLS_CC);
    hv = zend_get_hash_value(xkey, xlen);
    slot = hv & EA_USER_HASH_MAX;

    if (eaccelerator_mm_instance != NULL && (where == ea_shm_and_disk || where == ea_shm || where == ea_shm_only)) {
        ea_user_cache_entry *p, *q;
        ea_user_cache_entry *x = NULL;
        EACCELERATOR_UNPROTECT();
        EACCELERATOR_LOCK_RW();
        q = NULL;
        p = eaccelerator_mm_instance->user_hash[slot];
        while (p != NULL) {
            if ((p->hv == hv) && (strcmp(p->key, xkey) == 0)) {
                x = p;
                if (p->ttl != 0 && p->ttl < EAG(req_start)) {
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
            if (xlen != key_len) {
                efree(xkey);
            }
            return 1;
        }
    }

    /*
     * key is not found in shared memory try to load it from file 
     */
    if ((where == ea_shm_and_disk || where == ea_shm || where == ea_disk_only) && 
            eaccelerator_md5(s, "/eaccelerator-user-", xkey TSRMLS_CC)) {
        int use_shm = 1;
        int ret = 0;
        int f;

        if ((f = open(s, O_RDONLY | O_BINARY)) > 0) {
            ea_file_header hdr;

            EACCELERATOR_FLOCK(f, LOCK_SH);
            if (read(f, &hdr, sizeof(hdr)) != sizeof(hdr) || !check_header(&hdr)) {
                EACCELERATOR_FLOCK(f, LOCK_UN);
                close(f);
                unlink(s);
                if (xlen != key_len)
                    efree(xkey);
                return 0;
            }
            if (hdr.mtime == 0 || hdr.mtime > EAG(req_start)) {
                /*
                 * try to put it into shared memory 
                 */
                ea_user_cache_entry *p = NULL;
                if (eaccelerator_mm_instance != NULL && (where == ea_shm_and_disk || where == ea_shm)) {
                    if (ea_shm_max == 0 || hdr.size <= ea_shm_max) {
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
                    if (read(f, p, hdr.size) == hdr.size && hdr.size == p->size 
                            && hdr.crc32 == eaccelerator_crc32((const char *) p, p->size)) {
                        EAG(mem) = (char *) ((long) p - (long) p->next);
                        EAG(compress) = 1;
                        fixup_zval(((char *) ((long) p - (long) p->next)), &p->value TSRMLS_CC);

                        if (strcmp(xkey, p->key) != 0) {
                            if (use_shm) {
                                eaccelerator_free(p);
                            } else {
                                efree(p);
                            }
                            EACCELERATOR_FLOCK(f, LOCK_UN);
                            close(f);
                            unlink(s);
                            if (use_shm) {
                                EACCELERATOR_PROTECT();
                            }
                            if (xlen != key_len) {
                                efree(xkey);
                            }
                            return 0;
                        }

                        memcpy(return_value, &p->value, sizeof(zval));
                        restore_zval(return_value TSRMLS_CC);
                        ret = 1;
                        if (use_shm) {
                            /* put it into shared memory */
                            ea_user_cache_entry *q, *prev;

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
                if (use_shm)
                    EACCELERATOR_PROTECT();
            } else {
                EACCELERATOR_FLOCK(f, LOCK_UN);
                close(f);
                unlink(s);
            }
            if (xlen != key_len) {
                efree(xkey);
            }
            return ret;
        }
    }
    if (xlen != key_len) {
        efree(xkey);
    }
    return 0;
}

/* remove a key from the cache */
int eaccelerator_rm(const char *key, int key_len, ea_cache_place where TSRMLS_DC)
{
    unsigned int hv, slot;
    ea_user_cache_entry *p, *q;
    char s[MAXPATHLEN];
    int xlen;
    char *xkey;

    xkey = build_key(key, key_len, &xlen TSRMLS_CC);
    /*
     * removing file 
     */
    if ((where == ea_shm_and_disk || where == ea_shm || where == ea_disk_only) && 
    		eaccelerator_md5(s, "/eaccelerator-user-", xkey TSRMLS_CC)) {
        unlink(s);
    }

    /*
     * removing from shared memory 
     */
    if (eaccelerator_mm_instance != NULL && (where == ea_shm_and_disk || 
                where == ea_shm || where == ea_shm_only)) {
        hv = zend_get_hash_value(xkey, xlen);
        slot = hv & EA_USER_HASH_MAX;

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
    if (xlen != key_len) {
        efree(xkey);
    }
    return 1;
}

/* do garbage collection on the keys */
size_t eaccelerator_gc(TSRMLS_D)
{
    size_t size = 0;
    unsigned int i;
    time_t t = EAG(req_start);

    if (eaccelerator_mm_instance == NULL) {
        return 0;
    }
    EACCELERATOR_UNPROTECT();
    EACCELERATOR_LOCK_RW();
    for (i = 0; i < EA_USER_HASH_SIZE; i++) {
        ea_user_cache_entry **p = &eaccelerator_mm_instance->user_hash[i];
        while (*p != NULL) {
            if ((*p)->ttl != 0 && (*p)->ttl < t) {
                ea_user_cache_entry *r = *p;
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

#ifdef WITH_EACCELERATOR_INFO
/* get list of all keys stored in memory that matches hostname or namespace */
int eaccelerator_list_keys(zval *return_value TSRMLS_DC) 
{
    unsigned int i, xlen;
    zval *list;
    char *xkey = "";
    ea_user_cache_entry *p;

    // create key prefix for current host / namespace
    xlen = strlen(EAG(name_space));
    if (xlen > 0) {
        xkey = emalloc(xlen + 1);
        memcpy(xkey, EAG(name_space), xlen);
    } else {
        xlen = strlen(EAG(hostname));
        if (xlen > 0) {
            xkey = emalloc(xlen + 1);
            memcpy(xkey, EAG(hostname), xlen);
        }
    }

    // initialize return value as an array
    array_init(return_value);

    for (i = 0; i < EA_USER_HASH_SIZE; ++i) {
        p = eaccelerator_mm_instance->user_hash[i];
        while(p != NULL) {
            if (!xlen || strncmp(p->key, xkey, xlen) == 0) {
                list = NULL;
                ALLOC_INIT_ZVAL(list);
                array_init(list);
                
                if (strlen(p->key) > xlen) {
                    add_assoc_string(list, "name", (p->key) + xlen, 1);
                } else {
                    add_assoc_string(list, "name", p->key, 1);
                }
                
                if (p->ttl) {
                    if (p->ttl > EAG(req_start)) {
                        add_assoc_long(list, "ttl", p->ttl); // ttl
                    } else {
                        add_assoc_long(list, "ttl", -1); // expired
                    }
                } else {
                    add_assoc_long(list, "ttl", 0); // no ttl
                }
                
                add_assoc_long(list, "created", p->create);
                add_assoc_long(list, "size", p->size);
                add_next_index_zval(return_value, list);
            }
            p = p->next;
        }
    }

    if (xlen > 0) 
        efree(xkey);
    return 1;
}
#endif /* WITH_EACCELERATOR_INFO */

#endif /* WITH_EACCELERATOR_CONTENT_CACHING) || WITH_EACCELERATOR_SESSIONS || WITH_EACCELERATOR_SHM */

#endif /* HAVE_EACCELERATOR */

