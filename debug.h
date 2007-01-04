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

#ifndef INCLUDED_DEBUG_H
#define INCLUDED_DEBUG_H

#include "zend.h"
#include "zend_API.h"
#include "zend_extensions.h"
#ifdef ZEND_WIN32
#include "win32/time.h"
#endif

/*
 * This macro is used to make sure debug code is not included in a non-debug build,
 * without swamping the code with ifdef statements. This approach (as opposed to the
 * previous empty-function-if-no-debug-build) also makes sure debug function arguments
 * such as the tons of getpid()'s don't get compiled in and executed in a non-debug build.
 *
 * It takes the debug function as first arg and the arguments as the second, like this:
 *
 * DBG(ea_debug_printf, ("Hello %s", world));
 *
 * The reason why the function arguments are passed by one macro variable is to prevent
 * the use of variadic macros, keeping the win32 VC 6.0 folks happy
 */
#ifdef DEBUG
#define DBG(func, list) func list
#else
#define DBG(func, list)
#endif

/* print information about the file that's loaded or cached */
#define EA_LOG	 		(1<<0L)

/* print debugging information, mostly about the storing and restoring of a 
 * script's data structures. Gives you detailed information about what eA is
 * doing 
 */
#define EA_DEBUG		(1<<1L)

/* profile php opcodes */
#define EA_PROFILE_OPCODES	(1<<2L)

/* print out performance data (start - end time) */

#define EA_TEST_PERFORMANCE	(1<<3L)

/* log the hashkeys used to cache scripts */
#define EA_LOG_HASHKEYS		(1<<4L)

void ea_debug_init (TSRMLS_D);
void ea_debug_shutdown ();
void ea_debug_printf (long debug_level, char *format, ...);
void ea_debug_error (char *format, ...);
void ea_debug_pad (long debug_level TSRMLS_DC);
void ea_debug_log (char *format, ...);
void ea_debug_binary_print (long debug_level, char *p, int len);
void ea_debug_put (long debug_level, char *message);
void ea_debug_log_hashkeys (char *p, HashTable * ht);

void ea_debug_start_time (struct timeval *tvstart);
long ea_debug_elapsed_time (struct timeval *tvstart);

void ea_debug_hash_display(HashTable * ht);

#endif /* INCLUDED_DEBUG_H */
