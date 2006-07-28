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
   $Id: ea_restore.c 176 2006-03-05 12:18:54Z bart $
*/

#include "eaccelerator.h"

#ifdef HAVE_EACCELERATOR

#include "debug.h"
#include "ea_restore.h"
#include "opcodes.h"
#include "zend.h"
#include "zend_API.h"
#include "zend_extensions.h"
#ifdef ZEND_ENGINE_2_1
#include "zend_vm.h"
#endif

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
	zend_hash_destroy(&dummy_class_entry.function_table);
	zend_hash_destroy(&dummy_class_entry.constants_table);
	zend_hash_destroy(&dummy_class_entry.properties_info);
#  ifdef ZEND_ENGINE_2_1
	zend_hash_destroy(&dummy_class_entry.default_static_members);
#  endif
#  if defined(ZEND_ENGINE_2) && !defined(ZEND_ENGINE_2_1)
	zend_hash_destroy(dummy_class_entry.static_members);
        FREE_HASHTABLE(dummy_class_entry.static_members);
#  endif
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
#ifdef ZEND_ENGINE_2_1
	FIXUP(from->doc_comment);
#endif
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
	switch (Z_TYPE_P(zv) & ~IS_CONSTANT_INDEX) {
	case IS_CONSTANT:			/* fallthrough */
    case IS_OBJECT:             /* fallthrough: object are serialized */
	case IS_STRING:
		FIXUP(Z_STRVAL_P(zv));
		break;
	case IS_ARRAY:				/* fallthrough */
	case IS_CONSTANT_ARRAY:
		FIXUP(Z_ARRVAL_P(zv));
		fixup_zval_hash(Z_ARRVAL_P(zv));
		break;
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
#  ifdef ZEND_ENGINE_2_1
			ZEND_VM_SET_OPCODE_HANDLER(opline);
#  elif defined(ZEND_ENGINE_2)
            opline->handler = zend_opcode_handlers[opline->opcode];
#  else
			opline->handler = get_opcode_handler(opline->opcode TSRMLS_CC);
#  endif

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
#ifdef ZEND_ENGINE_2_1
	if (from->vars != NULL) {
		int i;
		FIXUP(from->vars);
		for (i = 0; i < from->last_var; i++) {
			FIXUP(from->vars[i].name);
		}
	}
#endif
	FIXUP(from->filename);
}

void fixup_class_entry(eaccelerator_class_entry * from TSRMLS_DC)
{
	FIXUP(from->name);
	FIXUP(from->parent);
#ifdef ZEND_ENGINE_2
	FIXUP(from->filename);
	fixup_zval_hash(&from->constants_table);
	fixup_zval_hash(&from->default_properties);
	fixup_hash(&from->properties_info,
			   (fixup_bucket_t) fixup_property_info TSRMLS_CC);
#  ifdef ZEND_ENGINE_2_1
	fixup_zval_hash(&from->default_static_members);
	if (from->static_members != NULL) {
		FIXUP(from->static_members);
		if (from->static_members != &from->default_static_members) {
			fixup_zval_hash(from->static_members);
		}
	}
#  else
	if (from->static_members != NULL) {
		FIXUP(from->static_members);
		fixup_zval_hash(from->static_members);
	}
#  endif
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
	/* hrak: reset refcount to make sure there is one reference to this val, and prevent memleaks */
	p->refcount = 1;
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
    case IS_OBJECT:
	case IS_STRING:
		if (Z_STRVAL_P(zv) == NULL || Z_STRVAL_P(zv) == "" || Z_STRLEN_P(zv) == 0) {
			Z_STRLEN_P(zv) = 0;
			Z_STRVAL_P(zv) = empty_string;
			return;
		} else {
			char *p = emalloc(Z_STRLEN_P(zv) + 1);
			memcpy(p, Z_STRVAL_P(zv), Z_STRLEN_P(zv) + 1);
			Z_STRVAL_P(zv) = p;
		}
		return;

	case IS_ARRAY:
	case IS_CONSTANT_ARRAY:
		if (Z_ARRVAL_P(zv) != NULL && Z_ARRVAL_P(zv) != &EG(symbol_table)) {
			Z_ARRVAL_P(zv) = restore_zval_hash(NULL, Z_ARRVAL_P(zv));
			Z_ARRVAL_P(zv)->pDestructor = ZVAL_PTR_DTOR;
		}
		return;
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
    union {
    	zend_function *v;
        void *ptr;
    } function;
#ifdef ZEND_ENGINE_2
	int fname_len = 0;
	char *fname_lc = NULL;
#endif

	DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
	DBG(ea_debug_printf, (EA_DEBUG, "[%d] restore_op_array: %s type=%x\n", getpid(),
					from->function_name ? from->function_name : "(top)", from->type));

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
        union {
            zend_class_entry *v;
            void *ptr;
        } scope;
		char *from_scope_lc = zend_str_tolower_dup(from->scope_name, from->scope_name_len);
        scope.v = to->scope;
		if (zend_hash_find (CG(class_table), (void *) from_scope_lc, from->scope_name_len + 1, &scope.ptr) != SUCCESS) {
			DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
			DBG(ea_debug_printf, (EA_DEBUG, "[%d]                   can't find '%s' in class_table. use EAG(class_entry).\n", getpid(), from->scope_name));
			to->scope = EAG(class_entry);
		} else {
			DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
			DBG(ea_debug_printf, (EA_DEBUG, "[%d]                   found '%s' in hash\n", getpid(), from->scope_name));
			to->scope = *(zend_class_entry **) to->scope;
		}
		efree(from_scope_lc);
	} else {					// zoeloelip: is this needed? scope is always stored -> hra: no its not :P only if from->scope!=null in ea_store
		DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
		DBG(ea_debug_printf, (EA_DEBUG, "[%d]                   from is NULL\n", getpid()));
		if (EAG(class_entry)) {
			zend_class_entry *p;
			for (p = EAG(class_entry)->parent; p; p = p->parent) {
				DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
				DBG(ea_debug_printf, (EA_DEBUG, "[%d]                   checking parent '%s' have '%s'\n", getpid(), p->name, fname_lc));
				if (zend_hash_find(&p->function_table, fname_lc, fname_len + 1, &function.ptr) == SUCCESS) {
					DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
					DBG(ea_debug_printf, (EA_DEBUG, "[%d]                                   '%s' has '%s' of scope '%s'\n", 
                            getpid(), p->name, fname_lc, function.v->common.scope->name));
					to->scope = function.v->common.scope;
					break;
				}
			}
		} else {
			to->scope = NULL;
		}
	}

	DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
	DBG(ea_debug_printf, (EA_DEBUG, "[%d]                   %s's scope is '%s'\n", getpid(), 
            from->function_name ? from->function_name : "(top)", to->scope ? to->scope->name : "NULL"));
#endif
	if (from->type == ZEND_INTERNAL_FUNCTION) {
		zend_class_entry *class_entry = EAG(class_entry);
		DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
		DBG(ea_debug_printf, (EA_DEBUG, "[%d]                   [internal function from=%08x,to=%08x] class_entry='%s' [%08x]\n", 
                getpid(), from, to, class_entry->name, class_entry));
		if (class_entry) {
			DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
			DBG(ea_debug_printf, (EA_DEBUG, "[%d]                                       class_entry->parent='%s' [%08x]\n", 
                    getpid(), class_entry->parent->name, class_entry->parent));
		}
		if (class_entry != NULL && class_entry->parent != NULL && 
                zend_hash_find(&class_entry->parent->function_table,
#ifdef ZEND_ENGINE_2
                fname_lc, fname_len + 1,
#else
                to->function_name, strlen(to->function_name) + 1,
#endif
				&function.ptr) == SUCCESS && function.v->type == ZEND_INTERNAL_FUNCTION) {
			DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
			DBG(ea_debug_printf, (EA_DEBUG, "[%d]                                       found in function table\n", getpid()));
			((zend_internal_function *) (to))->handler = ((zend_internal_function *) function.v)->handler;
		} else {
			/* FIXME. I don't know how to fix handler.
			 * TODO: must solve this somehow, to avoid returning damaged structure...
			 */
			DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
			DBG(ea_debug_printf, (EA_DEBUG, "[%d]                                       can't find\n", getpid()));
		}		
#ifdef ZEND_ENGINE_2
		/* hrak: slight memleak here. dont forget to free the lowercase function name! */
		if (fname_lc != NULL) {
			efree(fname_lc);
		}
		/* zend_internal_function also contains return_reference in ZE2 */
		to->return_reference = from->return_reference;
		/* this gets set by zend_do_inheritance */
		to->prototype = NULL;
#endif
		return to;
	}
#ifdef ZEND_ENGINE_2
	/* hrak: slight memleak here. dont forget to free the lowercase function name! */
	if (fname_lc != NULL) {
		efree(fname_lc);
	}
#endif
	to->opcodes = from->opcodes;
	to->last = to->size = from->last;
	to->T = from->T;
	to->brk_cont_array = from->brk_cont_array;
	to->last_brk_cont = from->last_brk_cont;
	
	   to->current_brk_cont = -1;
	   to->static_variables = from->static_variables;
/*	   to->start_op         = to->opcodes; */
	   to->backpatch_count  = 0;
	
	to->return_reference = from->return_reference;
	to->done_pass_two = 1;
	to->filename = from->filename;
/*	if (from->filename != NULL) {
		size_t len = strlen(from->filename) + 1;
		to->filename = emalloc(len);
		memcpy(to->filename, from->filename, len);
	}*/

#ifdef ZEND_ENGINE_2
	/* HOESH: try & catch support */
	to->try_catch_array = from->try_catch_array;
	to->last_try_catch = from->last_try_catch;
	to->uses_this = from->uses_this;

	to->line_start = from->line_start;
	to->line_end = from->line_end;
	to->doc_comment_len = 0;
	to->doc_comment = NULL;
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

#ifdef ZEND_ENGINE_2_1
	to->vars             = from->vars;
	to->last_var         = from->last_var;
	to->size_var         = 0;
/*	if (from->vars) {
		zend_uint i;
		to->vars = (zend_compiled_variable*)emalloc(from->last_var*sizeof(zend_compiled_variable));		
		memcpy(to->vars, from->vars, sizeof(zend_compiled_variable) * from->last_var);
		for (i = 0; i < from->last_var; i ++) {
			to->vars[i].name = estrndup(from->vars[i].name, from->vars[i].name_len);
		}
	}*/
#endif

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
#ifdef ZEND_ENGINE_2_1
    to->doc_comment_len = 0;
    to->doc_comment = NULL;
#endif
#ifdef ZEND_ENGINE_2_2
    to->ce = EAG(class_entry);
#endif
	return to;
}
#endif

/* restore the parent class with the given name for the given class */
void restore_class_parent(char *parent, int len,
						  zend_class_entry * to TSRMLS_DC)
{
#ifdef ZEND_ENGINE_2
	zend_class_entry** parent_ptr = NULL;
	if (zend_lookup_class(parent, len, &parent_ptr TSRMLS_CC) != SUCCESS)
#else
	char *name_lc = estrndup(parent, len);
	zend_str_tolower(name_lc, len);
	if (zend_hash_find(CG(class_table), (void *) name_lc, len + 1, (void **) &to->parent) != SUCCESS)
#endif
	{
		DBG(ea_debug_error, ("[%d] EACCELERATOR can't restore parent class \"%s\" of class \"%s\"\n", 
                getpid(), (char *) parent, to->name));
		to->parent = NULL;
	} else {
		/* parent found */
#ifdef ZEND_ENGINE_2
		to->parent = *parent_ptr;
#endif
		DBG(ea_debug_printf, (EA_DEBUG, "restore_class_parent: found parent %s..\n", to->parent->name));
		DBG(ea_debug_printf, (EA_DEBUG, "restore_class_parent: parent type=%d child type=%d\n", to->parent->type, to->type));
	}
#ifndef ZEND_ENGINE_2
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
	Bucket *p = to->function_table.pListHead;

	while (p != NULL) {
		f = p->pData;
		fname_len = strlen(f->common.function_name);
		fname_lc = zend_str_tolower_dup(f->common.function_name, fname_len);
		
		if (fname_len == cname_len && !memcmp(fname_lc, cname_lc, fname_len) && f->common.scope != to->parent) {
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
#  ifdef ZEND_ENGINE_2_1
			else if (fname_len == sizeof(ZEND_UNSET_FUNC_NAME) - 1 &&
					memcmp(fname_lc, ZEND_UNSET_FUNC_NAME, sizeof(ZEND_UNSET_FUNC_NAME)) == 0)
				to->__unset = f;
			else if (fname_len == sizeof(ZEND_ISSET_FUNC_NAME) - 1 &&
					memcmp(fname_lc, ZEND_ISSET_FUNC_NAME, sizeof(ZEND_ISSET_FUNC_NAME)) == 0)
				to->__isset = f;
#  endif
			else if (fname_len == sizeof(ZEND_CALL_FUNC_NAME) - 1 &&
					 memcmp(fname_lc, ZEND_CALL_FUNC_NAME, sizeof(ZEND_CALL_FUNC_NAME)) == 0)
				to->__call = f;
#  ifdef ZEND_ENGINE_2_2
            else if (fname_len == sizeof(ZEND_TOSTRING_FUNC_NAME) - 1 &&
                     memcmp(fname_lc, ZEND_TOSTRING_FUNC_NAME, sizeof(ZEND_TOSTRING_FUNC_NAME)) == 0)
                to->__tostring = f;
#  endif
		}
		if (to->parent) {
			/* clear the child's prototype and IMPLEMENTED_ABSTRACT flag,
			   these are properly restored by zend_do_inheritance() (see do_inherit_method_check) */
			f->common.prototype = NULL;
			f->common.fn_flags = f->common.fn_flags & (~ZEND_ACC_IMPLEMENTED_ABSTRACT);
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

	DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
	DBG(ea_debug_printf, (EA_DEBUG, "[%d] restore_class_entry: %s\n", getpid(), from->name ? from->name : "(top)"));
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
	to->num_interfaces = from->num_interfaces;
	to->interfaces = NULL;

	if (to->num_interfaces > 0) {
		/* hrak: Allocate the slots which will later be populated by ZEND_ADD_INTERFACE */
		to->interfaces = (zend_class_entry **) emalloc(sizeof(zend_class_entry *) * to->num_interfaces);
		memset(to->interfaces, 0, sizeof(zend_class_entry *) * to->num_interfaces);
	}
#endif

	if (from->name != NULL) {
		to->name_length = from->name_length;
		to->name = emalloc(from->name_length + 1);
		memcpy(to->name, from->name, from->name_length + 1);
	}

	old = EAG(class_entry);
	EAG(class_entry) = to;

#ifdef ZEND_ENGINE_2
	to->refcount = 1;

	to->line_start = from->line_start;
	to->line_end = from->line_end;
	to->doc_comment_len = 0;
    to->doc_comment = NULL;
/*	if (from->filename != NULL) {
		size_t len = strlen(from->filename) + 1;
		to->filename = emalloc(len);
		memcpy(to->filename, from->filename, len);
	}*/
	to->filename = from->filename;

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

#  ifdef ZEND_ENGINE_2_1
	/* restore default_static_members */
	restore_zval_hash(&to->default_static_members, &from->default_static_members);
	to->default_static_members.pDestructor = ZVAL_PTR_DTOR;
	
	if (from->static_members != &(from->default_static_members)) {
		ALLOC_HASHTABLE(to->static_members);
		restore_zval_hash(to->static_members, from->static_members);
		to->static_members->pDestructor = ZVAL_PTR_DTOR;
	} else {
		to->static_members = &(to->default_static_members);
	}
#  else
	if (from->static_members != NULL) {
		ALLOC_HASHTABLE(to->static_members);
		restore_zval_hash(to->static_members, from->static_members);
		to->static_members->pDestructor = ZVAL_PTR_DTOR;
	}
#  endif
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
	if (from->parent != NULL) {
		restore_class_parent(from->parent, strlen(from->parent), to TSRMLS_CC);
	} else {
		DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
		DBG(ea_debug_printf, (EA_DEBUG, "[%d] parent = NULL\n", getpid()));
		to->parent = NULL;
	}

	restore_hash(&to->function_table, &from->function_table,
				 (restore_bucket_t) restore_op_array_ptr TSRMLS_CC);
	to->function_table.pDestructor = ZEND_FUNCTION_DTOR;

#ifdef ZEND_ENGINE_2
	restore_class_methods(to TSRMLS_CC);
#endif
	if (to->parent)
		zend_do_inheritance(to, to->parent TSRMLS_CC);
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
