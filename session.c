/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 - 2006 eAccelerator                               |
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
   $Id: session.c 176 2006-03-05 12:18:54Z bart $
*/

#include "eaccelerator.h"
#include "session.h"

#ifdef WITH_EACCELERATOR_SESSIONS

#include "cache.h"
#include "ext/standard/md5.h"
#include <fcntl.h>

#ifdef WIN32
#	include "win32/time.h"
#endif

#if defined(HAVE_PHP_SESSIONS_SUPPORT) && defined(PS_CREATE_SID_ARGS)
#	include "ext/standard/php_lcg.h"
#endif

eaccelerator_cache_place eaccelerator_sessions_cache_place =
	eaccelerator_shm_and_disk;
int eaccelerator_sessions_registered = 0;
extern eaccelerator_mm *eaccelerator_mm_instance;

/* set the updated ini value of the cache place */
PHP_INI_MH (eaccelerator_OnUpdateSessionCachePlace)
{
	if (strncasecmp ("shm_and_disk", new_value, sizeof ("shm_and_disk")) == 0) {
		eaccelerator_sessions_cache_place = eaccelerator_shm_and_disk;
	} else if (strncasecmp ("shm", new_value, sizeof ("shm")) == 0) {
		eaccelerator_sessions_cache_place = eaccelerator_shm;
	} else if (strncasecmp ("shm_only", new_value, sizeof ("shm_only")) == 0) {
		eaccelerator_sessions_cache_place = eaccelerator_shm_only;
	} else if (strncasecmp ("disk_only", new_value, sizeof ("disk_only")) == 0) {
		eaccelerator_sessions_cache_place = eaccelerator_disk_only;
	} else if (strncasecmp ("none", new_value, sizeof ("none")) == 0) {
		eaccelerator_sessions_cache_place = eaccelerator_none;
	}
	return SUCCESS;
}

/* session unlock */
static int do_session_unlock (TSRMLS_D)
{
	if (EAG (session) != NULL) {
		eaccelerator_unlock (EAG (session), strlen (EAG (session)) TSRMLS_CC);
		efree (EAG (session));
		EAG (session) = NULL;
	}
	return 1;
}

/* session locking */
static int do_session_lock (const char *sess_name TSRMLS_DC)
{
	if (EAG (session) != NULL) {
		if (strcmp (EAG (session), sess_name) == 0) {
			return 1;
		} else {
			do_session_unlock (TSRMLS_C);
		}
	}
	if (eaccelerator_lock (sess_name, strlen (sess_name) TSRMLS_CC)) {
		EAG (session) = estrdup (sess_name);
		return 1;
	} else {
		return 0;
	}
}

#ifdef HAVE_PHP_SESSIONS_SUPPORT	/* PHP_SESSION_API >= 20020306 */
/******************************************************************************/
/* Session api functions 													  */
/******************************************************************************/

PS_OPEN_FUNC (eaccelerator)
{
	if (eaccelerator_mm_instance == NULL) {
		return FAILURE;
	}
	PS_SET_MOD_DATA ((void *) 1);
	return SUCCESS;
}

PS_CLOSE_FUNC (eaccelerator)
{
	if (eaccelerator_mm_instance == NULL) {
		return FAILURE;
	}
	do_session_unlock (TSRMLS_C);
	return SUCCESS;
}

PS_READ_FUNC (eaccelerator)
{
	char *skey;
	int len;
	zval ret;

	len = sizeof ("sess_") + strlen (key);
	skey = do_alloca (len + 1);
	strcpy (skey, "sess_");
	strcat (skey, key);
	do_session_lock (skey TSRMLS_CC);
	if (eaccelerator_get
		(skey, len, &ret, eaccelerator_sessions_cache_place TSRMLS_CC)
		&& ret.type == IS_STRING) {
		*val = estrdup (ret.value.str.val);
		*vallen = ret.value.str.len;
		zval_dtor (&ret);
	} else {
		*val = emalloc (1);
		(*val)[0] = '\0';
		*vallen = 0;
	}
	free_alloca (skey);
	return SUCCESS;
}

PS_WRITE_FUNC (eaccelerator)
{
	char *skey;
	int len;
	time_t ttl;
	zval sval;
	char *tmp;

	len = sizeof ("sess_") + strlen (key);
	skey = do_alloca (len + 1);
	strcpy (skey, "sess_");
	strcat (skey, key);
	if (cfg_get_string ("session.gc_maxlifetime", &tmp) == FAILURE) {
		ttl = 1440;
	} else {
		ttl = atoi (tmp);
	}
	sval.type = IS_STRING;
	sval.value.str.val = (char *) val;
	sval.value.str.len = vallen;

	do_session_lock (skey TSRMLS_CC);
	if (eaccelerator_put
		(skey, len, &sval, ttl, eaccelerator_sessions_cache_place TSRMLS_CC)) {
		free_alloca (skey);
		return SUCCESS;
	} else {
		free_alloca (skey);
		return FAILURE;
	}
}

PS_DESTROY_FUNC (eaccelerator)
{
	char *skey;
	int len;

	len = sizeof ("sess_") + strlen (key);
	skey = do_alloca (len + 1);
	strcpy (skey, "sess_");
	strcat (skey, key);
	if (eaccelerator_rm
		(skey, len, eaccelerator_sessions_cache_place TSRMLS_CC)) {
		free_alloca (skey);
		return SUCCESS;
	} else {
		free_alloca (skey);
		return FAILURE;
	}
}

PS_GC_FUNC (eaccelerator)
{
	if (eaccelerator_mm_instance == NULL) {
		return FAILURE;
	}
	eaccelerator_gc (TSRMLS_C);
	return SUCCESS;
}

#ifdef PS_CREATE_SID_ARGS
PS_CREATE_SID_FUNC (eaccelerator)
{
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

	if (cfg_get_string ("session.entropy_length", &entropy_file) == FAILURE) {
		entropy_length = 0;
	} else {
		entropy_length = atoi (entropy_file);
	}
	if (cfg_get_string ("session.entropy_file", &entropy_file) == FAILURE) {
		entropy_file = empty_string;
	}

	gettimeofday (&tv, NULL);
	PHP_MD5Init (&context);

	sprintf (buf, "%ld%ld%0.8f", tv.tv_sec, tv.tv_usec,
			 php_combined_lcg (TSRMLS_C) * 10);
	PHP_MD5Update (&context, (unsigned char *) buf, strlen (buf));

	if (entropy_length > 0) {
		int fd;

		fd = VCWD_OPEN (entropy_file, O_RDONLY);
		if (fd >= 0) {
			unsigned char buf[2048];
			int n;
			size_t to_read = entropy_length;

			while (to_read > 0) {
				n = read (fd, buf, MIN (to_read, sizeof (buf)));
				if (n <= 0)
					break;
				PHP_MD5Update (&context, buf, n);
				to_read -= n;
			}
			close (fd);
		}
	}

	PHP_MD5Final (digest, &context);

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
#endif

#else
/******************************************************************************/
/* PHP function to register as user session handlers when the session api 	  */
/* available.																  */
/******************************************************************************/

PHP_FUNCTION (_eaccelerator_session_open)
{
	if (eaccelerator_mm_instance == NULL) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}

PHP_FUNCTION (_eaccelerator_session_close)
{
	if (eaccelerator_mm_instance == NULL) {
		RETURN_FALSE;
	}
	do_session_unlock (TSRMLS_C);
	RETURN_TRUE;
}

PHP_FUNCTION (_eaccelerator_session_read)
{
	zval **arg_key;
	char *key;
	int len;

	if (ZEND_NUM_ARGS () != 1
		|| zend_get_parameters_ex (1, &arg_key) == FAILURE) {
		WRONG_PARAM_COUNT;
	}
	len = sizeof ("sess_") + (*arg_key)->value.str.len;
	key = do_alloca (len + 1);
	strcpy (key, "sess_");
	strcat (key, (*arg_key)->value.str.val);
	do_session_lock (key TSRMLS_CC);
	if (eaccelerator_get
		(key, len, return_value, eaccelerator_sessions_cache_place TSRMLS_CC)) {
		free_alloca (key);
		return;
	} else {
		free_alloca (key);
		RETURN_EMPTY_STRING ();
	}
}

PHP_FUNCTION (_eaccelerator_session_write)
{
	zval **arg_key, **arg_val;
	char *key;
	int len;
	time_t ttl;

	if (ZEND_NUM_ARGS () != 2
		|| zend_get_parameters_ex (2, &arg_key, &arg_val) == FAILURE) {
		WRONG_PARAM_COUNT;
	}
	len = sizeof ("sess_") + (*arg_key)->value.str.len;
	key = do_alloca (len + 1);
	strcpy (key, "sess_");
	strcat (key, (*arg_key)->value.str.val);
	ttl = PS (gc_maxlifetime);
	if (ttl < 0)
		ttl = 1440;
	do_session_lock (key TSRMLS_CC);
	if (eaccelerator_put
		(key, len, *arg_val, ttl,
		 eaccelerator_sessions_cache_place TSRMLS_CC)) {
		free_alloca (key);
		RETURN_TRUE;
	} else {
		free_alloca (key);
		RETURN_FALSE;
	}
}

PHP_FUNCTION (_eaccelerator_session_destroy)
{
	zval **arg_key;
	char *key;
	int len;

	if (ZEND_NUM_ARGS () != 1
		|| zend_get_parameters_ex (1, &arg_key) == FAILURE) {
		WRONG_PARAM_COUNT;
	}
	len = sizeof ("sess_") + (*arg_key)->value.str.len;
	key = do_alloca (len + 1);
	strcpy (key, "sess_");
	strcat (key, (*arg_key)->value.str.val);
	if (eaccelerator_rm (key, len, eaccelerator_sessions_cache_place TSRMLS_CC)) {
		free_alloca (key);
		RETURN_TRUE;
	} else {
		free_alloca (key);
		RETURN_FALSE;
	}
}

PHP_FUNCTION (_eaccelerator_session_gc)
{
	if (eaccelerator_mm_instance == NULL) {
		RETURN_FALSE;
	}
	eaccelerator_gc (TSRMLS_C);
	RETURN_TRUE;
}
#endif /* ELSE HAVE_PHP_SESSIONS_SUPPORT */

ps_module ps_mod_eaccelerator = {
#ifdef PS_CREATE_SID_ARGS
	PS_MOD_SID (eaccelerator)
#else
	PS_MOD (eaccelerator)
#endif
};

/* is the eA registered as session handler */
int eaccelerator_session_registered ()
{
	return !(eaccelerator_sessions_cache_place != eaccelerator_none &&
			 eaccelerator_sessions_registered == 0);
}

/* register ea as session handler */
void eaccelerator_register_session ()
{
	php_session_register_module (&ps_mod_eaccelerator);
	eaccelerator_sessions_registered = 1;
}

/* register ea as the custom session handler */
int eaccelerator_set_session_handlers (TSRMLS_D)
{
	zval func;
	zval retval;
	int ret = 1;
#ifdef HAVE_PHP_SESSIONS_SUPPORT	// do it with the session api
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
	ZVAL_STRING (&func, "session_module_name", 0);
	INIT_ZVAL (param);
	params[0] = &param;
	ZVAL_STRING (params[0], "eaccelerator", 0);
	if (call_user_function (EG (function_table), NULL, &func, &retval,
							1, params TSRMLS_CC) == FAILURE) {
		ret = 0;
	}
	zval_dtor (&retval);
	return ret;
#else // register the functions as custom user functions
	zval *params[6];
	int i;

	if (eaccelerator_sessions_cache_place == eaccelerator_none) {
		return 0;
	}
	if (eaccelerator_mm_instance == NULL) {
		return 0;
	}
	if (!zend_hash_exists
		(EG (function_table), "session_set_save_handler",
		 sizeof ("session_set_save_handler"))) {
		return 0;
	}

	ZVAL_STRING (&func, "session_set_save_handler", 0);
	MAKE_STD_ZVAL (params[0]);
	ZVAL_STRING (params[0], "_eaccelerator_session_open", 1);
	MAKE_STD_ZVAL (params[1]);
	ZVAL_STRING (params[1], "_eaccelerator_session_close", 1);
	MAKE_STD_ZVAL (params[2]);
	ZVAL_STRING (params[2], "_eaccelerator_session_read", 1);
	MAKE_STD_ZVAL (params[3]);
	ZVAL_STRING (params[3], "_eaccelerator_session_write", 1);
	MAKE_STD_ZVAL (params[4]);
	ZVAL_STRING (params[4], "_eaccelerator_session_destroy", 1);
	MAKE_STD_ZVAL (params[5]);
	ZVAL_STRING (params[5], "_eaccelerator_session_gc", 1);
	if (call_user_function (EG (function_table), NULL, &func, &retval,
							6, params TSRMLS_CC) == FAILURE) {
		ret = 0;
	}
	zval_dtor (&retval);
	for (i = 0; i < 6; i++)
		zval_ptr_dtor (&params[i]);
	return ret;
#endif
}

/* function to call from a php script to register ea as session handler */
PHP_FUNCTION (eaccelerator_set_session_handlers)
{
	if (eaccelerator_set_session_handlers (TSRMLS_C)) {
		RETURN_TRUE;
	} else {
		RETURN_FALSE;
	}
}

#endif /* HAVE_EACCELERATOR_SESSIONS */
