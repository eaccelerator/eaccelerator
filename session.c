/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 - 2007 eAccelerator                               |
   | http://eaccelerator.net                                     		  |
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
#include "session.h"

#ifdef WITH_EACCELERATOR_SESSIONS

#include "cache.h"
#include "ext/standard/md5.h"
#include "ext/standard/php_lcg.h"
#include <fcntl.h>

#ifdef WIN32
#	include "win32/time.h"
#endif

ea_cache_place eaccelerator_sessions_cache_place = ea_shm_and_disk;
int eaccelerator_sessions_registered = 0;
extern eaccelerator_mm *eaccelerator_mm_instance;

/* set the updated ini value of the cache place */
PHP_INI_MH(eaccelerator_OnUpdateSessionCachePlace)
{
	if (strncasecmp("shm_and_disk", new_value, sizeof("shm_and_disk")) == 0) {
		eaccelerator_sessions_cache_place = ea_shm_and_disk;
	} else if (strncasecmp("shm", new_value, sizeof ("shm")) == 0) {
		eaccelerator_sessions_cache_place = ea_shm;
	} else if (strncasecmp("shm_only", new_value, sizeof ("shm_only")) == 0) {
		eaccelerator_sessions_cache_place = ea_shm_only;
	} else if (strncasecmp("disk_only", new_value, sizeof ("disk_only")) == 0) {
		eaccelerator_sessions_cache_place = ea_disk_only;
	} else if (strncasecmp("none", new_value, sizeof ("none")) == 0) {
		eaccelerator_sessions_cache_place = ea_none;
	}
	return SUCCESS;
}

/* session unlock */
static int do_session_unlock(TSRMLS_D)
{
	if (EAG (session) != NULL) {
		eaccelerator_unlock(EAG(session), strlen(EAG(session)) TSRMLS_CC);
		efree(EAG(session));
		EAG(session) = NULL;
	}
	return 1;
}

/* session locking */
static int do_session_lock(const char *sess_name TSRMLS_DC)
{
	if (EAG(session) != NULL) {
		if (strcmp(EAG(session), sess_name) == 0) {
			return 1;
		} else {
			do_session_unlock(TSRMLS_C);
		}
	}
	if (eaccelerator_lock(sess_name, strlen(sess_name) TSRMLS_CC)) {
		EAG(session) = estrdup(sess_name);
		return 1;
	} else {
		return 0;
	}
}

/******************************************************************************/
/* Session api functions 													  */
/******************************************************************************/
PS_OPEN_FUNC(eaccelerator)
{
	if (eaccelerator_mm_instance == NULL) {
		return FAILURE;
	}
	PS_SET_MOD_DATA((void *) 1);
	return SUCCESS;
}

PS_CLOSE_FUNC(eaccelerator)
{
	if (eaccelerator_mm_instance == NULL) {
		return FAILURE;
	}
	do_session_unlock(TSRMLS_C);
	return SUCCESS;
}

PS_READ_FUNC(eaccelerator)
{
	char *skey;
	int len;
	zval ret;

	len = sizeof("sess_") + strlen(key);
	skey = do_alloca(len + 1);
	strcpy(skey, "sess_");
	strcat(skey, key);
	do_session_lock(skey TSRMLS_CC);
	if (eaccelerator_get(skey, len, &ret, eaccelerator_sessions_cache_place TSRMLS_CC)
		    && ret.type == IS_STRING) {
		*val = estrdup(Z_STRVAL(ret));
		*vallen = Z_STRLEN(ret);
		zval_dtor(&ret);
	} else {
		*val = emalloc(1);
		(*val)[0] = '\0';
		*vallen = 0;
	}
	free_alloca(skey);
	return SUCCESS;
}

PS_WRITE_FUNC(eaccelerator)
{
	char *skey;
	int len;
	time_t ttl = INI_INT("session.gc_maxlifetime");
	zval sval;

	len = sizeof("sess_") + strlen(key);
	skey = do_alloca(len + 1);
	strcpy(skey, "sess_");
	strcat(skey, key);
	if (!ttl) {
		ttl = 1440;
	}
	Z_TYPE(sval) = IS_STRING;
	Z_STRVAL(sval) = (char *) val;
	Z_STRLEN(sval) = vallen;

	do_session_lock(skey TSRMLS_CC);
	if (eaccelerator_put(skey, len, &sval, ttl, eaccelerator_sessions_cache_place TSRMLS_CC)) {
		free_alloca(skey);
		return SUCCESS;
	} else {
		free_alloca(skey);
		return FAILURE;
	}
}

PS_DESTROY_FUNC(eaccelerator)
{
	char *skey;
	int len;

	len = sizeof("sess_") + strlen(key);
	skey = do_alloca(len + 1);
	strcpy(skey, "sess_");
	strcat(skey, key);
	if (eaccelerator_rm(skey, len, eaccelerator_sessions_cache_place TSRMLS_CC)) {
		free_alloca(skey);
		return SUCCESS;
	} else {
		free_alloca(skey);
		return FAILURE;
	}
}

PS_GC_FUNC(eaccelerator)
{
	if (eaccelerator_mm_instance == NULL) {
		return FAILURE;
	}
	eaccelerator_gc(TSRMLS_C);
	return SUCCESS;
}

PS_CREATE_SID_FUNC(eaccelerator)
{
	static char hexconvtab[] = "0123456789abcdef";
	PHP_MD5_CTX context;
	unsigned char digest[16];
	char buf[256];
	struct timeval tv;
	int i;
	int j = 0;
	unsigned char c;

	long entropy_length = INI_INT("session.entropy_length");
	char *entropy_file = INI_STR("session.entropy_file");

	if (!entropy_length) {
		entropy_length = 0;
	}
	if (!entropy_file) {
		entropy_file = empty_string;
	}

	gettimeofday(&tv, NULL);
	PHP_MD5Init(&context);

	sprintf(buf, "%ld%ld%0.8f", tv.tv_sec, tv.tv_usec, php_combined_lcg (TSRMLS_C) * 10);
	PHP_MD5Update(&context, (unsigned char *)buf, strlen(buf));

	if (entropy_length > 0) {
		int fd;

		fd = VCWD_OPEN(entropy_file, O_RDONLY);
		if (fd >= 0) {
			unsigned char buf[2048];
			int n;
			size_t to_read = entropy_length;

			while (to_read > 0) {
				n = read(fd, buf, MIN (to_read, sizeof(buf)));
				if (n <= 0)
					break;
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
	return estrdup (buf);
}

ps_module ps_mod_eaccelerator = {
	PS_MOD_SID(eaccelerator)
};

/* register ea as session handler */
void eaccelerator_register_session()
{
	if (eaccelerator_sessions_cache_place != ea_none && eaccelerator_sessions_registered == 0) {
		if (php_session_register_module(&ps_mod_eaccelerator) != 0) {
			zend_error(E_CORE_ERROR, "Could not register eAccelerator session handler!");	
		}
		eaccelerator_sessions_registered = 1;
		return;
	}
}

#endif /* HAVE_EACCELERATOR_SESSIONS */
