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
   $Id: ea_store.c 418 2010-06-03 11:13:47Z hans $
*/

#include "eaccelerator.h"

#ifdef HAVE_EACCELERATOR

#include "ea_store.h"
#include "debug.h"

/******************************************************************************/
/* Functions to calculate the size of different structure that a compiled php */
/* script contains.                                                           */
/* Each function needs to return a size that is aligned to the machine word   */
/* size.                                                                      */
/******************************************************************************/

// add the given length to the size var and aling it
#define ADDSIZE(size, len) (size) += (len); \
    EA_SIZE_ALIGN(size);

#ifndef DEBUG
inline
#endif
#ifdef ZEND_ENGINE_2_3
static size_t calc_string(const char *str, int len TSRMLS_DC)
#else
static size_t calc_string(char *str, int len TSRMLS_DC)
#endif
{
    if (len > MAX_DUP_STR_LEN || zend_hash_add(&EAG(strings), str, len, (void *) &str, sizeof(char *), NULL) == SUCCESS) {
        EA_SIZE_ALIGN(len);
        return len;
    }
    return 0;
}

typedef size_t (*calc_bucket_t) (void * TSRMLS_DC);

#define calc_hash_ex(from, start, calc_bucket) \
  calc_hash_int(from, start, calc_bucket TSRMLS_CC)

#define calc_hash(from, calc_bucket) \
  calc_hash_ex(from, (from)->pListHead, calc_bucket)

#define calc_zval_hash(from) \
  calc_hash(from, (calc_bucket_t)calc_zval_ptr)

#define calc_zval_hash_ex(from, start) \
  calc_hash_ex(from, start, (calc_bucket_t)calc_zval_ptr)

static size_t calc_zval_ptr(zval ** from TSRMLS_DC)
{
    size_t size = 0;

    ADDSIZE(size, sizeof(zval));
    size += calc_zval(*from TSRMLS_CC);

    return size;
}

static size_t calc_property_info(zend_property_info * from TSRMLS_DC)
{
    size_t size = 0;

    ADDSIZE(size, sizeof(zend_property_info));

    size += calc_string(from->name, from->name_length + 1 TSRMLS_CC);
#ifdef INCLUDE_DOC_COMMENTS
    if (from->doc_comment != NULL) {
        size += calc_string(from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);
    }
#endif
    return size;
}

/* Calculate the size of an HashTable */
static size_t calc_hash_int(HashTable * source, Bucket * start,
                            calc_bucket_t calc_bucket TSRMLS_DC)
{
    Bucket *p;
    size_t size = 0;

    if (source->nNumOfElements > 0) {
        ADDSIZE(size, source->nTableSize * sizeof(Bucket *));
        p = start;
        while (p) {
#ifdef ZEND_ENGINE_2_4
//            if (!IS_INTERNED(p->arKey) && p->nKeyLength) {
            ADDSIZE(size, sizeof(Bucket));
            ADDSIZE(size, p->nKeyLength);
//            }
#else
            ADDSIZE(size, offsetof(Bucket, arKey) + p->nKeyLength);
#endif
            size += calc_bucket(p->pData TSRMLS_CC);
            p = p->pListNext;
        }
    }
    return size;
}

size_t calc_zval(zval *zv TSRMLS_DC)
{
    size_t size = 0;

    switch (EA_ZV_TYPE_P(zv)) {
    case IS_CONSTANT:
    case IS_OBJECT: /* object should have been serialized before storing them */
    case IS_STRING:
        size += calc_string(Z_STRVAL_P(zv), Z_STRLEN_P(zv) + 1 TSRMLS_CC);
        break;

    case IS_ARRAY:
    case IS_CONSTANT_ARRAY:
        if (Z_ARRVAL_P(zv) != NULL && Z_ARRVAL_P(zv) != &EG(symbol_table)) {
            ADDSIZE(size, sizeof(HashTable));
            size += calc_zval_hash(Z_ARRVAL_P(zv));
        }
        break;

    case IS_RESOURCE:
        DBG(ea_debug_error, ("[%d] EACCELERATOR can't cache resources\n", getpid()));
        zend_bailout();
        break;
    default:
        break;
    }
    return size;
}

/* Calculate the size of an op_array */
static size_t calc_op_array(zend_op_array * from TSRMLS_DC)
{
    size_t size = 0;
#ifndef ZEND_ENGINE_2_4
    zend_op *opline, *end;
#endif

    if (from->type == ZEND_INTERNAL_FUNCTION) {
        ADDSIZE(size, sizeof(zend_internal_function));
    } else if (from->type == ZEND_USER_FUNCTION) {
        ADDSIZE(size, sizeof(ea_op_array));
    } else {
        DBG(ea_debug_error, ("[%d] EACCELERATOR can't cache function \"%s\"\n", getpid(), from->function_name));
        zend_bailout();
    }
    if (from->num_args > 0) {
        zend_uint i;

        ADDSIZE(size, from->num_args * sizeof(zend_arg_info));
        for (i = 0; i < from->num_args; i++) {
            if (from->arg_info[i].name) {
                size += calc_string(from->arg_info[i].name, from->arg_info[i].name_len + 1 TSRMLS_CC);
            }
            if (from->arg_info[i].class_name) {
                size += calc_string(from->arg_info[i].class_name, from->arg_info[i].class_name_len + 1 TSRMLS_CC);
            }
        }
    }
    if (from->function_name != NULL) {
        size += calc_string(from->function_name, strlen(from->function_name) + 1 TSRMLS_CC);
    }
    if (from->scope != NULL) {
        // HOESH: the same problem?
        Bucket *q = CG(class_table)->pListHead;

        while (q != NULL) {
            if (*(zend_class_entry **) q->pData == from->scope) {
                size += calc_string(q->arKey, q->nKeyLength TSRMLS_CC);
                break;
            }
            q = q->pListNext;
        }
    }
    if (from->type == ZEND_INTERNAL_FUNCTION) {
        return size;
    }

    if (from->opcodes != NULL) {
        ADDSIZE(size, from->last * sizeof(zend_op));

#ifndef ZEND_ENGINE_2_4
        opline = from->opcodes;
        end = opline + from->last;
        for (; opline < end; opline++) {
            if (opline->op1.op_type == IS_CONST) {
                size += calc_zval(&opline->op1.u.constant TSRMLS_CC);
            }
            if (opline->op2.op_type == IS_CONST) {
                size += calc_zval(&opline->op2.u.constant TSRMLS_CC);
            }
        }
#endif
    }
#ifdef ZEND_ENGINE_2_4
    if (from->literals != NULL) {
        zend_literal *l, *end;

        ADDSIZE(size, sizeof(zend_literal) * from->last_literal);

        l = from->literals;
        end = l + from->last_literal;
        for (; l < end; l++) {
            size += calc_zval(&l->constant TSRMLS_CC);
        }
    }
#endif
    if (from->brk_cont_array != NULL) {
        ADDSIZE(size, sizeof(zend_brk_cont_element) * from->last_brk_cont);
    }
    if (from->try_catch_array != NULL) {
        ADDSIZE(size, sizeof(zend_try_catch_element) * from->last_try_catch);
    }
    if (from->static_variables != NULL) {
        ADDSIZE(size, sizeof(HashTable));
        size += calc_zval_hash(from->static_variables);
    }
    if (from->vars != NULL) {
        int i;

        ADDSIZE(size, sizeof(zend_compiled_variable) * from->last_var);
        for (i = 0; i < from->last_var; i ++) {
            size += calc_string(from->vars[i].name, from->vars[i].name_len+1 TSRMLS_CC);
        }
    }
    if (from->filename != NULL) {
        size += calc_string(from->filename, strlen(from->filename) + 1 TSRMLS_CC);
    }
#ifdef INCLUDE_DOC_COMMENTS
    if (from->doc_comment != NULL) {
        size += calc_string(from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);
    }
#endif

    return size;
}

/* Calculate the size of a class entry */
static size_t calc_class_entry(zend_class_entry * from TSRMLS_DC)
{
    size_t size = 0;
    if (from->type != ZEND_USER_CLASS) {
        DBG(ea_debug_error, ("[%d] EACCELERATOR can't cache internal class \"%s\"\n", getpid(), from->name));
        zend_bailout();
    }
    ADDSIZE(size, sizeof(ea_class_entry));

    if (from->name != NULL) {
        size += calc_string(from->name, from->name_length + 1 TSRMLS_CC);
    }
    if (from->parent != NULL && from->parent->name) {
        size += calc_string(from->parent->name, from->parent->name_length + 1 TSRMLS_CC);
    }
#ifdef ZEND_ENGINE_2_4
    if (from->info.user.filename != NULL) {
        size += calc_string(from->info.user.filename, strlen(from->info.user.filename) + 1 TSRMLS_CC);
    }
#  ifdef INCLUDE_DOC_COMMENTS
    if (from->info.user.doc_comment != NULL) {
        size += calc_string(from->info.user.doc_comment, from->info.user.doc_comment_len + 1 TSRMLS_CC);
    }
#  endif
#else
    if (from->filename != NULL) {
        size += calc_string(from->filename, strlen(from->filename) + 1 TSRMLS_CC);
    }
#  ifdef INCLUDE_DOC_COMMENTS
    if (from->doc_comment != NULL) {
        size += calc_string(from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);
    }
#  endif
#endif

    size += calc_zval_hash(&from->constants_table);
    size += calc_hash(&from->properties_info, (calc_bucket_t) calc_property_info);
#ifdef ZEND_ENGINE_2_4
    if (from->default_properties_count) {
        int i = 0;

        ADDSIZE(size, sizeof(zval *) * from->default_properties_count);
        for (i = 0; i < from->default_properties_count; i++) {
            if (from->default_properties_table[i]) {
                size += calc_zval_ptr(&from->default_properties_table[i] TSRMLS_CC);
            }
        }
    }
    if (from->default_static_members_count) {
        int i = 0;

        ADDSIZE(size, sizeof(zval *) * from->default_static_members_count);
        for (i = 0; i < from->default_static_members_count; i++) {
            if (from->default_static_members_table[i]) {
                size += calc_zval_ptr(&from->default_static_members_table[i] TSRMLS_CC);
            }
        }
    }
#else
    size += calc_zval_hash(&from->default_properties);

    size += calc_zval_hash(&from->default_static_members);
    if ((from->static_members != NULL) && (from->static_members != &from->default_static_members)) {
        ADDSIZE(size, sizeof(HashTable));
        size += calc_zval_hash(from->static_members);
    }
#endif
    size += calc_hash(&from->function_table, (calc_bucket_t) calc_op_array);

    return size;
}

/* Calculate the size of a cache entry with its given op_array and function and
   class bucket */
size_t calc_size(char *key, zend_op_array * op_array, Bucket * f, Bucket * c TSRMLS_DC)
{
    Bucket *b;
    const char *x;
    int len = strlen(key);
    size_t size = 0;

    zend_hash_init(&EAG(strings), 0, NULL, NULL, 0);
    ADDSIZE(size, offsetof(ea_cache_entry, realfilename) + len + 1);
    zend_hash_add(&EAG(strings), key, len + 1, &key, sizeof(char *), NULL);
    b = c;
    while (b != NULL) {
        ADDSIZE(size, offsetof(ea_fc_entry, htabkey) + b->nKeyLength);

        x = b->arKey;
        zend_hash_add(&EAG(strings), b->arKey, b->nKeyLength, &x, sizeof(char *), NULL);
        b = b->pListNext;
    }
    b = f;
    while (b != NULL) {
        ADDSIZE(size, offsetof(ea_fc_entry, htabkey) + b->nKeyLength);

        x = b->arKey;
        zend_hash_add(&EAG(strings), b->arKey, b->nKeyLength, &x, sizeof(char *), NULL);
        b = b->pListNext;
    }
    while (c != NULL) {
        size += calc_class_entry(*(zend_class_entry **) c->pData TSRMLS_CC);
        c = c->pListNext;
    }
    while (f != NULL) {
        size += calc_op_array((zend_op_array *) f->pData TSRMLS_CC);
        f = f->pListNext;
    }
    size += calc_op_array(op_array TSRMLS_CC);
    zend_hash_destroy(&EAG(strings));

    return size;
}

// this macro returns the current position to place data and advances the pointer
// to the next positions and aligns it
#define ALLOCATE(at, len) (*at);\
    (*at) += (len); \
    EACCELERATOR_ALIGN((*at));

/* ugh, dirty */
#ifdef ZEND_ENGINE_2_4
static inline const char *store_string(char **at, const char *str, int len TSRMLS_DC)
#elif  defined(ZEND_ENGINE_2_3)
static inline char *store_string(char **at, const char *str, int len TSRMLS_DC)
#else
static inline char *store_string(char **at, char *str, int len TSRMLS_DC)
#endif
{
    char *p;

    if (len > MAX_DUP_STR_LEN) {
        p = ALLOCATE(at, len);
        memcpy(p, str, len);
    } else if (zend_hash_find(&EAG(strings), str, len , (void *) &p) == SUCCESS) {
        p = *(char **) p;
    } else {
        p = ALLOCATE(at, len);
        memcpy(p, str, len);
        zend_hash_add(&EAG(strings), str, len, (void *) &p, sizeof(char *), NULL);
    }
    return p;
}

typedef void *(*store_bucket_t) (char **, void * TSRMLS_DC);
typedef void *(*check_bucket_t) (Bucket *, zend_class_entry *);

#define store_hash_ex(p, to, from, start, store_bucket, check_bucket, from_ce) \
  store_hash_int(p, to, from, start, store_bucket, check_bucket, from_ce)

#define store_hash(p, to, from, store_bucket, check_bucket, from_ce) \
  store_hash_ex(p, to, from, (from)->pListHead, store_bucket, check_bucket, from_ce)

#define store_zval_hash(p, to, from) \
  store_hash(p, to, from, (store_bucket_t)store_zval_ptr, NULL, NULL)

#define store_zval_hash_ex(p, to, from, start) \
  store_hash_ex(p, to, from, start, (store_bucket_t)store_zval_ptr, NULL)

static zval *store_zval_ptr(char **at, zval *from TSRMLS_DC)
{
    zval *to = (zval *)ALLOCATE(at, sizeof(zval));

    memcpy(to, from, sizeof(zval));
    store_zval(at, to TSRMLS_CC);
    return to;
}

static void store_hash_int(char **at, HashTable *target, HashTable *source,
                           Bucket *start, store_bucket_t copy_bucket,
                           check_bucket_t check_bucket,
                           zend_class_entry *from_ce)
{
    Bucket *p, *np, *prev_p;
    int nIndex;

    TSRMLS_FETCH();

    assert(target != NULL);
    assert(source != NULL);

    memcpy(target, source, sizeof(HashTable));

    if (source->nNumOfElements > 0) {
        target->arBuckets = (Bucket **)ALLOCATE(at, target->nTableSize * sizeof(Bucket *));
        memset(target->arBuckets, 0, target->nTableSize * sizeof(Bucket *));

        target->pDestructor = NULL;
        target->persistent = 1;
        target->pInternalPointer = NULL;
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

#ifdef ZEND_ENGINE_2_4
//            if (IS_INTERNED(p->arKey)) {
            /* TODO */
//                DBG(ea_debug_printf, (EA_DEBUG, "[%d] store_hash_int: storing interned arKey '%s'\n", getpid(), p->arKey));
//                np = (Bucket *)ALLOCATE(at, sizeof(Bucket));
//                memcpy(np, p, sizeof(Bucket));
//            } else if (!p->nKeyLength) {
//                DBG(ea_debug_printf, (EA_DEBUG, "[%d] store_hash_int: storing zero length arKey '%s'\n", getpid(), p->arKey));
//                np = (Bucket *)ALLOCATE(at, sizeof(Bucket));
//                memcpy(np, p, sizeof(Bucket));
//            } else {
            DBG(ea_debug_printf, (EA_DEBUG, "[%d] store_hash_int: storing regular arKey '%s'\n", getpid(), p->arKey));
            np = (Bucket *)ALLOCATE(at, sizeof(Bucket));
            memcpy(np, p, sizeof(Bucket));
            np->arKey = (char *)ALLOCATE(at, p->nKeyLength);
            memcpy((char*)np->arKey, p->arKey, p->nKeyLength);
//            }
#else
            np = (Bucket *)ALLOCATE(at, offsetof(Bucket, arKey) + p->nKeyLength);
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
                np->pData = copy_bucket(at, p->pData TSRMLS_CC);
                np->pDataPtr = NULL;
            } else {
                np->pDataPtr = copy_bucket(at, p->pDataPtr TSRMLS_CC);
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
    }
}

void store_zval(char **at, zval *zv TSRMLS_DC)
{
    switch (EA_ZV_TYPE_P(zv)) {
    case IS_CONSTANT:
    case IS_OBJECT: /* object should have been serialized before storing them */
    case IS_STRING:
        Z_STRVAL_P(zv) = (char*)store_string(at, Z_STRVAL_P(zv), Z_STRLEN_P(zv) + 1 TSRMLS_CC);
        break;

    case IS_ARRAY:
    case IS_CONSTANT_ARRAY:
        if (Z_ARRVAL_P(zv) != NULL && Z_ARRVAL_P(zv) != &EG(symbol_table)) {
            HashTable *q;
            q = (HashTable *)ALLOCATE(at, sizeof(HashTable));
            store_zval_hash(at, q, Z_ARRVAL_P(zv));
            Z_ARRVAL_P(zv) = q;
        }
        break;

    default:
        break;
    }
}

static ea_op_array *store_op_array(char **at, zend_op_array * from TSRMLS_DC)
{
    ea_op_array *to;
    zend_op *opline;
    zend_uint i;

    DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
    DBG(ea_debug_printf, (EA_DEBUG, "[%d] store_op_array: %s [scope=%s type=%x]\n",
                          getpid(), from->function_name ? from->function_name : "(top)",
                          from->scope ? from->scope->name : "NULL"
                          , from->type
                         ));

    if (from->type == ZEND_INTERNAL_FUNCTION) {
        to = (ea_op_array *)ALLOCATE(at, sizeof(zend_internal_function));
    } else if (from->type == ZEND_USER_FUNCTION) {
        to = (ea_op_array *)ALLOCATE(at, sizeof(ea_op_array));
    } else {
        return NULL;
    }

    to->type = from->type;
    to->num_args = from->num_args;
    to->required_num_args = from->required_num_args;

    if (from->num_args > 0) {
        to->arg_info = (zend_arg_info *)ALLOCATE(at, from->num_args * sizeof(zend_arg_info));
        memcpy(to->arg_info, from->arg_info, from->num_args * sizeof(zend_arg_info));
        
        for (i = 0; i < from->num_args; i++) {
            if (from->arg_info[i].name) {
                to->arg_info[i].name = store_string(at, from->arg_info[i].name, from->arg_info[i].name_len + 1 TSRMLS_CC);
            }
            if (from->arg_info[i].class_name) {
                to->arg_info[i].class_name = store_string(at, from->arg_info[i].class_name, from->arg_info[i].class_name_len + 1 TSRMLS_CC);
            }
        }
    }

#ifndef ZEND_ENGINE_2_4
    to->pass_rest_by_reference = from->pass_rest_by_reference;
#endif
    if (from->function_name != NULL) {
        to->function_name = store_string(at, from->function_name, strlen(from->function_name) + 1 TSRMLS_CC);
    }
    to->fn_flags = from->fn_flags;
    to->scope_name = NULL;
    to->scope_name_len = 0;
    if (from->scope != NULL) {
        Bucket *q = CG(class_table)->pListHead;

        while (q != NULL) {
            if (*(zend_class_entry **) q->pData == from->scope) {
                to->scope_name = (char*)store_string(at, q->arKey, q->nKeyLength TSRMLS_CC);
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

    if (from->type == ZEND_INTERNAL_FUNCTION) {
#ifndef ZEND_ENGINE_2_4
        /* zend_internal_function also contains return_reference in ZE2 */
        to->return_reference = from->return_reference;
#endif
        return to;
    }

    to->last = from->last;
    to->T = from->T;
#ifdef ZEND_ENGINE_2_3
    to->this_var = from->this_var;
    to->early_binding = from->early_binding;
#else
    to->uses_this = from->uses_this;
#endif
#ifdef ZEND_ENGINE_2_5
    to->nested_calls = from->nested_calls;
    to->used_stack = from->used_stack;
#endif

#ifdef ZEND_ENGINE_2_4
    if (from->literals != NULL) {
        zend_literal *p, *q, *end;

        to->literals = (zend_literal *)ALLOCATE(at, sizeof(zend_literal) * from->last_literal);
        memcpy(to->literals, from->literals, sizeof(zend_literal) * from->last_literal);

        q = from->literals;
        p = to->literals;
        end = p + from->last_literal;
        while (p < end) {
            *p = *q;
            store_zval(at, &p->constant TSRMLS_CC);
            p++;
            q++;
        }
    } else {
        to->literals = NULL;
    }
    to->last_literal = from->last_literal;

    to->last_cache_slot = from->last_cache_slot;
#endif

#ifndef ZEND_ENGINE_2_4
    to->return_reference = from->return_reference;
#endif
    to->filename = from->filename;

    if (from->opcodes != NULL) {
        to->opcodes = (zend_op *)ALLOCATE(at, from->last * sizeof(zend_op));
        memcpy(to->opcodes, from->opcodes, from->last * sizeof(zend_op));

        for (i = 0; i < from->last; i++) {
            opline = &(to->opcodes[i]);

#ifndef ZEND_ENGINE_2_4
            if (opline->op1.op_type == IS_CONST) {
                store_zval(at, &opline->op1.u.constant TSRMLS_CC);
            }
            if (opline->op2.op_type == IS_CONST) {
                store_zval(at, &opline->op2.u.constant TSRMLS_CC);
            }
#endif
            switch (opline->opcode) {
#ifdef ZEND_GOTO
            case ZEND_GOTO:
#endif
            case ZEND_JMP:
#ifdef ZEND_ENGINE_2_4
                opline->op1.jmp_addr = to->opcodes + (opline->op1.jmp_addr - from->opcodes);
#else
                opline->op1.u.jmp_addr = to->opcodes + (opline->op1.u.jmp_addr - from->opcodes);
#endif
                break;
            case ZEND_JMPZ:
            case ZEND_JMPNZ:
            case ZEND_JMPZ_EX:
            case ZEND_JMPNZ_EX:
#ifdef ZEND_JMP_SET
            case ZEND_JMP_SET:
#endif
#ifdef ZEND_ENGINE_2_4
            case ZEND_JMP_SET_VAR:
                opline->op2.jmp_addr = to->opcodes + (opline->op2.jmp_addr - from->opcodes);
#else
                opline->op2.u.jmp_addr = to->opcodes + (opline->op2.u.jmp_addr - from->opcodes);
#endif
                break;
            }

#ifdef ZEND_ENGINE_2_4
            if (opline->op1_type == IS_CONST) {
                to->opcodes[i].op1.literal = from->opcodes[i].op1.literal - from->literals + to->literals;
            }
            if (opline->op2_type == IS_CONST) {
                to->opcodes[i].op2.literal = from->opcodes[i].op2.literal - from->literals + to->literals;
            }
#endif
        }
    }

    to->last_try_catch = from->last_try_catch;
    if (from->try_catch_array != NULL) {
        to->try_catch_array = (zend_try_catch_element *)ALLOCATE(at, sizeof(zend_try_catch_element) * from->last_try_catch);
        memcpy(to->try_catch_array, from->try_catch_array, sizeof(zend_try_catch_element) * from->last_try_catch);
    }

    to->last_brk_cont = from->last_brk_cont;
    if (from->brk_cont_array != NULL) {
        to->brk_cont_array = (zend_brk_cont_element *)ALLOCATE(at, sizeof(zend_brk_cont_element) * from->last_brk_cont);
        memcpy(to->brk_cont_array, from->brk_cont_array, sizeof(zend_brk_cont_element) * from->last_brk_cont);
    }

    if (from->static_variables != NULL) {
        to->static_variables = (HashTable *)ALLOCATE(at, sizeof(HashTable));
        store_zval_hash(at, to->static_variables, from->static_variables);
    } else {
        to->static_variables = NULL;
    }

    if (from->vars != NULL) {
        int i;

        to->last_var = from->last_var;
        to->vars = (zend_compiled_variable*)ALLOCATE(at, sizeof(zend_compiled_variable) * from->last_var);
        memcpy(to->vars, from->vars, sizeof(zend_compiled_variable) * from->last_var);
        for (i = 0; i < from->last_var; i++) {
            to->vars[i].name = store_string(at, from->vars[i].name, from->vars[i].name_len + 1 TSRMLS_CC);
        }
    } else {
        to->last_var = 0;
        to->vars = NULL;
    }

    to->line_start = from->line_start;
    to->line_end = from->line_end;

#ifdef INCLUDE_DOC_COMMENTS
    to->doc_comment_len = from->doc_comment_len;
    if (from->doc_comment != NULL) {
        to->doc_comment = store_string(at, from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);
    }
#endif

    if (from->filename != NULL) {
        to->filename = store_string(at, from->filename, strlen(from->filename) + 1 TSRMLS_CC);
    }

    return to;
}

static zend_property_info *store_property_info(char **at, zend_property_info * from TSRMLS_DC)
{
    zend_property_info *to;

    to = (zend_property_info *)ALLOCATE(at, sizeof(zend_property_info));

    memcpy(to, from, sizeof(*from));

    to->name = NULL;

    if (from->name) {
        to->name = store_string(at, from->name, from->name_length + 1 TSRMLS_CC);
    }
#ifdef INCLUDE_DOC_COMMENTS
    to->doc_comment_len = from->doc_comment_len;
    if (from->doc_comment) {
        to->doc_comment = store_string(at, from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);
    }
#else
    to->doc_comment_len = 0;
    to->doc_comment = NULL;
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
 * do_inherit_property_access_check
*/
static int store_property_access_check(Bucket * p, zend_class_entry * from)
{
    zend_class_entry *parent = from->parent;
    zend_property_info* child_info = (zend_property_info*)p->pData;
    zend_property_info* parent_info = NULL;

#ifdef ZEND_ENGINE_2_2
    return (child_info->ce != from);
#endif

    if (parent && zend_hash_quick_find(&parent->properties_info, p->arKey, p->nKeyLength, p->h, (void **) &parent_info)==SUCCESS) {
        if(parent_info->flags & ZEND_ACC_PRIVATE) {
            return ZEND_HASH_APPLY_KEEP;
        }
        /* if public/private/protected mask differs: copy, else let zend_do_inheritance handle this */
        if((parent_info->flags & ZEND_ACC_PPP_MASK) != (child_info->flags & ZEND_ACC_PPP_MASK)) {
            return ZEND_HASH_APPLY_KEEP;
        }
        return ZEND_HASH_APPLY_REMOVE;
    }
    return ZEND_HASH_APPLY_KEEP;
}

#ifndef ZEND_ENGINE_2_4

static int store_default_property_access_check(Bucket * p, zend_class_entry * from)
{
    zend_class_entry *parent = from->parent;
    union {
        zend_property_info *v;
        void *ptr;
    } pinfo;
    union {
        zval **v;
        void *ptr;
    } pprop, cprop;

    cprop.v = p->pData;
    pprop.v = NULL;
    /* Check if this is a parent class. If so, copy unconditionally */
    if (parent &&
            zend_hash_quick_find(&parent->default_properties, p->arKey, p->nKeyLength, p->h, &pinfo.ptr)==SUCCESS) {

        if ((pprop.v == cprop.v) && (*pprop.v == *cprop.v)) {
            return ZEND_HASH_APPLY_REMOVE;
        }
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
#  ifdef ZEND_ENGINE_2_2
        zend_unmangle_property_name(p->arKey, p->nKeyLength-1, &cname, &mname);
#  else
        zend_unmangle_property_name(p->arKey, &cname, &mname);
#  endif

        /* lookup the member's info in parent and child */
        if((zend_hash_find(&parent->properties_info, mname, strlen(mname)+1, &pinfo.ptr) == SUCCESS) &&
                (zend_hash_find(&from->properties_info, mname, strlen(mname)+1, &cinfo.ptr) == SUCCESS)) {
            /* If the static member points to the same value in parent and child, remove for proper inheritance during restore */
            if(zend_hash_quick_find(&parent->default_static_members, p->arKey, p->nKeyLength, p->h, &pprop.ptr) == SUCCESS) {
                if(*pprop.v == *cprop.v) {
                    return ZEND_HASH_APPLY_REMOVE;
                }
            }
        }
    }
    return ZEND_HASH_APPLY_KEEP;
}

#endif /* ZEND_ENGINE_2_4 */

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

static ea_class_entry *store_class_entry(char **at, zend_class_entry * from TSRMLS_DC)
{
    ea_class_entry *to;
    unsigned int i;

    to = (ea_class_entry *)ALLOCATE(at, sizeof(ea_class_entry));
    memset(to, 0, sizeof(ea_class_entry));

    to->type = from->type;
    to->name = NULL;
    to->name_length = from->name_length;
    to->parent = NULL;

    DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
    DBG(ea_debug_printf, (EA_DEBUG, "[%d] store_class_entry: %s parent was '%s'\n",
                          getpid(), from->name ? from->name : "(top)",
                          from->parent ? from->parent->name : "NULL"));
#ifdef DEBUG
    EAG(xpad)++;
#endif

    if (from->name != NULL) {
        to->name = store_string(at, from->name, from->name_length + 1 TSRMLS_CC);
    }
    if (from->parent != NULL && from->parent->name) {
        to->parent = (char*)store_string(at, from->parent->name, from->parent->name_length + 1 TSRMLS_CC);
    }

    to->ce_flags = from->ce_flags;

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

    /*
     * hrak: no need to really store the interfaces since these get populated
     * at/after restore by zend_do_inheritance and ZEND_ADD_INTERFACE
     */

#ifdef ZEND_ENGINE_2_4
    to->line_start = from->info.user.line_start;
    to->line_end = from->info.user.line_end;
#  ifdef INCLUDE_DOC_COMMENTS
    to->doc_comment_len = from->info.user.doc_comment_len;
#  endif

    if (from->info.user.filename != NULL) {
        to->filename = store_string(at, from->info.user.filename, strlen(from->info.user.filename) + 1 TSRMLS_CC);
    }
#  ifdef INCLUDE_DOC_COMMENTS
    if (from->info.user.doc_comment != NULL) {
        to->doc_comment = store_string(at, from->info.user.doc_comment, from->info.user.doc_comment_len + 1 TSRMLS_CC);
    }
#  endif
#else
    to->line_start = from->line_start;
    to->line_end = from->line_end;
#  ifdef INCLUDE_DOC_COMMENTS
    to->doc_comment_len = from->doc_comment_len;
#  endif

    if (from->filename != NULL) {
        to->filename = store_string(at, from->filename, strlen(from->filename) + 1 TSRMLS_CC);
    }
#  ifdef INCLUDE_DOC_COMMENTS
    if (from->doc_comment != NULL) {
        to->doc_comment = store_string(at, from->doc_comment, from->doc_comment_len + 1 TSRMLS_CC);
    }
#  endif
#endif

    store_zval_hash(at, &to->constants_table, &from->constants_table);
    /* Store default_properties */
#ifdef ZEND_ENGINE_2_4
    to->default_properties_count = from->default_properties_count;
    if (from->default_properties_count) {
        to->default_properties_table = (zval **)ALLOCATE(at, (sizeof(zval*) * from->default_properties_count));
        for (i = 0; i < from->default_properties_count; i++) {
            if (from->default_properties_table[i]) {
                to->default_properties_table[i] = store_zval_ptr(at, (zval*)from->default_properties_table[i] TSRMLS_CC);
            } else {
                to->default_properties_table[i] = NULL;
            }
        }
    } else {
        to->default_properties_table = NULL;
    }
#else
    store_hash(at, &to->default_properties, &from->default_properties, (store_bucket_t) store_zval_ptr, (check_bucket_t) store_default_property_access_check, from);
#endif
    /* Store properties_info */
    store_hash(at, &to->properties_info, &from->properties_info, (store_bucket_t) store_property_info, (check_bucket_t) store_property_access_check, from);

#ifdef ZEND_ENGINE_2_4
    to->default_static_members_count = from->default_static_members_count;
    if (from->default_static_members_count > 0) {
        to->default_static_members_table = (zval **)ALLOCATE(at, (sizeof(zval*) * from->default_static_members_count));
        for (i = 0; i < from->default_static_members_count; i++) {
            if (from->default_static_members_table[i]) {
                to->default_static_members_table[i] = store_zval_ptr(at, (zval*)from->default_static_members_table[i] TSRMLS_CC);
            } else {
                to->default_static_members_table[i] = NULL;
            }
        }
    } else {
        to->default_static_members_table = NULL;
    }
    to->static_members_table = to->default_static_members_table;
#else
    to->static_members = NULL;
    if ((from->static_members != NULL) && (from->static_members != &from->default_static_members)) {
        store_zval_hash(at, &to->default_static_members, &from->default_static_members);

        to->static_members = (HashTable *)ALLOCATE(at, sizeof(HashTable));

        store_hash(at, to->static_members, from->static_members, (store_bucket_t) store_zval_ptr, (check_bucket_t) store_static_member_access_check, from);
    } else {
        store_hash(at, &to->default_static_members, &from->default_static_members, (store_bucket_t) store_zval_ptr, (check_bucket_t) store_static_member_access_check, from);
        to->static_members = &to->default_static_members;
    }
#endif
    store_hash(at, &to->function_table, &from->function_table, (store_bucket_t) store_op_array, (check_bucket_t) store_function_inheritance_check, from);

#ifdef DEBUG
    EAG(xpad)--;
#endif

    return to;
}

/* Create a cache entry from the given op_array, functions and classes of a
   script */
void eaccelerator_store_int(ea_cache_entry *entry, char *key, int len, zend_op_array *op_array, Bucket *f, Bucket *c TSRMLS_DC)
{
    char *p;
    ea_fc_entry *fc;
    ea_fc_entry *q;
    char *x;

    DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
    DBG(ea_debug_printf, (EA_DEBUG, "[%d] eaccelerator_store_int: key='%s'\n", getpid (), key));

    zend_hash_init(&EAG(strings), 0, NULL, NULL, 0);

    p = (char *)entry;
    x = ALLOCATE(&p, offsetof(ea_cache_entry, realfilename) + len + 1);
    x = NULL;

    entry->nhits = 0;
    entry->use_cnt = 0;
    entry->removed = 0;
    entry->f_head = NULL;
    entry->c_head = NULL;

    memcpy(entry->realfilename, key, len + 1);
    x = entry->realfilename;
    zend_hash_add(&EAG(strings), key, len + 1, &x, sizeof(char *), NULL);

    q = NULL;
    while (c != NULL) {
        DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
        DBG(ea_debug_printf, (EA_DEBUG, "[%d] eaccelerator_store_int:     class hashkey(%d)=", getpid(), c->nKeyLength));
        DBG(ea_debug_binary_print, (EA_DEBUG, c->arKey, c->nKeyLength));

        fc = (ea_fc_entry *)ALLOCATE(&p, offsetof(ea_fc_entry, htabkey) + c->nKeyLength);

        memcpy(fc->htabkey, c->arKey, c->nKeyLength);
        fc->htablen = c->nKeyLength;
        fc->next = NULL;
        fc->fc = *(zend_class_entry **) c->pData;
        x = fc->htabkey;
        zend_hash_add(&EAG(strings), c->arKey, c->nKeyLength, &x, sizeof(char *), NULL);
        c = c->pListNext;
        if (q == NULL) {
            entry->c_head = fc;
        } else {
            q->next = fc;
        }
        q = fc;
    }

    q = NULL;
    while (f != NULL) {
        DBG(ea_debug_pad, (EA_DEBUG TSRMLS_CC));
        DBG(ea_debug_printf, (EA_DEBUG, "[%d] eaccelerator_store_int:     function hashkey(%d)=", getpid(), f->nKeyLength));
        DBG(ea_debug_binary_print, (EA_DEBUG, f->arKey, f->nKeyLength));

        fc = (ea_fc_entry *)ALLOCATE(&p, offsetof (ea_fc_entry, htabkey) + f->nKeyLength);

        memcpy(fc->htabkey, f->arKey, f->nKeyLength);
        fc->htablen = f->nKeyLength;
        fc->next = NULL;
        fc->fc = f->pData;
        x = fc->htabkey;
        zend_hash_add(&EAG(strings), f->arKey, f->nKeyLength, &x, sizeof(char *), NULL);
        f = f->pListNext;
        if (q == NULL) {
            entry->f_head = fc;
        } else {
            q->next = fc;
        }
        q = fc;
    }

    q = entry->c_head;
    while (q != NULL) {
        q->fc = store_class_entry(&p, (zend_class_entry *) q->fc TSRMLS_CC);
        q = q->next;
    }

    q = entry->f_head;
    while (q != NULL) {
        q->fc = store_op_array(&p, (zend_op_array *) q->fc TSRMLS_CC);
        q = q->next;
    }
    entry->op_array = store_op_array(&p, op_array TSRMLS_CC);

    zend_hash_destroy(&EAG(strings));
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
