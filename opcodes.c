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
   | Author(s): Dmitry Stogov <dstogov@users.sourceforge.net>             |
   +----------------------------------------------------------------------+
   $Id$
*/

#include "eaccelerator.h"
#include "opcodes.h"

#ifdef HAVE_EACCELERATOR

#ifdef WITH_EACCELERATOR_DISASSEMBLER
#  define OPDEF(NAME, OPS) {NAME, OPS}
#else
#  define OPDEF(NAME, OPS) {OPS}
#endif

#ifdef ZEND_ENGINE_2
#  define OP1_VAR_2 OP1_STD
#else
#  define OP1_VAR_2 OP1_VAR
#endif

#ifdef ZEND_ENGINE_2
#  define LAST_OPCODE 149
#else
#  define LAST_OPCODE 106
#endif

static const opcode_dsc opcodes[] = {
  OPDEF("NOP",                       EXT_UNUSED | OP1_UNUSED | OP2_UNUSED | RES_UNUSED), /* 0 */
  OPDEF("ADD",                       EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 1 */
  OPDEF("SUB",                       EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 2 */
  OPDEF("MUL",                       EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 3 */
  OPDEF("DIV",                       EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 4 */
  OPDEF("MOD",                       EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 5 */
  OPDEF("SL",                        EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 6 */
  OPDEF("SR",                        EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 7 */
  OPDEF("CONCAT",                    EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 8 */
  OPDEF("BW_OR",                     EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 9 */
  OPDEF("BW_AND",                    EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 10 */
  OPDEF("BW_XOR",                    EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 11 */
  OPDEF("BW_NOT",                    EXT_UNUSED | OP1_STD    | OP2_UNUSED | RES_TMP), /* 12 */
  OPDEF("BOOL_NOT",                  EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 13 */
  OPDEF("BOOL_XOR",                  EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 14 */
  OPDEF("IS_IDENTICAL",              EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 15 */
  OPDEF("IS_NOT_IDENTICAL",          EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 16 */
  OPDEF("IS_EQUAL",                  EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 17 */
  OPDEF("IS_NOT_EQUAL",              EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 18 */
  OPDEF("IS_SMALLER",                EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 19 */
  OPDEF("IS_SMALLER_OR_EQUAL",       EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 20 */
  OPDEF("CAST",                      EXT_CAST   | OP1_STD    | OP2_UNUSED | RES_TMP), /* 21 */
  OPDEF("QM_ASSIGN",                 EXT_UNUSED | OP1_STD    | OP2_UNUSED | RES_TMP), /* 22 */
#ifdef ZEND_ENGINE_2
  OPDEF("ASSIGN_ADD",                EXT_ASSIGN | OP1_STD    | OP2_STD    | RES_VAR), /* 23 */
  OPDEF("ASSIGN_SUB",                EXT_ASSIGN | OP1_STD    | OP2_STD    | RES_VAR), /* 24 */
  OPDEF("ASSIGN_MUL",                EXT_ASSIGN | OP1_STD    | OP2_STD    | RES_VAR), /* 25 */
  OPDEF("ASSIGN_DIV",                EXT_ASSIGN | OP1_STD    | OP2_STD    | RES_VAR), /* 26 */
  OPDEF("ASSIGN_MOD",                EXT_ASSIGN | OP1_STD    | OP2_STD    | RES_VAR), /* 27 */
  OPDEF("ASSIGN_SL",                 EXT_ASSIGN | OP1_STD    | OP2_STD    | RES_VAR), /* 28 */
  OPDEF("ASSIGN_SR",                 EXT_ASSIGN | OP1_STD    | OP2_STD    | RES_VAR), /* 29 */
  OPDEF("ASSIGN_CONCAT",             EXT_ASSIGN | OP1_STD    | OP2_STD    | RES_VAR), /* 30 */
  OPDEF("ASSIGN_BW_OR",              EXT_ASSIGN | OP1_STD    | OP2_STD    | RES_VAR), /* 31 */
  OPDEF("ASSIGN_BW_AND",             EXT_ASSIGN | OP1_STD    | OP2_STD    | RES_VAR), /* 32 */
  OPDEF("ASSIGN_BW_XOR",             EXT_ASSIGN | OP1_STD    | OP2_STD    | RES_VAR), /* 33 */
#else
  OPDEF("ASSIGN_ADD",                EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 23 */
  OPDEF("ASSIGN_SUB",                EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 24 */
  OPDEF("ASSIGN_MUL",                EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 25 */
  OPDEF("ASSIGN_DIV",                EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 26 */
  OPDEF("ASSIGN_MOD",                EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 27 */
  OPDEF("ASSIGN_SL",                 EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 28 */
  OPDEF("ASSIGN_SR",                 EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 29 */
  OPDEF("ASSIGN_CONCAT",             EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 30 */
  OPDEF("ASSIGN_BW_OR",              EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 31 */
  OPDEF("ASSIGN_BW_AND",             EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 32 */
  OPDEF("ASSIGN_BW_XOR",             EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 33 */
#endif
  OPDEF("PRE_INC",                   EXT_UNUSED | OP1_VAR    | OP2_UNUSED | RES_VAR), /* 34 */
  OPDEF("PRE_DEC",                   EXT_UNUSED | OP1_VAR    | OP2_UNUSED | RES_VAR), /* 35 */
  OPDEF("POST_INC",                  EXT_UNUSED | OP1_VAR    | OP2_UNUSED | RES_TMP), /* 36 */
  OPDEF("POST_DEC",                  EXT_UNUSED | OP1_VAR    | OP2_UNUSED | RES_TMP), /* 37 */
  OPDEF("ASSIGN",                    EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 38 */
  OPDEF("ASSIGN_REF",                EXT_UNUSED | OP1_VAR    | OP2_VAR    | RES_VAR), /* 39 */
  OPDEF("ECHO",                      EXT_UNUSED | OP1_STD    | OP2_UNUSED | RES_UNUSED), /* 40 */
  OPDEF("PRINT",                     EXT_UNUSED | OP1_STD    | OP2_UNUSED | RES_TMP), /* 41 */
#ifdef ZEND_ENGINE_2
  OPDEF("JMP",                       EXT_UNUSED | OP1_JMPADDR| OP2_UNUSED | RES_UNUSED), /* 42 */
  OPDEF("JMPZ",                      EXT_UNUSED | OP1_STD    | OP2_JMPADDR| RES_UNUSED), /* 43 */
  OPDEF("JMPNZ",                     EXT_UNUSED | OP1_STD    | OP2_JMPADDR| RES_UNUSED), /* 44 */
#else
  OPDEF("JMP",                       EXT_UNUSED | OP1_OPLINE | OP2_UNUSED | RES_UNUSED), /* 42 */
  OPDEF("JMPZ",                      EXT_UNUSED | OP1_STD    | OP2_OPLINE | RES_UNUSED), /* 43 */
  OPDEF("JMPNZ",                     EXT_UNUSED | OP1_STD    | OP2_OPLINE | RES_UNUSED), /* 44 */
#endif
  OPDEF("JMPZNZ",                    EXT_OPLINE | OP1_STD    | OP2_OPLINE | RES_UNUSED), /* 45 */
#ifdef ZEND_ENGINE_2
  OPDEF("JMPZ_EX",                   EXT_UNUSED | OP1_STD    | OP2_JMPADDR| RES_TMP), /* 46 */
  OPDEF("JMPNZ_EX",                  EXT_UNUSED | OP1_STD    | OP2_JMPADDR| RES_TMP), /* 47 */
#else
  OPDEF("JMPZ_EX",                   EXT_UNUSED | OP1_STD    | OP2_OPLINE | RES_TMP), /* 46 */
  OPDEF("JMPNZ_EX",                  EXT_UNUSED | OP1_STD    | OP2_OPLINE | RES_TMP), /* 47 */
#endif
  OPDEF("CASE",                      EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 48 */
  OPDEF("SWITCH_FREE",               EXT_BIT    | OP1_STD    | OP2_UNUSED | RES_UNUSED), /* 49 */
  OPDEF("BRK",                       EXT_UNUSED | OP1_BRK    | OP2_STD    | RES_UNUSED), /* 50 */
  OPDEF("CONT",                      EXT_UNUSED | OP1_CONT   | OP2_STD    | RES_UNUSED), /* 51 */
  OPDEF("BOOL",                      EXT_UNUSED | OPS_STD    | OP2_UNUSED | RES_TMP), /* 52 */
  OPDEF("INIT_STRING",               EXT_UNUSED | OP1_UNUSED | OP2_UNUSED | RES_TMP), /* 53 */
  OPDEF("ADD_CHAR",                  EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 54 */
  OPDEF("ADD_STRING",                EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 55 */
  OPDEF("ADD_VAR",                   EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 56 */
  OPDEF("BEGIN_SILENCE",             EXT_UNUSED | OP1_UNUSED | OP2_UNUSED | RES_TMP), /* 57 */
  OPDEF("END_SILENCE",               EXT_UNUSED | OP1_TMP    | OP2_UNUSED | RES_UNUSED), /* 58 */
  OPDEF("INIT_FCALL_BY_NAME",        EXT_INIT_FCALL | OP1_STD | OP2_STD   | RES_UNUSED), /* 59 */
#ifdef ZEND_ENGINE_2
  OPDEF("DO_FCALL",                  EXT_FCALL  | OP1_STD    | OP2_OPLINE | RES_VAR), /* 60 */
  OPDEF("DO_FCALL_BY_NAME",          EXT_FCALL  | OP1_STD    | OP2_OPLINE | RES_VAR), /* 61 */
#else
  OPDEF("DO_FCALL",                  EXT_FCALL  | OP1_STD    | OP2_UNUSED | RES_VAR), /* 60 */
  OPDEF("DO_FCALL_BY_NAME",          EXT_FCALL  | OP1_STD    | OP2_UNUSED | RES_VAR), /* 61 */
#endif
  OPDEF("RETURN",                    EXT_UNUSED | OP1_STD    | OP2_UNUSED | RES_UNUSED), /* 62 */
  OPDEF("RECV",                      EXT_UNUSED | OP1_ARG    | OP2_UNUSED | RES_VAR), /* 63 */
  OPDEF("RECV_INIT",                 EXT_UNUSED | OP1_ARG    | OP2_STD    | RES_VAR), /* 64 */
  OPDEF("SEND_VAL",                  EXT_SEND   | OP1_STD    | OP2_ARG    | RES_UNUSED), /* 65 */
  OPDEF("SEND_VAR",                  EXT_SEND   | OP1_VAR    | OP2_ARG    | RES_UNUSED), /* 66 */
  OPDEF("SEND_REF",                  EXT_SEND   | OP1_VAR    | OP2_ARG    | RES_UNUSED), /* 67 */
#ifdef ZEND_ENGINE_2
  OPDEF("NEW",                       EXT_UNUSED | OP1_CLASS  | OP2_UNUSED | RES_VAR), /* 68 */
#else
  OPDEF("NEW",                       EXT_UNUSED | OP1_STD    | OP2_UNUSED | RES_VAR), /* 68 */
#endif
  OPDEF("JMP_NO_CTOR",               EXT_UNUSED | OP1_STD    | OP2_OPLINE | RES_UNUSED), /* 69 */
  OPDEF("FREE",                      EXT_UNUSED | OP1_TMP    | OP2_UNUSED | RES_UNUSED), /* 70 */
  OPDEF("INIT_ARRAY",                EXT_BIT    | OP1_STD    | OP2_STD    | RES_TMP), /* 71 */
  OPDEF("ADD_ARRAY_ELEMENT",         EXT_BIT    | OP1_STD    | OP2_STD    | RES_TMP), /* 72 */
  OPDEF("INCLUDE_OR_EVAL",           EXT_UNUSED | OP1_STD    | OP2_INCLUDE| RES_VAR), /* 73 */
#ifdef ZEND_ENGINE_2
  OPDEF("UNSET_VAR",                 EXT_UNUSED | OP1_STD    | OP2_FETCH  | RES_UNUSED), /* 74 */
  OPDEF("UNSET_DIM_OBJ",             EXT_STD    | OP1_VAR    | OP2_STD    | RES_UNUSED), /* 75 */
  OPDEF("ISSET_ISEMPTY",             EXT_ISSET  | OP1_VAR    | OP2_UNUSED | RES_TMP), /* 76 */
#else
  OPDEF("UNSET_VAR",                 EXT_UNUSED | OP1_STD    | OP2_UNUSED | RES_UNUSED), /* 74 */
  OPDEF("UNSET_DIM_OBJ",             EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_UNUSED), /* 75 */
  OPDEF("ISSET_ISEMPTY",             EXT_UNUSED | OP1_VAR    | OP2_ISSET  | RES_TMP), /* 76 */
#endif
  OPDEF("FE_RESET",                  EXT_BIT    | OP1_STD    | OP2_UNUSED | RES_VAR), /* 77 */
  OPDEF("FE_FETCH",                  EXT_UNUSED | OP1_STD    | OP2_OPLINE | RES_TMP), /* 78 */
  OPDEF("EXIT",                      EXT_UNUSED | OP1_STD    | OP2_UNUSED | RES_UNUSED), /* 79 */
  OPDEF("FETCH_R",                   EXT_UNUSED | OP1_STD    | OP2_FETCH  | RES_VAR), /* 80 */
  OPDEF("FETCH_DIM_R",               EXT_FETCH  | OP1_VAR    | OP2_STD    | RES_VAR), /* 81 */
  OPDEF("FETCH_OBJ_R",               EXT_UNUSED | OP1_VAR_2  | OP2_STD    | RES_VAR), /* 82 */
  OPDEF("FETCH_W",                   EXT_UNUSED | OP1_STD    | OP2_FETCH  | RES_VAR), /* 83 */
  OPDEF("FETCH_DIM_W",               EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 84 */
  OPDEF("FETCH_OBJ_W",               EXT_UNUSED | OP1_VAR_2  | OP2_STD    | RES_VAR), /* 85 */
  OPDEF("FETCH_RW",                  EXT_UNUSED | OP1_STD    | OP2_FETCH  | RES_VAR), /* 86 */
  OPDEF("FETCH_DIM_RW",              EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 87 */
  OPDEF("FETCH_OBJ_RW",              EXT_UNUSED | OP1_VAR_2  | OP2_STD    | RES_VAR), /* 88 */
  OPDEF("FETCH_IS",                  EXT_UNUSED | OP1_STD    | OP2_FETCH  | RES_VAR), /* 89 */
  OPDEF("FETCH_DIM_IS",              EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 90 */
  OPDEF("FETCH_OBJ_IS",              EXT_UNUSED | OP1_VAR_2  | OP2_STD    | RES_VAR), /* 91 */
  OPDEF("FETCH_FUNC_ARG",            EXT_ARG    | OP1_STD    | OP2_FETCH  | RES_VAR), /* 92 */
  OPDEF("FETCH_DIM_FUNC_ARG",        EXT_ARG    | OP1_VAR    | OP2_STD    | RES_VAR), /* 93 */
  OPDEF("FETCH_OBJ_FUNC_ARG",        EXT_ARG    | OP1_VAR_2  | OP2_STD    | RES_VAR), /* 94 */
  OPDEF("FETCH_UNSET",               EXT_UNUSED | OP1_STD    | OP2_FETCH  | RES_VAR), /* 95 */
  OPDEF("FETCH_DIM_UNSET",           EXT_UNUSED | OP1_VAR    | OP2_STD    | RES_VAR), /* 96 */
  OPDEF("FETCH_OBJ_UNSET",           EXT_UNUSED | OP1_VAR_2  | OP2_STD    | RES_VAR), /* 97 */
  OPDEF("FETCH_DIM_TMP_VAR",         EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 98 */

#ifdef ZEND_ENGINE_2
  OPDEF("FETCH_CONSTANT",            EXT_UNUSED | OP1_UCLASS | OP2_STD    | RES_TMP), /* 99 */
#else
  OPDEF("FETCH_CONSTANT",            EXT_UNUSED | OP1_STD    | OP2_UNUSED | RES_TMP), /* 99 */
#endif
  OPDEF("DECLARE_FUNCTION_OR_CLASS", EXT_DECLARE| OP1_STD    | OP2_STD    | RES_UNUSED), /* 100 */
  OPDEF("EXT_STMT",                  EXT_STD    | OP1_STD    | OP2_STD    | RES_STD), /* 101 */
  OPDEF("EXT_FCALL_BEGIN",           EXT_STD    | OP1_STD    | OP2_STD    | RES_STD), /* 102 */
  OPDEF("EXT_FCALL_END",             EXT_STD    | OP1_STD    | OP2_STD    | RES_STD), /* 103 */
  OPDEF("EXT_NOP",                   EXT_UNUSED | OP1_UNUSED | OP2_UNUSED | RES_UNUSED), /* 104 */
  OPDEF("TICKS",                     EXT_UNUSED | OP1_STD    | OP2_UNUSED | RES_UNUSED), /* 105 */
  OPDEF("SEND_VAR_NO_REF",           EXT_SEND_NOREF| OP1_VAR | OP2_ARG    | RES_UNUSED)  /* 106 */
#ifdef ZEND_ENGINE_2
  ,
  OPDEF("CATCH",                     EXT_OPLINE | OP1_CLASS  | OP2_STD    | RES_UNUSED), /* 107 */
  OPDEF("THROW",                     EXT_UNUSED | OP1_STD    | OP2_OPLINE | RES_UNUSED), /* 108 */
  OPDEF("FETCH_CLASS",               EXT_FCLASS | OP1_STD    | OP2_STD    | RES_CLASS), /* 109 */
  OPDEF("CLONE",                     EXT_UNUSED | OP1_STD    | OP2_UNUSED | RES_VAR), /* 110 */
  OPDEF("INIT_CTOR_CALL",            EXT_UNUSED | OP1_STD    | OP2_UNUSED | RES_UNUSED), /* 111 */
  OPDEF("INIT_METHOD_CALL",          EXT_UNUSED | OP1_STD    | OP2_STD    | RES_UNUSED), /* 112 */
  OPDEF("INIT_STATIC_METHOD_CALL",   EXT_UNUSED | OP1_UCLASS | OP2_STD    | RES_UNUSED), /* 113 */
  OPDEF("ISSET_ISEMPTY_VAR",         EXT_ISSET  | OP1_STD    | OP2_FETCH  | RES_TMP), /* 114 */
  OPDEF("ISSET_ISEMPTY_DIM_OBJ",     EXT_ISSET  | OP1_STD    | OP2_STD    | RES_TMP), /* 115 */
  OPDEF("IMPORT_FUNCTION",           EXT_UNUSED | OP1_CLASS  | OP2_STD    | RES_UNUSED), /* 116 */
  OPDEF("IMPORT_CLASS",              EXT_UNUSED | OP1_CLASS  | OP2_STD    | RES_UNUSED), /* 117 */
  OPDEF("IMPORT_CONST",              EXT_UNUSED | OP1_CLASS  | OP2_STD    | RES_UNUSED), /* 118 */
  OPDEF("OP_119",                    EXT_STD    | OP1_STD    | OP2_STD    | RES_STD), /* 119 */
  OPDEF("OP_120",                    EXT_STD    | OP1_STD    | OP2_STD    | RES_STD), /* 120 */
  OPDEF("ASSIGN_ADD_OBJ",            EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 121 */
  OPDEF("ASSIGN_SUB_OBJ",            EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 122 */
  OPDEF("ASSIGN_MUL_OBJ",            EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 123 */
  OPDEF("ASSIGN_DIV_OBJ",            EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 124 */
  OPDEF("ASSIGN_MOD_OBJ",            EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 125 */
  OPDEF("ASSIGN_SL_OBJ",             EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 126 */
  OPDEF("ASSIGN_SR_OBJ",             EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 127 */
  OPDEF("ASSIGN_CONCAT_OBJ",         EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 128 */
  OPDEF("ASSIGN_BW_OR_OBJ",          EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 129 */
  OPDEF("ASSIGN_BW_AND_OBJ",         EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 130 */
  OPDEF("ASSIGN_BW_XOR_OBJ",         EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 131 */
  OPDEF("PRE_INC_OBJ",               EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 132 */
  OPDEF("PRE_DEC_OBJ",               EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 133 */
  OPDEF("POST_INC_OBJ",              EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 134 */
  OPDEF("POST_DEC_OBJ",              EXT_UNUSED | OP1_STD    | OP2_STD    | RES_TMP), /* 135 */
  OPDEF("ASSIGN_OBJ",                EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR), /* 136 */
  OPDEF("OP_DATA",                   EXT_UNUSED | OP1_STD    | OP2_STD    | RES_UNUSED), /* 137 */
  OPDEF("INSTANCEOF",                EXT_UNUSED | OP1_STD    | OP2_CLASS  | RES_TMP), /* 138 */
  OPDEF("DECLARE_CLASS",             EXT_UNUSED | OP1_STD    | OP2_STD    | RES_CLASS), /* 139 */
  OPDEF("DECLARE_INHERITED_CLASS",   EXT_CLASS  | OP1_STD    | OP2_STD    | RES_CLASS), /* 140 */
  OPDEF("DECLARE_FUNCTION",          EXT_UNUSED | OP1_STD    | OP2_STD    | RES_UNUSED), /* 141 */
  OPDEF("RAISE_ABSTRACT_ERROR",      EXT_UNUSED | OP1_UNUSED | OP2_UNUSED | RES_UNUSED), /* 142 */
  OPDEF("START_NAMESPACE",           EXT_UNUSED | OP1_STD    | OP2_UNUSED | RES_UNUSED), /* 143 */
  OPDEF("ADD_INTERFACE",             EXT_IFACE  | OP1_CLASS  | OP2_CLASS  | RES_UNUSED), /* 144 */
  OPDEF("VERIFY_INSTANCEOF",         EXT_UNUSED | OP1_CLASS  | OP2_STD    | RES_UNUSED), /* 145 */
  OPDEF("VERIFY_ABSTRACT_CLASS",     EXT_UNUSED | OP1_CLASS  | OP2_UNUSED | RES_UNUSED), /* 146 */
  OPDEF("ASSIGN_DIM",                EXT_UNUSED | OP1_STD    | OP2_STD    | RES_VAR),  /* 147 */

  OPDEF("ISSET_ISEMPTY_PROP_OBJ",    EXT_ISSET  | OP1_STD    | OP2_STD    | RES_TMP), /* 148 */
  OPDEF("HANDLE_EXCEPTION",          EXT_STD    | OP1_UNUSED | OP2_UNUSED | RES_STD)  /* 149 */
#endif
};

const opcode_dsc* get_opcode_dsc(unsigned int n) {
  if (n <= LAST_OPCODE) {
    return &opcodes[n];
  } else {
    return NULL;
  }
}
#endif

#ifdef ZEND_ENGINE_2
static opcode_handler_t eaccelerator_opcode_handlers[LAST_OPCODE+1];
static int handlers_retrived = 0;

ZEND_DLEXPORT void retrive_opcode_handlers_handler(zend_op_array *op_array) {
  unsigned char i;
  efree(op_array->opcodes);
  op_array->opcodes = (zend_op*)emalloc(sizeof(zend_op)*(LAST_OPCODE+1));
  op_array->last = LAST_OPCODE+1;
  op_array->size = LAST_OPCODE+1;
  op_array->T    = 0;
  for (i=0; i<=LAST_OPCODE; i++) {
    op_array->opcodes[i].opcode = i;
    op_array->opcodes[i].op1.op_type = IS_UNUSED;
    op_array->opcodes[i].op1.u.opline_num = i;
    op_array->opcodes[i].op2.op_type = IS_UNUSED;
    op_array->opcodes[i].op2.u.opline_num = i;
    op_array->opcodes[i].result.op_type = IS_UNUSED;
  }
}

static int retrive_opcode_handlers(TSRMLS_D) {
  zend_extension* ext;

  if ((ext = zend_get_extension(EACCELERATOR_EXTENSION_NAME)) != NULL) {
    zend_op_array* p;
    zval str;
    void (*old)(zend_op_array *o_a);

    str.type = IS_STRING;
    str.is_ref = 1;
    str.refcount = 2;
    str.value.str.val = "return 1;";
    str.value.str.len = 9;
    old = ext->op_array_handler;
    ext->op_array_handler = retrive_opcode_handlers_handler;
    p = compile_string(&str, empty_string TSRMLS_CC);
    ext->op_array_handler = old;
    if (p != NULL && p->last == LAST_OPCODE+1) {
      int i = 0;
      while (i <= LAST_OPCODE) {
         eaccelerator_opcode_handlers[p->opcodes[i].opcode] = p->opcodes[i].handler;
         ++i;
      }
      return 1;
    }
  }
  return 0;
}

opcode_handler_t get_opcode_handler(zend_uchar opcode TSRMLS_DC) {
  if (!handlers_retrived) {
    if (retrive_opcode_handlers(TSRMLS_C)) {
      handlers_retrived = 1;
    } else {
      return NULL;
    }
  }
  if (opcode <= LAST_OPCODE) {
    return eaccelerator_opcode_handlers[opcode];
  } else {
    return (opcode_handler_t) NULL;
  }
}
#endif
