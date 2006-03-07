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
   $Id: shm.h 178 2006-03-06 09:08:40Z bart $
*/

#ifndef INCLUDED_SHM_H
#define INCLUDED_SHM_H

#ifdef WITH_EACCELERATOR_SHM

#include "php_ini.h"

PHP_FUNCTION(eaccelerator_put);
PHP_FUNCTION(eaccelerator_get);
PHP_FUNCTION(eaccelerator_rm);
PHP_FUNCTION(eaccelerator_gc);
PHP_FUNCTION(eaccelerator_lock);
PHP_FUNCTION(eaccelerator_unlock);
PHP_INI_MH(eaccelerator_OnUpdateKeysCachePlace);

#endif /* WITH_EACCELERATOR_SHM */
#endif /* INCLUDED_SHM_H */
