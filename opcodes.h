/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 eAccelerator                                      |
   | http://eaccelerator.sourceforge.net                                  |
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

#define EXT_MASK       0x0ff00
#define EXT_UNUSED     0x0ff00
#define EXT_STD        0x00000
#define EXT_OPLINE     0x00100
#define EXT_FCALL      0x00200
#define EXT_ARG        0x00300
#define EXT_SEND       0x00400
#define EXT_CAST       0x00500
#define EXT_INIT_FCALL 0x00600
#define EXT_FETCH      0x00700
#define EXT_DECLARE    0x00800
#define EXT_SEND_NOREF 0x00900
#define EXT_FCLASS     0x00a00
#define EXT_IFACE      0x00b00
#define EXT_ISSET      0x00c00
#define EXT_BIT        0x00d00
#define EXT_CLASS      0x00e00
#define EXT_ASSIGN     0x00f00

#define OP1_MASK       0x000f0
#define OP1_UNUSED     0x000f0
#define OP1_STD        0x00000
#define OP1_OPLINE     0x00010
#define OP1_ARG        0x00020
#define OP1_BRK        0x00030
#define OP1_CONT       0x00040
#define OP1_JMPADDR    0x00050
#define OP1_CLASS      0x00060
#define OP1_VAR        0x00070
#define OP1_TMP        0x00080
#define OP1_UCLASS     0x00090

#define OP2_MASK       0x0000f
#define OP2_UNUSED     0x0000f
#define OP2_STD        0x00000
#define OP2_OPLINE     0x00001
#define OP2_FETCH      0x00002
#define OP2_INCLUDE    0x00003
#define OP2_ARG        0x00004
#define OP2_ISSET      0x00005
#define OP2_JMPADDR    0x00006
#define OP2_CLASS      0x00007
#define OP2_VAR        0x00008
#define OP2_TMP        0x00009

#define RES_MASK       0xf0000
#define RES_UNUSED     0xf0000
#define RES_STD        0x00000
#define RES_CLASS      0x10000
#define RES_TMP        0x20000
#define RES_VAR        0x30000

#define OPS_STD       EXT_STD | OP1_STD | OP2_STD | RES_STD

#ifdef ZEND_ENGINE_2
#  define VAR_NUM(var) ((unsigned int)(((temp_variable *)(var))-((temp_variable *)NULL)))
#  define VAR_VAL(var) ((unsigned int)((var)*sizeof(temp_variable)))
#else
#  define VAR_NUM(var) ((unsigned int)(var))
#  define VAR_VAL(var) ((unsigned int)(var))
#endif

typedef struct {
#ifdef WITH_EACCELERATOR_DISASSEMBLER
  const char*  opname;
#endif
  unsigned int ops;
} opcode_dsc;

const opcode_dsc* get_opcode_dsc(unsigned int n);

#ifdef ZEND_ENGINE_2
opcode_handler_t get_opcode_handler(zend_uchar opcode TSRMLS_DC);
#endif

#endif
