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
   | A copy is availble at http://www.gnu.org/copyleft/gpl.txt            |
   +----------------------------------------------------------------------+
   $Id: ea_restore.c 405 2010-02-18 12:53:07Z hans $
*/

#include "eaccelerator.h"

#ifdef HAVE_EACCELERATOR

#include "debug.h"
#include "ea_restore.h"
#include "opcodes.h"
#include "zend.h"
#include "zend_API.h"
#include "zend_extensions.h"
#include "zend_vm.h"

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

    zend_hash_destroy(&dummy_class_entry.function_table);
    zend_hash_destroy(&dummy_class_entry.constants_table);
    zend_hash_destroy(&dummy_class_entry.properties_info);
#ifndef ZEND_ENGINE_2_4
    zend_hash_destroy(&dummy_class_entry.default_properties);
    zend_hash_destroy(&dummy_class_entry.default_static_members);
#endif

    return property_dtor;
}

/******************************************************************************/
/* Functions to restore a cached script from file cache                       */
/******************************************************************************/

typedef void (*fixup_bucket_t) (char *, void *TSRMLS_DC);

#define fixup_zval_hash(base, from) \
    fixup_hash(base, from, (fixup_bucket_t)fixup_zval TSRMLS_CC)

static void fixup_property_info(char *base, zend_property_info * from TSRMLS_DC)
{
    FIXUP(base, from->name);
    FIXUP(base, from->doc_comment);
}

static void fixup_hash(char *base, HashTable * source,
                       fixup_bucket_t fixup_bucket TSRMLS_DC)
{
    unsigned int i;
    Bucket *p;

    if (source->nNumOfElements > 0) {
        if (source->arBuckets != NULL) {
            FIXUP(base, source->arBuckets);
            for (i = 0; i < source->nTableSize; i++) {
                FIXUP(base, source->arBuckets[i]);
            }
        }
        FIXUP(base, source->pListHead);
        FIXUP(base, source->pListTail);

        p = source->pListHead;
        while (p) {
#ifdef ZEND_ENGINE_2_4
            FIXUP(base, p->arKey);
#endif
            FIXUP(base, p->pNext);
            FIXUP(base, p->pLast);
            FIXUP(base, p->pData);
            FIXUP(base, p->pDataPtr);
            FIXUP(base, p->pListLast);
            FIXUP(base, p->pListNext);
            if (p->pDataPtr) {
                fixup_bucket(base, p->pDataPtr TSRMLS_CC);
                p->pData = &p->pDataPtr;
            } else {
                fixup_bucket(base, p->pData TSRMLS_CC);
            }
            p = p->pListNext;
        }
        source->pInternalPointer = source->pListHead;
    }
}

void fixup_zval(char *base, zval * zv TSRMLS_DC)
{
    switch (EA_ZV_TYPE_P(zv)) {
    case IS_CONSTANT:           /* fallthrough */
    case IS_OBJECT:             /* fallthrough: object are serialized */
    case IS_STRING:
        FIXUP(base, Z_STRVAL_P(zv));
        break;

    case IS_ARRAY:              /* fallthrough */
    case IS_CONSTANT_ARRAY:
        FIXUP(base, Z_ARRVAL_P(zv));
        fixup_zval_hash(base, Z_ARRVAL_P(zv));
        break;

    default:
        break;
    }
}

static void fixup_op_array(char *base, ea_op_array * from TSRMLS_DC)
{
    zend_op *opline;
    zend_op *end;
    zend_uint i;

    if (from->num_args > 0) {
        FIXUP(base, from->arg_info);
        for (i = 0; i < from->num_args; i++) {
            FIXUP(base, from->arg_info[i].name);
            FIXUP(base, from->arg_info[i].class_name);
        }
    }
    FIXUP(base, from->function_name);
    FIXUP(base, from->scope_name);
    if (from->type == ZEND_INTERNAL_FUNCTION) {
        return;
    }

#ifdef ZEND_ENGINE_2_4
    if (from->literals != NULL) {
        zend_literal *l, *end;

        FIXUP(base, from->literals);
        l = from->literals;
        end = from->literals + from->last_literal;
        while (l < end) {
                fixup_zval(base, &l->constant TSRMLS_CC);
            l++;
        }
    }
#endif

    if (from->opcodes != NULL) {
        FIXUP(base, from->opcodes);

        opline = from->opcodes;
        end = opline + from->last;
        for (; opline < end; opline++) {
#ifdef ZEND_ENGINE_2_4
            if (opline->op1_type == IS_CONST) {
                FIXUP(base, opline->op1.literal);
            }
            if (opline->op2_type == IS_CONST) {
                FIXUP(base, opline->op2.literal);
            }
#else
            if (opline->op1.op_type == IS_CONST) {
                fixup_zval(base, &opline->op1.u.constant TSRMLS_CC);
            }
            if (opline->op2.op_type == IS_CONST) {
                fixup_zval(base, &opline->op2.u.constant TSRMLS_CC);
            }
#endif

            switch (opline->opcode) {
#ifdef ZEND_GOTO
            case ZEND_GOTO:
#endif
            case ZEND_JMP:
#ifdef ZEND_ENGINE_2_4
                FIXUP(base, opline->op1.jmp_addr);
#else
                FIXUP(base, opline->op1.u.jmp_addr);
#endif
                break;
            case ZEND_JMPZ: /* fallthrough */
            case ZEND_JMPNZ:
            case ZEND_JMPZ_EX:
            case ZEND_JMPNZ_EX:
#ifdef ZEND_ENGINE_2_3
            case ZEND_JMP_SET:
#endif
#ifdef ZEND_ENGINE_2_4
            case ZEND_JMP_SET_VAR:
                FIXUP(base, opline->op2.jmp_addr);
#else
                FIXUP(base, opline->op2.u.jmp_addr);
#endif
                break;
            }
            ZEND_VM_SET_OPCODE_HANDLER(opline);
        }
    }
    FIXUP(base, from->brk_cont_array);
    FIXUP(base, from->try_catch_array);
    if (from->static_variables != NULL) {
        FIXUP(base, from->static_variables);
        fixup_zval_hash(base, from->static_variables);
    }
    if (from->vars != NULL) {
        int i;
        FIXUP(base, from->vars);
        for (i = 0; i < from->last_var; i++) {
            FIXUP(base, from->vars[i].name);
        }
    }
    FIXUP(base, from->filename);
#ifdef INCLUDE_DOC_COMMENTS
    FIXUP(base, from->doc_comment);
#endif
}

static void fixup_class_entry(char *base, ea_class_entry *from TSRMLS_DC)
{
#ifdef ZEND_ENGINE_2_4
    int i;
#endif

    FIXUP(base, from->name);
    FIXUP(base, from->parent);
    FIXUP(base, from->filename);
    fixup_zval_hash(base, &from->constants_table);
    fixup_hash(base, &from->properties_info,
               (fixup_bucket_t) fixup_property_info TSRMLS_CC);
#ifndef ZEND_ENGINE_2_4
    fixup_zval_hash(base, &from->default_properties);
    fixup_zval_hash(base, &from->default_static_members);
    if (from->static_members != NULL) {
        FIXUP(base, from->static_members);
        if (from->static_members != &from->default_static_members) {
            fixup_zval_hash(base, from->static_members);
        }
    }
#else
    if (from->default_properties_count) {
        FIXUP(base, from->default_properties_table);
        for (i = 0; i < from->default_properties_count; i++) {
            if (from->default_properties_table[i]) {
                FIXUP(base, from->default_properties_table[i]);
                fixup_zval(base, from->default_properties_table[i] TSRMLS_CC);
            }
        }
    }

    if (from->default_static_members_count) {
        FIXUP(base, from->default_static_members_table);
        for (i = 0; i < from->default_static_members_count; i++) {
            if (from->default_static_members_table[i]) {
                FIXUP(base, from->default_static_members_table[i]);
                fixup_zval(base, from->default_static_members_table[i] TSRMLS_CC);
            }
        }
    }
#endif
    fixup_hash(base, &from->function_table,(fixup_bucket_t) fixup_op_array TSRMLS_CC);
#ifdef INCLUDE_DOC_COMMENTS
    FIXUP(base, from->doc_comment);
#endif
}

void eaccelerator_fixup(ea_cache_entry *p TSRMLS_DC)
{
    ea_fc_entry *q;
    char *base;

    base = (char *) ((long) p - (long) p->next);
    p->next = NULL;
    FIXUP(base, p->op_array);
    FIXUP(base, p->f_head);
    FIXUP(base, p->c_head);
    fixup_op_array(base, p->op_array TSRMLS_CC);
    q = p->f_head;
    while (q != NULL) {
        FIXUP(base, q->fc);
        fixup_op_array(base, (ea_op_array *) q->fc TSRMLS_CC);
        FIXUP(base, q->next);
        q = q->next;
    }
    q = p->c_head;
    while (q != NULL) {
        FIXUP(base, q->fc);
        fixup_class_entry(base, (ea_class_entry *) q->fc TSRMLS_CC);
        FIXUP(base, q->next);
        q = q->next;
    }
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
    /*#ifdef ZEND_ENGINE_2_3
        Z_SET_REFCOUNT_P(p, 1);
    #else
        p->refcount = 1;
    #endif*/
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
    target->pInternalPointer = NULL;
    target->pListHead = NULL;
    target->pListTail = NULL;
#if HARDENING_PATCH_HASH_PROTECT
    target->canary = zend_hash_canary;
#endif

    p = source->pListHead;
    prev_p = NULL;
    np = NULL;
    while (p) {
#ifdef ZEND_ENGINE_2_4
//        if (IS_INTERNED(p->arKey)) {
        /* TODO */
//            DBG(ea_debug_printf, (EA_DEBUG, "[%d] restore_hash: restoring interned arKey '%s'\n", getpid(), p->arKey));
//            np = (Bucket *) emalloc(sizeof(Bucket));
//            memcpy(np, p, sizeof(Bucket));
//        } else if (!p->nKeyLength) {
//            DBG(ea_debug_printf, (EA_DEBUG, "[%d] restore_hash: restoring zero length arKey '%s'\n", getpid(), p->arKey));
//            np = (Bucket *) emalloc(sizeof(Bucket));
//           memcpy(np, p, sizeof(Bucket));
//        } else {
        DBG(ea_debug_printf, (EA_DEBUG, "[%d] restore_hash: restoring regular arKey '%s'\n", getpid(), p->arKey));
        np = (Bucket *) emalloc(sizeof(Bucket));
        memcpy(np, p, sizeof(Bucket));
        np->arKey = (char *) emalloc(p->nKeyLength);
        memcpy((char*)np->arKey, p->arKey, p->nKeyLength);
//        }
#else
        np = (Bucket *) emalloc(offsetof(Bucket, arKey) + p->nKeyLength);
        memcpy(np, p, offsetof(Bucket, arKey) + p->nKeyLength);
#endif

        nIndex = p->h % target->nTableSize;
        if (target->arBuckets[nIndex]) {
            np->pNext = target->arBuckets[nIndex];
            np->pLast = NULL;
            np->pNext->pLast = np;
        } else {
            np->pNext = NULL;
            np->pLast = NULL;
        }
        target->arBuckets[nIndex] = np;

        if (p->pDataPtr == NULL) {
            np->pData = copy_bucket(p->pData TSRMLS_CC);
            np->pDataPtr = NULL;
        } else {
            np->pDataPtr = copy_bucket(p->pDataPtr TSRMLS_CC);
            np->pData = &np->pDataPtr;
        }

        np->pListLast = prev_p;
        np->pListNext = NULL;

        if (prev_p) {
            prev_p->pListNext = np;
        } else {
            target->pListHead = np;
        }
        prev_p = np;
        p = p->pListNext;
    }
    target->pListTail = np;
    zend_hash_internal_pointer_reset(target);
    return target;
}

void restore_zval(zval * zv TSRMLS_DC)
{
    switch (EA_ZV_TYPE_P(zv)) {
    case IS_CONSTANT:
    case IS_OBJECT:
    case IS_STRING:
        if (Z_STRVAL_P(zv) == NULL || Z_STRLEN_P(zv) == 0) {
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

zend_op_array *restore_op_array(zend_op_array * to, ea_op_array * from TSRMLS_DC)
{
    union {
        zend_function *v;
        void *ptr;
    } function;
    int fname_len = 0;
    char *fname_lc = NULL;

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
    to->num_args = from->num_args;
    to->required_num_args = from->required_num_args;
    to->arg_info = from->arg_info;
#ifndef ZEND_ENGINE_2_4
    to->pass_rest_by_reference = from->pass_rest_by_reference;
#endif
    to->function_name = from->function_name;

    if (to->function_name) {
        fname_len = strlen(to->function_name);
        fname_lc = zend_str_tolower_dup(to->function_name, fname_len);
    }

    to->fn_flags = from->fn_flags;

#ifdef ZEND_ENGINE_2_4
    to->literals = from->literals;
    to->last_literal = from->last_literal;
    to->last_cache_slot = from->last_cache_slot;
#endif

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
        if (zend_hash_find (CG(class_table), (void *) from_scope_lc, from->scope_name_len + 1, &scope.ptr) == SUCCESS &&
                to->scope != NULL) {
            DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
            DBG(ea_debug_printf, (EA_DEBUG, "[%d]                   found '%s' in hash\n", getpid(), from->scope_name));
            DBG(ea_debug_printf, (EA_DEBUG, "name=%s :: to->scope is 0x%x", to->function_name, to->scope));
            to->scope = *(zend_class_entry **) to->scope;
        } else {
            DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
            DBG(ea_debug_printf, (EA_DEBUG, "[%d]                   can't find '%s' in class_table. use EAG(class_entry).\n", getpid(), from->scope_name));
            to->scope = EAG(class_entry);
        }
        efree(from_scope_lc);
    } else {
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
                               fname_lc, fname_len + 1,
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
        /* hrak: slight memleak here. dont forget to free the lowercase function name! */
        if (fname_lc != NULL) {
            efree(fname_lc);
        }
#ifndef ZEND_ENGINE_2_4
        /* zend_internal_function also contains return_reference in ZE2 */
        to->return_reference = from->return_reference;
#endif
        /* this gets set by zend_do_inheritance */
        to->prototype = NULL;
        return to;
    }
    /* hrak: slight memleak here. dont forget to free the lowercase function name! */
    if (fname_lc != NULL) {
        efree(fname_lc);
    }
    to->opcodes = from->opcodes;

#ifdef ZEND_ENGINE_2_4
    to->last = from->last;
#else
    to->last = to->size = from->last;
#endif
    to->T = from->T;
    to->brk_cont_array = from->brk_cont_array;
    to->last_brk_cont = from->last_brk_cont;

#ifndef ZEND_ENGINE_2_4
    to->current_brk_cont = -1;
#endif
    to->static_variables = from->static_variables;
#ifndef ZEND_ENGINE_2_4
    to->backpatch_count  = 0;

    to->return_reference = from->return_reference;
    to->done_pass_two = 1;
#endif
    to->filename = from->filename;

    to->try_catch_array = from->try_catch_array;
    to->last_try_catch = from->last_try_catch;
#ifdef ZEND_ENGINE_2_3
    to->this_var = from->this_var;
    to->early_binding = from->early_binding;
#else
    to->uses_this = from->uses_this;
#endif

    to->line_start = from->line_start;
    to->line_end = from->line_end;
#ifdef INCLUDE_DOC_COMMENTS
    to->doc_comment_len = from->doc_comment_len;
    to->doc_comment = from->doc_comment;
#else
    to->doc_comment_len = 0;
    to->doc_comment = NULL;
#endif
    if (from->static_variables) {
        to->static_variables = restore_zval_hash(NULL, from->static_variables);
        to->static_variables->pDestructor = ZVAL_PTR_DTOR;
    }

    to->vars             = from->vars;
    to->last_var         = from->last_var;
#ifndef ZEND_ENGINE_2_4
    to->size_var         = 0;
#endif

    /* disable deletion in destroy_op_array */
    ++EAG(refcount_helper);
    to->refcount = &EAG(refcount_helper);

    return to;
}

static zend_op_array *restore_op_array_ptr(ea_op_array *from TSRMLS_DC)
{
    return restore_op_array(NULL, from TSRMLS_CC);
}

static zend_property_info *restore_property_info(zend_property_info *
        from TSRMLS_DC)
{
    zend_property_info *to = emalloc(sizeof(zend_property_info));
    memcpy(to, from, sizeof(zend_property_info));
    to->name = emalloc(from->name_length + 1);
    memcpy((char*)to->name, from->name, from->name_length + 1);
#ifdef INCLUDE_DOC_COMMENTS
    if (from->doc_comment != NULL) {
        to->doc_comment = emalloc(from->doc_comment_len + 1);
        memcpy((char*)to->doc_comment, from->doc_comment, from->doc_comment_len + 1);
    }
#endif
#ifdef ZEND_ENGINE_2_2
    to->ce = EAG(class_entry);
#endif
    DBG(ea_debug_printf, (EA_DEBUG, "restore_property_info: restored property '%s'\n", to->name));
    return to;
}

/* restore the parent class with the given name for the given class */
static void restore_class_parent(char *parent, int len, zend_class_entry * to TSRMLS_DC)
{
    zend_class_entry** parent_ptr = NULL;
    if (zend_lookup_class_ex(parent,
                                len,
#ifdef ZEND_ENGINE_2_4
                                NULL,
#endif
                                0,
                                &parent_ptr TSRMLS_CC)) {
        /* parent found */
        to->parent = *parent_ptr;
        DBG(ea_debug_printf, (EA_DEBUG, "restore_class_parent: found parent %s..\n", to->parent->name));
        DBG(ea_debug_printf, (EA_DEBUG, "restore_class_parent: parent type=%d child type=%d\n", to->parent->type, to->type));
    } else {
        ea_debug_error("[%d] EACCELERATOR can't restore parent class \"%s\" of class \"%s\"\n", getpid(), (char *) parent, to->name);
        to->parent = NULL;
    }
}

static void restore_class_methods(zend_class_entry * to TSRMLS_DC)
{
    int cname_len = to->name_length;
    char *cname_lc = zend_str_tolower_dup(to->name, cname_len);
    int fname_len = 0;
    char *fname_lc = NULL;
    zend_function *f = NULL;
    Bucket *p = to->function_table.pListHead;

    to->constructor = NULL;

    while (p != NULL) {
        f = p->pData;
        fname_len = strlen(f->common.function_name);
        fname_lc = zend_str_tolower_dup(f->common.function_name, fname_len);

        /* only put the function that has the same name as the class as contructor if there isn't a __construct function */
        if (fname_len == cname_len && !memcmp(fname_lc, cname_lc, fname_len) && f->common.scope != to->parent
                && to->constructor == NULL) {
            to->constructor = f;
        } else if (fname_lc[0] == '_' && fname_lc[1] == '_' && f->common.scope != to->parent) {
            if (fname_len == sizeof(ZEND_CONSTRUCTOR_FUNC_NAME) - 1 &&
                    memcmp(fname_lc, ZEND_CONSTRUCTOR_FUNC_NAME, sizeof(ZEND_CONSTRUCTOR_FUNC_NAME)) == 0) {
                to->constructor = f;
            } else if (fname_len == sizeof(ZEND_DESTRUCTOR_FUNC_NAME) - 1 &&
                       memcmp(fname_lc, ZEND_DESTRUCTOR_FUNC_NAME, sizeof(ZEND_DESTRUCTOR_FUNC_NAME)) == 0) {
                to->destructor = f;
            } else if (fname_len == sizeof(ZEND_CLONE_FUNC_NAME) - 1 &&
                       memcmp(fname_lc, ZEND_CLONE_FUNC_NAME, sizeof(ZEND_CLONE_FUNC_NAME)) == 0) {
                to->clone = f;
            } else if (fname_len == sizeof(ZEND_GET_FUNC_NAME) - 1 &&
                       memcmp(fname_lc, ZEND_GET_FUNC_NAME, sizeof(ZEND_GET_FUNC_NAME)) == 0) {
                to->__get = f;
            } else if (fname_len == sizeof(ZEND_SET_FUNC_NAME) - 1 &&
                       memcmp(fname_lc, ZEND_SET_FUNC_NAME, sizeof(ZEND_SET_FUNC_NAME)) == 0) {
                to->__set = f;
            } else if (fname_len == sizeof(ZEND_UNSET_FUNC_NAME) - 1 &&
                       memcmp(fname_lc, ZEND_UNSET_FUNC_NAME, sizeof(ZEND_UNSET_FUNC_NAME)) == 0) {
                to->__unset = f;
            } else if (fname_len == sizeof(ZEND_ISSET_FUNC_NAME) - 1 &&
                       memcmp(fname_lc, ZEND_ISSET_FUNC_NAME, sizeof(ZEND_ISSET_FUNC_NAME)) == 0) {
                to->__isset = f;
            } else if (fname_len == sizeof(ZEND_CALL_FUNC_NAME) - 1 &&
                       memcmp(fname_lc, ZEND_CALL_FUNC_NAME, sizeof(ZEND_CALL_FUNC_NAME)) == 0) {
                to->__call = f;
            }
#  ifdef ZEND_ENGINE_2_3
            else if (fname_len == sizeof(ZEND_CALLSTATIC_FUNC_NAME) - 1 &&
                     memcmp(fname_lc, ZEND_CALLSTATIC_FUNC_NAME, sizeof(ZEND_CALLSTATIC_FUNC_NAME)) == 0) {
                to->__callstatic = f;
            }
#  endif
#  ifdef ZEND_ENGINE_2_2
            else if (fname_len == sizeof(ZEND_TOSTRING_FUNC_NAME) - 1 &&
                     memcmp(fname_lc, ZEND_TOSTRING_FUNC_NAME, sizeof(ZEND_TOSTRING_FUNC_NAME)) == 0) {
                to->__tostring = f;
            }
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

static zend_class_entry *restore_class_entry(zend_class_entry * to, ea_class_entry * from TSRMLS_DC)
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

    if (from->name != NULL) {
        to->name_length = from->name_length;
        to->name = emalloc(from->name_length + 1);
        memcpy((char*)to->name, from->name, from->name_length + 1);
    }

    old = EAG(class_entry);
    EAG(class_entry) = to;

    to->ce_flags = from->ce_flags;
    to->num_interfaces = from->num_interfaces;
    to->interfaces = NULL;
    to->refcount = 1;
#ifdef ZEND_ENGINE_2_4
    to->info.user.line_start = from->line_start;
    to->info.user.line_end = from->line_end;
#else
    to->line_start = from->line_start;
    to->line_end = from->line_end;
#endif

    if (to->num_interfaces > 0) {
        /* hrak: Allocate the slots which will later be populated by ZEND_ADD_INTERFACE */
        to->interfaces = (zend_class_entry **) emalloc(sizeof(zend_class_entry *) * to->num_interfaces);
        memset(to->interfaces, 0, sizeof(zend_class_entry *) * to->num_interfaces);
    }
#ifdef INCLUDE_DOC_COMMENTS
#  ifdef ZEND_ENGINE_2_4
    to->info.user.doc_comment_len = from->doc_comment_len;
    if (from->doc_comment != NULL) {
        to->info.user.doc_comment = emalloc(from->doc_comment_len + 1);
        memcpy((char*)to->info.user.doc_comment, from->doc_comment, from->doc_comment_len + 1);
    }
#  else
    to->doc_comment_len = from->doc_comment_len;
    if (from->doc_comment != NULL) {
        to->doc_comment = emalloc(from->doc_comment_len + 1);
        memcpy(to->doc_comment, from->doc_comment, from->doc_comment_len + 1);
    }
#  endif
#else
#  ifdef ZEND_ENGINE_2_4
    to->info.user.doc_comment_len = 0;
    to->info.user.doc_comment = NULL;
#  else
    to->doc_comment_len = 0;
    to->doc_comment = NULL;
#  endif
#endif

#ifdef ZEND_ENGINE_2_4
    to->info.user.filename = from->filename;
#else
    to->filename = from->filename;
#endif

    /* restore constants table */
    restore_zval_hash(&to->constants_table, &from->constants_table);
    to->constants_table.pDestructor = ZVAL_PTR_DTOR;

    /* restore properties */
    restore_hash(&to->properties_info, &from->properties_info, (restore_bucket_t) restore_property_info TSRMLS_CC);
    to->properties_info.pDestructor = properties_info_dtor;

#ifdef ZEND_ENGINE_2_4
    to->default_properties_count = from->default_properties_count;
    int i;
    if (from->default_properties_count) {
        to->default_properties_table = (zval **) emalloc((sizeof(zval*) * from->default_properties_count));
        for (i = 0; i < from->default_properties_count; i++) {
            if (from->default_properties_table[i]) {
                to->default_properties_table[i] = restore_zval_ptr((zval*)from->default_properties_table[i] TSRMLS_CC);
            } else {
                to->default_properties_table[i] = NULL;
            }
        }
    } else {
        to->default_properties_table = NULL;
    }

    to->default_static_members_count = from->default_static_members_count;
    if (from->default_static_members_count > 0) {
        to->default_static_members_table = (zval **) emalloc((sizeof(zval*) * from->default_static_members_count));
        for (i = 0; i < from->default_static_members_count; i++) {
            if (from->default_static_members_table[i]) {
                to->default_static_members_table[i] = restore_zval_ptr((zval*)from->default_static_members_table[i] TSRMLS_CC);
            } else {
                to->default_static_members_table[i] = NULL;
            }
        }
    } else {
        to->default_static_members_table = NULL;
    }
    to->static_members_table = to->default_static_members_table;
#else
    /* restore default properties */
    restore_zval_hash(&to->default_properties, &from->default_properties);
    to->default_properties.pDestructor = ZVAL_PTR_DTOR;

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
#endif

    if (from->parent != NULL) {
        restore_class_parent(from->parent, strlen(from->parent), to TSRMLS_CC);
    } else {
        DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
        DBG(ea_debug_printf, (EA_DEBUG, "[%d] parent = NULL\n", getpid()));
        to->parent = NULL;
    }

    restore_hash(&to->function_table, &from->function_table, (restore_bucket_t)restore_op_array_ptr TSRMLS_CC);
    to->function_table.pDestructor = ZEND_FUNCTION_DTOR;

    restore_class_methods(to TSRMLS_CC);

    if (to->parent) {
        zend_do_inheritance(to, to->parent TSRMLS_CC);
    }
    EAG(class_entry) = old;

#ifdef DEBUG
    EAG(xpad)--;
#endif
    return to;
}

void restore_function(ea_fc_entry * p TSRMLS_DC)
{
    zend_op_array op_array;

    if (p->htabkey[0] == '\0' && p->htablen != 0) {
        if (zend_hash_exists(CG(function_table), p->htabkey, p->htablen)) {
            return;
        }
    }
    if (restore_op_array(&op_array, (ea_op_array *) p->fc TSRMLS_CC) != NULL) {
        if (zend_hash_add(CG(function_table), p->htabkey, p->htablen, &op_array, sizeof(zend_op_array), NULL) == FAILURE) {
            CG(in_compilation) = 1;
            CG(compiled_filename) = EAG(mem);
            CG(zend_lineno) = op_array.line_start;
            zend_error(E_ERROR, "Cannot redeclare %s()", p->htabkey);
        }
    }
}

/*
 * Class handling.
 */
void restore_class(ea_fc_entry * p TSRMLS_DC)
{
    zend_class_entry *ce;

    if (p->htabkey[0] == '\0' && p->htablen != 0) {
        if (zend_hash_exists(CG(class_table), p->htabkey, p->htablen)) {
            return;
        }
    }
    ce = restore_class_entry(NULL, (ea_class_entry *) p->fc TSRMLS_CC);
    if (ce != NULL) {
        if (zend_hash_add(CG(class_table), p->htabkey, p->htablen, &ce, sizeof(zend_class_entry *), NULL) == FAILURE) {
            CG(in_compilation) = 1;
            CG(compiled_filename) = EAG(mem);
#ifdef ZEND_ENGINE_2_4
            CG(zend_lineno) = ce->info.user.line_start;
#else
            CG(zend_lineno) = ce->line_start;
#endif
            zend_error(E_ERROR, "Cannot redeclare class %s", p->htabkey);
        }
    }
}

#endif /* HAVE_EACCELERATOR */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: et sw=4 ts=4 fdm=marker
 * vim<600: et sw=4 ts=4
 */
