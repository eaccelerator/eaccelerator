/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 - 2007 eAccelerator                               |
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
   $Id$
*/

#include "eaccelerator.h"

#ifndef INCLUDED_CACHE_H
#define INCLUDED_CACHE_H

#include "zend.h"
#include "zend_API.h"
#include "zend_extensions.h"

#if defined(WITH_EACCELERATOR_CONTENT_CACHING) || defined(WITH_EACCELERATOR_SESSIONS) || defined(WITH_EACCELERATOR_SHM)
int eaccelerator_put (const char *key, int key_len, zval * val, time_t ttl, ea_cache_place where TSRMLS_DC);
int eaccelerator_get (const char *key, int key_len, zval * return_value, ea_cache_place where TSRMLS_DC);
int eaccelerator_rm (const char *key, int key_len, ea_cache_place where TSRMLS_DC);
#endif
size_t eaccelerator_gc (TSRMLS_D);


#ifdef WITH_EACCELERATOR_INFO
int eaccelerator_list_keys(zval *return_value TSRMLS_DC);
#endif

#endif							/* INCLUDED_CACHE_H */
