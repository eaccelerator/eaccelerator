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
   | A copy is available at http://www.gnu.org/copyleft/gpl.txt            |
   +----------------------------------------------------------------------+
   $Id: opcodes.h 377 2010-01-20 14:58:03Z hans $
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
#define EXT_FE         0x01000
#define EXT_FETCHTYPE  0x01100

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
#define RES_OPLINE     0x40000

#define OPS_STD       EXT_STD | OP1_STD | OP2_STD | RES_STD

#define VAR_NUM(var) ((unsigned int)(((temp_variable *)((intptr_t) var))-((temp_variable *)NULL)))
#define VAR_VAL(var) ((unsigned int)((var)*sizeof(temp_variable)))

#ifdef ZEND_ENGINE_2_4
#define OP1_TYPE(op) (op)->op1_type
#define OP2_TYPE(op) (op)->op2_type
#define RES_TYPE(op) (op)->result_type

/* OP1_VAR/OP2_VAR/RES_VAR already defined above, hence the VARR */
#define OP1_VARR(op) (op)->op1.var
#define OP2_VARR(op) (op)->op2.var
#define RES_VARR(op) (op)->result.var

#define OP1_OPLINE_NUM(op) (op)->op1.opline_num
#define OP2_OPLINE_NUM(op) (op)->op2.opline_num
#define RES_OPLINE_NUM(op) (op)->result.opline_num

#define OP1_JMP_ADDR(op) (op)->op1.jmp_addr
#define OP2_JMP_ADDR(op) (op)->op2.jmp_addr
#define RES_JMP_ADDR(op) (op)->result.jmp_addr


#define OP1_CONST(op) op_array->literals[(op)->op1.constant].constant
#define OP2_CONST(op) op_array->literals[(op)->op2.constant].constant
#define RES_CONST(op) op_array->literals[(op)->result.constant].constant

#define OP1_CONST_TYPE(op) OP1_CONST((op)).type
#define OP2_CONST_TYPE(op) OP2_CONST((op)).type
#define RES_CONST_TYPE(op) RES_CONST((op)).type

#define RES_USED(op) (op)->result_type

#else

#define OP1_TYPE(op) (op)->op1.op_type
#define OP2_TYPE(op) (op)->op2.op_type
#define RES_TYPE(op) (op)->result.op_type

#define OP1_VARR(op) (op)->op1.u.var
#define OP2_VARR(op) (op)->op2.u.var
#define RES_VARR(op) (op)->result.u.var

#define OP1_OPLINE_NUM(op) (op)->op1.u.opline_num
#define OP2_OPLINE_NUM(op) (op)->op2.u.opline_num
#define RES_OPLINE_NUM(op) (op)->result.u.opline_num

#define OP1_JMP_ADDR(op) (op)->op1.u.jmp_addr
#define OP2_JMP_ADDR(op) (op)->op2.u.jmp_addr
#define RES_JMP_ADDR(op) (op)->result.u.jmp_addr

#define OP1_CONST(op) (op)->op1.u.constant
#define OP2_CONST(op) (op)->op2.u.constant
#define RES_CONST(op) (op)->result.u.constant

#define OP1_CONST_TYPE(op) OP1_CONST((op)).type
#define OP2_CONST_TYPE(op) OP2_CONST((op)).type
#define RES_CONST_TYPE(op) RES_CONST((op)).type

#define RES_USED(op) (op)->result.u.EA.type
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

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: et sw=4 ts=4 fdm=marker
 * vim<600: et sw=4 ts=4
 */
