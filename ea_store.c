/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 - 2005 eAccelerator                               |
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
   | Author(s): Dmitry Stogov <dstogov@users.sourceforge.net>             |
   |            Seung Woo <segv@sayclub.com>                              |
   |            Everaldo Canuto <everaldo_canuto@yahoo.com.br>            |
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
	if (len > MAX_DUP_STR_LEN || zend_hash_add(&EAG(strings), str, len,
											   &str, sizeof(char *),
											   NULL) == SUCCESS) {
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
}

/* Calculate the size of a point to a class entry */
static void calc_class_entry_ptr(zend_class_entry ** from TSRMLS_DC)
{
	calc_class_entry(*from TSRMLS_CC);
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
	switch (zv->type & ~IS_CONSTANT_INDEX) {
	case IS_CONSTANT:
	case IS_STRING:
		if (zv->value.str.val == NULL || zv->value.str.val == empty_string || zv->value.str.len == 0) {
		} else {
			calc_string(zv->value.str.val, zv->value.str.len + 1 TSRMLS_CC);
		}
		break;
	case IS_ARRAY:
	case IS_CONSTANT_ARRAY:
		if (zv->value.ht != NULL && zv->value.ht != &EG(symbol_table)) {
			EACCELERATOR_ALIGN(EAG(mem));
			EAG(mem) += sizeof(HashTable);
			calc_zval_hash(zv->value.ht);
		}
		break;
	case IS_OBJECT:
#ifndef ZEND_ENGINE_2
		if (zv->value.obj.ce != NULL) {
			zend_class_entry *ce = zv->value.obj.ce;
			if (!EAG(compress)) {
				ea_debug_error("[%d] EACCELERATOR can't cache objects\n", getpid());
				zend_bailout();
			}
			while (ce != NULL) {
				if (ce->type != ZEND_USER_CLASS && strcmp(ce->name, "stdClass") != 0) {
					ea_debug_error("[%d] EACCELERATOR can't cache objects\n", getpid());
					zend_bailout();
				}
				ce = ce->parent;
			}
			calc_string(zv->value.obj.ce->name, zv->value.obj.ce->name_length + 1 TSRMLS_CC);
		}
		if (zv->value.obj.properties != NULL) {
			EACCELERATOR_ALIGN(EAG(mem));
			EAG(mem) += sizeof(HashTable);
			calc_zval_hash(zv->value.obj.properties);
		}
#endif
		return;
	case IS_RESOURCE:
		ea_debug_error("[%d] EACCELERATOR can't cache resources\n", getpid());
		zend_bailout();
	default:
		break;
	}
}

/* Calculate the size of an op_array */
void calc_op_array(zend_op_array * from TSRMLS_DC)
{
	zend_op *opline;
	zend_op *end;

	if (from->type == ZEND_INTERNAL_FUNCTION) {
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += sizeof(zend_internal_function);
	} else if (from->type == ZEND_USER_FUNCTION) {
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += sizeof(eaccelerator_op_array);
	} else {
		ea_debug_error("[%d] EACCELERATOR can't cache function \"%s\"\n", getpid(), from->function_name);
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
	if (from->filename != NULL)
		calc_string(from->filename, strlen(from->filename) + 1 TSRMLS_CC);
#ifdef ZEND_ENGINE_2
	if (from->doc_comment != NULL)
		calc_string(from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);
#endif
}

/* Calculate the size of a class entry */
void calc_class_entry(zend_class_entry * from TSRMLS_DC)
{
	if (from->type != ZEND_USER_CLASS) {
		ea_debug_error("[%d] EACCELERATOR can't cache internal class \"%s\"\n", getpid(), from->name);
		zend_bailout();
	}
	EACCELERATOR_ALIGN(EAG(mem));
	EAG(mem) += sizeof(eaccelerator_class_entry);

	if (from->name != NULL)
		calc_string(from->name, from->name_length + 1 TSRMLS_CC);
	if (from->parent != NULL && from->parent->name)
		calc_string(from->parent->name, from->parent->name_length + 1 TSRMLS_CC);
#ifdef ZEND_ENGINE_2
#if 0
	// what's problem. why from->interfaces[i] == 0x5a5a5a5a ?
	for (i = 0; i < from->num_interfaces; i++) {
		if (from->interfaces[i]) {
			calc_string(from->interfaces[i]->name,
						from->interfaces[i]->name_length);
		}
	}
#endif
	if (from->filename != NULL)
		calc_string(from->filename, strlen(from->filename) + 1 TSRMLS_CC);
	if (from->doc_comment != NULL)
		calc_string(from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);

	calc_zval_hash(&from->constants_table);
	calc_zval_hash(&from->default_properties);
	calc_hash(&from->properties_info, (calc_bucket_t) calc_property_info);
	if (from->static_members != NULL) {
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
int calc_size(char *key, zend_op_array * op_array,
			  Bucket * f, Bucket * c TSRMLS_DC)
{
	Bucket *b;
	char *x;
	int len = strlen(key);
	EAG(compress) = 1;
	EAG(mem) = NULL;

	zend_hash_init(&EAG(strings), 0, NULL, NULL, 0);
	EAG(mem) += offsetof(mm_cache_entry, realfilename) + len + 1;
	zend_hash_add(&EAG(strings), key, len + 1, &key, sizeof(char *), NULL);
	b = c;
	while (b != NULL) {
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += offsetof(mm_fc_entry, htabkey) + b->nKeyLength;
		x = b->arKey;
		zend_hash_add(&EAG(strings), b->arKey, b->nKeyLength, &x, sizeof(char *), NULL);
		b = b->pListNext;
	}
	b = f;
	while (b != NULL) {
		EACCELERATOR_ALIGN(EAG(mem));
		EAG(mem) += offsetof(mm_fc_entry, htabkey) + b->nKeyLength;
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

/** Functions to store a script **/
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

#define store_hash_ex(to, from, start, store_bucket) \
  store_hash_int(to, from, start, store_bucket TSRMLS_CC)

#define store_hash(to, from, store_bucket) \
  store_hash_ex(to, from, (from)->pListHead, store_bucket)

#define store_zval_hash(to, from) \
  store_hash(to, from, (store_bucket_t)store_zval_ptr)

#define store_zval_hash_ex(to, from, start) \
  store_hash_ex(to, from, start, (store_bucket_t)store_zval_ptr)

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
						   Bucket * start, store_bucket_t copy_bucket TSRMLS_DC)
{
	Bucket *p, *np, *prev_p;

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
	switch (zv->type & ~IS_CONSTANT_INDEX) {
	case IS_CONSTANT:
	case IS_STRING:
		if (zv->value.str.val == NULL ||
			zv->value.str.val == empty_string || zv->value.str.len == 0) {
			zv->value.str.val = empty_string;
			zv->value.str.len = 0;
		} else {
			zv->value.str.val = store_string(zv->value.str.val, zv->value.str.len + 1 TSRMLS_CC);
		}
		break;
	case IS_ARRAY:
	case IS_CONSTANT_ARRAY:
		if (zv->value.ht != NULL && zv->value.ht != &EG(symbol_table)) {
			HashTable *p;
			EACCELERATOR_ALIGN(EAG(mem));
			p = (HashTable *) EAG(mem);
			EAG(mem) += sizeof(HashTable);
			store_zval_hash(p, zv->value.ht);
			zv->value.ht = p;
		}
		break;
	case IS_OBJECT:
		if (!EAG(compress)) {
			return;
		}
#ifndef ZEND_ENGINE_2
		if (zv->value.obj.ce != NULL) {
			char *s = store_string(zv->value.obj.ce->name, zv->value.obj.ce->name_length + 1 TSRMLS_CC);
			zend_str_tolower(s, zv->value.obj.ce->name_length);
			zv->value.obj.ce = (zend_class_entry *) s;
		}
		if (zv->value.obj.properties != NULL) {
			HashTable *p;
			EACCELERATOR_ALIGN(EAG(mem));
			p = (HashTable *) EAG(mem);
			EAG(mem) += sizeof(HashTable);
			store_zval_hash(p, zv->value.obj.properties);
			zv->value.obj.properties = p;
		}
#endif
	default:
		break;
	}
}

eaccelerator_op_array *store_op_array(zend_op_array * from TSRMLS_DC)
{
	eaccelerator_op_array *to;
	zend_op *opline;
	zend_op *end;

	ea_debug_pad(EA_DEBUG TSRMLS_CC);
	ea_debug_printf(EA_DEBUG, "[%d] store_op_array: %s [scope=%s]\n", 
            getpid(), from->function_name ? from->function_name : "(top)",
#ifdef ZEND_ENGINE_2
			from->scope ? from->scope->name : "NULL"
#else
			"NULL"
#endif
		);

	if (from->type == ZEND_INTERNAL_FUNCTION) {
		EACCELERATOR_ALIGN(EAG(mem));
		to = (eaccelerator_op_array *) EAG(mem);
		EAG(mem) += offsetof(eaccelerator_op_array, opcodes);
	} else if (from->type == ZEND_USER_FUNCTION) {
		EACCELERATOR_ALIGN(EAG(mem));
		to = (eaccelerator_op_array *) EAG(mem);
		EAG(mem) += sizeof(eaccelerator_op_array);
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

				ea_debug_pad(EA_DEBUG TSRMLS_CC);
				ea_debug_printf(EA_DEBUG, 
                        "[%d]                 find scope '%s' in CG(class_table) save hashkey '%s' [%08x] as to->scope_name\n",
						getpid(), from->scope->name ? from->scope->name : "NULL", q->arKey, to->scope_name);
				break;
			}
			q = q->pListNext;
		}
	    if (to->scope_name == NULL) {
		    ea_debug_pad(EA_DEBUG TSRMLS_CC);
		    ea_debug_printf(EA_DEBUG,
						"[%d]                 could not find scope '%s' in CG(class_table), saving it to NULL\n",
						getpid(), from->scope->name ? from->scope->name : "NULL");
	    }
    }
#endif

	if (from->type == ZEND_INTERNAL_FUNCTION)
        return to;
    
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
	if (from->filename != NULL) {
		to->filename = store_string(to->filename, strlen(from->filename) + 1 TSRMLS_CC);
	}
#ifdef ZEND_ENGINE_2
	to->line_start = from->line_start;
	to->line_end = from->line_end;
	to->doc_comment_len = from->doc_comment_len;
	if (from->doc_comment != NULL)
		to->doc_comment = store_string(from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);
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
	return to;
}
#endif

eaccelerator_class_entry *store_class_entry(zend_class_entry * from TSRMLS_DC)
{
	eaccelerator_class_entry *to;
	EACCELERATOR_ALIGN(EAG(mem));
	to = (eaccelerator_class_entry *) EAG(mem);
	EAG(mem) += sizeof(eaccelerator_class_entry);
	to->type = from->type;
	to->name = NULL;
	to->name_length = from->name_length;
	to->parent = NULL;
#ifdef ZEND_ENGINE_2
	to->ce_flags = from->ce_flags;
	to->static_members = NULL;
	to->num_interfaces = from->num_interfaces;

#if 0
	// i need to check more. why this field is null.
	//
	for (i = 0; i < from->num_interfaces; i++) {
		if (from->interfaces[i]) {
			to->interfaces[i] =
				store_string(from->interfaces[i]->name,
							 from->interfaces[i]->name_length);
		}
	}
#endif

#endif

	ea_debug_pad(EA_DEBUG TSRMLS_CC);
	ea_debug_printf(EA_DEBUG, "[%d] store_class_entry: %s parent was '%s'\n",
					getpid(), from->name ? from->name : "(top)",
					from->parent ? from->parent->name : "NULL");
#ifdef DEBUG
	EAG(xpad)++;
#endif

	if (from->name != NULL)
		to->name = store_string(from->name, from->name_length + 1 TSRMLS_CC);
	if (from->parent != NULL && from->parent->name)
		to->parent = store_string(from->parent->name, from->parent->name_length + 1 TSRMLS_CC);

/*
  if (!from->constants_updated) {
    zend_hash_apply_with_argument(&from->default_properties, (apply_func_arg_t) zval_update_constant, (void *) 1 TSRMLS_CC);
    to->constants_updated = 1;
  }
*/
#ifdef ZEND_ENGINE_2
	to->line_start = from->line_start;
	to->line_end = from->line_end;
	to->doc_comment_len = from->doc_comment_len;
	to->iterator_funcs = from->iterator_funcs;
	to->create_object = from->create_object;
	to->get_iterator = from->get_iterator;
	to->interface_gets_implemented = from->interface_gets_implemented;

	if (from->filename != NULL)
		to->filename = store_string(from->filename, strlen(from->filename) + 1 TSRMLS_CC);
	if (from->doc_comment != NULL)
		to->doc_comment = store_string(from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);

	store_zval_hash(&to->constants_table, &from->constants_table);
	store_zval_hash(&to->default_properties, &from->default_properties);
	store_hash(&to->properties_info, &from->properties_info, (store_bucket_t) store_property_info);
	if (from->static_members != NULL) {
		EACCELERATOR_ALIGN(EAG(mem));
		to->static_members = (HashTable *) EAG(mem);
		EAG(mem) += sizeof(HashTable);
		store_zval_hash(to->static_members, from->static_members);
	}
#else
	store_zval_hash(&to->default_properties, &from->default_properties);
#endif
	store_hash(&to->function_table, &from->function_table, (store_bucket_t) store_op_array);

#ifdef DEBUG
	EAG(xpad)--;
#endif

	return to;
}

#endif /* HAVE_EACCELERATOR */
