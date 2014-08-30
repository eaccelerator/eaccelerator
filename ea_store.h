/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 - 2012 eAccelerator                               |
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
   | A copy is available at http://www.gnu.org/copyleft/gpl.txt           |
   +----------------------------------------------------------------------+
   $Id: ea_store.h 375 2010-01-19 15:49:13Z bart $
*/

#ifndef EA_STORE_H
#define EA_STORE_H

size_t calc_zval(zval *z TSRMLS_DC);
size_t calc_size(char *key, zend_op_array *op_array, Bucket *f, Bucket *c TSRMLS_DC);

void store_zval(char **p, zval *z TSRMLS_DC);
void eaccelerator_store_int(ea_cache_entry *entry, char *key, int len, zend_op_array *op_array, Bucket *f, Bucket *c TSRMLS_DC);

#endif /* EA_STORE_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: et sw=4 ts=4 fdm=marker
 * vim<600: et sw=4 ts=4
 */
