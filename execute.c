/*
   +----------------------------------------------------------------------+
   | Turck MMCache for PHP Version 4                                      |
   +----------------------------------------------------------------------+
   | Copyright (c) 2002-2003 TurckSoft, St. Petersburg                    |
   | http://www.turcksoft.com                                             |
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
   | Author: Dmitry Stogov <mmcache@turckware.ru>                         |
   +----------------------------------------------------------------------+
   $Id$
*/

#include "eaccelerator.h"

#ifdef HAVE_EACCELERATOR
#ifdef WITH_EACCELERATOR_EXECUTOR
#include "zend.h"

ZEND_DLEXPORT void eaccelerator_execute(zend_op_array *op_array TSRMLS_DC)
{
  zend_error(E_CORE_ERROR, "eaccelerator_execute() is not implemented");
}

#endif
#endif /* #ifdef HAVE_EACCELERATOR */
