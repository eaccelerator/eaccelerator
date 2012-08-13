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
   $Id: $
*/

#include "eaccelerator.h"

#ifdef HAVE_EACCELERATOR
#ifdef WITH_EACCELERATOR_OPTIMIZER

#include "zend.h"
#include "zend_API.h"
#include "zend_constants.h"
#ifdef ZEND_ENGINE_2_4
#include "zend_string.h"
#endif
#include "opcodes.h"

#include "debug.h"

typedef unsigned int* set;

struct _BBlink;

typedef struct _BB {
    zend_op*        start;
    unsigned int    len;
    zend_bool       used;
    zend_bool       protect_merge;

    struct _BB*     jmp_1;
    struct _BB*     jmp_2;
    struct _BB*     jmp_ext;
    struct _BB*     jmp_tc;

    struct _BB*     follow;
    struct _BBlink* pred;  // Gonna be a chain of BBs
    struct _BB*     next;
} BB;

typedef struct _BBlink {
    struct _BB*     bb;
    struct _BBlink* next;
} BBlink;

#ifdef DEBUG
static void dump_bb(BB* bb, zend_op_array *op_array)
{
    BB* p = bb;
    BBlink *q;
    DBG(ea_debug_printf, (EA_DEBUG, "=== CFG FOR %s:%s ===\n", op_array->filename, op_array->function_name));
    while (p) {
        DBG(ea_debug_printf, (EA_DEBUG, "  bb%u start=%u len=%d used=%d\n",
                              (unsigned int)(p-bb),
                              (unsigned int)(p->start-op_array->opcodes),
                              p->len,
                              p->used));
        if (p->jmp_1) {
            DBG(ea_debug_printf, (EA_DEBUG, "    jmp_1 bb%u start=%u  len=%d used=%d\n",
                                  (unsigned int)(p->jmp_1-bb),
                                  (unsigned int)(p->jmp_1->start-op_array->opcodes),
                                  p->jmp_1->len,
                                  p->jmp_1->used));
        }
        if (p->jmp_2) {
            DBG(ea_debug_printf, (EA_DEBUG, "    jmp_2 bb%u start=%u  len=%d used=%d\n",
                                  (unsigned int)(p->jmp_2-bb),
                                  (unsigned int)(p->jmp_2->start-op_array->opcodes),
                                  p->jmp_2->len,
                                  p->jmp_2->used));
        }
        if (p->jmp_ext) {
            DBG(ea_debug_printf, (EA_DEBUG, "    jmp_ext bb%u start=%u  len=%d used=%d\n",
                                  (unsigned int)(p->jmp_ext-bb),
                                  (unsigned int)(p->jmp_ext->start-op_array->opcodes),
                                  p->jmp_ext->len,
                                  p->jmp_ext->used));
        }
        if (p->jmp_tc) {
            DBG(ea_debug_printf, (EA_DEBUG, "    jmp_tc bb%u start=%u  len=%d used=%d\n",
                                  (unsigned int)(p->jmp_tc-bb),
                                  (unsigned int)(p->jmp_tc->start-op_array->opcodes),
                                  p->jmp_tc->len,
                                  p->jmp_tc->used));
        }
        if (p->follow) {
            DBG(ea_debug_printf, (EA_DEBUG, "    follow bb%u start=%u  len=%d used=%d\n",
                                  (unsigned int)(p->follow-bb),
                                  (unsigned int)(p->follow->start-op_array->opcodes),
                                  p->follow->len,
                                  p->follow->used));
        }
        q = p->pred;
        while (q != NULL) {
            DBG(ea_debug_printf, (EA_DEBUG, "    pred bb%u start=%u  len=%d used=%d (",
                                  (unsigned int)(q->bb-bb),
                                  (unsigned int)(q->bb->start-op_array->opcodes),
                                  q->bb->len,
                                  q->bb->used));
            if (q->bb->jmp_1 == p) {
                DBG(ea_debug_printf, (EA_DEBUG, "jmp_1 "));
            }
            if (q->bb->jmp_2 == p) {
                DBG(ea_debug_printf, (EA_DEBUG, "jmp_2 "));
            }
            if (q->bb->jmp_ext == p) {
                DBG(ea_debug_printf, (EA_DEBUG, "jmp_ext "));
            }
            if (q->bb->jmp_tc == p) {
                DBG(ea_debug_printf, (EA_DEBUG, "jmp_tc "));
            }
            if (q->bb->follow == p) {
                DBG(ea_debug_printf, (EA_DEBUG, "follow "));
            }
            DBG(ea_debug_printf, (EA_DEBUG, ")\n"));
            q = q->next;
        }
        p = p->next;
    }
    DBG(ea_debug_printf, (EA_DEBUG, "=== END OF CFG ===========================\n"));
}
#endif

#ifdef ZEND_ENGINE_2_4
#define SET_TO_NOP(op) \
  (op)->opcode = ZEND_NOP; \
  (op)->op1_type = IS_UNUSED; \
  (op)->op2_type = IS_UNUSED; \
  (op)->result_type = IS_UNUSED;
#else
#define SET_TO_NOP(op) \
  (op)->opcode = ZEND_NOP; \
  (op)->op1.op_type = IS_UNUSED; \
  (op)->op2.op_type = IS_UNUSED; \
  (op)->result.op_type = IS_UNUSED;
#endif

static void compute_live_var(BB* bb, zend_op_array* op_array, char* global)
{
    BB* p = bb;
    char* def;
    char* used;

#ifdef ZEND_ENGINE_2_3
    ALLOCA_FLAG(use_heap)
#endif

    memset(global, 0, op_array->T * sizeof(char));

    if (p != NULL && p->next != NULL) {
        int bb_count = 0;
#ifdef ZEND_ENGINE_2_3
        def = do_alloca(op_array->T * sizeof(char), use_heap);
#else
        def = do_alloca(op_array->T * sizeof(char));
#endif
        while (p) {
            zend_op* op = p->start;
            zend_op* end = op + p->len;
            memset(def, 0, op_array->T * sizeof(char));
            while (op < end) {
                if ((OP1_TYPE(op) == IS_VAR || OP1_TYPE(op) == IS_TMP_VAR) &&
                        !def[VAR_NUM(OP1_VARR(op))] && !global[VAR_NUM(OP1_VARR(op))]) {
                    global[VAR_NUM(OP1_VARR(op))] = 1;
                }
                if ((OP2_TYPE(op) == IS_VAR || OP2_TYPE(op) == IS_TMP_VAR) &&
                        !def[VAR_NUM(OP2_VARR(op))] && !global[VAR_NUM(OP2_VARR(op))]) {
                    if (op->opcode != ZEND_OP_DATA) {
                        global[VAR_NUM(OP2_VARR(op))] = 1;
                    }
                }
#ifdef ZEND_ENGINE_2_3
                if ((op->opcode == ZEND_DECLARE_INHERITED_CLASS || op->opcode == ZEND_DECLARE_INHERITED_CLASS_DELAYED) &&
#else
                if (op->opcode == ZEND_DECLARE_INHERITED_CLASS &&
#endif
                        !def[VAR_NUM(op->extended_value)] &&
                        !global[VAR_NUM(op->extended_value)]) {
                    global[VAR_NUM(op->extended_value)] = 1;
                }
                if ((RES_TYPE(op) & IS_VAR &&
                        (op->opcode == ZEND_RECV || op->opcode == ZEND_RECV_INIT ||
                         (RES_USED(op) & EXT_TYPE_UNUSED) == 0)) ||
                        (RES_TYPE(op) & IS_TMP_VAR)) {
                    if (!def[VAR_NUM(RES_VARR(op))] && !global[VAR_NUM(RES_VARR(op))]) {
                        switch (op->opcode) {
                        case ZEND_RECV:
                        case ZEND_RECV_INIT:
                        case ZEND_ADD_ARRAY_ELEMENT:
                            global[VAR_NUM(RES_VARR(op))] = 1;
                            break;
                        }
                    }
                    def[VAR_NUM(RES_VARR(op))] = 1;
                }
                op++;
            }
            p = p->next;
            bb_count++;
        }

#ifdef ZEND_ENGINE_2_3
        free_alloca(def, use_heap);
#else
        free_alloca(def);
#endif
    }
#ifdef ZEND_ENGINE_2_3
    used = do_alloca(op_array->T * sizeof(char), use_heap);
#else
    used = do_alloca(op_array->T * sizeof(char));
#endif
    p = bb;
    while (p) {
        zend_op* op = p->start;
        zend_op* end = op + p->len;
        memset(used, 0, op_array->T * sizeof(char));
        while (op < end) {
            end--;
            if (((RES_TYPE(end) & IS_VAR &&
                    (end->opcode == ZEND_RECV || end->opcode == ZEND_RECV_INIT ||
                     (RES_USED(end) & EXT_TYPE_UNUSED) == 0)) ||
                    (RES_TYPE(end) & IS_TMP_VAR)) &&
                    !global[VAR_NUM(RES_VARR(end))] && !used[VAR_NUM(RES_VARR(end))]) {
                switch(end->opcode) {
                case ZEND_JMPZ_EX:
                    end->opcode = ZEND_JMPZ;
                    RES_TYPE(end) = IS_UNUSED;
                    break;
                case ZEND_JMPNZ_EX:
                    end->opcode = ZEND_JMPNZ;
                    RES_TYPE(end) = IS_UNUSED;
                    break;
                case ZEND_ASSIGN_ADD:
                case ZEND_ASSIGN_SUB:
                case ZEND_ASSIGN_MUL:
                case ZEND_ASSIGN_DIV:
                case ZEND_ASSIGN_MOD:
                case ZEND_ASSIGN_SL:
                case ZEND_ASSIGN_SR:
                case ZEND_ASSIGN_CONCAT:
                case ZEND_ASSIGN_BW_OR:
                case ZEND_ASSIGN_BW_AND:
                case ZEND_ASSIGN_BW_XOR:
                case ZEND_PRE_INC:
                case ZEND_PRE_DEC:
                case ZEND_POST_INC:
                case ZEND_POST_DEC:
                case ZEND_ASSIGN:
                case ZEND_ASSIGN_REF:
                case ZEND_DO_FCALL:
                case ZEND_DO_FCALL_BY_NAME:
                    if (RES_TYPE(end) & IS_VAR) {
                        RES_USED(end) |= EXT_TYPE_UNUSED;
                    }
                    break;
                case ZEND_UNSET_VAR:
                case ZEND_UNSET_DIM:
                case ZEND_UNSET_OBJ:
                    RES_TYPE(end) = IS_UNUSED;
                    break;
                case ZEND_RECV:
                case ZEND_RECV_INIT:
                case ZEND_INCLUDE_OR_EVAL:
                case ZEND_NEW:
                case ZEND_FE_FETCH:
                case ZEND_PRINT:
                case ZEND_INIT_METHOD_CALL:
                case ZEND_INIT_STATIC_METHOD_CALL:
                case ZEND_ASSIGN_DIM:
                case ZEND_ASSIGN_OBJ:
                case ZEND_DECLARE_CLASS:
                case ZEND_DECLARE_INHERITED_CLASS:
#ifdef ZEND_DECLARE_INHERITED_CLASS_DELAYED
                case ZEND_DECLARE_INHERITED_CLASS_DELAYED:
#endif
                    break;
                default:
#ifndef ZEND_ENGINE_2_4
                    /* TODO: check this */
                    if (OP1_TYPE(end) == IS_CONST) {
                        zval_dtor(&end->op1.u.constant);
                    }
                    if (OP2_TYPE(end) == IS_CONST) {
                        zval_dtor(&end->op2.u.constant);
                    }
#endif
                    SET_TO_NOP(end);
                }
            } else if (RES_TYPE(end) & IS_VAR &&
                       (RES_USED(end) & EXT_TYPE_UNUSED) != 0 &&
                       end->opcode != ZEND_RECV && end->opcode != ZEND_RECV_INIT &&
                       used[VAR_NUM(RES_VARR(end))]) {
                RES_USED(end) &= ~EXT_TYPE_UNUSED;
            }
            if ((RES_TYPE(end) & IS_VAR &&
                    (end->opcode == ZEND_RECV || end->opcode == ZEND_RECV_INIT ||
                     (RES_USED(end) & EXT_TYPE_UNUSED) == 0)) ||
                    (RES_TYPE(end) & IS_TMP_VAR)) {
                switch (end->opcode) {
                case ZEND_RECV:
                case ZEND_RECV_INIT:
                case ZEND_ADD_ARRAY_ELEMENT:
                    used[VAR_NUM(RES_VARR(end))] = 1;
                    break;
                default:
                    used[VAR_NUM(RES_VARR(end))] = 0;
                }
            }
            if (OP1_TYPE(end) == IS_VAR || OP1_TYPE(end) == IS_TMP_VAR) {
                used[VAR_NUM(OP1_VARR(end))] = 1;
            }
            if (OP2_TYPE(end) == IS_VAR || OP2_TYPE(end) == IS_TMP_VAR) {
                used[VAR_NUM(OP2_VARR(end))] = 1;
            }
#ifdef ZEND_ENGINE_2_3
            if (end->opcode == ZEND_DECLARE_INHERITED_CLASS || end->opcode == ZEND_DECLARE_INHERITED_CLASS_DELAYED) {
#else
            if (end->opcode == ZEND_DECLARE_INHERITED_CLASS) {
#endif
                used[VAR_NUM(end->extended_value)] = 1;
            }
        }
        p = p->next;
    }

#ifdef ZEND_ENGINE_2_3
    free_alloca(used, use_heap);
#else
    free_alloca(used);
#endif
}

/* Adds FROM as predecessor of TO */
#define BB_ADD_PRED(TO,FROM) { \
                               BBlink *q = (TO)->pred; \
                               while (q != NULL) { \
                                 if (q->bb == (FROM)) break; \
                                 q = q->next; \
                               } \
                               if (q == NULL) { \
                                 q = emalloc(sizeof(*q)); \
                                 q->bb = (FROM); \
                                 q->next = (TO)->pred; \
                                 (TO)->pred = q; \
                               } \
                             }

/* Removes FROM from predecessors of TO */
#define BB_DEL_PRED(TO,FROM) { \
                               BBlink *q = (TO)->pred; \
                               if (q != NULL) { \
                                 if (q->bb == (FROM)) { \
                                   (TO)->pred = q->next; \
                                   efree(q); \
                                 } else { \
                                   while (q->next != NULL) { \
                                     if (q->next->bb == (FROM)) { \
                                       BBlink *r = q->next; \
                                       q->next = q->next->next; \
                                       efree(r); \
                                       break; \
                                     } \
                                     q = q->next; \
                                   } \
                                 } \
                               } \
                             }

#define RM_BB(p) do {if (p->pred == NULL && p != bb) rm_bb(p);} while (0)

static void mark_used_bb(BB* bb)
{
    if (bb->used) {
        return;
    }
    bb->used = 1;
    if (bb->jmp_1 != NULL) {
        mark_used_bb(bb->jmp_1);
        BB_ADD_PRED(bb->jmp_1, bb);
    }
    if (bb->jmp_2 != NULL) {
        mark_used_bb(bb->jmp_2);
        BB_ADD_PRED(bb->jmp_2, bb);
    }
    if (bb->jmp_ext != NULL) {
        mark_used_bb(bb->jmp_ext);
        BB_ADD_PRED(bb->jmp_ext, bb);
    }
    if (bb->jmp_tc != NULL) {
        mark_used_bb(bb->jmp_tc);
        BB_ADD_PRED(bb->jmp_tc, bb);
    }
    if (bb->follow != NULL) {
        mark_used_bb(bb->follow);
        BB_ADD_PRED(bb->follow, bb);
    }
}

static void mark_used_bb2(BB* bb)
{
    if (bb->used) {
        return;
    }
    bb->used = 1;
    if (bb->jmp_1 != NULL) {
        mark_used_bb2(bb->jmp_1);
    }
    if (bb->jmp_2 != NULL) {
        mark_used_bb2(bb->jmp_2);
    }
    if (bb->jmp_ext != NULL) {
        mark_used_bb2(bb->jmp_ext);
    }
    if (bb->jmp_tc != NULL) {
        mark_used_bb2(bb->jmp_tc);
    }
    if (bb->follow != NULL) {
        mark_used_bb2(bb->follow);
    }
}

static void rm_bb(BB* bb)
{
    if (!bb->used) {
        return;
    }
    bb->used = 0;
    if (bb->jmp_1 != NULL) {
        BB_DEL_PRED(bb->jmp_1, bb);
    }
    if (bb->jmp_2 != NULL) {
        BB_DEL_PRED(bb->jmp_2, bb);
    }
    if (bb->jmp_ext != NULL) {
        BB_DEL_PRED(bb->jmp_ext, bb);
    }
    if (bb->jmp_tc != NULL) {
        BB_DEL_PRED(bb->jmp_tc, bb);
    }
    if (bb->follow != NULL) {
        BB_DEL_PRED(bb->follow, bb);
    }
}

static void del_bb(BB* bb)
{
    zend_op* op = bb->start;
    zend_op* end = op + bb->len;

    rm_bb(bb);
    while (op < end) {
        --end;
#ifndef ZEND_ENGINE_2_4
        if (OP1_TYPE(end) == IS_CONST) {
            zval_dtor(&end->op1.u.constant);
        }
        if (OP2_TYPE(end) == IS_CONST) {
            zval_dtor(&end->op2.u.constant);
        }
#endif
        SET_TO_NOP(end);
    }
    bb->len  = 0;
    bb->used = 0;
}
/*
static void replace_bb(BB* src, BB* dst)
{
  BBlink* p = src->pred;
  while (p != NULL) {
    BBlink* q = p->next;
    if (p->bb->jmp_1   == src) {
      p->bb->jmp_1 = dst;
      BB_ADD_PRED(dst,p->bb);
    }
    if (p->bb->jmp_2   == src) {
      p->bb->jmp_2 = dst;
      BB_ADD_PRED(dst,p->bb);
    }
    if (p->bb->jmp_ext == src) {
      p->bb->jmp_ext = dst;
      BB_ADD_PRED(dst,p->bb);
    }
    if (p->bb->follow  == src) {
      p->bb->follow = dst;
      BB_ADD_PRED(dst,p->bb);
    }
    efree(p);
    p = q;
  }
  src->pred = NULL;
}
*/
static void optimize_jmp(BB* bb, zend_op_array* op_array)
{
    BB* p;

    while(1) {
        int ok = 1;

        /* Remove Unused Basic Blocks */
        p = bb;
        while (p->next != NULL) {
            if (p->next->used && p->next->pred) {
                p = p->next;
            } else {
                del_bb(p->next);
                p->next = p->next->next;
                ok = 0;
            }
        }

        /* JMP optimization */
        p = bb;
        while (p) {
            while (p->next != NULL && (!p->next->used || p->next->pred == NULL)) {
                del_bb(p->next);
                p->next = p->next->next;
                ok = 0;
            }
            if (p->used && p->len > 0) {
                zend_op* op = &p->start[p->len-1];

                switch (op->opcode) {
                case ZEND_JMP:
jmp:
                    /* L1: JMP L1+1  => NOP
                    */
                    if (p->jmp_1 == p->next) {
                        if (p->follow) {
                            BB_DEL_PRED(p->follow, p);
                        }
                        p->follow = p->jmp_1;
                        p->jmp_1   = NULL;
                        SET_TO_NOP(op);
                        --(p->len);
                        ok = 0;
                        break;
                    }
                    /*     JMP L1  =>  JMP L2
                           ...         ...
                       L1: JMP L2      JMP L2
                    */
                    while (p->jmp_1->len == 1 &&
                            p->jmp_1->start->opcode == ZEND_JMP &&
                            p->jmp_1 != p) {
                        BB* x_p = p->jmp_1;
                        BB_DEL_PRED(p->jmp_1, p);
                        RM_BB(x_p);
                        p->jmp_1 = x_p->jmp_1;
                        BB_ADD_PRED(p->jmp_1, p);
                        ok = 0;
                    }
                    break;
                case ZEND_JMPZNZ:
jmp_znz:
                    /* JMPZNZ  ?,L1,L1  =>  JMP L1
                    */
                    if (p->jmp_ext == p->jmp_2) {
                        op->opcode = ZEND_JMP;
                        op->extended_value = 0;
                        OP1_TYPE(op) = IS_UNUSED;
                        OP2_TYPE(op) = IS_UNUSED;
                        p->jmp_1 = p->jmp_2;
                        p->jmp_2 = NULL;
                        p->jmp_ext = NULL;
                        ok = 0;
                        goto jmp;
                    } else if (OP1_TYPE(op) == IS_CONST) {
                        /* JMPZNZ  0,L1,L2  =>  JMP L1
                        */
#ifdef ZEND_ENGINE_2_4
                        if (!zend_is_true(&CONSTANT(op->op1.constant))) {
#else
                        if (!zend_is_true(&op->op1.u.constant)) {
#endif
                            op->opcode = ZEND_JMP;
                            op->extended_value = 0;
                            OP1_TYPE(op) = IS_UNUSED;
                            OP2_TYPE(op) = IS_UNUSED;
                            if (p->jmp_ext != p->jmp_2) {
                                BB_DEL_PRED(p->jmp_ext, p);
                                RM_BB(p->jmp_ext);
                            }
                            p->jmp_1   = p->jmp_2;
                            p->jmp_2   = NULL;
                            p->jmp_ext = NULL;
                            p->follow  = NULL;
                            ok = 0;
                            goto jmp;
                            /* JMPZNZ  1,L1,L2  =>  JMP L2
                            */
                        } else {
                            op->opcode = ZEND_JMP;
                            op->extended_value = 0;
                            OP1_TYPE(op) = IS_UNUSED;
                            OP2_TYPE(op) = IS_UNUSED;
                            if (p->jmp_ext != p->jmp_2) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_1   = p->jmp_ext;
                            p->jmp_2   = NULL;
                            p->jmp_ext = NULL;
                            p->follow  = NULL;
                            ok = 0;
                            goto jmp;
                        }
                        /* L1: JMPZNZ ?,L2,L1+1  => JMPZ ?,L2
                        */
                    } else if (p->jmp_ext == p->next) {
                        op->opcode = ZEND_JMPZ;
                        op->extended_value = 0;
                        p->follow = p->jmp_ext;
                        p->jmp_ext = NULL;
                        ok = 0;
                        goto jmp_z;
                        /* L1: JMPZNZ ?,L1+1,L2  => JMPNZ ?,L2
                        */
                    } else if (p->jmp_2 == p->next) {
                        op->opcode = ZEND_JMPNZ;
                        op->extended_value = 0;
                        p->follow = p->jmp_2;
                        p->jmp_2  = p->jmp_ext;
                        p->jmp_ext = NULL;
                        ok = 0;
                        goto jmp_nz;
                    } else if (p->jmp_2->len == 1 &&
                               OP1_TYPE(op) == IS_TMP_VAR) {
                        /*     JMPZNZ $x,L1,L2  =>  JMPZNZ $x,L3,L2
                               ...                  ...
                           L1: JMPZ   $x,L3         JMPZ   $x,L3
                        */
                        /*     JMPZNZ $x,L1,L2  =>  JMPZNZ $x,L3,L2
                               ...                  ...
                           L1: JMPZNZ $x,L3,L4      JMPZNZ $x,L3,L4
                        */
                        if        ((p->jmp_2->start->opcode == ZEND_JMPZ ||
                                    p->jmp_2->start->opcode == ZEND_JMPZNZ) &&
                                   OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                   OP1_VARR(op) == OP1_VARR(p->jmp_2->start)) {
                            if (p->jmp_2 != p->jmp_ext) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->jmp_2;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_znz;
                            /*     JMPZNZ $x,L1,L2  =>  JMPZNZ $x,L1+1,L2
                                   ...                  ...
                               L1: JMPNZ  $x,L3         JMPNZ  $x,L3
                            */
                        } else if (p->jmp_2->start->opcode == ZEND_JMPNZ &&
                                   OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                   OP1_VARR(op) == OP1_VARR(p->jmp_2->start)) {
                            if (p->jmp_2 != p->jmp_ext) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->follow;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_znz;
                            /*     JMPZNZ $x,L1,L2  =>  JMPZNZ $x,L1,L3
                                   ...                  ...
                               L2: JMPNZ  $x,L3         JMPNZ  $x,L3
                            */
                        } else if (p->jmp_ext->start->opcode == ZEND_JMPNZ &&
                                   OP1_TYPE(p->jmp_ext->start) == IS_TMP_VAR &&
                                   OP1_VARR(op) == OP1_VARR(p->jmp_ext->start)) {
                            if (p->jmp_2 != p->jmp_ext) {
                                BB_DEL_PRED(p->jmp_ext, p);
                                RM_BB(p->jmp_ext);
                            }
                            p->jmp_ext = p->jmp_ext->jmp_2;
                            BB_ADD_PRED(p->jmp_ext, p);
                            ok = 0;
                            goto jmp_znz;
                            /*     JMPZNZ $x,L1,L2  =>  JMPZNZ $x,L1,L4
                                   ...                  ...
                               L2: JMPZNZ $x,L3,L4      JMPZNZ $x,L3,L4
                            */
                        } else if (p->jmp_ext->start->opcode == ZEND_JMPZNZ &&
                                   OP1_TYPE(p->jmp_ext->start) == IS_TMP_VAR &&
                                   OP1_VARR(op) == OP1_VARR(p->jmp_ext->start)) {
                            if (p->jmp_2 != p->jmp_ext) {
                                BB_DEL_PRED(p->jmp_ext, p);
                                RM_BB(p->jmp_ext);
                            }
                            p->jmp_ext = p->jmp_ext->jmp_ext;
                            BB_ADD_PRED(p->jmp_ext, p);
                            ok = 0;
                            goto jmp_znz;
                            /*     JMPZNZ $x,L1,L2  =>  JMPZNZ $x,L1,L2+1
                                   ...                  ...
                               L2: JMPZ   $x,L3         JMPZ   $x,L3
                            */
                        } else if (p->jmp_ext->start->opcode == ZEND_JMPZ &&
                                   OP1_TYPE(p->jmp_ext->start) == IS_TMP_VAR &&
                                   OP1_VARR(op) == OP1_VARR(p->jmp_ext->start)) {
                            if (p->jmp_2 != p->jmp_ext) {
                                BB_DEL_PRED(p->jmp_ext, p);
                                RM_BB(p->jmp_ext);
                            }
                            p->jmp_ext = p->jmp_ext->follow;
                            BB_ADD_PRED(p->jmp_ext, p);
                            ok = 0;
                            goto jmp_znz;
                        }
                    }
                    while (p->jmp_2->len == 1 && p->jmp_2->start->opcode == ZEND_JMP) {
                        BB* x_p = p->jmp_2;
                        if (p->jmp_2 != p->jmp_ext) {
                            BB_DEL_PRED(p->jmp_2, p);
                            RM_BB(x_p);
                        }
                        p->jmp_2 = x_p->jmp_1;
                        BB_ADD_PRED(p->jmp_2, p);
                        ok = 0;
                    }
                    while (p->jmp_ext->len == 1 && p->jmp_ext->start->opcode == ZEND_JMP) {
                        BB* x_p = p->jmp_ext;
                        if (p->jmp_2 != p->jmp_ext) {
                            BB_DEL_PRED(p->jmp_ext, p);
                            RM_BB(x_p);
                        }
                        p->jmp_ext = x_p->jmp_1;
                        BB_ADD_PRED(p->jmp_ext, p);
                        ok = 0;
                    }
                    break;
                case ZEND_JMPZ:
jmp_z:
                    /* L1: JMPZ  ?,L1+1  =>  NOP
                    */
                    if (p->follow == p->jmp_2) {
                        p->jmp_2   = NULL;
                        SET_TO_NOP(op);
                        --(p->len);
                        ok = 0;
                        break;
                    } else if (OP1_TYPE(op) == IS_CONST) {
                        /* JMPZ  0,L1  =>  JMP L1
                        */
#ifdef ZEND_ENGINE_2_4
                        if (!zend_is_true(&CONSTANT(op->op1.constant))) {
#else
                        if (!zend_is_true(&op->op1.u.constant)) {
#endif
                            op->opcode = ZEND_JMP;
                            OP1_TYPE(op) = IS_UNUSED;
                            OP2_TYPE(op) = IS_UNUSED;
                            if (p->follow != p->jmp_2) {
                                BB_DEL_PRED(p->follow, p);
                                RM_BB(p->follow);
                            }
                            p->jmp_1  = p->jmp_2;
                            p->jmp_2  = NULL;
                            p->follow = NULL;
                            ok = 0;
                            goto jmp;
                            /* JMPZ  1,L1  =>  NOP
                            */
                        } else {
                            if (p->follow != p->jmp_2) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2   = NULL;
                            SET_TO_NOP(op);
                            --(p->len);
                            ok = 0;
                            break;
                        }
                        /* JMPZ ?,L1  =>  JMPZNZ  ?,L1,L2
                           JMP  L2        JMP     L2
                        */
                    } else if (p->follow->len == 1 && p->follow->start->opcode == ZEND_JMP) {
                        BB* x_p = p->follow;
                        op->opcode = ZEND_JMPZNZ;
                        if (p->jmp_2 != p->follow) {
                            BB_DEL_PRED(p->follow, p);
                            RM_BB(x_p);
                        }
                        p->follow = NULL;
                        p->jmp_ext = x_p->jmp_1;
                        BB_ADD_PRED(p->jmp_ext, p);
                        ok = 0;
                        goto jmp_znz;
                    } else if (p->jmp_2->len == 1 &&
                               OP1_TYPE(op) == IS_TMP_VAR) {
                        /*     JMPZ $x,L1  =>  JMPZ $x,L2
                               ...             ...
                           L1: JMPZ $x,L2      JMPZ $x,L2
                           ----------------------------------------
                               JMPZ   $x,L1     =>  JMPZ  $x,L2
                               ...                   ...
                           L1: JMPZNZ $x,L2,L3      JMPZNZ $x,L2,L3
                        */
                        if       ((p->jmp_2->start->opcode == ZEND_JMPZ ||
                                   p->jmp_2->start->opcode == ZEND_JMPZNZ) &&
                                  OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                  OP1_VARR(op) == OP1_VARR(p->jmp_2->start)) {
                            if (p->jmp_2 != p->follow) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->jmp_2;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_z;
                            /*     JMPZ  $x,L1  =>  JMPZ  $x,L1+1
                                   ...              ...
                               L1: JMPNZ $x,L2      JMPNZ $x,L2
                            */
                        } else if (p->jmp_2->start->opcode == ZEND_JMPNZ &&
                                   OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                   OP1_VARR(op) == OP1_VARR(p->jmp_2->start)) {
                            if (p->jmp_2 != p->follow) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->follow;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_z;
                        }
                    }
                    goto jmp_2;
                case ZEND_JMPNZ:
jmp_nz:
                    /* L1: JMPNZ  ?,L1+1  =>  NOP
                    */
                    if (p->follow == p->jmp_2) {
                        p->jmp_2   = NULL;
                        SET_TO_NOP(op);
                        --(p->len);
                        ok = 0;
                        break;
                    } else if (OP1_TYPE(op) == IS_CONST) {
                        /* JMPNZ  1,L1  =>  JMP L1
                        */
#ifdef ZEND_ENGINE_2_4
                        if (zend_is_true(&CONSTANT(op->op1.constant))) {
#else
                        if (zend_is_true(&op->op1.u.constant)) {
#endif
                            op->opcode = ZEND_JMP;
                            OP1_TYPE(op) = IS_UNUSED;
                            OP2_TYPE(op) = IS_UNUSED;
                            if (p->follow != p->jmp_2) {
                                BB_DEL_PRED(p->follow, p);
                                RM_BB(p->follow);
                            }
                            p->jmp_1  = p->jmp_2;
                            p->jmp_2  = NULL;
                            p->follow = NULL;
                            ok = 0;
                            goto jmp;
                            /* JMPNZ  0,L1  =>  NOP
                            */
                        } else {
                            if (p->follow != p->jmp_2) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2   = NULL;
                            SET_TO_NOP(op);
                            --(p->len);
                            ok = 0;
                            break;
                        }
                        /* JMPNZ ?,L1  =>  JMPZNZ  ?,L2,L1
                           JMP   L2        JMP     L2
                        */
                    } else if (p->follow->len == 1 && p->follow->start->opcode == ZEND_JMP) {
                        BB* x_p = p->follow;
                        op->opcode = ZEND_JMPZNZ;
                        if (p->jmp_2 != p->follow) {
                            BB_DEL_PRED(p->follow, p);
                            RM_BB(p->follow);
                        }
                        p->follow = NULL;
                        p->jmp_ext = p->jmp_2;
                        p->jmp_2 = x_p->jmp_1;
                        BB_ADD_PRED(p->jmp_2, p);
                        ok = 0;
                        goto jmp_znz;
                        /*     JMPNZ $x,L1  =>  JMPNZ $x,L2
                               ...              ...
                           L1: JMPNZ $x,L2      JMPNZ $x,L2
                        */
                    } else if (p->jmp_2->len == 1 &&
                               OP1_TYPE(op) == IS_TMP_VAR) {
                        if        (p->jmp_2->start->opcode == ZEND_JMPNZ &&
                                   OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                   OP1_VARR(op) == OP1_VARR(p->jmp_2->start)) {
                            if (p->jmp_2 != p->follow) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->jmp_2;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_nz;
                            /*     JMPNZ  $x,L1  =>  JMPNZ  $x,L1+1
                                   ...               ...
                               L1: JMPZ   $x,L2      JMPZ $x,L2
                            */
                        } else if (p->jmp_2->start->opcode == ZEND_JMPZ &&
                                   OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                   OP1_VARR(op) == OP1_VARR(p->jmp_2->start)) {
                            if (p->jmp_2 != p->follow) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->follow;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_nz;
                            /*     JMPNZ  $x,L1     =>  JMPNZ  $x,L3
                                   ...                   ...
                               L1: JMPZNZ $x,L2,L3      JMPZNZ $x,L2,L3
                            */
                        } else if (p->jmp_2->start->opcode == ZEND_JMPZNZ &&
                                   OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                   OP1_VARR(op) == OP1_VARR(p->jmp_2->start)) {
                            if (p->jmp_2 != p->follow) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->jmp_ext;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_nz;
                        }
                    }
                    goto jmp_2;
                case ZEND_JMPZ_EX:
jmp_z_ex:
                    /* L1: JMPZ_EX  $x,L1+1,$x  =>  NOP
                    */
                    if (p->follow == p->jmp_2 &&
                            OP1_TYPE(op) == IS_TMP_VAR &&
                            RES_TYPE(op) == IS_TMP_VAR &&
                            OP1_VARR(op) == RES_VARR(op)) {
                        p->jmp_2   = NULL;
                        SET_TO_NOP(op);
                        --(p->len);
                        ok = 0;
                        break;
                        /* L1: JMPZ_EX  $x,L1+1,$y  =>  BOOL $x,$y
                        */
                    } else if (p->follow == p->jmp_2) {
                        p->jmp_2   = NULL;
                        op->opcode = ZEND_BOOL;
                        OP2_TYPE(op) = IS_UNUSED;
                        ok = 0;
                        break;
                    } else if (p->jmp_2->len == 1 &&
                               RES_TYPE(op) == IS_TMP_VAR) {
                        /*     JMPZ_EX ?,L1,$x  =>  JMPZ_EX ?,L2,$x
                               ...                  ...
                           L1: JMPZ    $x,L2        JMPZ    $x,L2
                           ------------------------------------------
                               JMPZ_EX ?,L1,$x  =>  JMPZ_EX ?,L2,$x
                               ...                  ...
                           L1: JMPZNZ  $x,L2,L3     JMPZNZ  $x,L2,L3
                           ------------------------------------------
                               JMPZ_EX ?,L1,$x  =>  JMPZ_EX ?,L2,$x
                               ...                  ...
                           L1: JMPZ_EX $x,L2,$x     JMPZ_EX $x,L2,$x
                        */
                        if       (((p->jmp_2->start->opcode == ZEND_JMPZ ||
                                    p->jmp_2->start->opcode == ZEND_JMPZNZ) &&
                                   OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                   RES_VARR(op) == OP1_VARR(p->jmp_2->start)) ||
                                  (p->jmp_2->start->opcode == ZEND_JMPZ_EX &&
                                   OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                   RES_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                   RES_VARR(op) == OP1_VARR(p->jmp_2->start) &&
                                   RES_VARR(op) == RES_VARR(p->jmp_2->start))) {
                            if (p->jmp_2 != p->follow) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->jmp_2;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_z_ex;
                            /*     JMPZ_EX ?,L1,$x   =>  JMPZ_EX ?,L2+1,$x
                                   ...                   ...
                               L1: JMPNZ    $x,L2        JMPNZ    $x,L2
                               ------------------------------------------
                                   JMPZ_EX ?,L1,$x   =>  JMPZ_EX  ?,L2+1,$x
                                   ...                   ...
                               L1: JMPNZ_EX $x,L2,$x     JMPNZ_EX $x,L2,$x
                            */
                        } else if ((p->jmp_2->start->opcode == ZEND_JMPNZ &&
                                    OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                    RES_VARR(op) == OP1_VARR(p->jmp_2->start)) ||
                                   (p->jmp_2->start->opcode == ZEND_JMPNZ_EX &&
                                    OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                    RES_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                    RES_VARR(op) == OP1_VARR(p->jmp_2->start) &&
                                    RES_VARR(op) == RES_VARR(p->jmp_2->start))) {
                            if (p->jmp_2 != p->follow) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->follow;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_z_ex;
                            /*     JMPZ_EX ?,L1,$x   =>  JMPZ_EX ?,L1+1,$y
                                   ...                   ...
                               L1: BOOL    $x,$y         BOOL    $x,$y
                            */
                        } else if (p->jmp_2->start->opcode == ZEND_BOOL &&
                                   OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                   RES_VARR(op) == OP1_VARR(p->jmp_2->start)) {
                            memcpy(&op->result, &p->jmp_2->start->result, sizeof(p->jmp_2->start->result));
#ifdef ZEND_ENGINE_2_4
                            RES_TYPE(op) = RES_TYPE(p->jmp_2->start);
#endif
                            if (p->jmp_2 != p->follow) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->follow;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_z_ex;
                            /*     JMPZ_EX ?,L1,$x   =>  JMPZ    ?,L1+1
                                   ...                   ...
                               L1: FREE    $x            FREE    $x
                            */
                        } else if (p->jmp_2->start->opcode == ZEND_FREE &&
                                   OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                   RES_VARR(op) == OP1_VARR(p->jmp_2->start)) {
                            op->opcode = ZEND_JMPZ;
                            RES_TYPE(op) = IS_UNUSED;
                            if (p->jmp_2 != p->follow) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->follow;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_z;
                        }
                        /*     JMPZ_EX ?,L1,$x   =>  JMPZ ?,L1+1
                               ...                   ...
                           L1: FREE    $x            FREE $x
                        */
                    } else if (RES_TYPE(op) == IS_TMP_VAR &&
                               p->jmp_2->start->opcode == ZEND_FREE &&
                               OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                               RES_VARR(op) == OP1_VARR(p->jmp_2->start)) {
                        if (p->jmp_2->len > 1) {
                            /* splitting */
                            BB* new_bb = (p->jmp_2+1);
                            new_bb->used   = 1;
                            new_bb->start  = p->jmp_2->start+1;
                            new_bb->len    = p->jmp_2->len-1;
                            p->jmp_2->len  = 1;
                            new_bb->next   = p->jmp_2->next;
                            p->jmp_2->next = new_bb;
                            new_bb->pred   = NULL;
                            if (p->jmp_2->jmp_1) {
                                new_bb->jmp_1     = p->jmp_2->jmp_1;
                                BB_ADD_PRED(new_bb->jmp_1, new_bb);
                                BB_DEL_PRED(new_bb->jmp_1, p->jmp_2);
                                p->jmp_2->jmp_1   = NULL;
                            }
                            if (p->jmp_2->jmp_2) {
                                new_bb->jmp_2     = p->jmp_2->jmp_2;
                                BB_ADD_PRED(new_bb->jmp_2, new_bb);
                                BB_DEL_PRED(new_bb->jmp_2, p->jmp_2);
                                p->jmp_2->jmp_2   = NULL;
                            }
                            if (p->jmp_2->jmp_ext) {
                                new_bb->jmp_ext     = p->jmp_2->jmp_ext;
                                BB_ADD_PRED(new_bb->jmp_ext, new_bb);
                                BB_DEL_PRED(new_bb->jmp_ext, p->jmp_2);
                                p->jmp_2->jmp_ext   = NULL;
                            }
                            op->opcode = ZEND_JMPZ;
                            RES_TYPE(op) = IS_UNUSED;
                            if (p->jmp_2->follow) {
                                new_bb->follow     = p->jmp_2->follow;
                                BB_ADD_PRED(new_bb->follow, new_bb);
                                BB_DEL_PRED(new_bb->follow, p->jmp_2);
                                p->jmp_2->follow   = NULL;
                            }
                            p->jmp_2->follow = new_bb;
                            BB_ADD_PRED(p->jmp_2->follow, p->jmp_2);
                        }
                        if (p->jmp_2 != p->follow) {
                            BB_DEL_PRED(p->jmp_2, p);
                            RM_BB(p->jmp_2);
                        }
                        p->jmp_2 = p->jmp_2->follow;
                        BB_ADD_PRED(p->jmp_2, p);
                        ok = 0;
                        goto jmp_z;
                    }
                    goto jmp_2;
                case ZEND_JMPNZ_EX:
jmp_nz_ex:
                    /* L1: JMPNZ_EX  $x,L1+1,$x  =>  NOP
                    */
                    if (p->follow == p->jmp_2 &&
                            OP1_TYPE(op) == IS_TMP_VAR &&
                            RES_TYPE(op) == IS_TMP_VAR &&
                            OP1_VARR(op) == RES_VARR(op)) {
                        p->jmp_2   = NULL;
                        SET_TO_NOP(op);
                        --(p->len);
                        ok = 0;
                        break;
                        /* L1: JMPNZ_EX  $x,L1+1,$y  =>  BOOL $x,$y
                        */
                    } else if (p->follow == p->jmp_2) {
                        p->jmp_2   = NULL;
                        op->opcode = ZEND_BOOL;
                        OP2_TYPE(op) = IS_UNUSED;
                        ok = 0;
                        break;
                    } else if (p->jmp_2->len == 1 &&
                               RES_TYPE(op) == IS_TMP_VAR) {
                        /*     JMPNZ_EX ?,L1,$x  =>  JMPNZ_EX ?,L2,$x
                               ...                   ...
                           L1: JMPNZ    $x,L2        JMPNZ    $x,L2
                           ------------------------------------------
                               JMPNZ_EX ?,L1,$x  =>  JMPNZ_EX ?,L2,$x
                               ...                   ...
                           L1: JMPNZ_EX $x,L2,$x     JMPNZ_EX $x,L2,$x
                        */
                        if        ((p->jmp_2->start->opcode == ZEND_JMPNZ &&
                                    OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                    RES_VARR(op) == OP1_VARR(p->jmp_2->start)) ||
                                   (p->jmp_2->start->opcode == ZEND_JMPNZ_EX &&
                                    OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                    RES_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                    RES_VARR(op) == OP1_VARR(p->jmp_2->start) &&
                                    RES_VARR(op) == RES_VARR(p->jmp_2->start))) {
                            if (p->jmp_2 != p->follow) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->jmp_2;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_nz_ex;
                            /*     JMPNZ_EX ?,L1,$x   =>  JMPNZ_EX ?,L3,$x
                                   ...                    ...
                               L1: JMPZNZ   $x,L2,L3      JMPZNZ   $x,L2,L3
                            */
                        } else if (p->jmp_2->start->opcode == ZEND_JMPZNZ &&
                                   OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                   RES_VARR(op) == OP1_VARR(p->jmp_2->start)) {
                            if (p->jmp_2 != p->follow) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->jmp_ext;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_nz_ex;
                            /*     JMPNZ_EX ?,L1,$x   =>  JMPNZ_EX ?,L1+1,$x
                                   ...                    ...
                               L1: JMPZ    $x,L2          JMPZ    $x,L2
                               ------------------------------------------
                                   JMPNZ_EX ?,L1,$x   =>  JMPNZ_EX  ?,L1+1,$x
                                   ...                    ...
                               L1: JMPZ_EX $x,L2,$x      JMPZ_EX $x,L2,$x
                            */
                        } else if ((p->jmp_2->start->opcode == ZEND_JMPZ &&
                                    OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                    RES_VARR(op) == OP1_VARR(p->jmp_2->start)) ||
                                   (p->jmp_2->start->opcode == ZEND_JMPZ_EX &&
                                    OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                    RES_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                    RES_VARR(op) == OP1_VARR(p->jmp_2->start) &&
                                    RES_VARR(op) == RES_VARR(p->jmp_2->start))) {
                            if (p->jmp_2 != p->follow) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->follow;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_nz_ex;
                            /*     JMPNZ_EX ?,L1,$x   =>  JMPNZ_EX ?,L1+1,$y
                                   ...                   ...
                               L1: BOOL    $x,$y         BOOL    $x,$y
                            */
                        } else if (p->jmp_2->start->opcode == ZEND_BOOL &&
                                   OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                   RES_VARR(op) == OP1_VARR(p->jmp_2->start)) {
                            memcpy(&op->result, &p->jmp_2->start->result, sizeof(p->jmp_2->start->result));
#ifdef ZEND_ENGINE_2_4
                            RES_TYPE(op) = RES_TYPE(p->jmp_2->start);
#endif
                            if (p->jmp_2 != p->follow) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->follow;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_nz_ex;
                            /*     JMPNZ_EX ?,L1,$x   =>  JMPNZ ?,L1+1
                                   ...                    ...
                               L1: FREE    $x             FREE    $x
                            */
                        } else if (p->jmp_2->start->opcode == ZEND_FREE &&
                                   OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                                   RES_VARR(op) == OP1_VARR(p->jmp_2->start)) {
                            op->opcode = ZEND_JMPNZ;
                            RES_TYPE(op) = IS_UNUSED;
                            if (p->jmp_2 != p->follow) {
                                BB_DEL_PRED(p->jmp_2, p);
                                RM_BB(p->jmp_2);
                            }
                            p->jmp_2 = p->jmp_2->follow;
                            BB_ADD_PRED(p->jmp_2, p);
                            ok = 0;
                            goto jmp_nz;
                        }
                        /*     JMPNZ_EX ?,L1,$x   =>  JMPNZ_EX ?,L1+1,$x
                               ...                    ...
                           L1: FREE    $x             FREE    $x
                        */
                    } else if (RES_TYPE(op) == IS_TMP_VAR &&
                               p->jmp_2->start->opcode == ZEND_FREE &&
                               OP1_TYPE(p->jmp_2->start) == IS_TMP_VAR &&
                               RES_VARR(op) == OP1_VARR(p->jmp_2->start)) {
                        if (p->jmp_2->len > 1) {
                            /* splitting */
                            BB* new_bb = (p->jmp_2+1);
                            new_bb->used   = 1;
                            new_bb->start  = p->jmp_2->start+1;
                            new_bb->len    = p->jmp_2->len-1;
                            p->jmp_2->len  = 1;
                            new_bb->next   = p->jmp_2->next;
                            p->jmp_2->next = new_bb;
                            new_bb->pred   = NULL;
                            if (p->jmp_2->jmp_1) {
                                new_bb->jmp_1     = p->jmp_2->jmp_1;
                                BB_ADD_PRED(new_bb->jmp_1, new_bb);
                                BB_DEL_PRED(new_bb->jmp_1, p->jmp_2);
                                p->jmp_2->jmp_1   = NULL;
                            }
                            if (p->jmp_2->jmp_2) {
                                new_bb->jmp_2     = p->jmp_2->jmp_2;
                                BB_ADD_PRED(new_bb->jmp_2, new_bb);
                                BB_DEL_PRED(new_bb->jmp_2, p->jmp_2);
                                p->jmp_2->jmp_2   = NULL;
                            }
                            if (p->jmp_2->jmp_ext) {
                                new_bb->jmp_ext     = p->jmp_2->jmp_ext;
                                BB_ADD_PRED(new_bb->jmp_ext, new_bb);
                                BB_DEL_PRED(new_bb->jmp_ext, p->jmp_2);
                                p->jmp_2->jmp_ext   = NULL;
                            }
                            if (p->jmp_2->follow) {
                                new_bb->follow     = p->jmp_2->follow;
                                BB_ADD_PRED(new_bb->follow, new_bb);
                                BB_DEL_PRED(new_bb->follow, p->jmp_2);
                                p->jmp_2->follow   = NULL;
                            }
                            p->jmp_2->follow = new_bb;
                            BB_ADD_PRED(p->jmp_2->follow, p->jmp_2);
                        }
                        op->opcode = ZEND_JMPNZ;
                        RES_TYPE(op) = IS_UNUSED;
                        if (p->jmp_2 != p->follow) {
                            BB_DEL_PRED(p->jmp_2, p);
                            RM_BB(p->jmp_2);
                        }
                        p->jmp_2 = p->jmp_2->follow;
                        BB_ADD_PRED(p->jmp_2, p);
                        ok = 0;
                        goto jmp_nz;
                    }
                    goto jmp_2;
                case ZEND_NEW:
                case ZEND_FE_FETCH:
jmp_2:
                    while (p->jmp_2->len == 1 && p->jmp_2->start->opcode == ZEND_JMP) {
                        BB* x_p = p->jmp_2;
                        if (p->jmp_2 != p->follow) {
                            BB_DEL_PRED(p->jmp_2, p);
                            RM_BB(x_p);
                        }
                        p->jmp_2 = x_p->jmp_1;
                        BB_ADD_PRED(p->jmp_2, p);
                        ok = 0;
                    }
                }
            }

            /* Merging Basic Blocks */
            if (p->used && p->pred != NULL && p->pred->bb->used && p->pred->next == NULL &&
                    p->pred->bb->follow == p &&
                    p->pred->bb->next == p &&
                    p->pred->bb->jmp_1 == NULL &&
                    p->pred->bb->jmp_2 == NULL &&
                    p->pred->bb->jmp_ext == NULL &&
                    /* HOESH: See structure declaration */
                    p->protect_merge == 0) {
                BB* x = p->pred->bb;
                BB_DEL_PRED(p, x);
                x->len = &p->start[p->len] - x->start;
                if (p->jmp_1 != NULL) {
                    x->jmp_1 = p->jmp_1;
                    BB_DEL_PRED(p->jmp_1, p);
                    BB_ADD_PRED(p->jmp_1, x);
                }
                if (p->jmp_2 != NULL) {
                    x->jmp_2   = p->jmp_2;
                    BB_DEL_PRED(p->jmp_2, p);
                    BB_ADD_PRED(p->jmp_2, x);
                }
                if (p->jmp_ext != NULL) {
                    x->jmp_ext   = p->jmp_ext;
                    BB_DEL_PRED(p->jmp_ext, p);
                    BB_ADD_PRED(p->jmp_ext, x);
                }
                x->follow  = p->follow;
                if (p->follow != NULL) {
                    BB_DEL_PRED(p->follow, p);
                    BB_ADD_PRED(p->follow, x);
                }
                p->used  = 0;
                p->len   = 0;
                ok = 0;
            }

            p = p->next;
        }

        if (ok) {
            /* Eliminate JMP to RETURN or EXIT */
            p = bb;
            while (p != NULL) {
                if (p->used && p->len > 0) {
                    zend_op* op = &p->start[p->len-1];
                    if (op->opcode == ZEND_JMP &&
                            p->jmp_1->len == 1 &&
                            (p->jmp_1->start->opcode == ZEND_RETURN ||
#ifdef ZEND_ENGINE_2_4
                             p->jmp_1->start->opcode == ZEND_RETURN_BY_REF ||
#endif
                             p->jmp_1->start->opcode == ZEND_EXIT)) {
                        if (op->extended_value == ZEND_BRK || op->extended_value == ZEND_CONT) {
                            op->extended_value = 0;
                        } else {
                            BB_DEL_PRED(p->jmp_1, p);
                            RM_BB(p->jmp_1);
                            memcpy(op, p->jmp_1->start, sizeof(zend_op));
#ifdef ZEND_ENGINE_2_4
                            if (OP1_TYPE(op) == IS_CONST) {
                                zval_copy_ctor(&CONSTANT(op->op1.constant));
                            }
#else
                            if (OP1_TYPE(op) == IS_CONST) {
                                zval_copy_ctor(&op->op1.u.constant);
                            }
#endif
                            p->jmp_1 = NULL;
                            ok = 0;
                        }
                    }
                }
                p = p->next;
            }
        }
        if (ok) {
            break;
        }
    }
}

static int opt_get_constant(const char* name, int name_len, zend_constant** result TSRMLS_DC)
{
    union {
        zend_constant *v;
        void *ptr;
    } c;
    int retval;
#ifdef ZEND_ENGINE_2_3
    ALLOCA_FLAG(use_heap)
    char *lookup_name = do_alloca(name_len+1, use_heap);
#else
    char *lookup_name = do_alloca(name_len+1);
#endif
    memcpy(lookup_name, name, name_len);
    lookup_name[name_len] = '\0';

    if (zend_hash_find(EG(zend_constants), lookup_name, name_len+1, &c.ptr)==SUCCESS) {
        *result = c.v;
        retval=1;
    } else {
        zend_str_tolower(lookup_name, name_len);

        if (zend_hash_find(EG(zend_constants), lookup_name, name_len+1, &c.ptr)==SUCCESS) {
            if ((c.v->flags & CONST_CS) && (memcmp(c.v->name, name, name_len)!=0)) {
                retval=0;
            } else {
                *result = c.v;
                retval=1;
            }
        } else {
            retval=0;
        }
    }
#ifdef ZEND_ENGINE_2_3
    free_alloca(lookup_name, use_heap);
#else
    free_alloca(lookup_name);
#endif
    return retval;
}

static int opt_function_exists(const char* name, int name_len TSRMLS_DC)
{
    char *lcname;
    char *lcfname;
    Bucket *p;

    lcname = estrndup(name,name_len+1);
    zend_str_tolower(lcname, name_len);
    p = module_registry.pListHead;
    while (p != NULL) {
        zend_module_entry *m = (zend_module_entry*)p->pData;
        if (m->type == MODULE_PERSISTENT) {
#ifdef ZEND_ENGINE_2_3
            const zend_function_entry* f = m->functions;
#else
            zend_function_entry* f = m->functions;
#endif
            if (f != NULL) {
                while (f->fname) {
                    lcfname = estrdup(f->fname);
                    zend_str_tolower(lcfname, strlen(lcfname));
                    if (strcmp(lcname,lcfname) == 0) {
                        efree(lcfname);
                        efree(lcname);
                        return 1;
                    }
                    efree(lcfname);
                    f++;
                }
            }
        }
        p = p->pListNext;
    }
    efree(lcname);
    return 0;
}

static int opt_extension_loaded(const char* name, int name_len TSRMLS_DC)
{
    Bucket *p = module_registry.pListHead;
    while (p != NULL) {
        zend_module_entry *m = (zend_module_entry*)p->pData;
        if (m->type == MODULE_PERSISTENT && strcmp(m->name,name) == 0) {
            return 1;
        }
        p = p->pListNext;
    }
    return 0;
}

static int opt_result_is_numeric(zend_op* op, zend_op_array* op_array)
{
    switch (op->opcode) {
    case ZEND_ADD:
    case ZEND_SUB:
    case ZEND_MUL:
    case ZEND_DIV:
    case ZEND_MOD:
    case ZEND_SL:
    case ZEND_SR:

    case ZEND_BOOL:
    case ZEND_BOOL_NOT:
    case ZEND_BOOL_XOR:

    case ZEND_IS_IDENTICAL:
    case ZEND_IS_NOT_IDENTICAL:
    case ZEND_IS_EQUAL:
    case ZEND_IS_NOT_EQUAL:
    case ZEND_IS_SMALLER:
    case ZEND_IS_SMALLER_OR_EQUAL:

    case ZEND_PRE_DEC:
    case ZEND_PRE_INC:
    case ZEND_POST_DEC:
    case ZEND_POST_INC:

    case ZEND_ASSIGN_ADD:
    case ZEND_ASSIGN_SUB:
    case ZEND_ASSIGN_MUL:
    case ZEND_ASSIGN_DIV:
    case ZEND_ASSIGN_MOD:
    case ZEND_ASSIGN_SL:
    case ZEND_ASSIGN_SR:
        return 1;

    case ZEND_CAST:
        if (op->extended_value == IS_BOOL ||
                op->extended_value == IS_LONG ||
                op->extended_value == IS_DOUBLE) {
            return 1;
        }
        return 0;

    case ZEND_DO_FCALL:
        /* list generated in ext/standard with:
           grep "proto int" *| awk '{ print $5}'|sed -r 's/^(.+)\((.*)/\1/'|sort -u
           + some function aliases and other frequently used funcs
        */
#ifdef ZEND_ENGINE_2_4
        if (OP1_TYPE(op) == IS_CONST &&
                Z_TYPE(CONSTANT(op->op1.constant)) == IS_STRING &&
                (strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"abs") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"array_push") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"array_unshift") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"assert") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"bindec") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"connection_aborted") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"connection_status") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"count") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"dl") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"extract") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"ezmlm_hash") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"file_put_contents") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"fileatime") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"filectime") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"filegroup") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"fileinode") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"filemtime") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"fileowner") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"fileperms") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"filesize") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"fpassthru") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"fprintf") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"fputcsv") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"fseek") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"ftell") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"ftok") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"fwrite") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"get_magic_quotes_gpc") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"get_magic_quotes_runtime") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"getlastmod") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"getmygid") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"getmyinode") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"getmypid") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"getmyuid") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"getprotobyname") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"getrandmax") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"getservbyname") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"hexdec") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"ignore_user_abort") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"intval") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"ip2long") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"levenshtein") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"link") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"linkinfo") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"mail") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"memory_get_peak_usage") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"memory_get_usage") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"mt_getrandmax") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"mt_rand") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"octdec") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"ord") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"pclose") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"printf") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"proc_close") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"rand") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"readfile") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"similar_text") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"strcasecmp") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"strcoll") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"strcmp") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"strcspn") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"stream_select") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"stream_set_write_buffer") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"stream_socket_enable_crypto") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"stream_socket_shutdown") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"stripos") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"strlen") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"strnatcasecmp") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"strnatcmp") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"strncmp") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"strpos") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"strripos") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"strrpos") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"strspn") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"substr_compare") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"substr_count") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"symlink") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"system") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"umask") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"version_compare") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"vfprintf") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"vprintf") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"fputs") == 0 ||		/* func alias of fwrite */
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"set_file_buffer") == 0 ||	/* func alias of stream_set_write_buffer */
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"sizeof") == 0 ||		/* func alias of count */
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"ereg") == 0 ||
                 strcmp(Z_STRVAL(CONSTANT(op->op1.constant)),"eregi") == 0)) {
            return 1;
        }
#else
        if (OP1_TYPE(op) == IS_CONST &&
                OP1_CONST_TYPE(op) == IS_STRING &&
                (strcmp(Z_STRVAL(OP1_CONST(op)),"abs") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"array_push") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"array_unshift") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"assert") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"bindec") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"connection_aborted") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"connection_status") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"count") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"dl") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"extract") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"ezmlm_hash") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"file_put_contents") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"fileatime") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"filectime") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"filegroup") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"fileinode") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"filemtime") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"fileowner") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"fileperms") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"filesize") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"fpassthru") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"fprintf") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"fputcsv") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"fseek") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"ftell") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"ftok") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"fwrite") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"get_magic_quotes_gpc") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"get_magic_quotes_runtime") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"getlastmod") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"getmygid") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"getmyinode") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"getmypid") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"getmyuid") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"getprotobyname") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"getrandmax") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"getservbyname") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"hexdec") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"ignore_user_abort") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"intval") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"ip2long") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"levenshtein") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"link") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"linkinfo") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"mail") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"memory_get_peak_usage") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"memory_get_usage") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"mt_getrandmax") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"mt_rand") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"octdec") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"ord") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"pclose") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"printf") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"proc_close") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"rand") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"readfile") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"similar_text") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"strcasecmp") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"strcoll") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"strcmp") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"strcspn") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"stream_select") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"stream_set_write_buffer") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"stream_socket_enable_crypto") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"stream_socket_shutdown") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"stripos") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"strlen") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"strnatcasecmp") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"strnatcmp") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"strncmp") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"strpos") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"strripos") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"strrpos") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"strspn") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"substr_compare") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"substr_count") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"symlink") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"system") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"umask") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"version_compare") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"vfprintf") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"vprintf") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"fputs") == 0 ||		/* func alias of fwrite */
                 strcmp(Z_STRVAL(OP1_CONST(op)),"set_file_buffer") == 0 ||	/* func alias of stream_set_write_buffer */
                 strcmp(Z_STRVAL(OP1_CONST(op)),"sizeof") == 0 ||		/* func alias of count */
                 strcmp(Z_STRVAL(OP1_CONST(op)),"ereg") == 0 ||
                 strcmp(Z_STRVAL(OP1_CONST(op)),"eregi") == 0)) {
            return 1;
        }
#endif
        return 0;

    default:
        return 0;
    }
    return 0;
}

#ifdef ZEND_ENGINE_2_4
/* Fetch types moved to op->extended_value in php 5.4.0 */
#define FETCH_TYPE(op) ((op)->extended_value)
#define SET_UNDEFINED(op) Ts[VAR_NUM((op).var)] = NULL;
#define SET_DEFINED(op)   Ts[VAR_NUM((op)->result.var)] = (op);
#define IS_DEFINED(op)    (Ts[VAR_NUM((op).var)] != NULL)
#define DEFINED_OP(op)    (Ts[VAR_NUM((op).var)])
#else
#define FETCH_TYPE(op) ((op)->op2.u.EA.type)
#define SET_UNDEFINED(op) Ts[VAR_NUM((op).u.var)] = NULL;
#define SET_DEFINED(op)   Ts[VAR_NUM((op)->result.u.var)] = (op);
#define IS_DEFINED(op)    (Ts[VAR_NUM((op).u.var)] != NULL)
#define DEFINED_OP(op)    (Ts[VAR_NUM((op).u.var)])
#endif

static void optimize_bb(BB* bb, zend_op_array* op_array, char* global, int pass TSRMLS_DC)
{
    zend_op* prev = NULL;
    zend_op* op = bb->start;
    zend_op* end = op + bb->len;

    HashTable assigns;
    HashTable fetch_dim;

#ifdef ZEND_ENGINE_2_3
    ALLOCA_FLAG(use_heap)
    zend_op** Ts = do_alloca(sizeof(zend_op*) * op_array->T, use_heap);
#else
    zend_op** Ts = do_alloca(sizeof(zend_op*) * op_array->T);
#endif
    memset(Ts, 0, sizeof(zend_op*) * op_array->T);

    zend_hash_init(&assigns, 0, NULL, NULL, 0);
    zend_hash_init(&fetch_dim, 0, NULL, NULL, 0);

    while (op < end) {
        /* Constant Folding */
        if (OP1_TYPE(op) == IS_TMP_VAR &&
                IS_DEFINED(op->op1) &&
                DEFINED_OP(op->op1)->opcode == ZEND_QM_ASSIGN &&
                OP1_TYPE(DEFINED_OP(op->op1)) == IS_CONST) {
            if (op->opcode != ZEND_CASE) {
                zend_op *x = DEFINED_OP(op->op1);
                SET_UNDEFINED(op->op1);
                memcpy(&op->op1, &x->op1, sizeof(x->op1));
#ifdef ZEND_ENGINE_2_4
                OP1_TYPE(op) = OP1_TYPE(x);
#endif
                SET_TO_NOP(x);
            }
        }
        if (OP2_TYPE(op) == IS_TMP_VAR &&
                IS_DEFINED(op->op2) &&
                DEFINED_OP(op->op2)->opcode == ZEND_QM_ASSIGN &&
                OP1_TYPE(DEFINED_OP(op->op2)) == IS_CONST) {
            zend_op *x = DEFINED_OP(op->op2);
            SET_UNDEFINED(op->op2);
            memcpy(&op->op2, &x->op1, sizeof(x->op1));
#ifdef ZEND_ENGINE_2_4
            OP2_TYPE(op) = OP1_TYPE(x);
#endif
            SET_TO_NOP(x);
        }

/* TODO PHP 5.4 */
#ifndef ZEND_ENGINE_2_4
        if (op->opcode == ZEND_IS_EQUAL) {
            if (OP1_TYPE(op) == IS_CONST &&
                    (Z_TYPE(OP1_CONST(op)) == IS_BOOL &&
                     Z_LVAL(OP1_CONST(op)) == 0)) {
                op->opcode = ZEND_BOOL_NOT;
                memcpy(&op->op1, &op->op2, sizeof(op->op2));
#ifdef ZEND_ENGINE_2_4
                OP1_TYPE(op) = OP2_TYPE(op);
#endif
                OP2_TYPE(op) = IS_UNUSED;
            } else if (OP1_TYPE(op) == IS_CONST &&
                       Z_TYPE(OP1_CONST(op)) == IS_BOOL &&
                       Z_LVAL(OP1_CONST(op)) == 1) {
                op->opcode = ZEND_BOOL;
                memcpy(&op->op1, &op->op2, sizeof(op->op2));
#ifdef ZEND_ENGINE_2_4
                OP1_TYPE(op) = OP2_TYPE(op);
#endif
                OP2_TYPE(op) = IS_UNUSED;
            } else if (OP2_TYPE(op) == IS_CONST &&
                       Z_TYPE(OP2_CONST(op)) == IS_BOOL &&
                       Z_LVAL(OP2_CONST(op)) == 0) {
                op->opcode = ZEND_BOOL_NOT;
                OP2_TYPE(op) = IS_UNUSED;
            } else if (OP2_TYPE(op) == IS_CONST &&
                       Z_TYPE(OP2_CONST(op)) == IS_BOOL &&
                       Z_LVAL(OP2_CONST(op)) == 1) {
                op->opcode = ZEND_BOOL;
                OP2_TYPE(op) = IS_UNUSED;
            } else if (OP2_TYPE(op) == IS_CONST &&
                       Z_TYPE(OP2_CONST(op)) == IS_LONG &&
                       Z_LVAL(OP2_CONST(op)) == 0 &&
                       (OP1_TYPE(op) == IS_TMP_VAR || OP1_TYPE(op) == IS_VAR) &&
                       IS_DEFINED(op->op1) &&
                       opt_result_is_numeric(DEFINED_OP(op->op1), op_array)) {
                op->opcode = ZEND_BOOL_NOT;
                OP2_TYPE(op) = IS_UNUSED;
            }
        } else if (op->opcode == ZEND_IS_NOT_EQUAL) {
            if (OP1_TYPE(op) == IS_CONST &&
                    Z_TYPE(OP1_CONST(op)) == IS_BOOL &&
                    Z_LVAL(OP1_CONST(op)) == 0) {
                op->opcode = ZEND_BOOL;
                memcpy(&op->op1, &op->op2, sizeof(op->op2));
#ifdef ZEND_ENGINE_2_4
                OP1_TYPE(op) = OP2_TYPE(op);
#endif
                OP2_TYPE(op) = IS_UNUSED;
            } else if (OP1_TYPE(op) == IS_CONST &&
                       Z_TYPE(OP1_CONST(op)) == IS_BOOL &&
                       Z_LVAL(OP1_CONST(op)) == 1) {
                op->opcode = ZEND_BOOL_NOT;
                memcpy(&op->op1, &op->op2, sizeof(op->op2));
#ifdef ZEND_ENGINE_2_4
                OP1_TYPE(op) = OP2_TYPE(op);
#endif
                OP2_TYPE(op) = IS_UNUSED;
            } else if (OP2_TYPE(op) == IS_CONST &&
                       Z_TYPE(OP2_CONST(op)) == IS_BOOL &&
                       Z_LVAL(OP2_CONST(op)) == 0) {
                op->opcode = ZEND_BOOL;
                OP2_TYPE(op) = IS_UNUSED;
            } else if (OP2_TYPE(op) == IS_CONST &&
                       Z_TYPE(OP2_CONST(op)) == IS_BOOL &&
                       Z_LVAL(OP2_CONST(op)) == 1) {
                op->opcode = ZEND_BOOL_NOT;
                OP2_TYPE(op) = IS_UNUSED;
            } else if (OP2_TYPE(op) == IS_CONST &&
                       Z_TYPE(OP2_CONST(op)) == IS_LONG &&
                       Z_LVAL(OP2_CONST(op)) == 0 &&
                       (OP1_TYPE(op) == IS_TMP_VAR || OP1_TYPE(op) == IS_VAR) &&
                       IS_DEFINED(op->op1) &&
                       opt_result_is_numeric(DEFINED_OP(op->op1), op_array)) {
                op->opcode = ZEND_BOOL;
                OP2_TYPE(op) = IS_UNUSED;
            }
        }
#endif

        /* Eliminate ZEND_ECHO's with empty strings (as in echo '') */
        if (op->opcode == ZEND_ECHO && OP1_TYPE(op) == IS_CONST &&
            Z_TYPE(OP1_CONST(op)) == IS_STRING && Z_STRLEN(OP1_CONST(op)) == 0) {
#ifndef ZEND_ENGINE_2_4
            /* TODO We can't go around free'ing random literal strings in PHP 5.4
                    For now, we just leave them. Nasty but works. */
            STR_FREE(Z_STRVAL(OP1_CONST(op)));
#endif
            SET_TO_NOP(op);
        }

        if ((op->opcode == ZEND_ADD ||
                op->opcode == ZEND_SUB ||
                op->opcode == ZEND_MUL ||
                op->opcode == ZEND_DIV ||
                op->opcode == ZEND_MOD ||
                op->opcode == ZEND_SL ||
                op->opcode == ZEND_SR ||
#ifndef ZEND_ENGINE_2_4
                /* TODO PHP 5.4 */
                op->opcode == ZEND_CONCAT ||
#endif
                op->opcode == ZEND_BW_OR ||
                op->opcode == ZEND_BW_AND ||
                op->opcode == ZEND_BW_XOR ||
                op->opcode == ZEND_BOOL_XOR ||
                op->opcode == ZEND_IS_IDENTICAL ||
                op->opcode == ZEND_IS_NOT_IDENTICAL ||
                op->opcode == ZEND_IS_EQUAL ||
                op->opcode == ZEND_IS_NOT_EQUAL ||
                op->opcode == ZEND_IS_SMALLER ||
                op->opcode == ZEND_IS_SMALLER_OR_EQUAL) &&
                OP1_TYPE(op) == IS_CONST &&
                OP2_TYPE(op) == IS_CONST &&
                RES_TYPE(op) == IS_TMP_VAR) {

            typedef int (*binary_op_type)(zval *, zval *, zval*  TSRMLS_DC);

            binary_op_type binary_op = (binary_op_type)get_binary_op(op->opcode);

            if (binary_op != NULL) {
                int old = EG(error_reporting);
                zval res;
                EG(error_reporting) = 0;
                if (binary_op(&res, &OP1_CONST(op), &OP2_CONST(op) TSRMLS_CC) != FAILURE) {
#ifndef ZEND_ENGINE_2_4
                    zval_dtor(&OP1_CONST(op));
                    zval_dtor(&OP2_CONST(op));
                    /* TODO check */
#endif
                    op->opcode = ZEND_QM_ASSIGN;
                    op->extended_value = 0;
                    INIT_PZVAL(&res);
                    OP1_TYPE(op) = IS_CONST;
                    memcpy(&OP1_CONST(op), &res, sizeof(zval));
                    OP2_TYPE(op) = IS_UNUSED;
                }
                EG(error_reporting) = old;
            }
        } else if ((op->opcode == ZEND_BW_NOT ||
                    op->opcode == ZEND_BOOL_NOT) &&
                   OP1_TYPE(op) == IS_CONST &&
                   RES_TYPE(op) == IS_TMP_VAR) {
            int (*unary_op)(zval *result, zval *op1) =
                unary_op = get_unary_op(op->opcode);
            if (unary_op != NULL) {
                int old = EG(error_reporting);
                zval res;
                EG(error_reporting) = 0;
                if (unary_op(&res, &OP1_CONST(op)) != FAILURE) {
#ifndef ZEND_ENGINE_2_4
                    zval_dtor(&OP1_CONST(op));
                    /* TODO check */
#endif
                    op->opcode = ZEND_QM_ASSIGN;
                    op->extended_value = 0;
                    INIT_PZVAL(&res);
                    OP1_TYPE(op) = IS_CONST;
                    memcpy(&OP1_CONST(op), &res, sizeof(zval));
                    OP2_TYPE(op) = IS_UNUSED;
                }
                EG(error_reporting) = old;
            }
        } else if ((op->opcode == ZEND_BOOL) &&
                   OP1_TYPE(op) == IS_CONST &&
                   RES_TYPE(op) == IS_TMP_VAR) {
            zval res;
            INIT_PZVAL(&res);
            res.type = IS_BOOL;
            res.value.lval = zend_is_true(&OP1_CONST(op));
#ifndef ZEND_ENGINE_2_4
            zval_dtor(&OP1_CONST(op));
            /* TODO check */
#endif
            op->opcode = ZEND_QM_ASSIGN;
            op->extended_value = 0;
            OP1_TYPE(op) = IS_CONST;
            memcpy(&OP1_CONST(op), &res, sizeof(zval));
            OP2_TYPE(op) = IS_UNUSED;
        } else if ((op->opcode == ZEND_CAST) &&
                   OP1_TYPE(op) == IS_CONST &&
                   RES_TYPE(op) == IS_TMP_VAR &&
                   op->extended_value != IS_ARRAY &&
                   op->extended_value != IS_OBJECT &&
                   op->extended_value != IS_RESOURCE) {
            zval res;
            memcpy(&res,&OP1_CONST(op),sizeof(zval));
            zval_copy_ctor(&res);
            switch (op->extended_value) {
            case IS_NULL:
                convert_to_null(&res);
                break;
            case IS_BOOL:
                convert_to_boolean(&res);
                break;
            case IS_LONG:
                convert_to_long(&res);
                break;
            case IS_DOUBLE:
                convert_to_double(&res);
                break;
            case IS_STRING:
                convert_to_string(&res);
                break;
            case IS_ARRAY:
                convert_to_array(&res);
                break;
            case IS_OBJECT:
                convert_to_object(&res);
                break;
            }
#ifndef ZEND_ENGINE_2_4
            zval_dtor(&OP1_CONST(op));
#endif
            op->opcode = ZEND_QM_ASSIGN;
            op->extended_value = 0;
            OP1_TYPE(op) = IS_CONST;
            memcpy(&OP1_CONST(op), &res, sizeof(zval));
            OP2_TYPE(op) = IS_UNUSED;

/* TODO PHP 5.4 */
#ifndef ZEND_ENGINE_2_4
            /* FREE(CONST) => NOP
            */
        } else if (op->opcode == ZEND_FREE &&
                   OP1_TYPE(op) == IS_CONST) {
            zval_dtor(&OP1_CONST(op));
            SET_TO_NOP(op);
#endif

            /* INIT_STRING ADD_CHAR ADD_STRING ADD_VAR folding */

            /* INIT_STRING($y) => QM_ASSIGN('',$y)
            */
        } else if (op->opcode == ZEND_INIT_STRING) {
            op->opcode = ZEND_QM_ASSIGN;
            OP1_TYPE(op) = IS_CONST;
            OP2_TYPE(op) = IS_UNUSED;
            Z_TYPE(OP1_CONST(op)) = IS_STRING;
            Z_STRLEN(OP1_CONST(op)) = 0;
            Z_STRVAL(OP1_CONST(op)) = empty_string;

            /* ADD_CHAR(CONST,CONST,$y) => QM_ASSIGN(CONST,$y)
            */
#ifdef ZEND_ENGINE_2_4
            /* TODO */
#else
        } else if (op->opcode == ZEND_ADD_CHAR &&
                   OP1_TYPE(op) == IS_CONST) {
            size_t len;
            op->opcode = ZEND_QM_ASSIGN;
            OP1_TYPE(op) = IS_CONST;
            OP2_TYPE(op) = IS_UNUSED;
            convert_to_string(&op->op1.u.constant);
            len = op->op1.u.constant.value.str.len + 1;
            STR_REALLOC(op->op1.u.constant.value.str.val,len+1);
            op->op1.u.constant.value.str.val[len-1] = (char) op->op2.u.constant.value.lval;
            op->op1.u.constant.value.str.val[len] = 0;
            op->op1.u.constant.value.str.len = len;
#endif
            /* ADD_STRING(CONST,CONST,$y) => QM_ASSIGN(CONST,$y)
            */
        } else if (op->opcode == ZEND_ADD_STRING &&
                   OP1_TYPE(op) == IS_CONST) {
            size_t len;
            op->opcode = ZEND_QM_ASSIGN;
            OP1_TYPE(op) = IS_CONST;
            OP2_TYPE(op) = IS_UNUSED;
            convert_to_string(&OP1_CONST(op));
            convert_to_string(&OP2_CONST(op));
            len = Z_STRLEN(OP1_CONST(op)) + Z_STRLEN(OP2_CONST(op));
            STR_REALLOC(Z_STRVAL(OP1_CONST(op)), len + 1);
            memcpy(Z_STRVAL(OP1_CONST(op)) + Z_STRLEN(OP1_CONST(op)),
                   Z_STRVAL(OP2_CONST(op)), Z_STRLEN(OP2_CONST(op)));
            Z_STRVAL(OP1_CONST(op))[len] = 0;
            Z_STRLEN(OP1_CONST(op)) = len;
#ifndef ZEND_ENGINE_2_4
            STR_FREE(Z_STRVAL(OP2_CONST(op)));
            /* TODO */
#endif
            /* ADD_VAR(CONST,VAR,$y) => CONCAT(CONST,$y)
            */
        } else if (op->opcode == ZEND_ADD_VAR &&
                   OP1_TYPE(op) == IS_CONST) {
            op->opcode = ZEND_CONCAT;
            /* CONCAT('',$x,$y) + ADD_CHAR($y,CHAR,$z) => CONCAT($x, CONST, $z)
            */
        } else if (op->opcode == ZEND_ADD_CHAR &&
                   OP1_TYPE(op) == IS_TMP_VAR &&
                   IS_DEFINED(op->op1) &&
                   DEFINED_OP(op->op1)->opcode == ZEND_CONCAT &&
                   OP1_TYPE(DEFINED_OP(op->op1)) == IS_CONST &&
                   Z_TYPE(OP1_CONST(DEFINED_OP(op->op1))) == IS_STRING &&
                   Z_STRLEN(OP1_CONST(DEFINED_OP(op->op1))) == 0) {
            char ch = (char) Z_LVAL(OP2_CONST(op));
            zend_op *x = DEFINED_OP(op->op1);
            SET_UNDEFINED(op->op1);
            memcpy(&op->op1, &x->op2, sizeof(x->op2));
#ifdef ZEND_ENGINE_2_4
            op->op1_type = x->op2_type;
#endif
            op->opcode = ZEND_CONCAT;
            Z_TYPE(OP2_CONST(op)) = IS_STRING;
            Z_STRVAL(OP2_CONST(op)) = emalloc(2);
            Z_STRVAL(OP2_CONST(op))[0] = ch;
            Z_STRVAL(OP2_CONST(op))[1] = '\000';
            Z_STRLEN(OP2_CONST(op)) = 1;
#ifndef ZEND_ENGINE_2_4
            STR_FREE(Z_STRVAL(OP1_CONST(x)));
#endif
            SET_TO_NOP(x);
            /*
               CONCAT('',$x,$y) + ADD_STRING($y,$v,$z) => CONCAT($x, $v, $z)
               CONCAT('',$x,$y) + CONCAT($y,$v,$z)     => CONCAT($x, $v, $z)
               CONCAT('',$x,$y) + ADD_VAR($y,$v,$z)    => CONCAT($x, $v, $z)
            */
        } else if ((op->opcode == ZEND_ADD_STRING ||
                    op->opcode == ZEND_CONCAT ||
                    op->opcode == ZEND_ADD_VAR) &&
                   OP1_TYPE(op) == IS_TMP_VAR &&
                   IS_DEFINED(op->op1) &&
                   DEFINED_OP(op->op1)->opcode == ZEND_CONCAT &&
                   OP1_TYPE(DEFINED_OP(op->op1)) == IS_CONST &&
                   Z_TYPE(OP1_CONST(DEFINED_OP(op->op1))) == IS_STRING &&
                   Z_STRLEN(OP1_CONST(DEFINED_OP(op->op1))) == 0) {
            zend_op *x = DEFINED_OP(op->op1);
            SET_UNDEFINED(op->op1);
            op->opcode = ZEND_CONCAT;
            memcpy(&op->op1, &x->op2, sizeof(x->op2));
#ifdef ZEND_ENGINE_2_4
            op->op1_type = x->op2_type;
#endif
#ifndef ZEND_ENGINE_2_4
            /* TODO */
            STR_FREE(Z_STRVAL(OP1_CONST(x)));
#endif
            SET_TO_NOP(x);
            /* ADD_CHAR($x,CONST,$y) + ADD_CHAR($y,CHAR,$z) => ADD_STRING($x, CONST, $z)
            */
        } else if (op->opcode == ZEND_ADD_CHAR &&
                   OP1_TYPE(op) == IS_TMP_VAR &&
                   IS_DEFINED(op->op1) &&
                   DEFINED_OP(op->op1)->opcode == ZEND_ADD_CHAR) {
            char ch1 = (char) Z_LVAL(OP2_CONST(DEFINED_OP(op->op1)));
            char ch2 = (char) Z_LVAL(OP2_CONST(op));
            Z_TYPE(OP2_CONST(DEFINED_OP(op->op1))) = IS_STRING;
            Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1))) = emalloc(3);
            Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1)))[0] = ch1;
            Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1)))[1] = ch2;
            Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1)))[2] = '\000';
            Z_STRLEN(OP2_CONST(DEFINED_OP(op->op1))) = 2;
            memcpy(&DEFINED_OP(op->op1)->result, &op->result, sizeof(op->result));
#ifdef ZEND_ENGINE_2_4
            DEFINED_OP(op->op1)->result_type = op->result_type;
#endif
            DEFINED_OP(op->op1)->opcode = ZEND_ADD_STRING;
            SET_DEFINED(DEFINED_OP(op->op1));
            SET_TO_NOP(op);
            /* CONCAT($x,CONST,$y) + ADD_CHAR($y,CONST,$z) => CONCAT($x, CONST, $z)
               ADD_STRING($x,CONST,$y) + ADD_CHAR($y,CONST,$z) => ADD_STRING($x, CONST, $z)
            */
        } else if (op->opcode == ZEND_ADD_CHAR &&
                   OP1_TYPE(op) == IS_TMP_VAR &&
                   IS_DEFINED(op->op1) &&
                   (DEFINED_OP(op->op1)->opcode == ZEND_CONCAT ||
                    DEFINED_OP(op->op1)->opcode == ZEND_ADD_STRING) &&
                   OP2_TYPE(DEFINED_OP(op->op1)) == IS_CONST) {
            size_t len;
            convert_to_string(&OP2_CONST(DEFINED_OP(op->op1)));
            len = Z_STRLEN(OP2_CONST(DEFINED_OP(op->op1))) + 1;
            STR_REALLOC(Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1))), len + 1);
            Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1)))[len - 1] = (char) Z_LVAL(OP2_CONST(op));
            Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1)))[len] = 0;
            Z_STRLEN(OP2_CONST(DEFINED_OP(op->op1))) = len;
            memcpy(&DEFINED_OP(op->op1)->result, &op->result, sizeof(op->result));
#ifdef ZEND_ENGINE_2_4
            DEFINED_OP(op->op1)->result_type = op->result_type;
#endif
            if (OP1_TYPE(DEFINED_OP(op->op1)) == RES_TYPE(DEFINED_OP(op->op1)) &&
                    OP1_VARR(DEFINED_OP(op->op1)) == RES_VARR(DEFINED_OP(op->op1))) {
                DEFINED_OP(op->op1)->opcode = ZEND_ADD_STRING;
            }
            SET_DEFINED(DEFINED_OP(op->op1));
            SET_TO_NOP(op);
            /* ADD_CHAR($x,CONST,$y) + ADD_STRING($y,CONST,$z) => ADD_STRING($x, CONST, $z)
               ADD_CHAR($x,CONST,$y) + CONCAT($y,CONST,$z) => CONCAT($x, CONST, $z)
            */
        } else if ((op->opcode == ZEND_ADD_STRING ||
                    op->opcode == ZEND_CONCAT) &&
                   OP2_TYPE(op) == IS_CONST &&
                   OP1_TYPE(op) == IS_TMP_VAR &&
                   IS_DEFINED(op->op1) &&
                   DEFINED_OP(op->op1)->opcode == ZEND_ADD_CHAR) {
            char ch = (char) Z_LVAL(OP2_CONST(DEFINED_OP(op->op1)));
            size_t len;
            convert_to_string(&OP2_CONST(op));
            len = Z_STRLEN(OP2_CONST(op)) + 1;
            Z_TYPE(OP2_CONST(DEFINED_OP(op->op1))) = IS_STRING;
            Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1))) = emalloc(len + 1);
            Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1)))[0] = ch;
            memcpy(Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1))) + 1,
                   Z_STRVAL(OP2_CONST(op)), Z_STRLEN(OP2_CONST(op)));
            Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1)))[len] = 0;
            Z_STRLEN(OP2_CONST(DEFINED_OP(op->op1))) = len;
#ifndef ZEND_ENGINE_2_4
            STR_FREE(Z_STRVAL(OP2_CONST(op)));
            /* TODO */
#endif
            memcpy(&DEFINED_OP(op->op1)->result, &op->result, sizeof(op->result));
#ifdef ZEND_ENGINE_2_4
            DEFINED_OP(op->op1)->result_type = op->result_type;
#endif
            DEFINED_OP(op->op1)->opcode = op->opcode;
            if (OP1_TYPE(DEFINED_OP(op->op1)) == RES_TYPE(DEFINED_OP(op->op1)) &&
                    OP1_VARR(DEFINED_OP(op->op1)) == RES_VARR(DEFINED_OP(op->op1))) {
                DEFINED_OP(op->op1)->opcode = ZEND_ADD_STRING;
            }
            SET_DEFINED(DEFINED_OP(op->op1));
            SET_TO_NOP(op);
            /* ADD_STRING($x,CONST,$y) + ADD_STRING($y,CONST,$z) => ADD_STRING($x, CONST, $z)
               ADD_STRING($x,CONST,$y) + CONCAT($y,CONST,$z) => CONCAT($x, CONST, $z)
               CONCAT($x,CONST,$y) + ADD_STRING($y,CONST,$z) => CONCAT($x, CONST, $z)
               CONCAT($x,CONST,$y) + CONCAT($y,CONST,$z) => CONCAT($x, CONST, $z)
            */
        } else if ((op->opcode == ZEND_ADD_STRING ||
                    op->opcode == ZEND_CONCAT) &&
                   OP2_TYPE(op) == IS_CONST &&
                   OP1_TYPE(op) == IS_TMP_VAR &&
                   IS_DEFINED(op->op1) &&
                   (DEFINED_OP(op->op1)->opcode == ZEND_CONCAT ||
                    DEFINED_OP(op->op1)->opcode == ZEND_ADD_STRING) &&
                   OP2_TYPE(DEFINED_OP(op->op1)) == IS_CONST) {
            size_t len;
            convert_to_string(&OP2_CONST(DEFINED_OP(op->op1)));
            convert_to_string(&OP2_CONST(op));
            len = Z_STRLEN(OP2_CONST(DEFINED_OP(op->op1))) + Z_STRLEN(OP2_CONST(op));
#ifdef ZEND_ENGINE_2_4
            if (IS_INTERNED(Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1)))) ) {
                //char *tmp = safe_emalloc(1, len, 1);
                char *tmp = emalloc(len + 1);
                memcpy(tmp, Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1))), Z_STRLEN(OP2_CONST(DEFINED_OP(op->op1))) + 1);
                Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1))) = tmp;
            } else {
                //Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1))) = safe_erealloc(Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1))), 1, len, 1);
                STR_REALLOC(Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1))), len + 1);
            }
#else
            STR_REALLOC(Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1))), len + 1);
#endif
            memcpy(Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1))) + Z_STRLEN(OP2_CONST(DEFINED_OP(op->op1))),
                   Z_STRVAL(OP2_CONST(op)), Z_STRLEN(OP2_CONST(op)));
            Z_STRVAL(OP2_CONST(DEFINED_OP(op->op1)))[len] = 0;
            Z_STRLEN(OP2_CONST(DEFINED_OP(op->op1))) = len;
#ifndef ZEND_ENGINE_2_4
            STR_FREE(Z_STRVAL(OP2_CONST(op)));
            /* TODO */
#endif
            memcpy(&DEFINED_OP(op->op1)->result, &op->result, sizeof(op->result));
#ifdef ZEND_ENGINE_2_4
            DEFINED_OP(op->op1)->result_type = op->result_type;
#endif
            if (op->opcode == ZEND_CONCAT) {
                DEFINED_OP(op->op1)->opcode = ZEND_CONCAT;
            }
            if (OP1_TYPE(DEFINED_OP(op->op1)) == RES_TYPE(DEFINED_OP(op->op1)) &&
                    OP1_VARR(DEFINED_OP(op->op1)) == RES_VARR(DEFINED_OP(op->op1))) {
                DEFINED_OP(op->op1)->opcode = ZEND_ADD_STRING;
            }
            SET_DEFINED(DEFINED_OP(op->op1));
            SET_TO_NOP(op);
#ifndef ZEND_ENGINE_2_3
            /* TODO: Doesn't work with PHP-5.3. Needs more research */

            /* FETCH_X      local("GLOBALS"),$x => FETCH_X global($y),$z
               FETCH_DIM_X  $x,$y,$z               NOP
            */
        } else if (
            ((op->opcode == ZEND_FETCH_DIM_R &&
              OP1_TYPE(op) == IS_VAR &&
              /*???               op->extended_value == ZEND_FETCH_STANDARD &&*/
              IS_DEFINED(op->op1) &&
              DEFINED_OP(op->op1)->opcode == ZEND_FETCH_R) ||
             (op->opcode == ZEND_FETCH_DIM_W &&
              OP1_TYPE(op) == IS_VAR &&
              IS_DEFINED(op->op1) &&
              DEFINED_OP(op->op1)->opcode == ZEND_FETCH_W) ||
             (op->opcode == ZEND_FETCH_DIM_RW &&
              OP1_TYPE(op) == IS_VAR &&
              IS_DEFINED(op->op1) &&
              DEFINED_OP(op->op1)->opcode == ZEND_FETCH_RW) ||
             (op->opcode == ZEND_FETCH_DIM_IS &&
              OP1_TYPE(op) == IS_VAR &&
              IS_DEFINED(op->op1) &&
              DEFINED_OP(op->op1)->opcode == ZEND_FETCH_IS) ||
             (op->opcode == ZEND_FETCH_DIM_FUNC_ARG &&
              OP1_TYPE(op) == IS_VAR &&
              IS_DEFINED(op->op1) &&
              DEFINED_OP(op->op1)->opcode == ZEND_FETCH_FUNC_ARG) ||
             (op->opcode == ZEND_FETCH_DIM_UNSET &&
              OP1_TYPE(op) == IS_VAR &&
              IS_DEFINED(op->op1) &&
              DEFINED_OP(op->op1)->opcode == ZEND_FETCH_UNSET)) &&
            FETCH_TYPE(DEFINED_OP(op->op1)) == ZEND_FETCH_GLOBAL &&
            OP1_TYPE(DEFINED_OP(op->op1)) == IS_CONST &&
            DEFINED_OP(op->op1)->op1.u.constant.type == IS_STRING &&
            DEFINED_OP(op->op1)->op1.u.constant.value.str.len == (sizeof("GLOBALS")-1) &&
            memcmp(DEFINED_OP(op->op1)->op1.u.constant.value.str.val, "GLOBALS", sizeof("GLOBALS")-1) == 0) {
            zend_op *x = op+1;
            if (x->opcode != op->opcode) {
                x = DEFINED_OP(op->op1);
                SET_UNDEFINED(op->op1);
                STR_FREE(x->op1.u.constant.value.str.val);
                FETCH_TYPE(x) = ZEND_FETCH_GLOBAL;
                memcpy(&x->op1,&op->op2,sizeof(op->op2));
                memcpy(&x->result,&op->result,sizeof(op->result));
                SET_DEFINED(x);
                SET_TO_NOP(op);
            }
#endif
#ifndef ZEND_ENGINE_2_4
            /* FETCH_IS               local("GLOBALS"),$x    ISSET_ISEMPTY_VAR $y(global),res
               ISSET_ISEMPTY_DIM_OBJ  $x,$y,$res          => NOP
            */
        } else if (op->opcode == ZEND_ISSET_ISEMPTY_DIM_OBJ &&
                   OP1_TYPE(op) == IS_VAR &&
                   IS_DEFINED(op->op1) &&
                   DEFINED_OP(op->op1)->opcode == ZEND_FETCH_IS &&
                   FETCH_TYPE(DEFINED_OP(op->op1)) == ZEND_FETCH_GLOBAL &&
                   OP1_TYPE(DEFINED_OP(op->op1)) == IS_CONST &&
                   Z_TYPE(OP1_CONST(DEFINED_OP(op->op1))) == IS_STRING &&
                   Z_STRLEN(OP1_CONST(DEFINED_OP(op->op1))) == (sizeof("GLOBALS")-1) &&
                   memcmp(Z_STRVAL(OP1_CONST(DEFINED_OP(op->op1))), "GLOBALS", sizeof("GLOBALS") - 1) == 0) {
            zend_op* x = DEFINED_OP(op->op1);
#ifndef ZEND_ENGINE_2_4
            STR_FREE(Z_STRVAL(OP1_CONST(x)));
            /* TODO */
#endif
            x->opcode = ZEND_ISSET_ISEMPTY_VAR;
            x->extended_value = op->extended_value;
            FETCH_TYPE(x) = ZEND_FETCH_GLOBAL;
            memcpy(&x->op1, &op->op2, sizeof(op->op2));
            memcpy(&x->result, &op->result, sizeof(op->result));
#ifdef ZEND_ENGINE_2_4
            x->op1_type = op->op2_type;
            x->result_type = op->result_type;
#endif
            SET_DEFINED(x);
            SET_TO_NOP(op);
#endif
        } else if (op->opcode == ZEND_FREE &&
                   OP1_TYPE(op) == IS_TMP_VAR &&
                   IS_DEFINED(op->op1)) {
            /* POST_INC + FREE => PRE_INC */
            if (DEFINED_OP(op->op1)->opcode == ZEND_POST_INC) {
                DEFINED_OP(op->op1)->opcode = ZEND_PRE_INC;
                RES_TYPE(DEFINED_OP(op->op1)) = IS_VAR;
                RES_USED(DEFINED_OP(op->op1)) |= EXT_TYPE_UNUSED;
                SET_UNDEFINED(op->op1);
                SET_TO_NOP(op);
                /* POST_DEC + FREE => PRE_DEC */
            } else if (DEFINED_OP(op->op1)->opcode == ZEND_POST_DEC) {
                DEFINED_OP(op->op1)->opcode = ZEND_PRE_DEC;
                RES_TYPE(DEFINED_OP(op->op1)) = IS_VAR;
                RES_USED(DEFINED_OP(op->op1)) |= EXT_TYPE_UNUSED;
                SET_UNDEFINED(op->op1);
                SET_TO_NOP(op);
                /* PRINT + FREE => ECHO */
            } else if (DEFINED_OP(op->op1)->opcode == ZEND_PRINT) {
                DEFINED_OP(op->op1)->opcode = ZEND_ECHO;
                RES_TYPE(DEFINED_OP(op->op1)) = IS_UNUSED;
                SET_UNDEFINED(op->op1);
                SET_TO_NOP(op);
                /* BOOL + FREE => NOP + NOP */
            } else if (DEFINED_OP(op->op1)->opcode == ZEND_BOOL) {
                SET_TO_NOP(DEFINED_OP(op->op1));
                SET_UNDEFINED(op->op1);
                SET_TO_NOP(op);
            }
            /* CMP + BOOL     => CMP + NOP */
        } else if (op->opcode == ZEND_BOOL &&
                   OP1_TYPE(op) == IS_TMP_VAR &&
                   (!global[VAR_NUM(OP1_VARR(op))] ||
                    (RES_TYPE(op) == IS_TMP_VAR &&
                     OP1_VARR(op) == RES_VARR(op))) &&
                   IS_DEFINED(op->op1) &&
                   (DEFINED_OP(op->op1)->opcode == ZEND_IS_IDENTICAL ||
                    DEFINED_OP(op->op1)->opcode == ZEND_IS_NOT_IDENTICAL ||
                    DEFINED_OP(op->op1)->opcode == ZEND_IS_EQUAL ||
                    DEFINED_OP(op->op1)->opcode == ZEND_IS_NOT_EQUAL ||
                    DEFINED_OP(op->op1)->opcode == ZEND_IS_SMALLER ||
                    DEFINED_OP(op->op1)->opcode == ZEND_IS_SMALLER_OR_EQUAL)) {
            memcpy(&DEFINED_OP(op->op1)->result, &op->result, sizeof(op->result));
#ifdef ZEND_ENGINE_2_4
            DEFINED_OP(op->op1)->result_type = op->result_type;
#endif
            SET_DEFINED(DEFINED_OP(op->op1));
            SET_TO_NOP(op);
            /* BOOL + BOOL     => NOP + BOOL
               BOOL + BOOL_NOT => NOP + BOOL_NOT
               BOOL + JMP...   => NOP + JMP...
            */
        } else if ((op->opcode == ZEND_BOOL ||
                    op->opcode == ZEND_BOOL_NOT ||
                    op->opcode == ZEND_JMPZ||
                    op->opcode == ZEND_JMPNZ ||
                    op->opcode == ZEND_JMPZNZ ||
                    op->opcode == ZEND_JMPZ_EX ||
                    op->opcode == ZEND_JMPNZ_EX) &&
                   OP1_TYPE(op) == IS_TMP_VAR &&
                   (!global[VAR_NUM(OP1_VARR(op))] ||
                    (RES_TYPE(op) == IS_TMP_VAR &&
                     OP1_VARR(op) == RES_VARR(op))) &&
                   IS_DEFINED(op->op1) &&
                   DEFINED_OP(op->op1)->opcode == ZEND_BOOL) {
            zend_op *x = DEFINED_OP(op->op1);
            SET_UNDEFINED(op->op1);
            memcpy(&op->op1, &x->op1, sizeof(x->op1));
#ifdef ZEND_ENGINE_2_4
            op->op1_type = x->op1_type;
#endif
            SET_TO_NOP(x);
            /* BOOL_NOT + BOOL     => NOP + BOOL_NOT
               BOOL_NOT + BOOL_NOT => NOP + BOOL
               BOOL_NOT + JMP...   => NOP + JMP[n]...
            */
        } else if ((op->opcode == ZEND_BOOL ||
                    op->opcode == ZEND_BOOL_NOT ||
                    op->opcode == ZEND_JMPZ||
                    op->opcode == ZEND_JMPNZ) &&
                   OP1_TYPE(op) == IS_TMP_VAR &&
                   (!global[VAR_NUM(OP1_VARR(op))] ||
                    (RES_TYPE(op) == IS_TMP_VAR &&
                     OP1_VARR(op) == RES_VARR(op))) &&
                   IS_DEFINED(op->op1) &&
                   DEFINED_OP(op->op1)->opcode == ZEND_BOOL_NOT) {
            zend_op *x = DEFINED_OP(op->op1);
            switch (op->opcode) {
            case ZEND_BOOL:
                op->opcode = ZEND_BOOL_NOT;
                break;
            case ZEND_BOOL_NOT:
                op->opcode = ZEND_BOOL;
                break;
            case ZEND_JMPZ:
                op->opcode = ZEND_JMPNZ;
                break;
            case ZEND_JMPNZ:
                op->opcode = ZEND_JMPZ;
                break;
            }
            SET_UNDEFINED(op->op1);
            memcpy(&op->op1, &x->op1, sizeof(x->op1));
#ifdef ZEND_ENGINE_2_4
            op->op1_type = x->op1_type;
#endif
            SET_TO_NOP(x);
            /* function_exists(STR) or is_callable(STR) */
        } else if ((op->opcode == ZEND_BOOL ||
                    op->opcode == ZEND_BOOL_NOT ||
                    op->opcode == ZEND_JMPZ||
                    op->opcode == ZEND_JMPNZ ||
                    op->opcode == ZEND_JMPZNZ ||
                    op->opcode == ZEND_JMPZ_EX ||
                    op->opcode == ZEND_JMPNZ_EX) &&
                   OP1_TYPE(op) == IS_VAR &&
                   !global[VAR_NUM(OP1_VARR(op))] &&
                   IS_DEFINED(op->op1) &&
                   DEFINED_OP(op->op1)->opcode == ZEND_DO_FCALL &&
                   DEFINED_OP(op->op1)->extended_value == 1 &&
                   OP1_TYPE(DEFINED_OP(op->op1)) == IS_CONST &&
                   Z_TYPE(OP1_CONST(DEFINED_OP(op->op1))) == IS_STRING) {
            zend_op* call = DEFINED_OP(op->op1);
            zend_op* send = call-1;
            if (send->opcode == ZEND_SEND_VAL &&
                    send->extended_value == ZEND_DO_FCALL &&
                    OP1_TYPE(send) == IS_CONST &&
                    Z_TYPE(OP1_CONST(send)) == IS_STRING &&
                    (strcmp(Z_STRVAL(OP1_CONST(call)), "function_exists") == 0 ||
                     strcmp(Z_STRVAL(OP1_CONST(call)), "is_callable") == 0)) {
                if (opt_function_exists(Z_STRVAL(OP1_CONST(send)), Z_STRLEN(OP1_CONST(send))  TSRMLS_CC)) {
                    SET_UNDEFINED(op->op1);
#ifndef ZEND_ENGINE_2_4
                    zval_dtor(&send->op1.u.constant);
                    zval_dtor(&call->op1.u.constant);
                    /* TODO: check */
#endif
                    SET_TO_NOP(send);
                    SET_TO_NOP(call);
                    OP1_TYPE(op) = IS_CONST;
                    Z_TYPE(OP1_CONST(op)) = IS_BOOL;
                    Z_LVAL(OP1_CONST(op)) = 1;
                }
            } else if (send->opcode == ZEND_SEND_VAL &&
                       send->extended_value == ZEND_DO_FCALL &&
                       OP1_TYPE(send) == IS_CONST &&
                       Z_TYPE(OP1_CONST(send)) == IS_STRING &&
                       strcmp(Z_STRVAL(OP1_CONST(call)), "extension_loaded") == 0) {
                if (opt_extension_loaded(Z_STRVAL(OP1_CONST(send)), Z_STRLEN(OP1_CONST(send))  TSRMLS_CC)) {
                    SET_UNDEFINED(op->op1);
#ifndef ZEND_ENGINE_2_4
                    zval_dtor(&send->op1.u.constant);
                    zval_dtor(&call->op1.u.constant);
                    /* TODO: check */
#endif
                    SET_TO_NOP(send);
                    SET_TO_NOP(call);
                    OP1_TYPE(op) = IS_CONST;
                    Z_TYPE(OP1_CONST(op)) = IS_BOOL;
                    Z_LVAL(OP1_CONST(op)) = 1;
                }
            } else if (send->opcode == ZEND_SEND_VAL &&
                       send->extended_value == ZEND_DO_FCALL &&
                       OP1_TYPE(send) == IS_CONST &&
                       Z_TYPE(OP1_CONST(send)) == IS_STRING &&
                       strcmp(Z_STRVAL(OP1_CONST(call)), "defined") == 0) {
                zend_constant *c = NULL;
                if (opt_get_constant(Z_STRVAL(OP1_CONST(send)), Z_STRLEN(OP1_CONST(send)), &c TSRMLS_CC) && c != NULL && ((c->flags & CONST_PERSISTENT) != 0)) {
                    SET_UNDEFINED(op->op1);
#ifndef ZEND_ENGINE_2_4
                    zval_dtor(&send->op1.u.constant);
                    zval_dtor(&call->op1.u.constant);
                    /* TODO: check */
#endif
                    SET_TO_NOP(send);
                    SET_TO_NOP(call);
                    OP1_TYPE(op) = IS_CONST;
                    Z_TYPE(OP1_CONST(op)) = IS_BOOL;
                    Z_LVAL(OP1_CONST(op)) = 1;
                }
            }
            /* QM_ASSIGN($x,$x) => NOP */
        } else if (op->opcode == ZEND_QM_ASSIGN &&
                   OP1_TYPE(op) == IS_TMP_VAR &&
                   RES_TYPE(op) == IS_TMP_VAR &&
                   OP1_VARR(op) == RES_VARR(op)) {
            SET_TO_NOP(op);
            /* ?(,,$tmp_x) +QM_ASSIGN($tmp_x,$tmp_y) => ?(,,$tmp_y) + NOP */
        } else if (op->opcode == ZEND_QM_ASSIGN &&
                   OP1_TYPE(op) == IS_TMP_VAR &&
                   !global[VAR_NUM(OP1_VARR(op))] &&
                   OP1_VARR(op) != RES_VARR(op) &&
                   IS_DEFINED(op->op1)) {
            zend_op *x = DEFINED_OP(op->op1);
            if (x->opcode != ZEND_ADD_ARRAY_ELEMENT &&
                    x->opcode != ZEND_ADD_STRING &&
                    x->opcode != ZEND_ADD_CHAR &&
                    x->opcode != ZEND_ADD_VAR) {
                SET_UNDEFINED(op->op1);
                memcpy(&x->result, &op->result, sizeof(op->result));
#ifdef ZEND_ENGINE_2_4
                x->result_type = op->result_type;
#endif
                SET_DEFINED(x);
                SET_TO_NOP(op);
            }
            /* ECHO(const) + ECHO(const) => ECHO(const) */
        } else if (prev != NULL &&
                   op->opcode == ZEND_ECHO &&
                   OP1_TYPE(op) == IS_CONST &&
                   prev->opcode == ZEND_ECHO &&
                   OP1_TYPE(prev) == IS_CONST) {
            int len;
            convert_to_string(&OP1_CONST(prev));
            convert_to_string(&OP1_CONST(op));
            len = Z_STRLEN(OP1_CONST(prev)) + Z_STRLEN(OP1_CONST(op));
#ifdef ZEND_ENGINE_2_4
            if (IS_INTERNED(Z_STRVAL(OP1_CONST(prev))) ) {
                //char *tmp = safe_emalloc(1, len, 1);
                char *tmp = emalloc(len + 1);
                memcpy(tmp, Z_STRVAL(OP1_CONST(prev)), Z_STRLEN(OP1_CONST(prev)));
                Z_STRVAL(OP1_CONST(prev)) = tmp;
            } else {
                //Z_STRVAL(OP1_CONST(prev)) = safe_erealloc(Z_STRVAL(OP1_CONST(prev)), 1, len, 1);
                STR_REALLOC(Z_STRVAL(OP1_CONST(prev)), len + 1);
            }
#else
            STR_REALLOC(Z_STRVAL(OP1_CONST(prev)), len + 1);
#endif
            memcpy(Z_STRVAL(OP1_CONST(prev)) + Z_STRLEN(OP1_CONST(prev)),
                   Z_STRVAL(OP1_CONST(op)), Z_STRLEN(OP1_CONST(op)));
            Z_STRVAL(OP1_CONST(prev))[len] = 0;
            Z_STRLEN(OP1_CONST(prev)) = len;
#ifndef ZEND_ENGINE_2_4
            /* TODO */
            STR_FREE(Z_STRVAL(OP1_CONST(op)));
#endif
            SET_TO_NOP(op);
            /* END_SILENCE + BEGIN_SILENCE => NOP + NOP */
        } else if (prev != NULL &&
                   prev->opcode == ZEND_END_SILENCE &&
                   op->opcode == ZEND_BEGIN_SILENCE) {
            zend_op *x = op + 1;
            while (x < end) {
                if (x->opcode == ZEND_END_SILENCE &&
                        OP1_VARR(x) == RES_VARR(op)) {
                    OP1_VARR(x) = OP1_VARR(prev);
                    SET_TO_NOP(prev);
                    SET_TO_NOP(op);
                    break;
                }
                x++;
            }
            /* BEGIN_SILENCE + END_SILENCE => NOP + NOP */
        } else if (prev != NULL &&
                   prev->opcode == ZEND_BEGIN_SILENCE &&
                   op->opcode == ZEND_END_SILENCE &&
                   RES_VARR(prev) == OP1_VARR(op)) {
            SET_TO_NOP(prev);
            SET_TO_NOP(op);
            /* SEND_VAR_NO_REF => SEND_VAR (cond) */
        } else if (op->opcode == ZEND_SEND_VAR_NO_REF &&
                   (op->extended_value & ZEND_ARG_COMPILE_TIME_BOUND) &&
                   !(op->extended_value & ZEND_ARG_SEND_BY_REF)) {
            op->opcode = ZEND_SEND_VAR;
            op->extended_value = ZEND_DO_FCALL;
            /* INIT_FCALL_BY_NAME + DO_FCALL_BY_NAME => DO_FCALL $x */
        } else if (prev != NULL &&
                   op->opcode == ZEND_DO_FCALL_BY_NAME &&
                   op->extended_value == 0 &&
                   OP1_TYPE(op) == IS_CONST &&
                   Z_TYPE(OP1_CONST(op)) == IS_STRING &&
                   prev->opcode == ZEND_INIT_FCALL_BY_NAME &&
                   OP1_TYPE(prev) == IS_UNUSED &&
                   OP2_TYPE(prev) == IS_CONST &&
                   Z_TYPE(OP2_CONST(prev)) == IS_STRING &&
                   Z_STRLEN(OP1_CONST(op)) == Z_STRLEN(OP2_CONST(prev)) &&
                   !memcmp(Z_STRVAL(OP1_CONST(op)), Z_STRVAL(OP2_CONST(prev)), Z_STRLEN(OP1_CONST(op)))) {
            op->opcode = ZEND_DO_FCALL;
#ifndef ZEND_ENGINE_2_4
            STR_FREE(Z_STRVAL(OP2_CONST(prev)));
            /* TODO */
#endif
            SET_TO_NOP(prev);
        }

        /* $a = $a + ? => $a+= ? */
        if (op->opcode == ZEND_ASSIGN &&
                OP1_TYPE(op) == IS_VAR &&
                OP2_TYPE(op) == IS_TMP_VAR &&
                IS_DEFINED(op->op1) &&
                IS_DEFINED(op->op2)) {
            zend_op* l = DEFINED_OP(op->op1);
            zend_op* r = DEFINED_OP(op->op2);
            if (l->opcode == ZEND_FETCH_W &&
                    OP1_TYPE(l) == IS_CONST &&
                    Z_TYPE(OP1_CONST(l)) == IS_STRING &&
                    (r->opcode  == ZEND_ADD ||
                     r->opcode  == ZEND_SUB ||
                     r->opcode  == ZEND_MUL ||
                     r->opcode  == ZEND_DIV ||
                     r->opcode  == ZEND_MOD ||
                     r->opcode  == ZEND_SL ||
                     r->opcode  == ZEND_SR ||
                     r->opcode  == ZEND_CONCAT ||
                     r->opcode  == ZEND_BW_OR ||
                     r->opcode  == ZEND_BW_AND ||
                     r->opcode  == ZEND_BW_XOR) &&
                    OP1_TYPE(r) == IS_VAR &&
                    IS_DEFINED(r->op1)) {
                zend_op* rl = DEFINED_OP(r->op1);
                if (rl->opcode == ZEND_FETCH_R &&
                        OP1_TYPE(rl) == IS_CONST &&
                        Z_TYPE(OP1_CONST(rl)) == IS_STRING &&
                        FETCH_TYPE(rl) == FETCH_TYPE(l) &&
                        Z_STRLEN(OP1_CONST(l)) == Z_STRLEN(OP1_CONST(rl)) &&
                        memcmp(Z_STRVAL(OP1_CONST(l)),
                               Z_STRVAL(OP1_CONST(rl)),
                               Z_STRLEN(OP1_CONST(l)) ) == 0) {
                    switch (r->opcode) {
                    case ZEND_ADD:
                        op->opcode = ZEND_ASSIGN_ADD;
                        break;
                    case ZEND_SUB:
                        op->opcode = ZEND_ASSIGN_SUB;
                        break;
                    case ZEND_MUL:
                        op->opcode = ZEND_ASSIGN_MUL;
                        break;
                    case ZEND_DIV:
                        op->opcode = ZEND_ASSIGN_DIV;
                        break;
                    case ZEND_MOD:
                        op->opcode = ZEND_ASSIGN_MOD;
                        break;
                    case ZEND_SL:
                        op->opcode = ZEND_ASSIGN_SL;
                        break;
                    case ZEND_SR:
                        op->opcode = ZEND_ASSIGN_SR;
                        break;
                    case ZEND_CONCAT:
                        op->opcode = ZEND_ASSIGN_CONCAT;
                        break;
                    case ZEND_BW_OR:
                        op->opcode = ZEND_ASSIGN_BW_OR;
                        break;
                    case ZEND_BW_AND:
                        op->opcode = ZEND_ASSIGN_BW_AND;
                        break;
                    case ZEND_BW_XOR:
                        op->opcode = ZEND_ASSIGN_BW_XOR;
                        break;
                    default:
                        break;
                    }
                    memcpy(&op->op2, &r->op2, sizeof(r->op2));
#ifdef ZEND_ENGINE_2_4
                    op->op2_type = r->op2_type;
#endif
                    l->opcode = ZEND_FETCH_RW;
                    SET_TO_NOP(r);
#ifndef ZEND_ENGINE_2_4
                    STR_FREE(Z_STRVAL(OP1_CONST(rl)));
                    /* TODO */
#endif
                    SET_TO_NOP(rl);
                }
            }
        }

        if (pass == 1) {
            /* FETCH_W var,$x + ASSIGN $x,?,_  + FETCH_R var,$y =>
               FETCH_W var,$x + ASSIGN $x,?,$y */
            if (op->opcode == ZEND_UNSET_VAR ||
                    op->opcode == ZEND_DO_FCALL ||
                    op->opcode == ZEND_DO_FCALL_BY_NAME ||
                    op->opcode == ZEND_POST_INC ||
                    op->opcode == ZEND_POST_DEC ||
                    op->opcode == ZEND_UNSET_DIM ||
                    op->opcode == ZEND_UNSET_OBJ ||
                    op->opcode == ZEND_INCLUDE_OR_EVAL ||
                    op->opcode == ZEND_ASSIGN_DIM ||
                    op->opcode == ZEND_ASSIGN_OBJ) {
                zend_hash_clean(&assigns);
                zend_hash_clean(&fetch_dim);
            } else if (op->opcode == ZEND_ASSIGN_REF ||
                       op->opcode == ZEND_ASSIGN ||
                       op->opcode == ZEND_PRE_INC ||
                       op->opcode == ZEND_PRE_DEC ||
                       op->opcode == ZEND_ASSIGN_ADD ||
                       op->opcode == ZEND_ASSIGN_SUB ||
                       op->opcode == ZEND_ASSIGN_MUL ||
                       op->opcode == ZEND_ASSIGN_DIV ||
                       op->opcode == ZEND_ASSIGN_MOD ||
                       op->opcode == ZEND_ASSIGN_SL ||
                       op->opcode == ZEND_ASSIGN_SR ||
                       op->opcode == ZEND_ASSIGN_CONCAT ||
                       op->opcode == ZEND_ASSIGN_BW_OR ||
                       op->opcode == ZEND_ASSIGN_BW_AND ||
                       op->opcode == ZEND_ASSIGN_BW_XOR) {
                zend_hash_clean(&assigns);
                zend_hash_clean(&fetch_dim);
                if ((RES_USED(op) & EXT_TYPE_UNUSED) != 0 &&
                        OP1_TYPE(op) == IS_VAR &&
                        op->extended_value != ZEND_ASSIGN_DIM &&
                        op->extended_value != ZEND_ASSIGN_OBJ &&
                        IS_DEFINED(op->op1)) {
                    zend_op *x = DEFINED_OP(op->op1);
                    if ((x->opcode == ZEND_FETCH_W || x->opcode == ZEND_FETCH_RW) &&
                            OP1_TYPE(x) == IS_CONST && Z_TYPE(OP1_CONST(x)) == IS_STRING) {
                        union {
                            zend_op *v;
                            void *ptr;
                        } op_copy;
                        zend_op *y = DEFINED_OP(x->op2);
                        
                        if (y){
                            unsigned int use_classname = 0;
                            unsigned int nKeyLength = Z_STRLEN(OP1_CONST(x)) + 2;
                            char *s;

                            if (y->opcode == ZEND_FETCH_CLASS &&
                                OP2_TYPE(y) == IS_CONST && Z_TYPE(OP2_CONST(y)) == IS_STRING) {
                                nKeyLength += Z_STRLEN(OP2_CONST(y));
                                use_classname = 1;
                            }
                            s = emalloc(nKeyLength);
                            op_copy.v = op;
                            memcpy(s, Z_STRVAL(OP1_CONST(x)), Z_STRLEN(OP1_CONST(x)));
                            s[Z_STRLEN(OP1_CONST(x))] = (char)FETCH_TYPE(x);
                            if (y->opcode == ZEND_FETCH_CLASS && use_classname) {
                                memcpy(&s[(Z_STRLEN(OP1_CONST(x)) + 1)], Z_STRVAL(OP2_CONST(y)), Z_STRLEN(OP2_CONST(y)));
                            }
                            s[nKeyLength - 1] = 0;
                            zend_hash_update(&assigns, s, nKeyLength, &op_copy.ptr, sizeof(void*), NULL);
                            efree(s);
                        }
                    }
                }
            /* Eliminate FETCH if the value has already been assigned earlier */
            } else if ((op->opcode == ZEND_FETCH_R || op->opcode == ZEND_FETCH_IS) &&
                       !global[VAR_NUM(RES_VARR(op))] && OP1_TYPE(op) == IS_CONST &&
                       Z_TYPE(OP1_CONST(op)) == IS_STRING) {
                union {
                    zend_op *v;
                    void *ptr;
                } x;
                zend_op *y = DEFINED_OP(op->op2);
                if (y) {
                    unsigned int use_classname = 0;
                    unsigned int nKeyLength = Z_STRLEN(OP1_CONST(op)) + 2;
                    char *s;

                    if (y->opcode == ZEND_FETCH_CLASS &&
                        OP2_TYPE(y) == IS_CONST && Z_TYPE(OP2_CONST(y)) == IS_STRING) {
                        nKeyLength += Z_STRLEN(OP2_CONST(y));
                        use_classname = 1;
                    }
                    s = emalloc(nKeyLength);
                    memcpy(s, Z_STRVAL(OP1_CONST(op)), Z_STRLEN(OP1_CONST(op)));
                    s[Z_STRLEN(OP1_CONST(op))] = (char)FETCH_TYPE(op);
                    if (y->opcode == ZEND_FETCH_CLASS && use_classname) {
                        memcpy(&s[(Z_STRLEN(OP1_CONST(op)) + 1)], Z_STRVAL(OP2_CONST(y)), Z_STRLEN(OP2_CONST(y)));
                    }
                    s[nKeyLength - 1] = 0;

                    if (zend_hash_find(&assigns, s, nKeyLength, &x.ptr) == SUCCESS) {
                        x.v = *(zend_op**)x.v;
                        memcpy(&x.v->result, &op->result, sizeof(op->result));
#ifdef ZEND_ENGINE_2_4
                        x.v->result_type = op->result_type;
#endif
                        RES_USED(x.v) &= ~EXT_TYPE_UNUSED;
                        SET_DEFINED(x.v);
                        zend_hash_del(&assigns, s, nKeyLength);
#ifndef ZEND_ENGINE_2_4
                        STR_FREE(Z_STRVAL(OP1_CONST(op)));
                        /* TODO */
#endif
                        SET_TO_NOP(op);
                    }
                    efree(s);
                }
            } else if (op->opcode == ZEND_FETCH_DIM_R &&
                       op->extended_value != ZEND_FETCH_ADD_LOCK &&
                       OP1_TYPE(op) == IS_VAR &&
                       IS_DEFINED(op->op1)) {
                zend_op *x = DEFINED_OP(op->op1);
                while ((x->opcode == ZEND_ASSIGN_REF ||
                        x->opcode == ZEND_ASSIGN ||
                        x->opcode == ZEND_PRE_INC ||
                        x->opcode == ZEND_PRE_DEC ||
                        x->opcode == ZEND_ASSIGN_ADD ||
                        x->opcode == ZEND_ASSIGN_SUB ||
                        x->opcode == ZEND_ASSIGN_MUL ||
                        x->opcode == ZEND_ASSIGN_DIV ||
                        x->opcode == ZEND_ASSIGN_MOD ||
                        x->opcode == ZEND_ASSIGN_SL ||
                        x->opcode == ZEND_ASSIGN_SR ||
                        x->opcode == ZEND_ASSIGN_CONCAT ||
                        x->opcode == ZEND_ASSIGN_BW_OR ||
                        x->opcode == ZEND_ASSIGN_BW_AND ||
                        x->opcode == ZEND_ASSIGN_BW_XOR) &&
                        OP1_TYPE(x) == IS_VAR &&
                        IS_DEFINED(x->op1)) {
                    x = DEFINED_OP(x->op1);
                }
                if ((x->opcode == ZEND_FETCH_R || x->opcode == ZEND_FETCH_W ||
                        x->opcode == ZEND_FETCH_RW) && OP1_TYPE(x) == IS_CONST &&
                        Z_TYPE(OP1_CONST(x)) == IS_STRING) {
                    union {
                        zend_op *v;
                        void *ptr;
                    } y;
                    union {
                        zend_op *v;
                        void *ptr;
                    } op_copy;
                    char *s = emalloc(Z_STRLEN(OP1_CONST(x)) + 2);
                    op_copy.v = op;
                    memcpy(s, Z_STRVAL(OP1_CONST(x)), Z_STRLEN(OP1_CONST(x)));
                    s[Z_STRLEN(OP1_CONST(x))] = (char)FETCH_TYPE(x);
                    s[Z_STRLEN(OP1_CONST(x)) + 1] = 0;
                    if (zend_hash_find(&fetch_dim, s, Z_STRLEN(OP1_CONST(x)) + 2,
                                       &y.ptr) == SUCCESS) {
                        y.v = *(zend_op**)y.v;
                        y.v->extended_value = ZEND_FETCH_ADD_LOCK;
                        zend_hash_update(&fetch_dim, s, Z_STRLEN(OP1_CONST(x)) + 2, &op_copy.ptr, sizeof(void*), NULL);
                        SET_UNDEFINED(x->result);
#ifndef ZEND_ENGINE_2_4
                        STR_FREE(Z_STRVAL(OP1_CONST(x)));
                        /* TODO */
#endif
                        SET_TO_NOP(x);
                        memcpy(&op->op1, &y.v->op1, sizeof(op->op1));
#ifdef ZEND_ENGINE_2_4
                        op->op1_type = y.v->op1_type;
#endif
                    } else {
                        zend_hash_update(&fetch_dim, s, Z_STRLEN(OP1_CONST(x)) + 2, &op_copy.ptr, sizeof(void*), NULL);
                    }
                    efree(s);
                }
            }
        }

        if (op->opcode != ZEND_NOP) {
            prev = op;
        }
        if ((RES_TYPE(op) & IS_VAR &&
                (op->opcode == ZEND_RECV || op->opcode == ZEND_RECV_INIT ||
                 (RES_USED(op) & EXT_TYPE_UNUSED) == 0)) ||
                (RES_TYPE(op) & IS_TMP_VAR)) {
            if (op->opcode == ZEND_RECV ||
                    op->opcode == ZEND_RECV_INIT) {
                SET_UNDEFINED(op->result);
            } else {
                SET_DEFINED(op);
            }
        }
        ++op;
    }

    /* NOP Removing */
    op = bb->start;
    end = op + bb->len;
    while (op < end) {
        if (op->opcode == ZEND_NOP) {
            zend_op *next = op+1;
            while (next < end && next->opcode == ZEND_NOP) {
                next++;
            }
            if (next < end) {
                memmove(op,next,(end-next) * sizeof(zend_op));
                while (next > op) {
                    --end;
                    SET_TO_NOP(end);
                    --next;
                }
            } else {
                end -= (next-op);
            }
        } else {
            ++op;
        }
    }
    bb->len = end - bb->start;
    zend_hash_destroy(&fetch_dim);
    zend_hash_destroy(&assigns);
#ifdef ZEND_ENGINE_2_3
    free_alloca(Ts, use_heap);
#else
    free_alloca(Ts);
#endif
}

/*
 * Find All Basic Blocks in op_array and build Control Flow Graph (CFG)
 */
static int build_cfg(zend_op_array *op_array, BB* bb)
{
    zend_op* op = op_array->opcodes;
    int len = op_array->last;
    int line_num;
    BB* p;
    zend_bool remove_brk_cont_array = 1;
    zend_uint innermost_catch;

    /* Mark try/catch blocks */
    if (op_array->last_try_catch > 0) {
        int i;
        zend_try_catch_element* tc_element = op_array->try_catch_array;

        for (i = 0; i < op_array->last_try_catch; i++, tc_element++) {
            bb[tc_element->try_op].start = &op_array->opcodes[tc_element->try_op];
            bb[tc_element->try_op].protect_merge = 1;

            bb[tc_element->catch_op].start = &op_array->opcodes[tc_element->catch_op];
            bb[tc_element->catch_op].protect_merge = 1;
            
            bb[tc_element->try_op].jmp_tc = &bb[tc_element->catch_op];
        }
    }

    /* Find Starts of Basic Blocks */
    bb[0].start = op;
    for (line_num = 0; line_num < len; op++,line_num++) {
        const opcode_dsc* dsc = get_opcode_dsc(op->opcode);
        if (dsc != NULL) {
#ifndef ZEND_ENGINE_2_3
            /* Does not work with PHP 5.3 due to namespaces */
            if ((dsc->ops & OP1_MASK) == OP1_UCLASS) {
                if (OP1_TYPE(op) != IS_UNUSED) {
                    OP1_TYPE(op) = IS_VAR;

                }
            } else if ((dsc->ops & OP1_MASK) == OP1_CLASS) {
                OP1_TYPE(op) = IS_VAR;
            } else
#endif
                if ((dsc->ops & OP1_MASK) == OP1_UNUSED) {
                    OP1_TYPE(op) = IS_UNUSED;
                }
            if ((dsc->ops & OP2_MASK) == OP2_CLASS) {
                OP2_TYPE(op) = IS_VAR;
            } else if ((dsc->ops & OP2_MASK) == OP2_UNUSED) {
                OP2_TYPE(op) = IS_UNUSED;
            }
#ifndef ZEND_ENGINE_2_4
            else if ((dsc->ops & OP2_MASK) == OP2_FETCH &&
                     FETCH_TYPE(op) == ZEND_FETCH_STATIC_MEMBER) {
                OP2_TYPE(op) = IS_VAR;
            }
#else
            else if ((dsc->ops & OP2_MASK) == OP2_FETCH &&
                     FETCH_TYPE(op) == ZEND_FETCH_STATIC_MEMBER) {
                OP2_TYPE(op) = IS_VAR;
            }
#endif

            if ((dsc->ops & RES_MASK) == RES_CLASS) {
                RES_TYPE(op) = IS_VAR;
                RES_USED(op) &= ~EXT_TYPE_UNUSED;
            } else if ((dsc->ops & RES_MASK) == RES_UNUSED) {
                RES_TYPE(op) = IS_UNUSED;
            }
        }
        switch(op->opcode) {
        case ZEND_RETURN:
#ifdef ZEND_ENGINE_2_4
        case ZEND_RETURN_BY_REF:
#endif
        case ZEND_EXIT:
            bb[line_num+1].start = op+1;
            break;
#ifdef ZEND_GOTO
        case ZEND_GOTO:
#endif
        case ZEND_JMP:
            bb[OP1_OPLINE_NUM(op)].start = &op_array->opcodes[OP1_OPLINE_NUM(op)];
            bb[line_num+1].start = op+1;
            break;
        case ZEND_JMPZNZ:
            bb[op->extended_value].start = &op_array->opcodes[op->extended_value];
            bb[OP2_OPLINE_NUM(op)].start = &op_array->opcodes[OP2_OPLINE_NUM(op)];
            bb[line_num+1].start = op+1;
            break;
        case ZEND_JMPZ:
        case ZEND_JMPNZ:
        case ZEND_JMPZ_EX:
        case ZEND_JMPNZ_EX:
#ifdef ZEND_JMP_SET
        case ZEND_JMP_SET:
#endif
        case ZEND_NEW:
        case ZEND_FE_RESET:
        case ZEND_FE_FETCH:
            bb[line_num+1].start = op+1;
            bb[OP2_OPLINE_NUM(op)].start = &op_array->opcodes[OP2_OPLINE_NUM(op)];
            break;
        case ZEND_BRK:
            /* Replace BRK by JMP */
            if (OP1_OPLINE_NUM(op) == -1) {
            }
#ifdef ZEND_ENGINE_2_4
            else if (OP2_TYPE(op) == IS_CONST && Z_TYPE(CONSTANT(op->op2.constant)) == IS_LONG) {
                int level = Z_LVAL(CONSTANT(op->op2.constant));
#else
            else if (OP2_TYPE(op) == IS_CONST && op->op2.u.constant.type == IS_LONG) {
                int level  = Z_LVAL(OP2_CONST(op));
#endif
                zend_uint offset = OP1_OPLINE_NUM(op);
                zend_brk_cont_element *jmp_to;

                do {
                    if (offset < 0 || offset >= op_array->last_brk_cont) {
                        goto brk_failed;
                    }
                    jmp_to = &op_array->brk_cont_array[offset];
                    if (level>1 &&
                            (op_array->opcodes[jmp_to->brk].opcode == ZEND_SWITCH_FREE ||
                             op_array->opcodes[jmp_to->brk].opcode == ZEND_FREE)) {
                        goto brk_failed;
                    }
                    offset = jmp_to->parent;
                } while (--level > 0);

                op->opcode = ZEND_JMP;
                OP1_OPLINE_NUM(op) = jmp_to->brk;
                OP2_TYPE(op) = IS_UNUSED;
                op->extended_value = ZEND_BRK; /* Mark the opcode as former ZEND_BRK */
                bb[OP1_OPLINE_NUM(op)].start = &op_array->opcodes[jmp_to->brk];
            } else {
brk_failed:
                remove_brk_cont_array = 0;
            }
            bb[line_num+1].start = op+1;
            break;
        case ZEND_CONT:
            /* Replace CONT by JMP */
            if (OP1_OPLINE_NUM(op) == -1) {
            }
#ifdef ZEND_ENGINE_2_4
            else if (OP2_TYPE(op) == IS_CONST && Z_TYPE(CONSTANT(op->op2.constant)) == IS_LONG) {
                int level  = Z_LVAL(CONSTANT(op->op2.constant));
#else
            else if (OP2_TYPE(op) == IS_CONST && op->op2.u.constant.type == IS_LONG) {
                int level  = Z_LVAL(OP2_CONST(op));
#endif
                zend_uint offset = OP1_OPLINE_NUM(op);
                zend_brk_cont_element *jmp_to;

                do {
                    if (offset < 0 || offset >= op_array->last_brk_cont) {
                        goto cont_failed;
                    }
                    jmp_to = &op_array->brk_cont_array[offset];
                    if (level > 1 &&
                            (op_array->opcodes[jmp_to->brk].opcode == ZEND_SWITCH_FREE ||
                             op_array->opcodes[jmp_to->brk].opcode == ZEND_FREE)) {
                        goto cont_failed;
                    }
                    offset = jmp_to->parent;
                } while (--level > 0);

                op->opcode = ZEND_JMP;
                OP1_OPLINE_NUM(op) = jmp_to->cont;
                OP2_TYPE(op) = IS_UNUSED;
                op->extended_value = ZEND_CONT; /* Mark the opcode as former ZEND_CONT */
                bb[OP1_OPLINE_NUM(op)].start = &op_array->opcodes[jmp_to->cont];
            } else {
cont_failed:
                remove_brk_cont_array = 0;
            }
            bb[line_num+1].start = op+1;
            break;
        case ZEND_CATCH:
            bb[op->extended_value].start = &op_array->opcodes[op->extended_value];
            bb[line_num+1].start = op+1;
            break;
        case ZEND_THROW:
            if (OP2_OPLINE_NUM(op) != -1) {
                bb[OP2_OPLINE_NUM(op)].start = &op_array->opcodes[OP2_OPLINE_NUM(op)];
            }
            bb[line_num+1].start = op+1;
            break;
        case ZEND_DO_FCALL:
        case ZEND_DO_FCALL_BY_NAME:
            bb[line_num+1].start = op+1;
            break;
        case ZEND_UNSET_VAR:
        case ZEND_UNSET_DIM:
            RES_TYPE(op) = IS_UNUSED;
            break;
        case ZEND_UNSET_OBJ:
            RES_TYPE(op) = IS_UNUSED;
            break;
        default:
            break;
        }
    }

    /* Find Lengths of Basic Blocks and build CFG */
    p = bb;
    for (line_num = 1; line_num < len; line_num++) {
        /* Calculate innermost CATCH op */
        innermost_catch = 0;
        if (op_array->last_try_catch > 0) {
            int i;
            zend_try_catch_element* tc_element = op_array->try_catch_array;
            for (i = 0; i < op_array->last_try_catch; i++, tc_element++) {
                // silence compile warnings. Line_num can't be negative here so casting is safe.
                if (tc_element->try_op <= (zend_uint)line_num - 1 &&
                        (zend_uint)line_num - 1 < tc_element->catch_op &&
                        (innermost_catch == 0 ||
                         innermost_catch > tc_element->catch_op)
                   ) {
                    innermost_catch = tc_element->catch_op;
                }
            }
        }
        if (bb[line_num].start != NULL) {
            p->len  = bb[line_num].start - p->start;
            p->next = &bb[line_num];
            op = &p->start[p->len - 1];
            switch (op->opcode) {
            case ZEND_JMP:
                p->jmp_1 = &bb[OP1_OPLINE_NUM(op)];
#  if (PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION >= 2 && PHP_RELEASE_VERSION >= 1) || PHP_MAJOR_VERSION >= 6
                /* php >= 5.2.1 introduces a ZEND_JMP before a ZEND_FETCH_CLASS and ZEND_CATCH
                   this leaves those blocks intact */
                if ((op + 1)->opcode == ZEND_FETCH_CLASS && (op + 2)->opcode == ZEND_CATCH) { /* fix for #242 */
                    p->follow = &bb[line_num];
                }
#  endif
                break;
            case ZEND_JMPZNZ:
                p->jmp_2 = &bb[OP2_OPLINE_NUM(op)];
                p->jmp_ext = &bb[op->extended_value];
                break;
            case ZEND_JMPZ:
            case ZEND_JMPNZ:
            case ZEND_JMPZ_EX:
            case ZEND_JMPNZ_EX:
            case ZEND_NEW:
            case ZEND_FE_RESET:
            case ZEND_FE_FETCH:
#ifdef ZEND_JMP_SET
            case ZEND_JMP_SET:
#endif
                p->jmp_2 = &bb[OP2_OPLINE_NUM(op)];
                p->follow = &bb[line_num];
                break;
#ifdef ZEND_GOTO
            case ZEND_GOTO:
                p->jmp_1 = &bb[OP1_OPLINE_NUM(op)];
                p->follow = &bb[line_num];
                break;
#endif
            case ZEND_RETURN:
#ifdef ZEND_ENGINE_2_4
            case ZEND_RETURN_BY_REF:
#endif
            case ZEND_EXIT:
            case ZEND_BRK:
            case ZEND_CONT:
                /* HOESH: The control might flow to the innermost CATCH
                 * op if an exception thrown earlier. We can follow to CATCH
                 * to protect it against unnecessary K.O. In that case,
                 * the last RETURN will hold HANDLE_EXCEPTION.
                 * If no CATCH op toward, then glue it to the last opcode,
                 * that is HANDLE_EXCEPTION.
                 */
                p->follow = (innermost_catch > 0) ? &bb[innermost_catch] : &bb[len - 1];
                break;
            case ZEND_DO_FCALL:
            case ZEND_DO_FCALL_BY_NAME:
                p->follow = &bb[line_num];
                break;
            case ZEND_CATCH:
                p->jmp_ext = &bb[op->extended_value];
                p->follow = &bb[line_num];
                break;
            case ZEND_THROW:
                if (OP2_OPLINE_NUM(op) != -1) {
                    p->jmp_2 = &bb[OP2_OPLINE_NUM(op)];
                }
                p->follow = &bb[line_num];
                break;
            default:
                p->follow = &bb[line_num];
            }
            p = &bb[line_num];
        }
    }
    p->len = (op_array->opcodes + op_array->last) - p->start;

    /* Remove Unused brk_cont_array (BRK and CONT instructions replaced by JMP)
    TODO: cannot be removed when ZEND_GOTO is used in oparray with php 5.3+
    if (remove_brk_cont_array)
    {
    	if (op_array->brk_cont_array != NULL)
    	{
    		efree(op_array->brk_cont_array);
    		op_array->brk_cont_array = NULL;
    	}
    	op_array->last_brk_cont = 0;
    }*/
    return remove_brk_cont_array;
}

/*
 * Emits Optimized Code
 */
static void emit_cfg(zend_op_array *op_array, BB* bb)
{
    /* Compacting Optimized Code */
    BB* p = bb;
    zend_op* start = op_array->opcodes;
    zend_op* op = start;
    zend_op* end = op + op_array->last;
    while (p != NULL) {
        if (p->used) {
            if (p->len > 0 && op != p->start) {
                memmove(op, p->start, p->len * sizeof(zend_op));
            }
            p->start = op;
            op += p->len;
        }
        p = p->next;
    }
    op_array->last = op - start;
#ifndef ZEND_ENGINE_2_4
    op_array->start_op = NULL;
#endif
    while (op < end) {
        SET_TO_NOP(op);
        op++;
    }

    /* Set Branch Targets */
    p = bb;
    while (p != NULL) {
        if (p->used && p->len > 0) {
#ifdef ZEND_ENGINE_2_4
            if (p->jmp_1 != NULL) {
                p->start[p->len-1].op1.opline_num = p->jmp_1->start - start;
            }
            if (p->jmp_2 != NULL) {
                p->start[p->len-1].op2.opline_num = p->jmp_2->start - start;
            }
#else
            if (p->jmp_1 != NULL) {
                p->start[p->len-1].op1.u.opline_num = p->jmp_1->start - start;
            }
            if (p->jmp_2 != NULL) {
                p->start[p->len-1].op2.u.opline_num = p->jmp_2->start - start;
            }
#endif
            if (p->jmp_ext != NULL) {
                p->start[p->len-1].extended_value = p->jmp_ext->start - start;
            }
        }
        p = p->next;
    }

    /*
     * HOESH: Reassign try & catch blocks
     */
    if (op_array->last_try_catch>0) {
        int i;
        int last_try_catch = op_array->last_try_catch;
        zend_try_catch_element* old_tc_element = op_array->try_catch_array;
        for (i=0; i<op_array->last_try_catch; i++, old_tc_element++) {
            if (bb[old_tc_element->try_op].used &&
                    bb[old_tc_element->catch_op].used) {
                old_tc_element->try_op = bb[old_tc_element->try_op].start - start;
                old_tc_element->catch_op = bb[old_tc_element->catch_op].start - start;
            } else {
                old_tc_element->try_op = 0;
                old_tc_element->catch_op = 0;
                last_try_catch--;
            }
        }
        if (op_array->last_try_catch > last_try_catch) {
            zend_try_catch_element* new_tc_array = NULL;
            if (last_try_catch > 0) {
                /* Lost some try & catch blocks */
                zend_try_catch_element* new_tc_element = emalloc(sizeof(zend_try_catch_element)*last_try_catch);
                new_tc_array = new_tc_element;
                old_tc_element = op_array->try_catch_array;
                for (i=0; i<op_array->last_try_catch; i++, old_tc_element++) {
                    if (old_tc_element->try_op != old_tc_element->catch_op) {
                        new_tc_element->try_op = old_tc_element->try_op;
                        new_tc_element->catch_op = old_tc_element->catch_op;
                        new_tc_element++;
                    }
                }
            }
            /* Otherwise lost all try & catch blocks */
            efree(op_array->try_catch_array);
            op_array->try_catch_array = new_tc_array;
            op_array->last_try_catch = last_try_catch;
        }
    }
}

#define GET_REG(R) {\
                     if (assigned[(R)] < 0) {\
                       zend_uint j = 0;\
                       while (j < op_array->T) {\
                         if (reg_pool[j] == 0 && (global[(R)] == 0 || used[j] == 0)) {\
                           reg_pool[j] = 1;\
                           assigned[(R)] = j;\
                           if (j + 1 > n) {\
                             n = j + 1;\
                           }\
                           break;\
                         }\
                         j++;\
                       }\
                     }\
                     used[assigned[(R)]] = 1;\
                   }

#define FREE_REG(R) reg_pool[(R)] = 0;


void reassign_registers(zend_op_array *op_array, BB* p, char *global)
{
    zend_uint i;
    zend_uint n = 0;

#ifdef ZEND_ENGINE_2_3
    int opline_num;
    int first_class_delayed = -1;
    int prev_class_delayed = -1;
    int last_class_delayed_in_prev_bb = -1;
    int last_class_delayed_in_this_bb = -1;

    ALLOCA_FLAG(use_heap)
    int* assigned  = do_alloca(op_array->T * sizeof(int), use_heap);
    char* reg_pool = do_alloca(op_array->T * sizeof(char), use_heap);
    char* used     = do_alloca(op_array->T * sizeof(char), use_heap);
#else
    int* assigned  = do_alloca(op_array->T * sizeof(int));
    char* reg_pool = do_alloca(op_array->T * sizeof(char));
    char* used     = do_alloca(op_array->T * sizeof(char));
#endif

    memset(assigned, -1, op_array->T * sizeof(int));
    memset(reg_pool, 0, op_array->T * sizeof(char));
    memset(used, 0, op_array->T * sizeof(char));

    while (p != NULL) {
        if (p->used && p->len > 0) {
            zend_op* start = p->start;
            zend_op* op    = start + p->len;

            for (i = 0; i < op_array->T; i++) {
                if (!global[i]) {
                    if (assigned[i] >= 0) {
                        reg_pool[assigned[i]] = 0;
                    }
                    assigned[i] = -1;
                }
            }

            while (start < op) {
                --op;
                if (op->opcode == ZEND_DO_FCALL_BY_NAME && OP1_TYPE(op) == IS_CONST) {
#ifndef ZEND_ENGINE_2_4
                    zval_dtor(&op->op1.u.constant);
#endif
                    OP1_TYPE(op) = IS_UNUSED;
                }
                if (OP1_TYPE(op) == IS_VAR || OP1_TYPE(op) == IS_TMP_VAR) {
                    int r = VAR_NUM(OP1_VARR(op));
                    GET_REG(r);

                    if (op->opcode == ZEND_DO_FCALL_BY_NAME) {
                        OP1_TYPE(op) = IS_UNUSED;
                    } else if (op->opcode == ZEND_FETCH_CONSTANT && OP1_TYPE(op) == IS_VAR) {
                        OP1_VARR(op) = VAR_VAL(assigned[r]);
#ifndef ZEND_ENGINE_2_3
                        /* restore op1 type from VAR to CONST (the opcode handler expects this or bombs out with invalid opcode)
                           FETCH_CONSTANT when fetching class constant screws up because of this with >=php-5.3 */
                        OP1_TYPE(op) = IS_CONST;
#endif
                    } else {
                        OP1_VARR(op) = VAR_VAL(assigned[r]);
                    }
                }
                if (OP2_TYPE(op) == IS_VAR || OP2_TYPE(op) == IS_TMP_VAR) {
                    int r = VAR_NUM(OP2_VARR(op));
                    GET_REG(r);
                    OP2_VARR(op) = VAR_VAL(assigned[r]);
                }
#ifdef ZEND_ENGINE_2_3
                if (op->opcode == ZEND_DECLARE_INHERITED_CLASS_DELAYED) {
                    int r = VAR_NUM(op->extended_value);
                    GET_REG(r);
                    op->extended_value = VAR_VAL(assigned[r]);

                    opline_num = op - op_array->opcodes;
                    /* store the very first occurence of ZEND_DECLARE_INHERITED_CLASS_DELAYED
                       we need this to restore op_array->early_binding later on */
                    if (first_class_delayed == -1) {
                        first_class_delayed = opline_num;
                    }
                    if (last_class_delayed_in_this_bb == -1) {
                        last_class_delayed_in_this_bb = opline_num;
                    }

                    if (prev_class_delayed != -1) {
                        /* link current ZEND_DECLARE_INHERITED_CLASS_DELAYED to previous one */
                        RES_OPLINE_NUM(op) = prev_class_delayed;
                    }
                    /* There might be another ZEND_DECLARE_INHERITED_CLASS_DELAYED down the road
                       (or actually up the road since were traversing the oparray backwards).
                       store current opline */
                    prev_class_delayed = opline_num;
                }
#endif
                if (op->opcode == ZEND_DECLARE_INHERITED_CLASS) {

                    int r = VAR_NUM(op->extended_value);
                    GET_REG(r);
                    op->extended_value = VAR_VAL(assigned[r]);
                }
                if (RES_TYPE(op) & IS_VAR || RES_TYPE(op) & IS_TMP_VAR) {
                    int r = VAR_NUM(RES_VARR(op));
                    GET_REG(r);
                    RES_VARR(op) = VAR_VAL(assigned[r]);
                    if (
                        (op->opcode != ZEND_RECV && op->opcode != ZEND_RECV_INIT &&
                         (RES_TYPE(op) & IS_VAR && RES_USED(op) & EXT_TYPE_UNUSED) != 0) ||
                        (!(OP1_TYPE(op) == RES_TYPE(op) && OP1_VARR(op) == RES_VARR(op)) &&
                         !(OP2_TYPE(op) == RES_TYPE(op) && OP2_VARR(op) == RES_VARR(op)) &&
                         !global[r] && op->opcode != ZEND_ADD_ARRAY_ELEMENT )
                    ) {
                        FREE_REG(VAR_NUM(RES_VARR(op)));
                    }
                }
            }
        }
#ifdef ZEND_ENGINE_2_3
        if (last_class_delayed_in_prev_bb != -1 && last_class_delayed_in_this_bb != -1) {
#  ifdef ZEND_ENGINE_2_4
            op_array->opcodes[last_class_delayed_in_prev_bb].result.opline_num = prev_class_delayed;
#  else
            op_array->opcodes[last_class_delayed_in_prev_bb].result.u.opline_num = prev_class_delayed;
#  endif
            last_class_delayed_in_prev_bb = -1;
        }
        if (last_class_delayed_in_this_bb != -1) {
            last_class_delayed_in_prev_bb = last_class_delayed_in_this_bb;
            last_class_delayed_in_this_bb = -1;
        }
        prev_class_delayed = -1;
#endif

        p = p->next;
    }
    op_array->T = n;
#ifdef ZEND_ENGINE_2_3
    /* link back op_array->early_binding to the first occurance of ZEND_DECLARE_INHERITED_CLASS_DELAYED */
    if (first_class_delayed != -1) {
        op_array->early_binding = first_class_delayed;
    }

    free_alloca(used, use_heap);
    free_alloca(reg_pool, use_heap);
    free_alloca(assigned, use_heap);
#else
    free_alloca(used);
    free_alloca(reg_pool);
    free_alloca(assigned);
#endif
}

void restore_operand_types(zend_op_array *op_array)
{
    zend_op* op = op_array->opcodes;
    int len = op_array->last;
    int line_num;

    for (line_num=0; line_num < len; op++,line_num++) {
        if (op->opcode == ZEND_FETCH_CONSTANT && OP1_TYPE(op) == IS_VAR) {
            /* restore op1 type from VAR to CONST (the opcode handler expects this or bombs out with invalid opcode) */
            OP1_TYPE(op) = IS_CONST;
        }
    }
}

#ifdef ZEND_ENGINE_2_3
/*
 * opt_undo_pass_two: Convert jmp_addrs back to opline_nums,
 *                    convert literal pointers back to literals array indices
 */
int opt_undo_pass_two(zend_op_array *op_array)
{
    zend_op *opline, *end;

    /* if pass_two() hasn't run yet, all opline nums and constant indexes are fine */
#ifdef ZEND_ENGINE_2_4
    if (!(op_array->fn_flags & ZEND_ACC_DONE_PASS_TWO)) {
#else
    if (!op_array->done_pass_two) {
#endif
        return 0;
    }

    opline = op_array->opcodes;
    end = opline + op_array->last;

    while (opline < end) {
#ifdef ZEND_ENGINE_2_4
        /* restore literal pointers back to literals array indices */
        if (OP1_TYPE(opline) == IS_CONST) {
            opline->op1.constant = opline->op1.literal - op_array->literals;
        }
        if (OP2_TYPE(opline) == IS_CONST) {
            opline->op2.constant = opline->op2.literal - op_array->literals;
        }
#endif

        switch (opline->opcode) {
#ifdef ZEND_ENGINE_2_3
        case ZEND_GOTO:
#endif
        case ZEND_JMP:
            OP1_OPLINE_NUM(opline) = OP1_JMP_ADDR(opline) - op_array->opcodes;
            break;
        case ZEND_JMPZ:
        case ZEND_JMPNZ:
        case ZEND_JMPZ_EX:
        case ZEND_JMPNZ_EX:
#ifdef ZEND_ENGINE_2_3
        case ZEND_JMP_SET:
#endif
            OP2_OPLINE_NUM(opline) = OP2_JMP_ADDR(opline) - op_array->opcodes;
            break;
        }
        opline++;
    }

    /* reset the pass_two flag */
#ifdef ZEND_ENGINE_2_4
    op_array->fn_flags &= ~ZEND_ACC_DONE_PASS_TWO;
#else
    op_array->done_pass_two = 0;
#endif

    return 0;
}
#endif

/*
 * Main Optimization Routine
 */
void eaccelerator_optimize(zend_op_array *op_array)
{
    BB* p;
    int i;
    BB* bb;
    zend_uint orig_compiler_options;

#ifdef ZEND_ENGINE_2_3
    ALLOCA_FLAG(use_heap)
#endif

    TSRMLS_FETCH();
    if (!EAG(compiler) || op_array->type != ZEND_USER_FUNCTION) {
        return;
    }

#ifdef ZEND_ENGINE_2_3
    /* We run pass_two() here to let the Zend engine resolve ZEND_GOTO labels
       this converts goto labels(string) to opline numbers(long)
       we need opline numbers for CFG generation, otherwise the optimizer will
       drop code blocks because it thinks they are unused.

       We set compiler options to 0 to prevent pass_two from running the
       op array handler (the optimizer in our case) in an endless loop */

    orig_compiler_options = CG(compiler_options);
    CG(compiler_options) = 0;
    pass_two(op_array TSRMLS_CC);
    CG(compiler_options) = orig_compiler_options;

    /* Convert jmp_addr pointers generated by pass_two() back to opline_nums
       > PHP-5.4.x also converts literal pointers back to literals array indices */
    opt_undo_pass_two(op_array);
#endif

    /* Allocate memory for CFG */
#ifdef ZEND_ENGINE_2_3
    bb = do_alloca(sizeof(BB)*(op_array->last+1), use_heap);
#else
    bb = do_alloca(sizeof(BB)*(op_array->last+1));
#endif
    if (bb == NULL) {
        return;
    }
    memset(bb, 0, sizeof(BB)*(op_array->last+1));

    /* Find All Basic Blocks and build CFG */
    if (build_cfg(op_array, bb)) {
#ifdef ZEND_ENGINE_2_3
        char *global = do_alloca(op_array->T * sizeof(char), use_heap);
#else
        char *global = do_alloca(op_array->T * sizeof(char));
#endif
        if (global == NULL) {
            return;
        }
        /* TODO free bb here */

        for (i=0; i<2; i++) {
            /* Determine used blocks and its predecessors */
            mark_used_bb(bb);

            /* JMP Optimization */
            optimize_jmp(bb, op_array);
            compute_live_var(bb, op_array, global);

            /* Optimize Each Basic Block */
            p = bb;
            while (p != NULL) {
                optimize_bb(p, op_array, global, i TSRMLS_CC);
                p = p->next;
            }

            /* Mark All Basic Blocks as Unused. Free Predecessors Links. */
            p = bb;
            while (p != NULL) {
                rm_bb(p);
                p = p->next;
            }
        }

        /* Mark Used Blocks */
        mark_used_bb2(bb);

        /* Remove Unused Basic Blocks */
        p = bb;
        while (p->next != NULL) {
            if (p->next->used) {
                p = p->next;
            } else {
                del_bb(p->next);
                p->next = p->next->next;
            }
        }

        /* Store Optimized Code */
        emit_cfg(op_array, bb);
        reassign_registers(op_array, bb, global);
//    dump_bb(bb, op_array);

#ifdef ZEND_ENGINE_2_3
        free_alloca(global, use_heap);
#else
        free_alloca(global);
#endif
    } else {
        /* build_cfg encountered some nested ZEND_BRK or ZEND_CONT's
           which it could not replace with JMP's

           now restore the operand type changes that build_cfg had
           already applied, to prevent 'invalid opcode' errors
           on opcode handlers that expect a strict set of operand
           types since php-5.1 (like ZEND_FETCH_CONSTANT)
        */
#ifndef ZEND_ENGINE_2_3
        /* FETCH_CONSTANT when fetching class constant screws up
           because of this with >=php-5.3 */
        restore_operand_types(op_array);
#endif
    }
#ifdef ZEND_ENGINE_2_3
    free_alloca(bb, use_heap);
#else
    free_alloca(bb);
#endif
}

#endif /* #ifdef WITH_EACCELERATOR_OPTIMIZER */
#endif /* #ifdef HAVE_EACCELERATOR */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: et sw=4 ts=4 fdm=marker
 * vim<600: et sw=4 ts=4
 */
