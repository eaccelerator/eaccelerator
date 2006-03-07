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
   $Id: session.h 176 2006-03-05 12:18:54Z bart $
*/

#ifndef INCLUDED_SESSION_H
#define INCLUDED_SESSION_H

#include "php_ini.h"

/* check if php is compiled with session support */
#undef HAVE_PHP_SESSIONS_SUPPORT
#if defined(HAVE_EXT_SESSION_PHP_SESSION_H)
#	include "ext/session/php_session.h"
#	if defined(PHP_SESSION_API) && PHP_SESSION_API >= 20020306
#   	define HAVE_PHP_SESSIONS_SUPPORT
#	endif
#else // no session support in php, undef eA session support
#	undef WITH_EACCELERATOR_SESSIONS
#endif

#ifdef WITH_EACCELERATOR_SESSIONS

int eaccelerator_set_session_handlers();
int eaccelerator_session_registered();
void eaccelerator_register_session();

#ifdef HAVE_PHP_SESSIONS_SUPPORT
PHP_FUNCTION(_eaccelerator_session_open);
PHP_FUNCTION(_eaccelerator_session_close);
PHP_FUNCTION(_eaccelerator_session_read);
PHP_FUNCTION(_eaccelerator_session_write);
PHP_FUNCTION(_eaccelerator_session_destroy);
PHP_FUNCTION(_eaccelerator_session_gc);
#endif
PHP_FUNCTION(eaccelerator_set_session_handlers);
PHP_INI_MH(eaccelerator_OnUpdateSessionCachePlace);

#endif

#endif /* INCLUDED_SESSION_H */
