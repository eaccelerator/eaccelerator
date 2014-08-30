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
   | A copy is available at http://www.gnu.org/copyleft/gpl.txt           |
   +----------------------------------------------------------------------+
   $Id: ea_info.h 375 2010-01-19 15:49:13Z bart $
*/

#ifndef INCLUDED_INFO_H
#define INCLUDED_INFO_H

#ifdef WITH_EACCELERATOR_INFO

PHP_FUNCTION(eaccelerator_caching);
PHP_FUNCTION(eaccelerator_check_mtime);
PHP_FUNCTION(eaccelerator_clear);
PHP_FUNCTION(eaccelerator_clean);
PHP_FUNCTION(eaccelerator_info);
PHP_FUNCTION(eaccelerator_purge);
PHP_FUNCTION(eaccelerator_cached_scripts);
PHP_FUNCTION(eaccelerator_removed_scripts);

#endif
#endif /* INCLUDED_INFO_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: et sw=4 ts=4 fdm=marker
 * vim<600: et sw=4 ts=4
 */
