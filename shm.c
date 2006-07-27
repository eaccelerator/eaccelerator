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
   $Id: shm.c 178 2006-03-06 09:08:40Z bart $
*/

#include "eaccelerator.h"
#include "eaccelerator_version.h"

#ifdef HAVE_EACCELERATOR
#ifdef WITH_EACCELERATOR_SHM

#include "cache.h"
#include "shm.h"

#include "zend.h"
#include "zend_API.h"
#include "zend_extensions.h"
#include "standard/php_var.h"

/* where to cache the keys */
eaccelerator_cache_place eaccelerator_keys_cache_place =
	eaccelerator_shm_and_disk;

/* set the eaccelerator_keys_cache_place */
PHP_INI_MH (eaccelerator_OnUpdateKeysCachePlace)
{
	if (strncasecmp ("shm_and_disk", new_value, sizeof ("shm_and_disk")) == 0)
		eaccelerator_keys_cache_place = eaccelerator_shm_and_disk;

	else if (strncasecmp ("shm", new_value, sizeof ("shm")) == 0)
		eaccelerator_keys_cache_place = eaccelerator_shm;

	else if (strncasecmp ("shm_only", new_value, sizeof ("shm_only")) == 0)
		eaccelerator_keys_cache_place = eaccelerator_shm_only;

	else if (strncasecmp ("disk_only", new_value, sizeof ("disk_only")) == 0)
		eaccelerator_keys_cache_place = eaccelerator_disk_only;

	else if (strncasecmp ("none", new_value, sizeof ("none")) == 0)
		eaccelerator_keys_cache_place = eaccelerator_none;

	return SUCCESS;
}

/******************************************************************************/
/* PHP function entries														  */
/******************************************************************************/

PHP_FUNCTION(eaccelerator_lock)
{
	char *key;
	int key_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &key, &key_len) == FAILURE)
		return;

	if (eaccelerator_lock(key, key_len TSRMLS_CC)) {
		RETURN_TRUE;
	} else {
		RETURN_FALSE;
	}
}

PHP_FUNCTION (eaccelerator_unlock)
{
	char *key;
	int key_len;

	if (zend_parse_parameters (ZEND_NUM_ARGS ()TSRMLS_CC, "s", &key, &key_len) == FAILURE)
		return;

	if (eaccelerator_unlock (key, key_len TSRMLS_CC)) {
		RETURN_TRUE;
	} else {
		RETURN_FALSE;
	}
}

PHP_FUNCTION (eaccelerator_put)
{
	char *key;
	int key_len;
	zval *val, *result;
	time_t ttl = 0;
	long where = eaccelerator_keys_cache_place;

	if (zend_parse_parameters (ZEND_NUM_ARGS ()TSRMLS_CC, "sz|ll", &key, &key_len, &val, &ttl, &where) == FAILURE)
		return;

    if ((Z_TYPE_P(val) & ~IS_CONSTANT_INDEX) == IS_OBJECT) {
        php_serialize_data_t var_hash;
        smart_str buf = {0};
        result = (zval *)emalloc(sizeof(result));

        PHP_VAR_SERIALIZE_INIT(var_hash);
        php_var_serialize(&buf, &val, &var_hash TSRMLS_CC);
        PHP_VAR_SERIALIZE_DESTROY(var_hash);

        Z_TYPE_P(result) = IS_NULL;
        if (buf.c) {
            ZVAL_STRINGL(result, buf.c, buf.len, 1);
            Z_TYPE_P(result) = Z_TYPE_P(val);
        }
        INIT_PZVAL(result);
    } else {
        result = val;
    }

	if (eaccelerator_put (key, key_len, result, ttl, where TSRMLS_CC)) {
		RETURN_TRUE;
	} else {
		RETURN_FALSE;
	}
}

PHP_FUNCTION (eaccelerator_get)
{
	char *key;
	int key_len;
	long where = eaccelerator_keys_cache_place;
    zval *value;

	if (zend_parse_parameters (ZEND_NUM_ARGS ()TSRMLS_CC, "s|l", &key, &key_len, &where) == FAILURE)
		return;

    ALLOC_ZVAL(value);
    if (eaccelerator_get (key, key_len, value, where TSRMLS_CC)) {
        if ((Z_TYPE_P(value) & ~IS_CONSTANT_INDEX) == IS_OBJECT) {
            const unsigned char *p;
            php_unserialize_data_t var_hash;

            p = (const unsigned char*)Z_STRVAL_P(value);
            PHP_VAR_UNSERIALIZE_INIT(var_hash);
            if (!php_var_unserialize(&return_value, &p, p + Z_STRLEN_P(value),  &var_hash TSRMLS_CC)) {
                PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
                zval_dtor(return_value);
                php_error_docref(NULL TSRMLS_CC, E_NOTICE, "Error at offset %ld of %d bytes", 
                        (long)((char*)p - Z_STRVAL_P(value)), Z_STRLEN_P(value));
            }
            PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
        }
        return;
	} else {
		RETURN_NULL ();
	}
}

PHP_FUNCTION (eaccelerator_rm)
{
	char *key;
	int key_len;
	long where = eaccelerator_keys_cache_place;

	if (zend_parse_parameters (ZEND_NUM_ARGS ()TSRMLS_CC, "s|l", &key, &key_len, &where) == FAILURE)
		return;

	if (eaccelerator_rm (key, key_len, where TSRMLS_CC)) {
		RETURN_TRUE;
	} else {
		RETURN_FALSE;
	}
}

PHP_FUNCTION (eaccelerator_gc)
{
	if (ZEND_NUM_ARGS () != 0)
		WRONG_PARAM_COUNT;

	eaccelerator_gc (TSRMLS_C);
	RETURN_TRUE;
}



#endif							/* WITH_EACCELERATOR_SHM */
#endif							/* HAVE_EACCELERATOR */
