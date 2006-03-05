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
   $Id$
*/

#ifndef EA_STORE_H
#define EA_STORE_H

int calc_size (char *key, zend_op_array * op_array, Bucket * f, Bucket * c TSRMLS_DC);
void calc_op_array (zend_op_array * from TSRMLS_DC);
void calc_class_entry (zend_class_entry * from TSRMLS_DC);

eaccelerator_op_array *store_op_array (zend_op_array * from TSRMLS_DC);
eaccelerator_class_entry *store_class_entry_ptr (zend_class_entry **from TSRMLS_DC);
eaccelerator_class_entry *store_class_entry (zend_class_entry *from TSRMLS_DC);

#endif /* EA_STORE_H */
