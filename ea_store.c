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

#include "eaccelerator.h"

#ifdef HAVE_EACCELERATOR

#include "ea_store.h"
#include "debug.h"

/******************************************************************************/
/* Functions to calculate the size of different structure that a compiled php */
/* script contains.                                                           */
/******************************************************************************/

#ifndef DEBUG
inline
#endif
static void calc_string(char *str, int len TSRMLS_DC)
{
	if (len > MAX_DUP_STR_LEN || 
            zend_hash_add(&EAG(strings), str, len, &str, sizeof(char *), NULL) == SUCCESS) {
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += len;
	}
}

typedef void (*calc_bucket_t) (void *TSRMLS_DC);

#define calc_hash_ex(from, start, calc_bucket) \
  calc_hash_int(from, start, calc_bucket TSRMLS_CC)

#define calc_hash(from, calc_bucket) \
  calc_hash_ex(from, (from)->pListHead, calc_bucket)

#define calc_zval_hash(from) \
  calc_hash(from, (calc_bucket_t)calc_zval_ptr)

#define calc_zval_hash_ex(from, start) \
  calc_hash_ex(from, start, (calc_bucket_t)calc_zval_ptr)


static void calc_zval_ptr(zval ** from TSRMLS_DC)
{
	EACCELERATOR_ALIGN(EAG(mem));
	EAG(mem) += sizeof(zval);
	calc_zval(*from TSRMLS_CC);
}

#ifdef ZEND_ENGINE_2
static void calc_property_info(zend_property_info * from TSRMLS_DC)
{
	EACCELERATOR_ALIGN(EAG(mem));
	EAG(mem) += sizeof(zend_property_info);
	calc_string(from->name, from->name_length + 1 TSRMLS_CC);
#ifdef INCLUDE_DOC_COMMENTS
#ifdef ZEND_ENGINE_2_1
     if (from->doc_comment != NULL) {
        calc_string(from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);
     }
#endif
#endif
}
#endif

/* Calculate the size of an HashTable */
static void calc_hash_int(HashTable * source, Bucket * start,
						  calc_bucket_t calc_bucket TSRMLS_DC)
{
	Bucket *p;

	if (source->nNumOfElements > 0) {
		if (!EAG(compress)) {
			EACCELERATOR_ALIGN(EAG(mem));
			EAG(mem) += source->nTableSize * sizeof(Bucket *);
		}
		p = start;
		while (p) {
			EACCELERATOR_ALIGN(EAG(mem));
			EAG(mem) += offsetof(Bucket, arKey) + p->nKeyLength;
			calc_bucket(p->pData TSRMLS_CC);
			p = p->pListNext;
		}
	}
}

void calc_zval(zval * zv TSRMLS_DC)
{
	switch (Z_TYPE_P(zv) & ~IS_CONSTANT_INDEX) {
	case IS_CONSTANT:
    case IS_OBJECT: /* object should have been serialized before storing them */
	case IS_STRING:
		calc_string(Z_STRVAL_P(zv), Z_STRLEN_P(zv) + 1 TSRMLS_CC);
		break;
	case IS_ARRAY:
	case IS_CONSTANT_ARRAY:
		if (Z_ARRVAL_P(zv) != NULL && Z_ARRVAL_P(zv) != &EG(symbol_table)) {
			EACCELERATOR_ALIGN(EAG(mem));
			EAG(mem) += sizeof(HashTable);
			calc_zval_hash(Z_ARRVAL_P(zv));
		}
		break;
	case IS_RESOURCE:
		DBG(ea_debug_error, ("[%d] EACCELERATOR can't cache resources\n", getpid()));
		zend_bailout();
	default:
		break;
	}
}

/* Calculate the size of an op_array */
static void calc_op_array(zend_op_array * from TSRMLS_DC)
{
	zend_op *opline;
	zend_op *end;

	if (from->type == ZEND_INTERNAL_FUNCTION) {
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += sizeof(zend_internal_function);
	} else if (from->type == ZEND_USER_FUNCTION) {
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += sizeof(ea_op_array);
	} else {
		DBG(ea_debug_error, ("[%d] EACCELERATOR can't cache function \"%s\"\n", getpid(), from->function_name));
		zend_bailout();
	}
#ifdef ZEND_ENGINE_2
	if (from->num_args > 0) {
		zend_uint i;
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += from->num_args * sizeof(zend_arg_info);
		for (i = 0; i < from->num_args; i++) {
			if (from->arg_info[i].name)
				calc_string(from->arg_info[i].name, from->arg_info[i].name_len + 1 TSRMLS_CC);
			if (from->arg_info[i].class_name)
				calc_string(from->arg_info[i].class_name, from->arg_info[i].class_name_len + 1 TSRMLS_CC);
		}
	}
#else
	if (from->arg_types != NULL)
		calc_string((char *) from->arg_types, (from->arg_types[0] + 1) * sizeof(zend_uchar) TSRMLS_CC);
#endif
	if (from->function_name != NULL)
		calc_string(from->function_name, strlen(from->function_name) + 1 TSRMLS_CC);
#ifdef ZEND_ENGINE_2
	if (from->scope != NULL) {
		// HOESH: the same problem?
		Bucket *q = CG(class_table)->pListHead;
		while (q != NULL) {
			if (*(zend_class_entry **) q->pData == from->scope) {
				calc_string(q->arKey, q->nKeyLength TSRMLS_CC);
				break;
			}
			q = q->pListNext;
		}
	}
#endif
	if (from->type == ZEND_INTERNAL_FUNCTION)
		return;

	if (from->opcodes != NULL) {
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += from->last * sizeof(zend_op);

		opline = from->opcodes;
		end = opline + from->last;
		EAG(compress) = 0;
		for (; opline < end; opline++) {
/*
      if (opline->result.op_type == IS_CONST) calc_zval(&opline->result.u.constant  TSRMLS_CC);
*/
			if (opline->op1.op_type == IS_CONST)
				calc_zval(&opline->op1.u.constant TSRMLS_CC);
			if (opline->op2.op_type == IS_CONST)
				calc_zval(&opline->op2.u.constant TSRMLS_CC);
		}
		EAG(compress) = 1;
	}
	if (from->brk_cont_array != NULL) {
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += sizeof(zend_brk_cont_element) * from->last_brk_cont;
	}
#ifdef ZEND_ENGINE_2
	/* HOESH: try & catch support */
	if (from->try_catch_array != NULL) {
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += sizeof(zend_try_catch_element) * from->last_try_catch;
	}
#endif
	if (from->static_variables != NULL) {
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += sizeof(HashTable);
		calc_zval_hash(from->static_variables);
	}
#ifdef ZEND_ENGINE_2_1
	if (from->vars != NULL) {
		int i;
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += sizeof(zend_compiled_variable) * from->last_var;
		for (i = 0; i < from->last_var; i ++) {
			calc_string(from->vars[i].name, from->vars[i].name_len+1 TSRMLS_CC);
		}
	}
#endif
	if (from->filename != NULL)
		calc_string(from->filename, strlen(from->filename) + 1 TSRMLS_CC);
#ifdef INCLUDE_DOC_COMMENTS
#ifdef ZEND_ENGINE_2
    if (from->doc_comment != NULL)
        calc_string(from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);
#endif
#endif
}

/* Calculate the size of a class entry */
static void calc_class_entry(zend_class_entry * from TSRMLS_DC)
{
	if (from->type != ZEND_USER_CLASS) {
		DBG(ea_debug_error, ("[%d] EACCELERATOR can't cache internal class \"%s\"\n", getpid(), from->name));
		zend_bailout();
	}
	EACCELERATOR_ALIGN(EAG(mem));
	EAG(mem) += sizeof(ea_class_entry);

	if (from->name != NULL)
		calc_string(from->name, from->name_length + 1 TSRMLS_CC);
	if (from->parent != NULL && from->parent->name)
		calc_string(from->parent->name, from->parent->name_length + 1 TSRMLS_CC);
#ifdef ZEND_ENGINE_2
	if (from->filename != NULL)
		calc_string(from->filename, strlen(from->filename) + 1 TSRMLS_CC);
#ifdef INCLUDE_DOC_COMMENTS
     if (from->doc_comment != NULL) 
        calc_string(from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);
#endif
	
    calc_zval_hash(&from->constants_table);
	calc_zval_hash(&from->default_properties);

	calc_hash(&from->properties_info, (calc_bucket_t) calc_property_info);

#  ifdef ZEND_ENGINE_2_1
	calc_zval_hash(&from->default_static_members);
	if ((from->static_members != NULL) && (from->static_members != &from->default_static_members)) {
#  else
	if (from->static_members != NULL) {
#  endif
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += sizeof(HashTable);
		calc_zval_hash(from->static_members);
	}
#else
	calc_zval_hash(&from->default_properties);
#endif
	calc_hash(&from->function_table, (calc_bucket_t) calc_op_array);
}

/* Calculate the size of a cache entry with its given op_array and function and
   class bucket */
int calc_size(char *key, zend_op_array * op_array, Bucket * f, Bucket * c TSRMLS_DC)
{
	Bucket *b;
	char *x;
	int len = strlen(key);
	EAG(compress) = 1;
	EAG(mem) = NULL;

	zend_hash_init(&EAG(strings), 0, NULL, NULL, 0);
	EAG(mem) += offsetof(ea_cache_entry, realfilename) + len + 1;
	zend_hash_add(&EAG(strings), key, len + 1, &key, sizeof(char *), NULL);
	b = c;
	while (b != NULL) {
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += offsetof(ea_fc_entry, htabkey) + b->nKeyLength;
		x = b->arKey;
		zend_hash_add(&EAG(strings), b->arKey, b->nKeyLength, &x, sizeof(char *), NULL);
		b = b->pListNext;
	}
	b = f;
	while (b != NULL) {
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += offsetof(ea_fc_entry, htabkey) + b->nKeyLength;
		x = b->arKey;
		zend_hash_add(&EAG(strings), b->arKey, b->nKeyLength, &x, sizeof(char *), NULL);
		b = b->pListNext;
	}
	while (c != NULL) {
#ifdef ZEND_ENGINE_2
		calc_class_entry(*(zend_class_entry **) c->pData TSRMLS_CC);
#else
		calc_class_entry((zend_class_entry *) c->pData TSRMLS_CC);
#endif
		c = c->pListNext;
	}
	while (f != NULL) {
		calc_op_array((zend_op_array *) f->pData TSRMLS_CC);
		f = f->pListNext;
	}
	calc_op_array(op_array TSRMLS_CC);
	EACCELERATOR_ALIGN(EAG(mem));
	zend_hash_destroy(&EAG(strings));
	return (size_t) EAG(mem);
}

static inline char *store_string(char *str, int len TSRMLS_DC)
{
	char *p;
	if (len > MAX_DUP_STR_LEN) {
		EACCELERATOR_ALIGN(EAG(mem));
		p = (char *) EAG(mem);
		EAG(mem) += len;
		memcpy(p, str, len);
	} else if (zend_hash_find(&EAG(strings), str, len, (void *) &p) == SUCCESS) {
		p = *(char **) p;
	} else {
		EACCELERATOR_ALIGN(EAG(mem));
		p = (char *) EAG(mem);
		EAG(mem) += len;
		memcpy(p, str, len);
		zend_hash_add(&EAG(strings), str, len, (void *) &p, sizeof(char *), NULL);
	}
	return p;
}

typedef void *(*store_bucket_t) (void *TSRMLS_DC);
typedef void *(*check_bucket_t) (Bucket*, zend_class_entry*);

#define store_hash_ex(to, from, start, store_bucket, check_bucket, from_ce) \
  store_hash_int(to, from, start, store_bucket, check_bucket, from_ce)

#define store_hash(to, from, store_bucket, check_bucket, from_ce) \
  store_hash_ex(to, from, (from)->pListHead, store_bucket, check_bucket, from_ce)

#define store_zval_hash(to, from) \
  store_hash(to, from, (store_bucket_t)store_zval_ptr, NULL, NULL)

#define store_zval_hash_ex(to, from, start) \
  store_hash_ex(to, from, start, (store_bucket_t)store_zval_ptr, NULL)

static zval *store_zval_ptr(zval * from TSRMLS_DC)
{
	zval *to;
	EACCELERATOR_ALIGN(EAG(mem));
	to = (zval *) EAG(mem);
	EAG(mem) += sizeof(zval);
	memcpy(to, from, sizeof(zval));
	store_zval(to TSRMLS_CC);
	return to;
}

static void store_hash_int(HashTable * target, HashTable * source, 
						   Bucket * start, store_bucket_t copy_bucket,
						   		   check_bucket_t check_bucket,
						   		   zend_class_entry * from_ce)
{
	Bucket *p, *np, *prev_p;
	TSRMLS_FETCH();

	memcpy(target, source, sizeof(HashTable));

	if (source->nNumOfElements > 0) {
		if (!EAG(compress)) {
			EACCELERATOR_ALIGN(EAG(mem));
			target->arBuckets = (Bucket **) EAG(mem);
			EAG(mem) += target->nTableSize * sizeof(Bucket *);
			memset(target->arBuckets, 0, target->nTableSize * sizeof(Bucket *));
		}

		target->pDestructor = NULL;
		target->persistent = 1;
		target->pListHead = NULL;
		target->pListTail = NULL;

		p = start;
		prev_p = NULL;
		np = NULL;
		while (p) {
			/* If a check function has been defined, run it */
			if (check_bucket) {
				/* If the check function returns ZEND_HASH_APPLY_REMOVE, don't store this record, skip over it */
				if(check_bucket(p, from_ce)) {
					p = p->pListNext;
					target->nNumOfElements--;
					/* skip to next itteration */
					continue;
				}
			}

			EACCELERATOR_ALIGN(EAG(mem));
			np = (Bucket *) EAG(mem);
			EAG(mem) += offsetof(Bucket, arKey) + p->nKeyLength;

			if (!EAG(compress)) {
				int nIndex = p->h % source->nTableSize;
				if (target->arBuckets[nIndex]) {
					np->pNext = target->arBuckets[nIndex];
					np->pLast = NULL;
					np->pNext->pLast = np;
				} else {
					np->pNext = NULL;
					np->pLast = NULL;
				}
				target->arBuckets[nIndex] = np;
			}
			np->h = p->h;
			np->nKeyLength = p->nKeyLength;

			if (p->pDataPtr == NULL) {
				np->pData = copy_bucket(p->pData TSRMLS_CC);
				np->pDataPtr = NULL;
			} else {
				np->pDataPtr = copy_bucket(p->pDataPtr TSRMLS_CC);
				np->pData = &np->pDataPtr;
			}

			np->pListLast = prev_p;
			np->pListNext = NULL;

			memcpy(np->arKey, p->arKey, p->nKeyLength);

			if (prev_p) {
				prev_p->pListNext = np;
			} else {
				target->pListHead = np;
			}
			prev_p = np;
			p = p->pListNext;
		}
		target->pListTail = np;
		target->pInternalPointer = target->pListHead;
	}
}

void store_zval(zval * zv TSRMLS_DC)
{
	switch (Z_TYPE_P(zv) & ~IS_CONSTANT_INDEX) {
	case IS_CONSTANT:
    case IS_OBJECT: /* object should have been serialized before storing them */
	case IS_STRING:
		Z_STRVAL_P(zv) = store_string(Z_STRVAL_P(zv), Z_STRLEN_P(zv) + 1 TSRMLS_CC);
		break;
	case IS_ARRAY:
	case IS_CONSTANT_ARRAY:
		if (Z_ARRVAL_P(zv) != NULL && Z_ARRVAL_P(zv) != &EG(symbol_table)) {
			HashTable *p;
			EACCELERATOR_ALIGN(EAG(mem));
			p = (HashTable *) EAG(mem);
			EAG(mem) += sizeof(HashTable);
			store_zval_hash(p, Z_ARRVAL_P(zv));
			Z_ARRVAL_P(zv) = p;
		}
		break;
	default:
		break;
	}
}

static ea_op_array *store_op_array(zend_op_array * from TSRMLS_DC)
{
	ea_op_array *to;
	zend_op *opline;
	zend_op *end;

	DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
#ifdef ZEND_ENGINE_2
	DBG(ea_debug_printf, (EA_DEBUG, "[%d] store_op_array: %s [scope=%s type=%x]\n", 
            getpid(), from->function_name ? from->function_name : "(top)",
			from->scope ? from->scope->name : "NULL"
			, from->type
		));
#else
	DBG(ea_debug_printf, (EA_DEBUG, "[%d] store_op_array: %s [scope=%s type=%x]\n", 
            getpid(), from->function_name ? from->function_name : "(top)",
			"NULL"
			, from->type
		));
#endif

	if (from->type == ZEND_INTERNAL_FUNCTION) {
		EACCELERATOR_ALIGN(EAG(mem));
		to = (ea_op_array *) EAG(mem);
		EAG(mem) += offsetof(ea_op_array, opcodes);
	} else if (from->type == ZEND_USER_FUNCTION) {
		EACCELERATOR_ALIGN(EAG(mem));
		to = (ea_op_array *) EAG(mem);
		EAG(mem) += sizeof(ea_op_array);
	} else {
		return NULL;
	}

	to->type = from->type;
#ifdef ZEND_ENGINE_2
	to->num_args = from->num_args;
	to->required_num_args = from->required_num_args;
	if (from->num_args > 0) {
		zend_uint i;
		EACCELERATOR_ALIGN(EAG(mem));
		to->arg_info = (zend_arg_info *) EAG(mem);
		EAG(mem) += from->num_args * sizeof(zend_arg_info);
		for (i = 0; i < from->num_args; i++) {
			if (from->arg_info[i].name) {
				to->arg_info[i].name = store_string(from->arg_info[i].name, from->arg_info[i].name_len + 1 TSRMLS_CC);
				to->arg_info[i].name_len = from->arg_info[i].name_len;
			}
			if (from->arg_info[i].class_name) {
				to->arg_info[i].class_name = store_string(from->arg_info[i].class_name, from->arg_info[i].class_name_len + 1 TSRMLS_CC);
				to->arg_info[i].class_name_len = from->arg_info[i].class_name_len;
			}
#  ifdef ZEND_ENGINE_2_1
			/* php 5.1 introduces this in zend_arg_info for array type hinting */
			to->arg_info[i].array_type_hint = from->arg_info[i].array_type_hint;
#  endif
			to->arg_info[i].allow_null = from->arg_info[i].allow_null;
			to->arg_info[i].pass_by_reference = from->arg_info[i].pass_by_reference;
			to->arg_info[i].return_reference = from->arg_info[i].return_reference;
		}
	}
	to->pass_rest_by_reference = from->pass_rest_by_reference;
#else
	if (from->arg_types != NULL)
		to->arg_types = (unsigned char *) store_string((char *) from->arg_types, (from->arg_types[0] + 1) * sizeof(zend_uchar) TSRMLS_CC);
#endif
	if (from->function_name != NULL)
		to->function_name = store_string(from->function_name, strlen(from->function_name) + 1 TSRMLS_CC);
#ifdef ZEND_ENGINE_2
	to->fn_flags = from->fn_flags;
	to->scope_name = NULL;
	to->scope_name_len = 0;
	if (from->scope != NULL) {
		Bucket *q = CG(class_table)->pListHead;
		while (q != NULL) {
			if (*(zend_class_entry **) q->pData == from->scope) {
				to->scope_name = store_string(q->arKey, q->nKeyLength TSRMLS_CC);
				to->scope_name_len = q->nKeyLength - 1;

				DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
				DBG(ea_debug_printf, (EA_DEBUG, 
                        "[%d]                 find scope '%s' in CG(class_table) save hashkey '%s' [%08x] as to->scope_name\n",
						getpid(), from->scope->name ? from->scope->name : "NULL", q->arKey, to->scope_name));
				break;
			}
			q = q->pListNext;
		}
	    if (to->scope_name == NULL) {
		    DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
		    DBG(ea_debug_printf, (EA_DEBUG,
						"[%d]                 could not find scope '%s' in CG(class_table), saving it to NULL\n",
						getpid(), from->scope->name ? from->scope->name : "NULL"));
	    }
    }
#endif

	if (from->type == ZEND_INTERNAL_FUNCTION) {
#ifdef ZEND_ENGINE_2
		/* zend_internal_function also contains return_reference in ZE2 */
		to->return_reference = from->return_reference;
#endif		
	        return to;
	}
    
	to->opcodes = from->opcodes;
	to->last = from->last;
	to->T = from->T;
	to->brk_cont_array = from->brk_cont_array;
	to->last_brk_cont = from->last_brk_cont;
#ifdef ZEND_ENGINE_2
	to->try_catch_array = from->try_catch_array;
	to->last_try_catch = from->last_try_catch;
	to->uses_this = from->uses_this;
#else
	to->uses_globals = from->uses_globals;
#endif
	to->static_variables = from->static_variables;
	to->return_reference = from->return_reference;
	to->filename = from->filename;

	if (from->opcodes != NULL) {
		EACCELERATOR_ALIGN(EAG(mem));
		to->opcodes = (zend_op *) EAG(mem);
		EAG(mem) += from->last * sizeof(zend_op);
		memcpy(to->opcodes, from->opcodes, from->last * sizeof(zend_op));

		opline = to->opcodes;
		end = opline + to->last;
		EAG(compress) = 0;
		for (; opline < end; opline++) {
			/*
			   if (opline->result.op_type == IS_CONST) 
			   store_zval(&opline->result.u.constant TSRMLS_CC);
			 */
			if (opline->op1.op_type == IS_CONST)
				store_zval(&opline->op1.u.constant TSRMLS_CC);
			if (opline->op2.op_type == IS_CONST)
				store_zval(&opline->op2.u.constant TSRMLS_CC);
#ifdef ZEND_ENGINE_2
			switch (opline->opcode) {
			case ZEND_JMP:
				opline->op1.u.jmp_addr = to->opcodes + (opline->op1.u.jmp_addr - from->opcodes);
				break;
			case ZEND_JMPZ:
			case ZEND_JMPNZ:
			case ZEND_JMPZ_EX:
			case ZEND_JMPNZ_EX:
				opline->op2.u.jmp_addr = to->opcodes + (opline->op2.u.jmp_addr - from->opcodes);
				break;
			}
#endif
		}
		EAG(compress) = 1;
	}
	if (from->brk_cont_array != NULL) {
		EACCELERATOR_ALIGN(EAG(mem));
		to->brk_cont_array = (zend_brk_cont_element *) EAG(mem);
		EAG(mem) += sizeof(zend_brk_cont_element) * from->last_brk_cont;
		memcpy(to->brk_cont_array, from->brk_cont_array, sizeof(zend_brk_cont_element) * from->last_brk_cont);
	} else {
		to->last_brk_cont = 0;
	}
#ifdef ZEND_ENGINE_2
	if (from->try_catch_array != NULL) {
		EACCELERATOR_ALIGN(EAG(mem));
		to->try_catch_array = (zend_try_catch_element *) EAG(mem);
		EAG(mem) += sizeof(zend_try_catch_element) * from->last_try_catch;
		memcpy(to->try_catch_array, from->try_catch_array, sizeof(zend_try_catch_element) * from->last_try_catch);
	} else {
		to->last_try_catch = 0;
	}
#endif
	if (from->static_variables != NULL) {
		EACCELERATOR_ALIGN(EAG(mem));
		to->static_variables = (HashTable *) EAG(mem);
		EAG(mem) += sizeof(HashTable);
		store_zval_hash(to->static_variables, from->static_variables);
	}
#ifdef ZEND_ENGINE_2_1
	if (from->vars != NULL) {
        	int i;
	        EACCELERATOR_ALIGN(EAG(mem));
	        to->last_var = from->last_var;
	        to->vars = (zend_compiled_variable*)EAG(mem);
	        EAG(mem) += sizeof(zend_compiled_variable) * from->last_var;
			memcpy(to->vars, from->vars, sizeof(zend_compiled_variable) * from->last_var);
	        for (i = 0; i < from->last_var; i ++) {
	        	to->vars[i].name = store_string(from->vars[i].name, from->vars[i].name_len+1 TSRMLS_CC);
		}
	} else {
		to->last_var = 0;
	        to->vars = NULL;
	}
#endif
	if (from->filename != NULL) {
		to->filename = store_string(from->filename, strlen(from->filename) + 1 TSRMLS_CC);
	}
#ifdef ZEND_ENGINE_2
	to->line_start = from->line_start;
	to->line_end = from->line_end;
#ifdef INCLUDE_DOC_COMMENTS
    to->doc_comment_len = from->doc_comment_len;
    if (from->doc_comment != NULL)
        to->doc_comment = store_string(from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);
#endif
#endif
	return to;
}

#ifdef ZEND_ENGINE_2
static zend_property_info *store_property_info(zend_property_info * from TSRMLS_DC)
{
	zend_property_info *to;
	EACCELERATOR_ALIGN(EAG(mem));
	to = (zend_property_info *) EAG(mem);
	EAG(mem) += sizeof(zend_property_info);
	memcpy(to, from, sizeof(zend_property_info));
	to->name = store_string(from->name, from->name_length + 1 TSRMLS_CC);
#ifdef ZEND_ENGINE_2_1
#ifdef INCLUDE_DOC_COMMENTS
to->doc_comment_len = from->doc_comment_len; 
if (from->doc_comment != NULL) { 
       to->doc_comment = store_string(from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);
}
#else
	to->doc_comment_len = 0;
	to->doc_comment = NULL;
#endif
#endif
	return to;
}

/* 
 * The following two functions handle access checking of properties (public/private/protected) 
 * and control proper inheritance during copying of the properties_info and (default_)static_members hashes
 *
 * Both functions return ZEND_HASH_APPLY_REMOVE if the property to be copied needs to be skipped, or
 * ZEND_HASH_APPLY_KEEP if the property needs to be copied over into the cache.
 *  
 * If the property is skipped due to access restrictions, or it needs inheritance of its value from the
 * parent, the restore phase will take care of that.
 *
 * Most of the logic behind all this can be found in zend_compile.c, functions zend_do_inheritance and
 * zend_do_inherit_property_access_check
*/
static int store_property_access_check(Bucket * p, zend_class_entry * from_ce)
{
	zend_class_entry *from = from_ce;
	zend_class_entry *parent = from->parent;
	
	if (parent) {
		// hra: TODO - do some usefull stuff :)
		// check for ACC_PRIVATE etc.
		// for now, just return keep
	}
	return ZEND_HASH_APPLY_KEEP;
}

static int store_static_member_access_check(Bucket * p, zend_class_entry * from_ce)
{
	zend_class_entry *from = from_ce;
	zend_class_entry *parent = from->parent;
    union {
        zend_property_info *v;
        void *ptr;
    } pinfo, cinfo;
    union {
    	zval **v;
        void *ptr;
    } pprop, cprop;
	char *mname, *cname = NULL;

    cprop.v = p->pData;
	/* Check if this is a parent class. If so, copy unconditionally */
	if (parent) {
		/* unpack the \0classname\0membername\0 style property name to seperate vars */
#ifdef ZEND_ENGINE_2_2
		zend_unmangle_property_name(p->arKey, p->nKeyLength, &cname, &mname);
#else
		zend_unmangle_property_name(p->arKey, &cname, &mname);
#endif
	
		/* lookup the member's info in parent and child */
		if((zend_hash_find(&parent->properties_info, mname, strlen(mname)+1, &pinfo.ptr) == SUCCESS) &&
			(zend_hash_find(&from->properties_info, mname, strlen(mname)+1, &cinfo.ptr) == SUCCESS)) {
			/* don't copy this static property if protected in parent and static public in child.
			   inheritance will handle this properly on restore */
			if(cinfo.v->flags & ZEND_ACC_STATIC && (pinfo.v->flags & ZEND_ACC_PROTECTED && cinfo.v->flags & ZEND_ACC_PUBLIC)) {
				return ZEND_HASH_APPLY_REMOVE;
			}
			/* If the static member points to the same value in parent and child, remove for proper inheritance during restore */
#  ifdef ZEND_ENGINE_2_1
			if(zend_hash_quick_find(&parent->default_static_members, p->arKey, p->nKeyLength, p->h, &pprop.ptr) == SUCCESS) {
#  else
			if(zend_hash_quick_find(parent->static_members, p->arKey, p->nKeyLength, p->h, &pprop.ptr) == SUCCESS) {
#  endif
				if(*pprop.v == *cprop.v) {
					return ZEND_HASH_APPLY_REMOVE;
				}
			}
		}
	}
	return ZEND_HASH_APPLY_KEEP;
}

/*
 * This function makes sure that functions/methods that are not in the scope of the current
 * class being stored, do not get copied to the function_table hash. This makes sure they
 * get properly inherited on restore by zend_do_inheritance
 *
 * If we dont do this, it will result in broken inheritance, problems with final methods
 * (e.g. "Cannot override final method") and the like.
 */
static int store_function_inheritance_check(Bucket * p, zend_class_entry * from_ce)
{
	zend_class_entry *from = from_ce;
	zend_function *zf = p->pData;
	
	if (zf->common.scope == from) {
		return ZEND_HASH_APPLY_KEEP;
	}
	return ZEND_HASH_APPLY_REMOVE;
}
#endif

static ea_class_entry *store_class_entry(zend_class_entry * from TSRMLS_DC)
{
	ea_class_entry *to;
	unsigned int i;

	EACCELERATOR_ALIGN(EAG(mem));
	to = (ea_class_entry *) EAG(mem);
	EAG(mem) += sizeof(ea_class_entry);
	to->type = from->type;
	to->name = NULL;
	to->name_length = from->name_length;
	to->parent = NULL;
#ifdef ZEND_ENGINE_2
	to->ce_flags = from->ce_flags;
	to->static_members = NULL;

	/*
	 * Scan the interfaces looking for the first one which isn't 0
	 * This is the first inherited interface and should not be counted in the stored object
	 */
	for (i = 0 ; i < from->num_interfaces ; i++) {
		if (from->interfaces[i] != 0) {
			break;
		}
	}
	to->num_interfaces = i;
	DBG(ea_debug_printf, (EA_DEBUG, "from->num_interfaces=%d, to->num_interfaces=%d\n", from->num_interfaces, to->num_interfaces));

	/*
	 * hrak: no need to really store the interfaces since these get populated
	 * at/after restore by zend_do_inheritance and ZEND_ADD_INTERFACE
	 */
#endif

	DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
	DBG(ea_debug_printf, (EA_DEBUG, "[%d] store_class_entry: %s parent was '%s'\n",
					getpid(), from->name ? from->name : "(top)",
					from->parent ? from->parent->name : "NULL"));
#ifdef DEBUG
	EAG(xpad)++;
#endif

	if (from->name != NULL)
		to->name = store_string(from->name, from->name_length + 1 TSRMLS_CC);
	if (from->parent != NULL && from->parent->name)
		to->parent = store_string(from->parent->name, from->parent->name_length + 1 TSRMLS_CC);

#ifdef ZEND_ENGINE_2
	to->line_start = from->line_start;
	to->line_end = from->line_end;
#ifdef INCLUDE_DOC_COMMENTS
    to->doc_comment_len = from->doc_comment_len;
#endif

	if (from->filename != NULL)
		to->filename = store_string(from->filename, strlen(from->filename) + 1 TSRMLS_CC);
#ifdef INCLUDE_DOC_COMMENTS
    if (from->doc_comment != NULL)
        to->doc_comment = store_string(from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);
#endif

	store_zval_hash(&to->constants_table, &from->constants_table);
	store_zval_hash(&to->default_properties, &from->default_properties);
	//store_hash(&to->properties_info, &from->properties_info, (store_bucket_t) store_property_info, NULL, NULL);
	store_hash(&to->properties_info, &from->properties_info, (store_bucket_t) store_property_info, (check_bucket_t) store_property_access_check, from);
#  ifdef ZEND_ENGINE_2_1
	if((from->static_members != NULL) && (from->static_members != &from->default_static_members)) {
		store_zval_hash(&to->default_static_members, &from->default_static_members);
		EACCELERATOR_ALIGN(EAG(mem));
		to->static_members = (HashTable *) EAG(mem);
		EAG(mem) += sizeof(HashTable);
		store_hash(to->static_members, from->static_members, (store_bucket_t) store_zval_ptr, (check_bucket_t) store_static_member_access_check, from);
	} else {
		/*EACCELERATOR_ALIGN(EAG(mem));
		to->static_members = (HashTable *) EAG(mem);
		EAG(mem) += sizeof(HashTable);*/
		store_hash(&to->default_static_members, &from->default_static_members, (store_bucket_t) store_zval_ptr, (check_bucket_t) store_static_member_access_check, from);
		to->static_members = &to->default_static_members;
	}
#  elif defined(ZEND_ENGINE_2) && !defined(ZEND_ENGINE_2_1)
	/* for php-5.0 */
	if(from->static_members != NULL) {
		EACCELERATOR_ALIGN(EAG(mem));
		to->static_members = (HashTable *) EAG(mem);
		EAG(mem) += sizeof(HashTable);
		store_hash(to->static_members, from->static_members, (store_bucket_t) store_zval_ptr, (check_bucket_t) store_static_member_access_check, from);
	}	
#  endif
#else
	store_zval_hash(&to->default_properties, &from->default_properties);
#endif

#ifdef ZEND_ENGINE_2
	store_hash(&to->function_table, &from->function_table, (store_bucket_t) store_op_array, (check_bucket_t) store_function_inheritance_check, from);
#else
	store_hash(&to->function_table, &from->function_table, (store_bucket_t) store_op_array, NULL, NULL);
#endif

#ifdef DEBUG
	EAG(xpad)--;
#endif

	return to;
}

/* Create a cache entry from the given op_array, functions and classes of a
   script */
ea_cache_entry *eaccelerator_store_int (char *key, int len, 
        zend_op_array *op_array, Bucket *f, Bucket *c TSRMLS_DC)
{
    ea_cache_entry *p;
    ea_fc_entry *fc;
    ea_fc_entry *q;
    char *x;

    DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
    DBG(ea_debug_printf, (EA_DEBUG, "[%d] eaccelerator_store_int: key='%s'\n", 
                getpid (), key));

    EAG (compress) = 1;
    zend_hash_init (&EAG (strings), 0, NULL, NULL, 0);
    p = (ea_cache_entry *) EAG (mem);
    EAG (mem) += offsetof (ea_cache_entry, realfilename) + len + 1;

    p->nhits = 0;
    p->use_cnt = 0;
    p->removed = 0;
    p->f_head = NULL;
    p->c_head = NULL;
    memcpy (p->realfilename, key, len + 1);
    x = p->realfilename;
    zend_hash_add (&EAG (strings), key, len + 1, &x, sizeof (char *), NULL);

    q = NULL;
    while (c != NULL) {
        DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
        DBG(ea_debug_printf, (EA_DEBUG, 
                    "[%d] eaccelerator_store_int:     class hashkey=", getpid ()));
        DBG(ea_debug_binary_print, (EA_DEBUG, c->arKey, c->nKeyLength));

        EACCELERATOR_ALIGN (EAG (mem));
        fc = (ea_fc_entry *) EAG (mem);
        EAG (mem) += offsetof (ea_fc_entry, htabkey) + c->nKeyLength;
        memcpy (fc->htabkey, c->arKey, c->nKeyLength);
        fc->htablen = c->nKeyLength;
        fc->next = NULL;
#ifdef ZEND_ENGINE_2
        fc->fc = *(zend_class_entry **) c->pData;
#else
        fc->fc = c->pData;
#endif
        c = c->pListNext;
        x = fc->htabkey;
        zend_hash_add (&EAG (strings), fc->htabkey, fc->htablen, &x, 
                sizeof (char *), NULL);
        if (q == NULL) {
            p->c_head = fc;
        } else {
            q->next = fc;
        }
        q = fc;
    }

    q = NULL;
    while (f != NULL) {
        DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
        DBG(ea_debug_printf, (EA_DEBUG, 
                    "[%d] eaccelerator_store_int:     function hashkey='%s'\n", getpid (), f->arKey));

        EACCELERATOR_ALIGN (EAG (mem));
        fc = (ea_fc_entry *) EAG (mem);
        EAG (mem) += offsetof (ea_fc_entry, htabkey) + f->nKeyLength;
        memcpy (fc->htabkey, f->arKey, f->nKeyLength);
        fc->htablen = f->nKeyLength;
        fc->next = NULL;
        fc->fc = f->pData;
        f = f->pListNext;
        x = fc->htabkey;
        zend_hash_add (&EAG (strings), fc->htabkey, fc->htablen, &x,
                sizeof (char *), NULL);
        if (q == NULL) {
            p->f_head = fc;
        } else {
            q->next = fc;
        }
        q = fc;
    }

    q = p->c_head;
    while (q != NULL) {
        q->fc = store_class_entry ((zend_class_entry *) q->fc TSRMLS_CC);
        q = q->next;
    }

    q = p->f_head;
    while (q != NULL) {
        q->fc = store_op_array ((zend_op_array *) q->fc TSRMLS_CC);
        q = q->next;
    }
    p->op_array = store_op_array (op_array TSRMLS_CC);

    zend_hash_destroy (&EAG (strings));
    return p;
}

#endif /* HAVE_EACCELERATOR */
