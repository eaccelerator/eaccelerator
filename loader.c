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
#include "eaccelerator_version.h"

#ifdef HAVE_EACCELERATOR
#ifdef WITH_EACCELERATOR_LOADER

#include "opcodes.h"

#include "zend.h"
#include "zend_API.h"
#include "php.h"

#include <math.h>

#define MMC_ENCODER_VERSION   0x00000002
#define MMC_ENCODER_END       0x00
#define MMC_ENCODER_NAMESPACE 0x01
#define MMC_ENCODER_CLASS     0x02
#define MMC_ENCODER_FUNCTION  0x03

typedef struct loader_data {
  long  version;
  char* filename;
  uint  lineno;
} loader_data;

static inline unsigned char decode(char** p, unsigned int* l) {
  unsigned char c;
  if (*l == 0) {
    zend_bailout();
  }
  c = **p;
  (*p)++;
  (*l)--;
  return c;
}

static inline unsigned int decode32(char** p, unsigned int* l) {
  unsigned int i = decode(p, l);
  i += ((unsigned int)decode(p, l)) << 8;
  i += ((unsigned int)decode(p, l)) << 16;
  i += ((unsigned int)decode(p, l)) << 24;
  return i;
}

static inline unsigned short decode16(char** p, unsigned int* l) {
  unsigned short i = decode(p, l);
  i += ((unsigned short)decode(p, l)) << 8;
  return i;
}

static unsigned int decode_var(unsigned int count, char** p, unsigned int* l) {
  unsigned int var;
  if (count < 0xff) {
    var = decode(p, l);
  } else if (count < 0xffff) {
    var = decode16(p, l);
  } else {
    var = decode32(p, l);
  }
  if (var >= count) {
    zend_bailout();
  }
#ifdef ZEND_ENGINE_2
  return (unsigned int)(((temp_variable*)NULL) + var);
#else
  return var;
#endif
}

static unsigned int decode_opline(unsigned int last, char** p, unsigned int* l) {
  unsigned int opline;
  if (last < 0xff-1) {
    opline = decode(p, l);
    if (opline == 0xff) return (unsigned int)-1;
  } else if (last < 0xffff-1) {
    opline = decode16(p, l);
    if (opline == 0xffff) return (unsigned int)-1;
  } else {
    opline = decode32(p, l);
  }
  if (opline >= last) {
    zend_bailout();
  }
  return opline;
}

static char* decode_zstr(char** p, unsigned int* l) {
  char *s = *p;
  unsigned int len = 0;
  while (s[len] != '\0') {
    len++;
    if (len > (*l)) {
      zend_bailout();
    }
  }
  if (len == 0) {
    (*p)++;
    (*l)--;
    return NULL;
  } else {
    char *str = emalloc(len+1);
    memcpy(str, *p, len+1);
    *p += len+1;
    *l -= len+1;
    return str;
  }
}

static char* decode_zstr_noalloc(char** p, unsigned int* l) {
  char *s = *p;
  unsigned int len = 0;
  while (s[len] != '\0') {
    len++;
    if (len > (*l)) {
      zend_bailout();
    }
  }
  if (len == 0) {
    (*p)++;
    (*l)--;
    return NULL;
  } else {
    *p += len+1;
    *l -= len+1;
    return s;
  }
}

#if MMC_ENCODER_VERSION < 2
static char* decode_filename(char** p, unsigned int* l TSRMLS_DC) {
  char *s = *p;
  unsigned int len = 0;
  while (s[len] != '\0') {
    len++;
    if (len > (*l)) {
      zend_bailout();
    }
  }
  if (len == 0) {
    (*p)++;
    (*l)--;
    return NULL;
  } else {
    if (((loader_data*)MMCG(mem))->filename == NULL ||
        strcmp(((loader_data*)MMCG(mem))->filename,*p) != 0) {
      char* old = CG(compiled_filename);
      ((loader_data*)MMCG(mem))->filename = zend_set_compiled_filename(*p TSRMLS_CC);
      CG(compiled_filename) = old;
    }
    *p += len+1;
    *l -= len+1;
    return ((loader_data*)MMCG(mem))->filename;
  }
}
#endif

static char* decode_lstr(unsigned int* len, char** p, unsigned int* l) {
  *len = decode32(p, l);
  if (*len == 0) {
    return NULL;
  } else {
    char* str;
    if (*len > *l) {
      zend_bailout();
    }
    str = emalloc((*len)+1);
    memcpy(str, *p, *len);
    str[(*len)] = '\0';
    *p += *len;
    *l -= *len;
    return str;
  }
}

static char* decode_lstr_noalloc(unsigned int* len, char** p, unsigned int* l) {
  *len = decode32(p, l);
  if (*len == 0) {
    return NULL;
  } else {
    char* str = *p;
    if (*len > *l) {
      zend_bailout();
    }
    *p += *len;
    *l -= *len;
    return str;
  }
}

static unsigned char* decode_pstr(char** p, unsigned int* l) {
  unsigned char c = decode(p, l);
  if (c == 0) {
    return NULL;
  } else {
    unsigned char *str;
    if (c > *l) {
      zend_bailout();
    }
    str = emalloc(c+1);
    str[0] = c;
    memcpy(&str[1], *p, c);
    *p += c;
    *l -= c;
    return str;
  }
}

static double decode_double(char** p, unsigned int* l) {
  unsigned char sign;
  int exp;
  unsigned int i1, i2;
  double d;

  sign = decode(p, l);
  exp = decode32(p, l);
  i1 = decode32(p, l);
  i2 = decode32(p, l);
  d = ldexp((((double)i2 / 4294967296.0) + (double)i1) / 4294967296.0, exp);
  if (sign) {
    d = -d;
  }
  return d;
}

typedef void* (*decode_bucket_t)(void* to, char**, unsigned int* TSRMLS_DC);

#define decode_zval_hash(to, p, l) decode_hash(to, sizeof(zval*), (decode_bucket_t)decode_zval_ptr, p, l TSRMLS_CC)
#define decode_zval_hash_noref(to, p, l) decode_hash(to, sizeof(zval*), (decode_bucket_t)decode_zval_ptr_noref, p, l TSRMLS_CC)

static HashTable* decode_hash(HashTable* to, int size, decode_bucket_t decode_bucket, char**p, unsigned int* l TSRMLS_DC);
static zval* decode_zval_ptr(zval* to, char** p, unsigned int* l TSRMLS_DC);

static zval* decode_zval(zval* to, int refs, char** p, unsigned int* l TSRMLS_DC) {
  to->type     = decode(p ,l);
  if (refs) {
    to->is_ref = decode(p, l);
    to->refcount = decode32(p, l);
  } else {
    to->is_ref   = 1;
    to->refcount = 2;
  }
  switch (to->type & ~IS_CONSTANT_INDEX) {
    case IS_NULL:
      break;
    case IS_BOOL:
      to->value.lval = decode(p, l);
      break;
    case IS_LONG:
      {
  int x = decode32(p,l);
        to->value.lval = x;
      }
      break;
    case IS_DOUBLE:
      to->value.dval = decode_double(p, l);
      break;
    case IS_CONSTANT:
    case IS_STRING:
/*???    case FLAG_IS_BC:*/
      to->value.str.val = decode_lstr((unsigned int*)&to->value.str.len, p, l);
      if (to->value.str.val == NULL) {
        to->value.str.val = empty_string;
      }
      break;
    case IS_ARRAY:
    case IS_CONSTANT_ARRAY:
      to->value.ht = decode_zval_hash(NULL, p ,l);
      if (to->value.ht) {
        to->value.ht->pDestructor = ZVAL_PTR_DTOR;
      } else {
        ALLOC_HASHTABLE(to->value.ht);
        zend_hash_init(to->value.ht, 0, NULL, ZVAL_PTR_DTOR, 0);
      }
      break;
    case IS_OBJECT:
    case IS_RESOURCE:
      /*???*/
    default:
      zend_bailout();
      break;
  }
  return to;
}

static zval* decode_zval_ptr(zval* to, char** p, unsigned int* l TSRMLS_DC) {
  if (to == NULL) {
    ALLOC_ZVAL(to);
  }
  decode_zval(to, 1, p, l TSRMLS_CC);
  return to;
}

static zval* decode_zval_ptr_noref(zval* to, char** p, unsigned int* l TSRMLS_DC) {
  if (to == NULL) {
    ALLOC_ZVAL(to);
  }
  decode_zval(to, 1, p, l TSRMLS_CC);
  to->is_ref   = 0;
  to->refcount = 1;
  return to;
}

static void decode_znode(znode* to, unsigned int vars_count, char** p, unsigned int* l TSRMLS_DC) {
  to->op_type = decode(p, l);
  if (to->op_type == IS_CONST) {
    decode_zval(&to->u.constant, 0, p, l TSRMLS_CC);
    to->u.constant.is_ref = 1;
    to->u.constant.refcount = 2;
  } else if (to->op_type == IS_VAR ||
             to->op_type == IS_TMP_VAR) {
    to->u.var = decode_var(vars_count, p, l);
  } else if (to->op_type != IS_UNUSED) {
    zend_bailout();
  }
}

static HashTable* decode_hash(HashTable* to, int size, decode_bucket_t decode_bucket, char** p, unsigned int* l TSRMLS_DC) {
  unsigned int n;
  void *data = NULL;

  if (size != sizeof(void*)) {
    data = do_alloca(size);
  }
  n = decode32(p, l);
  if (to == NULL) {
    if (n == 0 ) return NULL;
    ALLOC_HASHTABLE(to);
    zend_hash_init(to, 0, NULL, NULL, 0);
  }
  while (n > 0) {
    void* x;
    char* s;
    unsigned int len;

    s = decode_lstr_noalloc(&len, p, l);
    if (s == NULL) {
      len = decode32(p, l);
    }
    if (size == sizeof(void*)) {
      x = decode_bucket(NULL, p, l TSRMLS_CC);
      if (s != NULL) {
        zend_hash_add(to, s, len, &x, size, NULL);
      } else {
        zend_hash_index_update(to, len, &x, size, NULL);
      }
    } else {
      decode_bucket(data, p, l TSRMLS_CC);
      if (s != NULL) {
        zend_hash_add(to, s, len, data, size, NULL);
      } else {
        zend_hash_index_update(to, len, data, size, NULL);
      }
    }
    --n;
  }
  if (size != sizeof(void*)) {
    free_alloca(data);
  }
  return to;
}

static void call_op_array_ctor_handler(zend_extension *extension, zend_op_array *op_array TSRMLS_DC) {
  if (extension->op_array_ctor) {
    extension->op_array_ctor(op_array);
  }
}

static zend_op_array* decode_op_array(zend_op_array *to, char** p, unsigned int* l TSRMLS_DC) {
  char c;
  zend_op *opline;
  zend_op *end;
#ifdef ZEND_ENGINE_2
  char* scope_name;
  int   scope_name_len;
#endif

  c = decode(p, l);
  if (c == ZEND_INTERNAL_FUNCTION) {
    if (to == NULL) {
      to = (zend_op_array*)emalloc(sizeof(zend_internal_function));
    }
    memset(to, 0, sizeof(zend_internal_function));
  } else if (c == ZEND_USER_FUNCTION) {
    if (to == NULL) {
      to = (zend_op_array*)emalloc(sizeof(zend_op_array));
    }
    memset(to, 0, sizeof(zend_op_array));
    zend_llist_apply_with_argument(&zend_extensions, (llist_apply_with_arg_func_t) call_op_array_ctor_handler, to TSRMLS_CC);
  } else {
    zend_bailout();
  }
  to->type = c;
#ifdef ZEND_ENGINE_2
  to->num_args = decode32(p, l);
  if (to->num_args > 0) {
    zend_uint i;
    to->arg_info = (zend_arg_info*)emalloc(to->num_args * sizeof(zend_arg_info));
    for (i = 0; i < to->num_args; i++) {
      to->arg_info[i].name = decode_lstr(&to->arg_info[i].name_len, p ,l);
      to->arg_info[i].class_name = decode_lstr(&to->arg_info[i].class_name_len, p ,l);
      to->arg_info[i].allow_null = decode(p, l);
      to->arg_info[i].pass_by_reference = decode(p, l);
    }
  } else {
    to->arg_info = NULL;
  }
  to->pass_rest_by_reference = decode(p, l);
#else
  to->arg_types     = decode_pstr(p, l);
#endif
  to->function_name = decode_zstr(p, l);
#ifdef ZEND_ENGINE_2
	to->scope            = MMCG(class_entry);
	to->fn_flags         = decode32(p, l);
	scope_name = decode_lstr((unsigned int*)&scope_name_len, p, l);
	if (to->scope == NULL && scope_name != NULL)
	{
		if (zend_hash_find(CG(class_table),
			(void *)scope_name, scope_name_len,
			(void **)&to->scope) == SUCCESS)
		{
			to->scope = *(zend_class_entry**)to->scope;
		}
		else
		{
/*???
			debug_printf("[%d] EACCELERATOR can't restore parent class "
				"\"%s\" of function \"%s\"\n", getpid(),
				(char*)scope_name, to->function_name);
*/
				to->scope = NULL;
		}
	}
	if (to->scope != NULL)
	{
		unsigned int len = strlen(to->function_name);
		char *lcname = zend_str_tolower_dup(to->function_name, len);
		/*
		 * HOESH: As explained in restore_op_array()!
		 */
		if  (
				to->scope->name_length == len &&
				memcmp(to->scope->name, lcname, len) == 0 &&
				(
					to->scope->constructor == NULL || // case 0)
					to->scope->constructor->type == ZEND_INTERNAL_FUNCTION || // case A)
					to->scope->constructor->op_array.scope->name_length != len || // case B)
					memcmp(to->scope->constructor->op_array.scope->name, lcname, len) != 0
				)
			)
		{
			to->scope->constructor = (zend_function*)to;
		}
		else if (*lcname == '_' && *(lcname+1) == '_')
		{
			if (len == sizeof(ZEND_CONSTRUCTOR_FUNC_NAME)-1 &&
				memcmp(lcname, ZEND_CONSTRUCTOR_FUNC_NAME, sizeof(ZEND_CONSTRUCTOR_FUNC_NAME)) == 0)
			{
				to->scope->constructor = (zend_function*)to;
			}
			else if (len == sizeof(ZEND_DESTRUCTOR_FUNC_NAME)-1 &&
				memcmp(lcname, ZEND_DESTRUCTOR_FUNC_NAME, sizeof(ZEND_DESTRUCTOR_FUNC_NAME)) == 0)
			{
				to->scope->destructor = (zend_function*)to;
			}
			else if (len == sizeof(ZEND_CLONE_FUNC_NAME)-1 &&
				memcmp(lcname, ZEND_CLONE_FUNC_NAME, sizeof(ZEND_CLONE_FUNC_NAME)) == 0)
			{
				to->scope->clone = (zend_function*)to;
			}
			else if (len == sizeof(ZEND_GET_FUNC_NAME)-1 &&
				memcmp(lcname, ZEND_GET_FUNC_NAME, sizeof(ZEND_GET_FUNC_NAME)) == 0)
			{
				to->scope->__get = (zend_function*)to;
			}
			else if (len == sizeof(ZEND_SET_FUNC_NAME)-1 &&
				memcmp(lcname, ZEND_SET_FUNC_NAME, sizeof(ZEND_SET_FUNC_NAME)) == 0)
			{
				to->scope->__set = (zend_function*)to;
			}
			else if (len == sizeof(ZEND_CALL_FUNC_NAME)-1 &&
				memcmp(lcname, ZEND_CALL_FUNC_NAME, sizeof(ZEND_CALL_FUNC_NAME)) == 0)
			{
				to->scope->__call = (zend_function*)to;
			}
		}
		efree(lcname);
	}
#endif
  if (to->type == ZEND_INTERNAL_FUNCTION) {
    return to;
  }
  to->T = decode32(p, l);
#ifdef ZEND_ENGINE_2
  to->uses_this = decode(p, l);
#else
  to->uses_globals = decode(p, l);
#endif
  to->return_reference = decode(p, l);

  to->last = decode32(p, l);
  to->size = to->last;
  if (to->last > 0) {
    to->last_brk_cont = decode32(p, l);
    if (to->last_brk_cont > 0) {
      zend_uint i;
      to->brk_cont_array = emalloc(sizeof(zend_brk_cont_element)*to->last_brk_cont);
      for (i = 0; i < to->last_brk_cont; i++) {
        to->brk_cont_array[i].brk = decode_opline(to->last, p, l);
        to->brk_cont_array[i].cont = decode_opline(to->last, p, l);
        to->brk_cont_array[i].parent = decode_opline(to->last_brk_cont, p, l);
      }
    } else {
      to->brk_cont_array = NULL;
    }
#ifdef ZEND_ENGINE_2
	to->last_try_catch = decode32(p, l);
	if (to->last_try_catch > 0)
	{
		zend_uint i;
		to->try_catch_array = emalloc(sizeof(zend_try_catch_element)*to->last_try_catch);
		for (i = 0; i < to->last_try_catch; i++)
		{
			to->try_catch_array[i].try_op = decode_opline(to->last, p, l);
			to->try_catch_array[i].catch_op = decode_opline(to->last, p, l);
//			to->try_catch_array[i].parent = decode_opline(to->last_brk_cont, p, l);
		}
	}
	else
	{
		to->try_catch_array = NULL;
}
#endif
    to->opcodes = emalloc(sizeof(zend_op)*to->last);
    memset(to->opcodes, 0, sizeof(zend_op)*to->last);
    opline = to->opcodes;
    end = opline + to->last;
    for (;opline < end; opline++) {
      const opcode_dsc* op_dsc;
      opline->opcode = decode(p, l);
      op_dsc = get_opcode_dsc(opline->opcode);
      if (op_dsc == NULL) {
        zend_bailout();
      } else {
        unsigned int ops = op_dsc->ops;
#ifdef ZEND_ENGINE_2
/*??? FIXME
        opline->handler = zend_opcode_handlers[opline->opcode];
*/
        opline->handler = get_opcode_handler(opline->opcode TSRMLS_CC);
#endif
#if MMC_ENCODER_VERSION < 2
        opline->lineno = decode32(p, l);
#else
        if (((loader_data*)MMCG(mem))->version < 2) {
          opline->lineno = decode32(p, l);
        }
        opline->lineno = ((loader_data*)MMCG(mem))->lineno;
#endif
        opline->extended_value = 0;
        opline->result.op_type = IS_UNUSED;
        opline->op1.op_type    = IS_UNUSED;
        opline->op2.op_type    = IS_UNUSED;

        switch (ops & EXT_MASK) {
          case EXT_UNUSED:
            break;
          case EXT_STD:
          case EXT_FCALL:
          case EXT_ARG:
          case EXT_IFACE:
            opline->extended_value = decode32(p, l);
            break;
          case EXT_SEND:
          case EXT_SEND_NOREF:
          case EXT_INIT_FCALL:
          case EXT_FETCH:
          case EXT_CAST:
          case EXT_DECLARE:
          case EXT_FCLASS:
          case EXT_BIT:
          case EXT_ISSET:
          case EXT_ASSIGN:
            opline->extended_value = decode(p, l);
            break;
          case EXT_OPLINE:
            opline->extended_value = decode_opline(to->last, p, l);
            break;
          case EXT_CLASS:
            opline->extended_value = decode_var(to->T, p, l);
            break;
          default:
            zend_bailout();
            break;
        }
        switch (ops & RES_MASK) {
          case RES_UNUSED:
            break;
          case RES_TMP:
            opline->result.op_type = IS_TMP_VAR;
            opline->result.u.var = decode_var(to->T, p, l);
            break;
          case RES_CLASS:
            opline->result.u.var = decode_var(to->T, p, l);
            break;
          case RES_VAR:
            opline->result.op_type = IS_VAR;
            opline->result.u.var = decode_var(to->T, p, l);
            opline->result.u.EA.type = 0;
            if (decode(p, l)) {
              opline->result.u.EA.type |= EXT_TYPE_UNUSED;
            }
            break;
          case RES_STD:
            decode_znode(&opline->result, to->T, p, l TSRMLS_CC);
            if (opline->result.op_type == IS_VAR) {
              opline->result.u.EA.type = 0;
              if (decode(p, l)) {
                opline->result.u.EA.type |= EXT_TYPE_UNUSED;
              }
            }
            break;
          default:
            zend_bailout();
            break;
        }
        switch (ops & OP1_MASK) {
          case OP1_UNUSED:
            break;
          case OP1_OPLINE:
            opline->op1.u.opline_num = decode_opline(to->last, p, l);
            break;
          case OP1_BRK:
          case OP1_CONT:
            opline->op1.u.opline_num = decode_opline(to->last_brk_cont, p, l);
            break;
          case OP1_CLASS:
            opline->op1.u.var = decode_var(to->T, p, l);
            break;
          case OP1_UCLASS:
            opline->op1.op_type = decode(p, l);
            if (opline->op1.op_type != IS_UNUSED) {
              opline->op1.u.var = decode_var(to->T, p, l);
            }
            break;
          case OP1_TMP:
            opline->op1.op_type = IS_TMP_VAR;
            opline->op1.u.var = decode_var(to->T, p, l);
            break;
          case OP1_VAR:
            opline->op1.op_type = IS_VAR;
            opline->op1.u.var = decode_var(to->T, p, l);
            break;
          case OP1_ARG:
            opline->op1.op_type = IS_CONST;
            opline->op1.u.constant.type = IS_LONG;
            opline->op1.u.constant.value.lval = decode32(p, l);
            break;
#ifdef ZEND_ENGINE_2
          case OP1_JMPADDR:
            opline->op1.u.jmp_addr = to->opcodes + decode_opline(to->last, p, l);
            break;
#endif
          case OP1_STD:
            decode_znode(&opline->op1, to->T, p, l TSRMLS_CC);
            break;
          default:
            zend_bailout();
            break;
        }
        switch (ops & OP2_MASK) {
          case OP2_UNUSED:
            break;
          case OP2_OPLINE:
            opline->op2.u.opline_num = decode_opline(to->last, p, l);
            break;
          case OP2_ARG:
            opline->op2.u.opline_num = decode32(p, l);
            break;
          case OP2_ISSET:
          case OP2_INCLUDE:
            opline->op2.op_type = IS_CONST;
            opline->op2.u.constant.type = IS_LONG;
            opline->op2.u.constant.value.lval = decode(p, l);
            break;
          case OP2_FETCH:
#ifdef ZEND_ENGINE_2
            opline->op2.u.EA.type = decode(p, l);
            if (opline->op2.u.EA.type == ZEND_FETCH_STATIC_MEMBER) {
              opline->op2.u.var = decode_var(to->T, p, l);
            }
#else
            opline->op2.u.fetch_type = decode(p, l);
#endif
            break;
          case OP2_CLASS:
            opline->op2.u.var = decode_var(to->T, p, l);
            break;
          case OP2_TMP:
            opline->op2.op_type = IS_TMP_VAR;
            opline->op2.u.var = decode_var(to->T, p, l);
            break;
          case OP2_VAR:
            opline->op2.op_type = IS_VAR;
            opline->op2.u.var = decode_var(to->T, p, l);
            break;
#ifdef ZEND_ENGINE_2
          case OP2_JMPADDR:
            opline->op2.u.jmp_addr = to->opcodes + decode_opline(to->last, p, l);
            break;
#endif
          case OP2_STD:
            decode_znode(&opline->op2, to->T, p, l TSRMLS_CC);
            break;
          default:
            zend_bailout();
            break;
        }
      }
    }
  } else {
    to->opcodes = NULL;
  }

#ifdef ZEND_ENGINE_2
  to->static_variables = decode_zval_hash(NULL, p, l);
#else
  to->static_variables = decode_zval_hash_noref(NULL, p, l);
#endif
  if (to->static_variables) {
    to->static_variables->pDestructor = ZVAL_PTR_DTOR;
  }
#if MMC_ENCODER_VERSION < 2
  to->filename = decode_filename(p, l TSRMLS_CC);
#else
  if (((loader_data*)MMCG(mem))->version < 2) {
    to->filename = decode_zstr(p, l);
    efree(to->filename);
  }
  to->filename = ((loader_data*)MMCG(mem))->filename;
#endif
#ifdef ZEND_ENGINE_2
  to->line_start = decode32(p, l);
  to->line_end = decode32(p, l);
  to->doc_comment = decode_lstr(&to->doc_comment_len, p, l);
#endif
  to->start_op = to->opcodes;
  to->current_brk_cont = 0xffffffff;
  to->backpatch_count  = 0;
  to->done_pass_two    = 1;
  to->refcount = emalloc(sizeof(*to->refcount));
   *to->refcount=1;
  return to;
}


#ifdef ZEND_ENGINE_2
static zend_property_info* decode_property_info(zend_property_info* to, char** p, unsigned int* l TSRMLS_DC) {
  if (to == NULL) {
    to = emalloc(sizeof(zend_property_info));
  }
  to->flags = decode32(p, l);
  to->name = decode_lstr((unsigned int*)&to->name_length, p, l);
  return to;
}
#endif

static zend_class_entry* decode_class_entry(zend_class_entry* to, char** p, unsigned int* l TSRMLS_DC) {
  char c;
  zend_class_entry* old;
  char*             s;
  unsigned int      len;

  c = decode(p, l);
  if (c == ZEND_USER_CLASS) {
    if (to == NULL) {
      to = emalloc(sizeof(zend_class_entry));
    }
    memset(to, 0, sizeof(zend_class_entry));
  } else {
    zend_bailout();
  }
  to->type = c;
  to->name = decode_lstr(&to->name_length, p ,l);
#ifdef ZEND_ENGINE_2
  to->ce_flags = decode32(p ,l);
  to->num_interfaces = decode32(p, l);
  if (to->num_interfaces > 0) {
    to->interfaces = (zend_class_entry **) emalloc(sizeof(zend_class_entry *)*to->num_interfaces);
  } else {
    to->interfaces = NULL;
  }
  to->create_object = NULL;
#endif

  to->parent      = NULL;
  s = decode_lstr(&len, p, l);
  if (s != NULL) {
    if (zend_hash_find(CG(class_table), s, len+1, (void **)&to->parent) != SUCCESS) {
/*???
      debug_printf("[%d] EACCELERATOR can't restore parent class "
          "\"%s\" of class \"%s\"\n", getpid(), s, to->name);
*/
      to->parent = NULL;
    } else {
#ifdef ZEND_ENGINE_2
	  /*
	   * HOESH: See restore_class_entry() on details.
	   */
	  to->parent = *(zend_class_entry**)to->parent;
	  to->constructor  = to->parent->constructor;
	  to->destructor  = to->parent->destructor;
	  to->clone  = to->parent->clone;
	  to->__get  = to->parent->__get;
      to->__set  = to->parent->__set;
      to->__call = to->parent->__call;
	  to->create_object = to->parent->create_object;
#else
	  to->handle_property_get  = to->parent->handle_property_get;
      to->handle_property_set  = to->parent->handle_property_set;
      to->handle_function_call = to->parent->handle_function_call;
#endif
    }
    efree(s);
  }

  old = MMCG(class_entry);
  MMCG(class_entry) = to;

#ifdef ZEND_ENGINE_2
  to->refcount = 1;

#if MMC_ENCODER_VERSION < 2
  to->line_start = decode32(p, l);
  to->line_end = decode32(p, l);
  to->filename = decode_filename(p, l TSRMLS_CC);
#else
  if (((loader_data*)MMCG(mem))->version < 2) {
    to->line_start = decode32(p, l);
    to->line_end = decode32(p, l);
    to->filename = decode_zstr(p, l);
    efree(to->filename);
  }
  to->line_start = ((loader_data*)MMCG(mem))->lineno;
  to->line_end = ((loader_data*)MMCG(mem))->lineno;
  to->filename = ((loader_data*)MMCG(mem))->filename;
#endif
  to->doc_comment = decode_lstr(&to->doc_comment_len, p, l);

  zend_hash_init(&to->constants_table, 0, NULL, ZVAL_PTR_DTOR, 0);
  decode_zval_hash(&to->constants_table, p, l);

  zend_hash_init(&to->default_properties, 0, NULL, ZVAL_PTR_DTOR, 0);
  decode_zval_hash(&to->default_properties, p, l);

/*???FIXME
  zend_hash_init_ex(&to->properties_info, 0, NULL, (dtor_func_t)zend_destroy_property_info, 0, 0);
*/
  zend_hash_init_ex(&to->properties_info, 0, NULL, (dtor_func_t)NULL, 0, 0);
  decode_hash(&to->properties_info, sizeof(zend_property_info), (decode_bucket_t)decode_property_info, p, l TSRMLS_CC);

  ALLOC_HASHTABLE(to->static_members);
  zend_hash_init_ex(to->static_members, 0, NULL, ZVAL_PTR_DTOR, 0, 0);
  decode_zval_hash(to->static_members, p, l);

  {
    Bucket *q = to->properties_info.pListHead;
    while (q != NULL) {
      zend_property_info* x = (zend_property_info*)q->pData;
      Bucket * y = NULL;
      if ((x->flags & ZEND_ACC_STATIC) && to->static_members != NULL && to->static_members->nNumOfElements > 0) {
        y = to->static_members->pListHead;
      } else if ((x->flags & ZEND_ACC_STATIC) == 0 && to->default_properties.nNumOfElements > 0) {
        y = to->default_properties.pListHead;
      }
      while (y != NULL) {
        if ((int)y->nKeyLength == x->name_length+1 &&
             memcmp(y->arKey, x->name, x->name_length+1) == 0) {
          x->h = y->h;
          break;
        }
        y = y->pListNext;
      }
      q = q->pListNext;
    }
  }

#else
  to->refcount = emalloc(sizeof(*to->refcount));
  *to->refcount = 1;

  zend_hash_init(&to->default_properties, 0, NULL, ZVAL_PTR_DTOR, 0);
  decode_zval_hash_noref(&to->default_properties, p, l);
#endif
  zend_hash_init(&to->function_table, 0, NULL, ZEND_FUNCTION_DTOR, 0);
  decode_hash(&to->function_table, sizeof(zend_op_array), (decode_bucket_t)decode_op_array, p, l TSRMLS_CC);
  to->constants_updated = 0;

  MMCG(class_entry) = old;

  return to;
}

zend_op_array* eaccelerator_load(char* src, int src_len TSRMLS_DC) {
  zval func;
  zval gzstring;
  zval retval;
  zval param;
  zval *params[1];
  zend_op_array* to = NULL;
  zend_bool error_reported = 0;

  if (!zend_hash_exists(EG(function_table), "gzuncompress", sizeof("gzuncompress"))) {
    zend_error(E_ERROR, "eAccelerator Loader requires php_zlib extension\n");
    return NULL;
  }

  ZVAL_STRING(&func, "base64_decode", 0);
  INIT_ZVAL(param);
  params[0] = &param;
  ZVAL_STRINGL(params[0], src, src_len, 0);
  if (call_user_function(CG(function_table), (zval**)NULL, &func, &gzstring, 1, params TSRMLS_CC) == SUCCESS &&
      gzstring.type == IS_STRING) {
    ZVAL_STRING(&func, "gzuncompress", 0);
    params[0] = &gzstring;
    if (call_user_function(CG(function_table), (zval**)NULL, &func, &retval, 1, params TSRMLS_CC) == SUCCESS &&
        retval.type == IS_STRING) {
      zend_bool old_in_compilation = CG(in_compilation);
      zend_bool old_in_execution   = EG(in_execution);
      zval_dtor(&gzstring);
      zend_try {
        char*        p = retval.value.str.val;
        unsigned int l = retval.value.str.len;
        char *s;
        unsigned char c;
        unsigned int  v;

        s = decode_zstr_noalloc(&p, &l);
        if (s != NULL && strcmp(s,"EACCELERATOR") == 0) {
          v = decode32(&p, &l);
          if (v <= MMC_ENCODER_VERSION) {
            loader_data data;
            data.version  = v;
            data.filename = NULL;
            data.lineno = 0;
            MMCG(mem) = (char*)&data;
            c = decode(&p, &l);
#ifdef ZEND_ENGINE_2
            if (c == 2) {
#else
            if (c == 1) {
#endif
              MMCG(class_entry) = NULL;
#if MMC_ENCODER_VERSION > 1
              if (CG(in_compilation)) {
                data.filename = CG(compiled_filename);
                data.lineno = 0;
              } else {
                char* old = CG(compiled_filename);
                if (EG(active_op_array && EG(active_op_array)->filename)) {
                  data.filename = zend_set_compiled_filename(EG(active_op_array)->filename TSRMLS_CC);
                }
                CG(compiled_filename) = old;
                data.lineno = zend_get_executed_lineno(TSRMLS_C);
              }
#endif
              while (1) {
                c = decode(&p, &l);
                if (c == MMC_ENCODER_CLASS) {
#ifdef ZEND_ENGINE_2
                  zend_class_entry* x;
                  s = decode_lstr_noalloc(&v, &p, &l);
                  x = decode_class_entry(NULL, &p, &l TSRMLS_CC);
#else
                  zend_class_entry x;
                  s = decode_lstr_noalloc(&v, &p, &l);
                  decode_class_entry(&x, &p, &l TSRMLS_CC);
#endif
                  if ((s[0] == '\000') &&
                      zend_hash_exists(CG(class_table), s, v)) {
#ifdef ZEND_ENGINE_2
                  } else if (zend_hash_add(CG(class_table), s, v,
                      &x, sizeof(zend_class_entry*), NULL) == FAILURE) {
#else
                  } else if (zend_hash_add(CG(class_table), s, v,
                      &x, sizeof(zend_class_entry), NULL) == FAILURE) {
#endif
                    error_reported = 1;
                    zend_error(E_ERROR, "Cannot redeclare class %s", s);
                  }
                } else if (c == MMC_ENCODER_END) {
                  break;
                } else {
                  zend_bailout();
                }
              }
              while (1) {
                c = decode(&p, &l);
                if (c == MMC_ENCODER_FUNCTION) {
                  zend_op_array x;
                  s = decode_lstr_noalloc(&v, &p, &l);
                  decode_op_array(&x, &p, &l TSRMLS_CC);
                  if ((s[0] == '\000') &&
                      zend_hash_exists(CG(function_table), s, v)) {
                  } else if (zend_hash_add(CG(function_table), s, v,
                             &x, sizeof(zend_op_array), NULL) == FAILURE) {
                    error_reported = 1;
                    zend_error(E_ERROR, "Cannot redeclare %s()", s);
                  }
                } else if (c == MMC_ENCODER_END) {
                  break;
                } else {
                  zend_bailout();
                }
              }
              to = decode_op_array(NULL, &p, &l TSRMLS_CC);
              if (l != 0) {
                zend_bailout();
              }
            } else {
              error_reported = 1;
              zend_error(E_ERROR, "MMCache Loader can't load code. Icorrect Zend Engine version");
            }
          } else {
            error_reported = 1;
            zend_error(E_ERROR, "MMCache Loader can't load code. Icorrect MMCache encoder version (%u)", v);
          }
        } else {
          error_reported = 1;
          zend_error(E_ERROR, "MMCache Loader can't load code. Icorrect code");
        }
      } zend_catch {
        CG(in_compilation) = old_in_compilation;
        EG(in_execution)   = old_in_execution;
        to = NULL;
      } zend_end_try();
      zval_dtor(&retval);
    }
  }
  if (to == NULL) {
    if (error_reported) {
      zend_bailout();
    } else {
      zend_error(E_ERROR, "MMCache Loader can't load code. Icorrect code");
    }
  }
  return to;
}

PHP_FUNCTION(eaccelerator_load) {
  char *src;
  int   src_len;
  zend_op_array* op_array;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
    "s", &src, &src_len) == FAILURE) {
    RETURN_FALSE;
  }
  if ((op_array = eaccelerator_load(src, src_len TSRMLS_CC)) != NULL) {
    zval *local_retval_ptr=NULL;
    zend_function_state *original_function_state_ptr = EG(function_state_ptr);
    zval **original_return_value = EG(return_value_ptr_ptr);
    zend_op_array *original_op_array = EG(active_op_array);
    zend_op **original_opline_ptr = EG(opline_ptr);

    EG(return_value_ptr_ptr) = &local_retval_ptr;
    EG(active_op_array) = op_array;

    zend_execute(op_array TSRMLS_CC);

    if (local_retval_ptr) {
      if (return_value != NULL) {
        COPY_PZVAL_TO_ZVAL(*return_value, local_retval_ptr);
      } else {
        zval_ptr_dtor(&local_retval_ptr);
      }
    } else if (return_value) {
      INIT_ZVAL(*return_value);
    }

#ifdef ZEND_ENGINE_2
    destroy_op_array(op_array TSRMLS_CC);
#else
    destroy_op_array(op_array);
#endif
    efree(op_array);

    EG(active_op_array) = original_op_array;
    EG(return_value_ptr_ptr)=original_return_value;
    EG(opline_ptr) = original_opline_ptr;
    EG(function_state_ptr) = original_function_state_ptr;

    return;
  }
  RETURN_FALSE;
}

PHP_FUNCTION(_eaccelerator_loader_file) {
  if (EG(active_op_array) && EG(active_op_array)->filename) {
    RETURN_STRING(EG(active_op_array)->filename, 1);
  } else {
    RETURN_EMPTY_STRING();
  }
}

PHP_FUNCTION(_eaccelerator_loader_line) {
  RETURN_LONG(zend_get_executed_lineno(TSRMLS_C));
}

#ifdef HAVE_EACCELERATOR_STANDALONE_LOADER
ZEND_DECLARE_MODULE_GLOBALS(eaccelerator)

function_entry eaccelerator_loader_functions[] = {
  PHP_FE(eaccelerator_load, NULL)
  PHP_FE(_eaccelerator_loader_file, NULL)
  PHP_FE(_eaccelerator_loader_line, NULL)
  {NULL, NULL, NULL}
};

static void eaccelerator_init_globals(zend_eaccelerator_globals *eaccelerator_globals) {
}

PHP_MINIT_FUNCTION(eaccelerator_loader) {
  if (zend_hash_exists(&module_registry, EACCELERATOR_EXTENSION_NAME, sizeof(EACCELERATOR_EXTENSION_NAME)) &&
      zend_hash_exists(CG(function_table), "eaccelerator_load", sizeof("eaccelerator_load"))) {
    zend_error(E_CORE_WARNING,"Extension \"%s\" is not need with \"%s\". Remove it from php.ini\n", EACCELERATOR_LOADER_EXTENSION_NAME, EACCELERATOR_EXTENSION_NAME);
    return FAILURE;
  }
  ZEND_INIT_MODULE_GLOBALS(eaccelerator, eaccelerator_init_globals, NULL);
  return SUCCESS;
}

zend_module_entry eaccelerator_loader_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
  STANDARD_MODULE_HEADER,
#endif
  EACCELERATOR_LOADER_EXTENSION_NAME,
  eaccelerator_loader_functions,
  PHP_MINIT(eaccelerator_loader),
  NULL,
  NULL,
  NULL,
  NULL,
#if ZEND_MODULE_API_NO >= 20010901
  EACCELERATOR_VERSION,          /* extension version number (string) */
#endif
  STANDARD_MODULE_PROPERTIES
};

#if defined(COMPILE_DL_ELOADER)
ZEND_GET_MODULE(eaccelerator_loader)
#endif
#endif

#endif
#endif
