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

#include "debug.h"
#include "ea_restore.h"
#include "opcodes.h"
#include "zend.h"
#include "zend_API.h"
#include "zend_extensions.h"

#ifndef INCOMPLETE_CLASS
#  define INCOMPLETE_CLASS "__PHP_Incomplete_Class"
#endif
#ifndef MAGIC_MEMBER
#  define MAGIC_MEMBER "__PHP_Incomplete_Class_Name"
#endif

extern zend_extension *ZendOptimizer;
#if HARDENING_PATCH_HASH_PROTECT
extern unsigned int zend_hash_canary;
#endif

#ifdef ZEND_ENGINE_2
/* pointer to the properties_info hashtable destructor */
dtor_func_t properties_info_dtor = NULL;

/* This function creates a dummy class entry to steal the pointer to the
 * properties_info hashtable destructor because it's declared static */
dtor_func_t get_zend_destroy_property_info(TSRMLS_D)
{
	dtor_func_t property_dtor;
	zend_class_entry dummy_class_entry;
	dummy_class_entry.type = ZEND_USER_CLASS;

	zend_initialize_class_data(&dummy_class_entry, 1 TSRMLS_CC);

	property_dtor = dummy_class_entry.properties_info.pDestructor;

	zend_hash_destroy(&dummy_class_entry.default_properties);
	zend_hash_destroy(&dummy_class_entry.properties_info);
	zend_hash_destroy(dummy_class_entry.static_members);
	zend_hash_destroy(&dummy_class_entry.function_table);
	FREE_HASHTABLE(dummy_class_entry.static_members);
	zend_hash_destroy(&dummy_class_entry.constants_table);

	return property_dtor;
}
#endif

/******************************************************************************/
/* Functions to restore a cached script from file cache                       */
/******************************************************************************/

typedef void (*fixup_bucket_t) (void *TSRMLS_DC);

#define fixup_zval_hash(from) \
    fixup_hash(from, (fixup_bucket_t)fixup_zval TSRMLS_CC)

#ifdef ZEND_ENGINE_2
static void fixup_property_info(zend_property_info * from TSRMLS_DC)
{
	FIXUP(from->name);
}
#endif

static void fixup_hash(HashTable * source,
					   fixup_bucket_t fixup_bucket TSRMLS_DC)
{
	unsigned int i;
	Bucket *p;

	if (source->nNumOfElements > 0) {
		if (!EAG(compress)) {
			if (source->arBuckets != NULL) {
				FIXUP(source->arBuckets);
				for (i = 0; i < source->nTableSize; i++) {
					FIXUP(source->arBuckets[i]);
				}
			}
		}
		FIXUP(source->pListHead);
		FIXUP(source->pListTail);

		p = source->pListHead;
		while (p) {
			FIXUP(p->pNext);
			FIXUP(p->pLast);
			FIXUP(p->pData);
			FIXUP(p->pDataPtr);
			FIXUP(p->pListLast);
			FIXUP(p->pListNext);
			if (p->pDataPtr) {
				fixup_bucket(p->pDataPtr TSRMLS_CC);
				p->pData = &p->pDataPtr;
			} else {
				fixup_bucket(p->pData TSRMLS_CC);
			}
			p = p->pListNext;
		}
		source->pInternalPointer = source->pListHead;
	}
}

void fixup_zval(zval * zv TSRMLS_DC)
{
	switch (zv->type & ~IS_CONSTANT_INDEX) {
	case IS_CONSTANT:			/* fallthrough */
	case IS_STRING:
		if (zv->value.str.val == NULL || zv->value.str.len == 0) {
			zv->value.str.val = empty_string;
			zv->value.str.len = 0;
		} else {
			FIXUP(zv->value.str.val);
		}
		break;
	case IS_ARRAY:				/* fallthrough */
	case IS_CONSTANT_ARRAY:
		if (zv->value.ht == NULL || zv->value.ht == &EG(symbol_table)) {
		} else {
			FIXUP(zv->value.ht);
			fixup_zval_hash(zv->value.ht);
		}
		break;
	case IS_OBJECT:
		if (!EAG(compress)) {
			return;
		}
#ifndef ZEND_ENGINE_2
		FIXUP(zv->value.obj.ce);
		if (zv->value.obj.properties != NULL) {
			FIXUP(zv->value.obj.properties);
			fixup_zval_hash(zv->value.obj.properties);
		}
#endif
	default:
		break;
	}
}

void fixup_op_array(eaccelerator_op_array * from TSRMLS_DC)
{
	zend_op *opline;
	zend_op *end;

#ifdef ZEND_ENGINE_2
	if (from->num_args > 0) {
		zend_uint i;
		FIXUP(from->arg_info);
		for (i = 0; i < from->num_args; i++) {
			FIXUP(from->arg_info[i].name);
			FIXUP(from->arg_info[i].class_name);
		}
	}
#else
	FIXUP(from->arg_types);
#endif
	FIXUP(from->function_name);
#ifdef ZEND_ENGINE_2
	FIXUP(from->scope_name);
#endif
	if (from->type == ZEND_INTERNAL_FUNCTION) {
		return;
	}

	if (from->opcodes != NULL) {
		FIXUP(from->opcodes);

		opline = from->opcodes;
		end = opline + from->last;
		EAG(compress) = 0;
		for (; opline < end; opline++) {
			/*
			   if (opline->result.op_type == IS_CONST) 
			   fixup_zval(&opline->result.u.constant TSRMLS_CC);
			 */
			if (opline->op1.op_type == IS_CONST)
				fixup_zval(&opline->op1.u.constant TSRMLS_CC);
			if (opline->op2.op_type == IS_CONST)
				fixup_zval(&opline->op2.u.constant TSRMLS_CC);
#ifdef ZEND_ENGINE_2
			switch (opline->opcode) {
			case ZEND_JMP:
				FIXUP(opline->op1.u.jmp_addr);
				break;
			case ZEND_JMPZ:	/* fallthrough */
			case ZEND_JMPNZ:
			case ZEND_JMPZ_EX:
			case ZEND_JMPNZ_EX:
				FIXUP(opline->op2.u.jmp_addr);
				break;
			}
			opline->handler = get_opcode_handler(opline->opcode TSRMLS_CC);
#endif
		}
		EAG(compress) = 1;
	}
	FIXUP(from->brk_cont_array);
#ifdef ZEND_ENGINE_2
	FIXUP(from->try_catch_array);
#endif
	if (from->static_variables != NULL) {
		FIXUP(from->static_variables);
		fixup_zval_hash(from->static_variables);
	}
	FIXUP(from->filename);
#ifdef ZEND_ENGINE_2
	FIXUP(from->doc_comment);
#endif
}

void fixup_class_entry(eaccelerator_class_entry * from TSRMLS_DC)
{
	FIXUP(from->name);
	FIXUP(from->parent);
#ifdef ZEND_ENGINE_2
	FIXUP(from->filename);
	FIXUP(from->doc_comment);
	fixup_zval_hash(&from->constants_table);
	fixup_zval_hash(&from->default_properties);
	fixup_hash(&from->properties_info,
			   (fixup_bucket_t) fixup_property_info TSRMLS_CC);
	if (from->static_members != NULL) {
		FIXUP(from->static_members);
		fixup_zval_hash(from->static_members);
	}
#else
	fixup_zval_hash(&from->default_properties);
#endif
	fixup_hash(&from->function_table,
			   (fixup_bucket_t) fixup_op_array TSRMLS_CC);
}

/******************************************************************************/
/* Functions to restore a php script from shared memory                       */
/******************************************************************************/

typedef void *(*restore_bucket_t) (void *TSRMLS_DC);

#define restore_zval_hash(target, source) \
    restore_hash(target, source, (restore_bucket_t)restore_zval_ptr TSRMLS_CC)

static zval *restore_zval_ptr(zval * from TSRMLS_DC)
{
	zval *p;
	ALLOC_ZVAL(p);
	memcpy(p, from, sizeof(zval));
	restore_zval(p TSRMLS_CC);
	return p;
}

static HashTable *restore_hash(HashTable * target, HashTable * source,
							   restore_bucket_t copy_bucket TSRMLS_DC)
{
	Bucket *p, *np, *prev_p;
	int nIndex;

	if (target == NULL) {
		ALLOC_HASHTABLE(target);
	}
	memcpy(target, source, sizeof(HashTable));
	target->arBuckets =
		(Bucket **) emalloc(target->nTableSize * sizeof(Bucket *));
	memset(target->arBuckets, 0, target->nTableSize * sizeof(Bucket *));
	target->pDestructor = NULL;
	target->persistent = 0;
	target->pListHead = NULL;
	target->pListTail = NULL;
#if HARDENING_PATCH_HASH_PROTECT
    target->canary = zend_hash_canary;
#endif

	p = source->pListHead;
	prev_p = NULL;
	np = NULL;
	while (p) {
		np = (Bucket *) emalloc(offsetof(Bucket, arKey) + p->nKeyLength);
		/*    np = (Bucket *) emalloc(sizeof(Bucket) + p->nKeyLength); */
		nIndex = p->h % source->nTableSize;
		if (target->arBuckets[nIndex]) {
			np->pNext = target->arBuckets[nIndex];
			np->pLast = NULL;
			np->pNext->pLast = np;
		} else {
			np->pNext = NULL;
			np->pLast = NULL;
		}
		target->arBuckets[nIndex] = np;
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
	return target;
}

void restore_zval(zval * zv TSRMLS_DC)
{
	switch (zv->type & ~IS_CONSTANT_INDEX) {
	case IS_CONSTANT:
	case IS_STRING:
		if (zv->value.str.val == NULL || 
                zv->value.str.val == empty_string || zv->value.str.len == 0) {
			zv->value.str.val = empty_string;
			return;
		} else {
			char *p = emalloc(zv->value.str.len + 1);
			memcpy(p, zv->value.str.val, zv->value.str.len + 1);
			zv->value.str.val = p;
		}
		return;

	case IS_ARRAY:
	case IS_CONSTANT_ARRAY:
		if (zv->value.ht != NULL && zv->value.ht != &EG(symbol_table)) {
			zv->value.ht = restore_zval_hash(NULL, zv->value.ht);
			zv->value.ht->pDestructor = ZVAL_PTR_DTOR;
		}
		return;

	case IS_OBJECT:
    {
#ifndef ZEND_ENGINE_2
        zend_bool incomplete_class = 0;
        char *class_name = (char *) zv->value.obj.ce;
        int name_len = 0;
        if (!EAG(compress)) {
            return;
        }
        if (class_name != NULL) {
            zend_class_entry *ce = NULL;
            name_len = strlen(class_name);
            if (zend_hash_find(CG(class_table), (void *) class_name, name_len + 1, (void **) &ce) != SUCCESS) {
                char *lowercase_name = estrndup(INCOMPLETE_CLASS, sizeof(INCOMPLETE_CLASS));
                zend_str_tolower(lowercase_name, sizeof(INCOMPLETE_CLASS));
                if (zend_hash_find(CG(class_table), lowercase_name,
                            sizeof(INCOMPLETE_CLASS), (void **) &ce) != SUCCESS) {
                    efree(lowercase_name);
                    zend_error(E_ERROR, "EACCELERATOR can't restore object's class \"%s\"", class_name);
                } else {
                    efree(lowercase_name);
                    zv->value.obj.ce = ce;
                    incomplete_class = 1;
                }
            } else {
                zv->value.obj.ce = ce;
            }
        }
        if (zv->value.obj.properties != NULL) {
            zv->value.obj.properties = restore_zval_hash(NULL, zv->value.obj.properties);
            zv->value.obj.properties->pDestructor = ZVAL_PTR_DTOR;
            /* Clearing references */
            {
                Bucket *p = zv->value.obj.properties->pListHead;
                while (p != NULL) {
                    ((zval *) (p->pDataPtr))->refcount = 1;
                    p = p->pListNext;
                }
            }
        }
        if (incomplete_class && class_name != NULL) {
            zval *val;
            MAKE_STD_ZVAL(val);
            Z_TYPE_P(val) = IS_STRING;
            Z_STRVAL_P(val) = estrndup(class_name, name_len);
            Z_STRLEN_P(val) = name_len;
            zend_hash_update(Z_OBJPROP_P(zv), MAGIC_MEMBER, sizeof(MAGIC_MEMBER), &val, sizeof(val), NULL);
        }
#endif
        return;
    }
	}
}

static void call_op_array_ctor_handler(zend_extension * extension,
									   zend_op_array * op_array TSRMLS_DC)
{
	if (extension->op_array_ctor) {
		extension->op_array_ctor(op_array);
	}
}

zend_op_array *restore_op_array(zend_op_array * to,
								eaccelerator_op_array * from TSRMLS_DC)
{
	zend_function *function;
#ifdef ZEND_ENGINE_2
	int fname_len = 0;
	char *fname_lc = NULL;
#endif

	ea_debug_pad(EA_DEBUG TSRMLS_CC);
	ea_debug_printf(EA_DEBUG, "[%d] restore_op_array: %s\n", getpid(),
					from->function_name ? from->function_name : "(top)");

	if (from->type == ZEND_INTERNAL_FUNCTION) {
		if (to == NULL) {
			to = emalloc(sizeof(zend_internal_function));
		}
		memset(to, 0, sizeof(zend_internal_function));
	} else {
		if (to == NULL) {
			to = emalloc(sizeof(zend_op_array));
		}
		memset(to, 0, sizeof(zend_op_array));
		if (ZendOptimizer) {
			zend_llist_apply_with_argument(&zend_extensions, 
                    (llist_apply_with_arg_func_t) call_op_array_ctor_handler, to TSRMLS_CC);
		}
	}
	to->type = from->type;
#ifdef ZEND_ENGINE_2
	to->num_args = from->num_args;
	to->required_num_args = from->required_num_args;
	to->arg_info = from->arg_info;
	to->pass_rest_by_reference = from->pass_rest_by_reference;
#else
	to->arg_types = from->arg_types;
#endif
	to->function_name = from->function_name;

#ifdef ZEND_ENGINE_2
	if (to->function_name) {
		fname_len = strlen(to->function_name);
		fname_lc = zend_str_tolower_dup(to->function_name, fname_len);
	}

	to->fn_flags = from->fn_flags;

	/* segv74:
	 * to->scope = EAG(class_entry)
	 *
	 * if  from->scope_name == NULL,
	 *     ; EAG(class) == NULL  : we are in function or outside function.
	 *     ; EAG(class) != NULL  : inherited method not defined in current file, should have to find.
	 *                              just LINK (zend_op_array *) to to original entry in parent,
	 *                              but, with what? !!! I don't know PARENT CLASS NAME !!!!
	 *
	 *
	 * if  from->scope_name != NULL,
	 *     ; we are in class member function 
	 *
	 *     ; we have to find appropriate (zend_class_entry*) to->scope for name from->scope_name
	 *     ; if we find in CG(class_table), link to it.
	 *     ; if fail, it should be EAG(class_entry)
	 *    
	 * am I right here ? ;-(
	 */
	if (from->scope_name != NULL) {
		char *from_scope_lc = zend_str_tolower_dup(from->scope_name, from->scope_name_len);
		if (zend_hash_find (CG(class_table), (void *) from_scope_lc, 
                    from->scope_name_len + 1, (void **) &to->scope) != SUCCESS) {
			ea_debug_pad(EA_DEBUG TSRMLS_CC);
			ea_debug_printf(EA_DEBUG, "[%d]                   can't find '%s' in hash. use EAG(class_entry).\n", getpid(), from_scope_lc);
			to->scope = EAG(class_entry);
		} else {
			ea_debug_pad(EA_DEBUG TSRMLS_CC);
			ea_debug_printf(EA_DEBUG, "[%d]                   found '%s' in hash\n", getpid(), from_scope_lc);
			to->scope = *(zend_class_entry **) to->scope;
		}
		efree(from_scope_lc);
	} else {					// zoeloelip: is this needed? scope is always stored
		ea_debug_pad(EA_DEBUG TSRMLS_CC);
		ea_debug_printf(EA_DEBUG, "[%d]                   from is NULL\n", getpid()); 
		if (EAG(class_entry)) {
			zend_class_entry *p;
			for (p = EAG(class_entry)->parent; p; p = p->parent) {
				ea_debug_pad(EA_DEBUG TSRMLS_CC);
				ea_debug_printf(EA_DEBUG, "[%d]                   checking parent '%s' have '%s'\n", getpid(), p->name, fname_lc);
				if (zend_hash_find(&p->function_table, fname_lc, fname_len + 1, (void **) &function) == SUCCESS) {
					ea_debug_pad(EA_DEBUG TSRMLS_CC);
					ea_debug_printf(EA_DEBUG, "[%d]                                   '%s' has '%s' of scope '%s'\n", 
                            getpid(), p->name, fname_lc, function->common.scope->name);
					to->scope = function->common.scope;
					break;
				}
			}
		} else {
			to->scope = NULL;
		}
	}

	ea_debug_pad(EA_DEBUG TSRMLS_CC);
	ea_debug_printf(EA_DEBUG, "[%d]                   %s's scope is '%s'\n", getpid(), 
            from->function_name ? from->function_name : "(top)", to->scope ? to->scope->name : "NULL");
#endif
	if (from->type == ZEND_INTERNAL_FUNCTION) {
		zend_class_entry *class_entry = EAG(class_entry);
		ea_debug_pad(EA_DEBUG TSRMLS_CC);
		ea_debug_printf(EA_DEBUG, "[%d]                   [internal function from=%08x,to=%08x] class_entry='%s' [%08x]\n", 
                getpid(), from, to, class_entry->name, class_entry);
		if (class_entry) {
			ea_debug_pad(EA_DEBUG TSRMLS_CC);
			ea_debug_printf(EA_DEBUG, "[%d]                                       class_entry->parent='%s' [%08x]\n", 
                    getpid(), class_entry->parent->name, class_entry->parent);
		}
		if (class_entry != NULL && class_entry->parent != NULL && 
                zend_hash_find(&class_entry->parent->function_table,
#ifdef ZEND_ENGINE_2
                fname_lc, fname_len + 1,
#else
                to->function_name, strlen(to->function_name) + 1,
#endif
				(void **) &function) == SUCCESS && function->type == ZEND_INTERNAL_FUNCTION) {
			ea_debug_pad(EA_DEBUG TSRMLS_CC);
			ea_debug_printf(EA_DEBUG, "[%d]                                       found in function table\n", getpid());
			((zend_internal_function *) (to))->handler = ((zend_internal_function *) function)->handler;
		} else {
			/* FIXME. I don't know how to fix handler.
			 * TODO: must solve this somehow, to avoid returning damaged structure...
			 */
			ea_debug_pad(EA_DEBUG TSRMLS_CC);
			ea_debug_printf(EA_DEBUG, "[%d]                                       can't find\n", getpid());
		}
		return to;
	}
	to->opcodes = from->opcodes;
	to->last = to->size = from->last;
	to->T = from->T;
	to->brk_cont_array = from->brk_cont_array;
	to->last_brk_cont = from->last_brk_cont;
	/*
	   to->current_brk_cont = -1;
	   to->static_variables = from->static_variables;
	   to->start_op         = to->opcodes;
	   to->backpatch_count  = 0;
	 */
	to->return_reference = from->return_reference;
	to->done_pass_two = 1;
	to->filename = from->filename;
#ifdef ZEND_ENGINE_2
	/* HOESH: try & catch support */
	to->try_catch_array = from->try_catch_array;
	to->last_try_catch = from->last_try_catch;
	to->uses_this = from->uses_this;

	to->line_start = from->line_start;
	to->line_end = from->line_end;
	to->doc_comment_len = from->doc_comment_len;
	to->doc_comment = from->doc_comment;
	/*???
	   if (from->doc_comment != NULL) {
	   to->doc_comment = emalloc(from->doc_comment_len+1);
	   memcpy(to->doc_comment, from->doc_comment, from->doc_comment_len+1);
	   }
	 */
#else
	to->uses_globals = from->uses_globals;
#endif
	if (from->static_variables) {
		to->static_variables = restore_zval_hash(NULL, from->static_variables);
		to->static_variables->pDestructor = ZVAL_PTR_DTOR;
#ifndef ZEND_ENGINE_2
		if (EAG(class_entry) != NULL) {
			Bucket *p = to->static_variables->pListHead;
			while (p != NULL) {
				((zval *) (p->pDataPtr))->refcount = 1;
				p = p->pListNext;
			}
		}
#endif
	}

	/* disable deletion in destroy_op_array */
	++EAG(refcount_helper);
	to->refcount = &EAG(refcount_helper);

	return to;
}

static zend_op_array *restore_op_array_ptr(eaccelerator_op_array *
										   from TSRMLS_DC)
{
	return restore_op_array(NULL, from TSRMLS_CC);
}

#ifdef ZEND_ENGINE_2
static zend_property_info *restore_property_info(zend_property_info *
												 from TSRMLS_DC)
{
	zend_property_info *to = emalloc(sizeof(zend_property_info));
	memcpy(to, from, sizeof(zend_property_info));
	to->name = emalloc(from->name_length + 1);
	memcpy(to->name, from->name, from->name_length + 1);
	return to;
}
#endif

/* restore the parent class with the given name for the given class */
void restore_class_parent(char *parent, int len,
						  zend_class_entry * to TSRMLS_DC)
{
#ifdef ZEND_ENGINE_2
	char *name_lc = zend_str_tolower_dup(parent, len);
	if (zend_hash_find(CG(class_table), (void *) name_lc, len + 1, (void **) &to->parent) != SUCCESS)
#else
	if (zend_hash_find(CG(class_table), (void *) parent, len + 1, (void **) &to->parent) != SUCCESS)
#endif
	{
		ea_debug_error("[%d] EACCELERATOR can't restore parent class \"%s\" of class \"%s\"\n", 
                getpid(), (char *) parent, to->name);
		to->parent = NULL;
	} else {
#ifdef ZEND_ENGINE_2
		/* inherit parent methods */
		to->parent = *(zend_class_entry **) to->parent;
		to->constructor = to->parent->constructor;
		to->destructor = to->parent->destructor;
		to->clone = to->parent->clone;
		to->__get = to->parent->__get;
		to->__set = to->parent->__set;
		to->__call = to->parent->__call;
		to->create_object = to->parent->create_object;
#else
		to->handle_property_get = to->parent->handle_property_get;
		to->handle_property_set = to->parent->handle_property_set;
		to->handle_function_call = to->parent->handle_function_call;
#endif
	}
#ifdef ZEND_ENGINE_2
	efree(name_lc);
#endif
}

#ifdef ZEND_ENGINE_2
void restore_class_methods(zend_class_entry * to TSRMLS_DC)
{
	int cname_len = to->name_length;
	char *cname_lc = zend_str_tolower_dup(to->name, cname_len);
	int fname_len = 0;
	char *fname_lc = NULL;
	zend_function *f = NULL;
	zend_function *old_ctor = to->constructor;
	Bucket *p = to->function_table.pListHead;

	while (p != NULL) {
		f = p->pData;
		fname_len = strlen(f->common.function_name);
		fname_lc = zend_str_tolower_dup(f->common.function_name, fname_len);

		if (fname_len == cname_len && !memcmp(fname_lc, cname_lc, fname_len) && 
                to->constructor == old_ctor && f->common.scope != to->parent) {
			to->constructor = f;
		} else if (fname_lc[0] == '_' && fname_lc[1] == '_' && f->common.scope != to->parent) {
			if (fname_len == sizeof(ZEND_CONSTRUCTOR_FUNC_NAME) - 1 && 
                    memcmp(fname_lc, ZEND_CONSTRUCTOR_FUNC_NAME, sizeof(ZEND_CONSTRUCTOR_FUNC_NAME)) == 0)
				to->constructor = f;
			else if (fname_len == sizeof(ZEND_DESTRUCTOR_FUNC_NAME) - 1 &&
					 memcmp(fname_lc, ZEND_DESTRUCTOR_FUNC_NAME, sizeof(ZEND_DESTRUCTOR_FUNC_NAME)) == 0)
				to->destructor = f;
			else if (fname_len == sizeof(ZEND_CLONE_FUNC_NAME) - 1 &&
					 memcmp(fname_lc, ZEND_CLONE_FUNC_NAME, sizeof(ZEND_CLONE_FUNC_NAME)) == 0)
				to->clone = f;
			else if (fname_len == sizeof(ZEND_GET_FUNC_NAME) - 1 &&
					 memcmp(fname_lc, ZEND_GET_FUNC_NAME, sizeof(ZEND_GET_FUNC_NAME)) == 0)
				to->__get = f;
			else if (fname_len == sizeof(ZEND_SET_FUNC_NAME) - 1 &&
					 memcmp(fname_lc, ZEND_SET_FUNC_NAME, sizeof(ZEND_SET_FUNC_NAME)) == 0)
				to->__set = f;
			else if (fname_len == sizeof(ZEND_CALL_FUNC_NAME) - 1 &&
					 memcmp(fname_lc, ZEND_CALL_FUNC_NAME, sizeof(ZEND_CALL_FUNC_NAME)) == 0)
				to->__call = f;
		}
		efree(fname_lc);
		p = p->pListNext;
	}
	efree(cname_lc);
}
#endif

zend_class_entry *restore_class_entry(zend_class_entry * to,
									  eaccelerator_class_entry * from TSRMLS_DC)
{
	zend_class_entry *old;
	zend_function *f = NULL;
	int fname_len = 0;
	char *fname_lc = NULL;
#ifdef ZEND_ENGINE_2
	int cname_len;
	char *cname_lc;
	Bucket *p;
	union _zend_function *old_ctor;
#endif

	ea_debug_pad(EA_DEBUG TSRMLS_CC);
	ea_debug_printf(EA_DEBUG, "[%d] retore_class_entry: %s\n", getpid(), from->name ? from->name : "(top)");
#ifdef DEBUG
	EAG(xpad)++;
#endif

	if (to == NULL) {
		to = emalloc(sizeof(zend_class_entry));
	}
	memset(to, 0, sizeof(zend_class_entry));
	to->type = from->type;
	/*
	   to->name        = NULL;
	   to->name_length = from->name_length;
	   to->constants_updated = 0;
	   to->parent      = NULL;
	 */
#ifdef ZEND_ENGINE_2
	to->ce_flags = from->ce_flags;
	/*
	   to->static_members = NULL;
	 */
	to->num_interfaces = from->num_interfaces;
	if (to->num_interfaces > 0) {
		to->interfaces = (zend_class_entry **) emalloc(sizeof(zend_class_entry *) * to->num_interfaces);
		// should find out class entry. what the hell !!!
		memset(to->interfaces, 0, sizeof(zend_class_entry *) * to->num_interfaces);
	} else {
		to->interfaces = NULL;
	}

	to->iterator_funcs = from->iterator_funcs;
	to->get_iterator = from->get_iterator;
	to->interface_gets_implemented = from->interface_gets_implemented;
#endif

	if (from->name != NULL) {
		to->name_length = from->name_length;
		to->name = emalloc(from->name_length + 1);
		memcpy(to->name, from->name, from->name_length + 1);
	}

	if (from->parent != NULL) {
		restore_class_parent(from->parent, strlen(from->parent), to TSRMLS_CC);
	} else {
		ea_debug_pad(EA_DEBUG TSRMLS_CC);
		ea_debug_printf(EA_DEBUG, "[%d] parent = NULL\n", getpid());
		to->parent = NULL;
	}

	old = EAG(class_entry);
	EAG(class_entry) = to;

#ifdef ZEND_ENGINE_2
	to->refcount = 1;

	to->line_start = from->line_start;
	to->line_end = from->line_end;
	to->doc_comment_len = from->doc_comment_len;
	if (from->filename != NULL) {
		size_t len = strlen(from->filename) + 1;
		to->filename = emalloc(len);
		memcpy(to->filename, from->filename, len);
	}
	if (from->doc_comment != NULL) {
		to->doc_comment = emalloc(from->doc_comment_len + 1);
		memcpy(to->doc_comment, from->doc_comment, from->doc_comment_len + 1);
	}

	/* restore constants table */
	restore_zval_hash(&to->constants_table, &from->constants_table);
	to->constants_table.pDestructor = ZVAL_PTR_DTOR;
	/* restore default properties */
	restore_zval_hash(&to->default_properties, &from->default_properties);
	to->default_properties.pDestructor = ZVAL_PTR_DTOR;
	/* restore properties */
	restore_hash(&to->properties_info, &from->properties_info, 
            (restore_bucket_t) restore_property_info TSRMLS_CC);
	to->properties_info.pDestructor = properties_info_dtor;

	if (from->static_members != NULL) {
		ALLOC_HASHTABLE(to->static_members);
		restore_zval_hash(to->static_members, from->static_members);
		to->static_members->pDestructor = ZVAL_PTR_DTOR;
		/*
		   } else {
		   ALLOC_HASHTABLE(to->static_members);
		   zend_hash_init_ex(to->static_members, 0, NULL, ZVAL_PTR_DTOR, 0, 0);
		 */
	}
#else
	to->refcount = emalloc(sizeof(*to->refcount));
	*to->refcount = 1;

	restore_zval_hash(&to->default_properties, &from->default_properties);
	to->default_properties.pDestructor = ZVAL_PTR_DTOR;
	/* Clearing references */
	{
		Bucket *p = to->default_properties.pListHead;
		while (p != NULL) {
			((zval *) (p->pDataPtr))->refcount = 1;
			p = p->pListNext;
		}
	}
#endif
	restore_hash(&to->function_table, &from->function_table,
				 (restore_bucket_t) restore_op_array_ptr TSRMLS_CC);
	to->function_table.pDestructor = ZEND_FUNCTION_DTOR;

#ifdef ZEND_ENGINE_2
	restore_class_methods(to TSRMLS_CC);
#endif
	EAG(class_entry) = old;

#ifdef DEBUG
	EAG(xpad)--;
#endif

	return to;
}

void restore_function(mm_fc_entry * p TSRMLS_DC)
{
	zend_op_array op_array;

	if ((p->htabkey[0] == '\000') && zend_hash_exists(CG(function_table), p->htabkey, p->htablen)) {
		return;
	}
	if (restore_op_array(&op_array, (eaccelerator_op_array *) p->fc TSRMLS_CC) != NULL) {
		if (zend_hash_add(CG(function_table), p->htabkey, p->htablen, &op_array, sizeof(zend_op_array), NULL) == FAILURE) {
			CG(in_compilation) = 1;
			CG(compiled_filename) = EAG(mem);
#ifdef ZEND_ENGINE_2
			CG(zend_lineno) = op_array.line_start;
#else
			CG(zend_lineno) = op_array.opcodes[0].lineno;
#endif
			zend_error(E_ERROR, "Cannot redeclare %s()", p->htabkey);
		}
	}
}

/*
 * Class handling.
 */
void restore_class(mm_fc_entry * p TSRMLS_DC)
{
#ifdef ZEND_ENGINE_2
	zend_class_entry *ce;
#else
	zend_class_entry ce;
#endif

	if ((p->htabkey[0] == '\000') && zend_hash_exists(CG(class_table), p->htabkey, p->htablen)) {
		return;
	}
#ifdef ZEND_ENGINE_2
	ce = restore_class_entry(NULL, (eaccelerator_class_entry *) p->fc TSRMLS_CC);
	if (ce != NULL)
#else
	if (restore_class_entry(&ce, (eaccelerator_class_entry *) p->fc TSRMLS_CC) != NULL)
#endif
	{
#ifdef ZEND_ENGINE_2
		if (zend_hash_add(CG(class_table), p->htabkey, p->htablen, &ce, sizeof(zend_class_entry *), NULL) == FAILURE)
#else
		if (zend_hash_add(CG(class_table), p->htabkey, p->htablen, &ce, sizeof(zend_class_entry), NULL) == FAILURE)
#endif
		{
			CG(in_compilation) = 1;
			CG(compiled_filename) = EAG(mem);
#ifdef ZEND_ENGINE_2
			CG(zend_lineno) = ce->line_start;
#else
			CG(zend_lineno) = 0;
#endif
			zend_error(E_ERROR, "Cannot redeclare class %s", p->htabkey);
		}
	}
}

#endif /* HAVE_EACCELERATOR */
