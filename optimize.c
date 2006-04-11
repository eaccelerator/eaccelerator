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
   $Id: optimize.c 176 2006-03-05 12:18:54Z bart $
*/

#include "eaccelerator.h"

#ifdef HAVE_EACCELERATOR
#ifdef WITH_EACCELERATOR_OPTIMIZER

#include "zend.h"
#include "zend_API.h"
#include "zend_constants.h"
#include "opcodes.h"

typedef unsigned int* set;

struct _BBlink;

typedef struct _BB {
  zend_op*        start;
  int             len;
  int             used;
  /*
   * HOESH: To protect merging. Primary
   * it abblies to try & catch blocks.
   * ZEND_ENGINE_2 specific, but can take place
   */
  int             protect_merge;
  struct _BB*     jmp_1;
  struct _BB*     jmp_2;
  struct _BB*     jmp_ext;
  struct _BB*     follow;
  struct _BBlink* pred;  // Gonna be a chain of BBs
  struct _BB*     next;
} BB;

typedef struct _BBlink {
  struct _BB*     bb;
  struct _BBlink* next;
} BBlink;

#if 0
static void dump_bb(BB* bb, zend_op_array *op_array) {
  BB* p = bb;
  BBlink *q;
  zend_printf("<pre>%s:%s\n", op_array->filename, op_array->function_name);
  while (p != NULL) {
    zend_printf("  bb%u start=%u len=%d used=%d\n",
                 (unsigned int)(p-bb),
                 (unsigned int)(p->start-op_array->opcodes),
                 p->len,
                 p->used);
    if (p->jmp_1) {
      zend_printf("    jmp_1 bb%u start=%u  len=%d used=%d\n",
                  (unsigned int)(p->jmp_1-bb),
                  (unsigned int)(p->jmp_1->start-op_array->opcodes),
                  p->jmp_1->len,
                  p->jmp_1->used);
    }
    if (p->jmp_2) {
      zend_printf("    jmp_2 bb%u start=%u  len=%d used=%d\n",
                  (unsigned int)(p->jmp_2-bb),
                  (unsigned int)(p->jmp_2->start-op_array->opcodes),
                  p->jmp_2->len,
                  p->jmp_2->used);
    }
    if (p->jmp_ext) {
      zend_printf("    jmp_ext bb%u start=%u  len=%d used=%d\n",
                  (unsigned int)(p->jmp_ext-bb),
                  (unsigned int)(p->jmp_ext->start-op_array->opcodes),
                  p->jmp_ext->len,
                  p->jmp_ext->used);
    }
    if (p->follow) {
      zend_printf("    follow bb%u start=%u  len=%d used=%d\n",
                  (unsigned int)(p->follow-bb),
                  (unsigned int)(p->follow->start-op_array->opcodes),
                  p->follow->len,
                  p->follow->used);
    }
    q = p->pred;
    while (q != NULL) {
      zend_printf("    pred bb%u start=%u  len=%d used=%d (",
                  (unsigned int)(q->bb-bb),
                  (unsigned int)(q->bb->start-op_array->opcodes),
                  q->bb->len,
                  q->bb->used);
      if (q->bb->jmp_1 == p) {
        zend_printf("jmp_1 ");
      }
      if (q->bb->jmp_2 == p) {
        zend_printf("jmp_2 ");
      }
      if (q->bb->jmp_ext == p) {
        zend_printf("jmp_ext ");
      }
      if (q->bb->follow == p) {
        zend_printf("follow ");
      }
      zend_printf(")\n");
      q = q->next;
    }
    p = p->next;
  }
  zend_printf("</pre><hr>\n");
  fflush(stdout);
}

static void dump_array(int nb,void *pos,char type)
{  int j;

   switch(type) {
   case 'i': {
     int *ptr=pos;
     for (j=0;j<nb;j++) {
       zend_printf("%d:%6d ",j,*ptr);
       ptr++;
     }
   }
   break; 
   case 'x': {
     int *ptr=pos;
     for (j=0;j<nb;j++) {
       zend_printf("%d:%x ",j,*ptr);
       ptr++;
     }
   }
   break; 
   case 'c': {
     unsigned char *ptr=pos;
     for (j=0;j<nb;j++) {
/*       if (*ptr>=32 && *ptr<128) zend_printf("%d:%c",j,*ptr);
       else if (*ptr>=128) zend_printf("%d:%2x",j,*ptr);
       else if (*ptr<16) zend_printf("%d:&%1x",j,*ptr);
       else zend_printf("%d:$%1x",j,(*ptr)-16); */
       zend_printf("%d:%1x ",j,*ptr);
       ptr++;
     }
   }
   break;   
   default:
     for (j=0;j<nb;j++)
       zend_printf("# ");
   }
   zend_printf("<br>\n");
}
#endif

#define SET_TO_NOP(op) \
  (op)->opcode = ZEND_NOP; \
  (op)->op1.op_type = IS_UNUSED; \
  (op)->op2.op_type = IS_UNUSED; \
  (op)->result.op_type = IS_UNUSED;

static void compute_live_var(BB* bb, zend_op_array* op_array, char* global)
{
  BB* p = bb;
  memset(global, 0, op_array->T * sizeof(char));
  if (p != NULL && p->next != NULL) {
    int bb_count = 0;
    char *def     = do_alloca(op_array->T * sizeof(char));
#if 0
    zend_printf("<hr>%s::%s<br>", op_array->filename, op_array->function_name);
#endif
    while (p != NULL) {
      zend_op* op = p->start;
      zend_op* end = op + p->len;
      memset(def, 0, op_array->T * sizeof(char));
      while (op < end) {
        if ((op->op1.op_type == IS_VAR || op->op1.op_type == IS_TMP_VAR) &&
            !def[VAR_NUM(op->op1.u.var)] && !global[VAR_NUM(op->op1.u.var)]) {
          global[VAR_NUM(op->op1.u.var)] = 1;
        }
        if ((op->op2.op_type == IS_VAR || op->op2.op_type == IS_TMP_VAR) &&
            !def[VAR_NUM(op->op2.u.var)] && !global[VAR_NUM(op->op2.u.var)]) {
#ifdef ZEND_ENGINE_2
          if (op->opcode != ZEND_OP_DATA) {
            global[VAR_NUM(op->op2.u.var)] = 1;
          }
#else
          global[VAR_NUM(op->op2.u.var)] = 1;
#endif
        }
#ifdef ZEND_ENGINE_2
        if (op->opcode == ZEND_DECLARE_INHERITED_CLASS &&
            !def[VAR_NUM(op->extended_value)] &&
            !global[VAR_NUM(op->extended_value)]) {
          global[VAR_NUM(op->extended_value)] = 1;
        }
#endif
        if ((op->result.op_type == IS_VAR &&
#ifdef ZEND_ENGINE_2
             (op->opcode == ZEND_RECV || op->opcode == ZEND_RECV_INIT ||
              (op->result.u.EA.type & EXT_TYPE_UNUSED) == 0)) ||
#else
             (op->result.u.EA.type & EXT_TYPE_UNUSED) == 0) ||
#endif
            (op->result.op_type == IS_TMP_VAR)) {
          if (!def[VAR_NUM(op->result.u.var)] && !global[VAR_NUM(op->result.u.var)]) {
            switch (op->opcode) {
              case ZEND_RECV:
              case ZEND_RECV_INIT:
              case ZEND_ADD_ARRAY_ELEMENT:
                global[VAR_NUM(op->result.u.var)] = 1;
             }
          }
          def[VAR_NUM(op->result.u.var)] = 1;
        }
        op++;
      }
      p = p->next;
      bb_count++;
    }
    free_alloca(def);
  }
  {
    char *used = do_alloca(op_array->T * sizeof(char));
    p = bb;
    while (p != NULL) {
      zend_op* op = p->start;
      zend_op* end = op + p->len;
      memset(used, 0, op_array->T * sizeof(char));
      while (op < end) {
        end--;
        if (((end->result.op_type == IS_VAR &&
#ifdef ZEND_ENGINE_2
             (end->opcode == ZEND_RECV || end->opcode == ZEND_RECV_INIT ||
              (end->result.u.EA.type & EXT_TYPE_UNUSED) == 0)) ||
#else
             (end->result.u.EA.type & EXT_TYPE_UNUSED) == 0) ||
#endif
             (end->result.op_type == IS_TMP_VAR)) &&
            !global[VAR_NUM(end->result.u.var)] && !used[VAR_NUM(end->result.u.var)]) {
           switch(end->opcode) {
             case ZEND_JMPZ_EX:
               end->opcode = ZEND_JMPZ;
               end->result.op_type = IS_UNUSED;
               break;
             case ZEND_JMPNZ_EX:
               end->opcode = ZEND_JMPNZ;
               end->result.op_type = IS_UNUSED;
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
               if (end->result.op_type == IS_VAR) {
                 end->result.u.EA.type |= EXT_TYPE_UNUSED;
               }
               break;
             case ZEND_UNSET_VAR:
#ifndef ZEND_ENGINE_2_1
/* Pre-PHP 5.1 only */
             case ZEND_UNSET_DIM_OBJ:
               end->result.op_type = IS_UNUSED;
               break;
#else
             case ZEND_UNSET_DIM:
             case ZEND_UNSET_OBJ:
               end->result.op_type = IS_UNUSED;
               break;
#endif
             case ZEND_RECV:
             case ZEND_RECV_INIT:
             /*case ZEND_ADD_ARRAY_ELEMENT:*/
             case ZEND_INCLUDE_OR_EVAL:
#ifndef ZEND_ENGINE_2_1
/* Pre-PHP 5.1 only */
             case ZEND_JMP_NO_CTOR:
#else
             case ZEND_NEW:
#endif
             case ZEND_FE_FETCH:
             case ZEND_PRINT:
#ifdef ZEND_ENGINE_2
             case ZEND_INIT_METHOD_CALL:
             case ZEND_INIT_STATIC_METHOD_CALL:
             case ZEND_ASSIGN_DIM:
             case ZEND_ASSIGN_OBJ:
             case ZEND_DECLARE_CLASS:
             case ZEND_DECLARE_INHERITED_CLASS:
#endif
              break;
            default:
              if (end->op1.op_type == IS_CONST) {
                zval_dtor(&end->op1.u.constant);
              }
              if (end->op2.op_type == IS_CONST) {
                zval_dtor(&end->op2.u.constant);
              }
              SET_TO_NOP(end);
          }
        } else if (end->result.op_type == IS_VAR &&
                   (end->result.u.EA.type & EXT_TYPE_UNUSED) != 0 &&
#ifdef ZEND_ENGINE_2
                   end->opcode != ZEND_RECV && end->opcode != ZEND_RECV_INIT &&
#endif
                   used[VAR_NUM(end->result.u.var)]) {
          end->result.u.EA.type &= ~EXT_TYPE_UNUSED;
        }
        if ((end->result.op_type == IS_VAR &&
#ifdef ZEND_ENGINE_2
            (end->opcode == ZEND_RECV || end->opcode == ZEND_RECV_INIT ||
             (end->result.u.EA.type & EXT_TYPE_UNUSED) == 0)) ||
#else
            (end->result.u.EA.type & EXT_TYPE_UNUSED) == 0) ||
#endif
            (end->result.op_type == IS_TMP_VAR)) {
          switch (end->opcode) {
            case ZEND_RECV:
            case ZEND_RECV_INIT:
            case ZEND_ADD_ARRAY_ELEMENT:
              used[VAR_NUM(end->result.u.var)] = 1;
              break;
            default:
              used[VAR_NUM(end->result.u.var)] = 0;
           }
        }
        if (end->op1.op_type == IS_VAR || end->op1.op_type == IS_TMP_VAR) {
          used[VAR_NUM(end->op1.u.var)] = 1;
        }
        if (end->op2.op_type == IS_VAR || end->op2.op_type == IS_TMP_VAR) {
          used[VAR_NUM(end->op2.u.var)] = 1;
        }
#ifdef ZEND_ENGINE_2
        if (end->opcode == ZEND_DECLARE_INHERITED_CLASS) {
          used[VAR_NUM(end->extended_value)] = 1;
        }
#endif
      }
      p = p->next;
    }
    free_alloca(used);
  }
/*
  while (1) {
    int change = 0;
    p = bb;
    while (p != NULL) {
      int n = SET_SIZE(op_array->T);
      while (n-- > 0) {
        unsigned int old_in = p->in[n];
        p->out[n] = 0;
        if (p->jmp_1   != NULL) p->out[n] |= p->jmp_1->in[n];
        if (p->jmp_2   != NULL) p->out[n] |= p->jmp_2->in[n];
        if (p->jmp_ext != NULL) p->out[n] |= p->jmp_ext->in[n];
        if (p->follow  != NULL) p->out[n] |= p->follow->in[n];
        p->in[n] = p->use[n] | (p->out[n] & ~p->def[n]);
        if (old_in != p->in[n]) change = 1;
      }
      p = p->next;
    }
    if (!change) break;
  }
*/
}

/* Adds FROM as predcessor of TO */
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

/* Removes FROM from predcessors of TO */
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
  if (bb->used) return;
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
  if (bb->follow != NULL) {
    mark_used_bb(bb->follow);
    BB_ADD_PRED(bb->follow, bb);
  }
}

static void mark_used_bb2(BB* bb)
{
  if (bb->used) return;
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
  if (bb->follow != NULL) {
    mark_used_bb2(bb->follow);
  }
}

static void rm_bb(BB* bb)
{
  if (bb->used == 0) {
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
    if (end->op1.op_type == IS_CONST) {
      zval_dtor(&end->op1.u.constant);
    }
    if (end->op2.op_type == IS_CONST) {
      zval_dtor(&end->op2.u.constant);
    }
    SET_TO_NOP(end);
  }
  bb->len  = 0;
  bb->used = 0;
}

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

    /* Clear leading NOP(s) */
    p = bb;
    while (p != NULL) {
      if (p->used) {
        while (p->len > 0 && p->start->opcode ==ZEND_NOP) {
          p->start++;
          p->len--;
        }
        if (p->len == 0 && p != bb) {
          if (p->follow) {
            replace_bb(p, p->follow);
          }
          rm_bb(p);
          p->used = 0;
          ok = 0;
        }
      }
      p = p->next;
    }

    /* JMP optimization */
    p = bb;
    while (p != NULL) {
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
              op->op1.op_type = IS_UNUSED;
              op->op2.op_type = IS_UNUSED;
              p->jmp_1 = p->jmp_2;
              p->jmp_2 = NULL;
              p->jmp_ext = NULL;
              ok = 0;
              goto jmp;
            } else if (op->op1.op_type == IS_CONST) {
              /* JMPZNZ  0,L1,L2  =>  JMP L1
              */
              if (!zend_is_true(&op->op1.u.constant)) {
                op->opcode = ZEND_JMP;
                op->extended_value = 0;
                op->op1.op_type = IS_UNUSED;
                op->op2.op_type = IS_UNUSED;
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
                op->op1.op_type = IS_UNUSED;
                op->op2.op_type = IS_UNUSED;
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
                       op->op1.op_type == IS_TMP_VAR) {
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
                       p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                       op->op1.u.var == p->jmp_2->start->op1.u.var) {
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
                       p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                       op->op1.u.var == p->jmp_2->start->op1.u.var) {
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
                       p->jmp_ext->start->op1.op_type == IS_TMP_VAR &&
                       op->op1.u.var == p->jmp_ext->start->op1.u.var) {
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
                       p->jmp_ext->start->op1.op_type == IS_TMP_VAR &&
                       op->op1.u.var == p->jmp_ext->start->op1.u.var) {
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
                       p->jmp_ext->start->op1.op_type == IS_TMP_VAR &&
                       op->op1.u.var == p->jmp_ext->start->op1.u.var) {
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
            } else if (op->op1.op_type == IS_CONST) {
              /* JMPZ  0,L1  =>  JMP L1
              */
              if (!zend_is_true(&op->op1.u.constant)) {
                op->opcode = ZEND_JMP;
                op->op1.op_type = IS_UNUSED;
                op->op2.op_type = IS_UNUSED;
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
                       op->op1.op_type == IS_TMP_VAR) {
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
                      p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                      op->op1.u.var == p->jmp_2->start->op1.u.var) {
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
                       p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                       op->op1.u.var == p->jmp_2->start->op1.u.var) {
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
            } else if (op->op1.op_type == IS_CONST) {
              /* JMPNZ  1,L1  =>  JMP L1
              */
              if (zend_is_true(&op->op1.u.constant)) {
                op->opcode = ZEND_JMP;
                op->op1.op_type = IS_UNUSED;
                op->op2.op_type = IS_UNUSED;
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
                       op->op1.op_type == IS_TMP_VAR) {
            if        (p->jmp_2->start->opcode == ZEND_JMPNZ &&
                       p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                       op->op1.u.var == p->jmp_2->start->op1.u.var) {
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
                       p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                       op->op1.u.var == p->jmp_2->start->op1.u.var) {
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
                       p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                       op->op1.u.var == p->jmp_2->start->op1.u.var) {
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
                op->op1.op_type == IS_TMP_VAR &&
                op->result.op_type == IS_TMP_VAR &&
                op->op1.u.var == op->result.u.var) {
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
              op->op2.op_type = IS_UNUSED;
              ok = 0;
              break;
            } else if (p->jmp_2->len == 1 &&
                       op->result.op_type == IS_TMP_VAR) {
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
                        p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                        op->result.u.var == p->jmp_2->start->op1.u.var) ||
                       (p->jmp_2->start->opcode == ZEND_JMPZ_EX &&
                        p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                        p->jmp_2->start->result.op_type == IS_TMP_VAR &&
                        op->result.u.var == p->jmp_2->start->op1.u.var &&
                        op->result.u.var == p->jmp_2->start->result.u.var)) {
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
                        p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                        op->result.u.var == p->jmp_2->start->op1.u.var) ||
                       (p->jmp_2->start->opcode == ZEND_JMPNZ_EX &&
                        p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                        p->jmp_2->start->result.op_type == IS_TMP_VAR &&
                        op->result.u.var == p->jmp_2->start->op1.u.var &&
                        op->result.u.var == p->jmp_2->start->result.u.var)) {
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
                       p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                       op->result.u.var == p->jmp_2->start->op1.u.var) {
              memcpy(&op->result, &p->jmp_2->start->result, sizeof(zval));
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
                       p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                       op->result.u.var == p->jmp_2->start->op1.u.var) {
              op->opcode = ZEND_JMPZ;
              op->result.op_type = IS_UNUSED;
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
            } else if (op->result.op_type == IS_TMP_VAR &&
                       p->jmp_2->start->opcode == ZEND_FREE &&
                       p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                       op->result.u.var == p->jmp_2->start->op1.u.var) {
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
                op->result.op_type = IS_UNUSED;
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
                op->op1.op_type == IS_TMP_VAR &&
                op->result.op_type == IS_TMP_VAR &&
                op->op1.u.var == op->result.u.var) {
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
              op->op2.op_type = IS_UNUSED;
              ok = 0;
              break;
            } else if (p->jmp_2->len == 1 &&
                       op->result.op_type == IS_TMP_VAR) {
            /*     JMPNZ_EX ?,L1,$x  =>  JMPNZ_EX ?,L2,$x
                   ...                   ...
               L1: JMPNZ    $x,L2        JMPNZ    $x,L2
               ------------------------------------------
                   JMPNZ_EX ?,L1,$x  =>  JMPNZ_EX ?,L2,$x
                   ...                   ...
               L1: JMPNZ_EX $x,L2,$x     JMPNZ_EX $x,L2,$x
            */
            if        ((p->jmp_2->start->opcode == ZEND_JMPNZ &&
                        p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                        op->result.u.var == p->jmp_2->start->op1.u.var) ||
                       (p->jmp_2->start->opcode == ZEND_JMPNZ_EX &&
                        p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                        p->jmp_2->start->result.op_type == IS_TMP_VAR &&
                        op->result.u.var == p->jmp_2->start->op1.u.var &&
                        op->result.u.var == p->jmp_2->start->result.u.var)) {
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
                       p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                       op->result.u.var == p->jmp_2->start->op1.u.var) {
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
                        p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                        op->result.u.var == p->jmp_2->start->op1.u.var) ||
                       (p->jmp_2->start->opcode == ZEND_JMPZ_EX &&
                        p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                        p->jmp_2->start->result.op_type == IS_TMP_VAR &&
                        op->result.u.var == p->jmp_2->start->op1.u.var &&
                        op->result.u.var == p->jmp_2->start->result.u.var)) {
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
                       p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                       op->result.u.var == p->jmp_2->start->op1.u.var) {
              memcpy(&op->result, &p->jmp_2->start->result, sizeof(zval));
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
                       p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                       op->result.u.var == p->jmp_2->start->op1.u.var) {
              op->opcode = ZEND_JMPNZ;
              op->result.op_type = IS_UNUSED;
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
            } else if (op->result.op_type == IS_TMP_VAR &&
                       p->jmp_2->start->opcode == ZEND_FREE &&
                       p->jmp_2->start->op1.op_type == IS_TMP_VAR &&
                       op->result.u.var == p->jmp_2->start->op1.u.var) {
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
              op->result.op_type = IS_UNUSED;
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
#ifndef ZEND_ENGINE_2_1
/* Pre-PHP 5.1 only */
          case ZEND_JMP_NO_CTOR:
#else
          case ZEND_NEW:
#endif
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
		  p->protect_merge == 0)
	  {
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
/*???
      } else if (p != bb && p->used && p->pred != NULL && p->pred->bb->used && p->pred->next == NULL &&
          p->follow == NULL &&
          p->pred->bb->follow == NULL &&
          p->pred->bb->jmp_1 == p &&
          p->pred->bb->jmp_2 == NULL &&
          p->pred->bb->jmp_ext == NULL &&
          p->pred->bb != p &&
          p->pred->bb->start[p->pred->bb->len-1].opcode == ZEND_JMP) {
        BB* x = p->pred->bb;
        int offset = p->len;
        zend_op* save = emalloc(sizeof(zend_op)*offset);
        memcpy(save, p->start, sizeof(zend_op)*offset);
        if (x->start < p->start) {
          if (offset > 1) {
            BB* q = x->next;
            while (q != p) {
              if (q->len > 0) {
                memcpy(q->start + (offset-1), q->start, sizeof(zend_op)*q->len);
              }
              q->start += offset - 1;
              q = q->next;
            }
          }
        } else {
          BB* q = p->next;
          while (q != x->next) {
            if (q->len > 0) {
              memcpy(q->start - offset, q->start, sizeof(zend_op)*q->len);
              q->start -= offset;
            }
            q = q->next;
          }
        }
        memcpy(x->start+(x->len-1),save, sizeof(zend_op)*offset);
        efree(save);
        x->len += offset-1;
        if (x->start < p->start) {
          SET_TO_NOP(&p->start[p->len-1]);
        } else {
          SET_TO_NOP(&x->start[x->len]);
        }

        BB_DEL_PRED(p, x);
        x->jmp_1 = p->jmp_1;
        if (p->jmp_1 != NULL) {
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
        if (p->follow != NULL) {
          x->follow  = p->follow;
          BB_DEL_PRED(p->follow, p);
          BB_ADD_PRED(p->follow, x);
        }
        p->start = NULL;
        p->used  = 0;
        p->len   = 0;
        ok = 0;
*/
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
               p->jmp_1->start->opcode == ZEND_EXIT))
		  {
            BB_DEL_PRED(p->jmp_1, p);
            RM_BB(p->jmp_1);
            memcpy(op, p->jmp_1->start, sizeof(zend_op));
            if (op->op1.op_type == IS_CONST)
			{
              zval_copy_ctor(&op->op1.u.constant);
            }
            p->jmp_1 = NULL;
            ok = 0;
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

static int opt_get_constant(const char* name, int name_len, zend_constant** result TSRMLS_DC) {
  if (!EAG(encoder) ||
      (name_len == sizeof("false")-1 && strcmp(name,"false") == 0) ||
      (name_len == sizeof("true")-1 && strcmp(name,"true") == 0)) {
    zend_constant *c;
    int retval;
    char *lookup_name = do_alloca(name_len+1);
    memcpy(lookup_name, name, name_len);
    lookup_name[name_len] = '\0';

    if (zend_hash_find(EG(zend_constants), lookup_name, name_len+1, (void **) &c)==SUCCESS) {
      *result = c;
      retval=1;
    } else {
      zend_str_tolower(lookup_name, name_len);

      if (zend_hash_find(EG(zend_constants), lookup_name, name_len+1, (void **) &c)==SUCCESS) {
        if ((c->flags & CONST_CS) && (memcmp(c->name, name, name_len)!=0)) {
          retval=0;
        } else {
          *result = c;
          retval=1;
        }
      } else {
        retval=0;
      }
    }
    free_alloca(lookup_name);
    return retval;
  } else {
    return 0;
  }
}

static int opt_function_exists(const char* name, int name_len TSRMLS_DC) {
  if (!EAG(encoder)) {
    char *lcname;
    char *lcfname;
    Bucket *p;

    lcname = estrndup(name,name_len+1);
    zend_str_tolower(lcname, name_len);
    p = module_registry.pListHead;
    while (p != NULL) {
      zend_module_entry *m = (zend_module_entry*)p->pData;
      if (m->type == MODULE_PERSISTENT) {
        zend_function_entry* f = m->functions;
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
  }
  return 0;
}

static int opt_extension_loaded(const char* name, int name_len TSRMLS_DC) {
  if (!EAG(encoder)) {
    Bucket *p = module_registry.pListHead;
    while (p != NULL) {
      zend_module_entry *m = (zend_module_entry*)p->pData;
      if (m->type == MODULE_PERSISTENT && strcmp(m->name,name) == 0) {
        return 1;
      }
      p = p->pListNext;
    }
  }
  return 0;
}

static int opt_result_is_numeric(zend_op* x) {
  switch (x->opcode) {
    case ZEND_ADD:
    case ZEND_SUB:
    case ZEND_MUL:
    case ZEND_DIV:
    case ZEND_MOD:
    case ZEND_SL:
    case ZEND_SR:
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
    case ZEND_ASSIGN_ADD:
    case ZEND_ASSIGN_SUB:
    case ZEND_ASSIGN_MUL:
    case ZEND_ASSIGN_DIV:
    case ZEND_ASSIGN_MOD:
    case ZEND_ASSIGN_SL:
    case ZEND_ASSIGN_SR:
    case ZEND_BOOL:
      return 1;
    case ZEND_CAST:
      if (x->extended_value == IS_BOOL ||
          x->extended_value == IS_LONG ||
          x->extended_value == IS_DOUBLE) {
        return 1;
      }
      return 0;
    case ZEND_DO_FCALL:
      if (x->op1.op_type == IS_CONST &&
          x->op1.u.constant.type == IS_STRING &&
          (strcmp(x->op1.u.constant.value.str.val,"count") == 0 ||
           strcmp(x->op1.u.constant.value.str.val,"sizeof") == 0 ||
           strcmp(x->op1.u.constant.value.str.val,"strcmp") == 0 ||
           strcmp(x->op1.u.constant.value.str.val,"strlen") == 0 ||
           strcmp(x->op1.u.constant.value.str.val,"strpos") == 0 ||
           strcmp(x->op1.u.constant.value.str.val,"strncmp") == 0 ||
           strcmp(x->op1.u.constant.value.str.val,"strcoll") == 0 ||
           strcmp(x->op1.u.constant.value.str.val,"strcasecmp") == 0)) {
        return 1;
      }
      return 0;
    default:
      return 0;
  }
  return 0;
}

#ifndef ZEND_ENGINE_2
#define FETCH_TYPE(op) ((op)->op2.u.fetch_type)
#else
#define FETCH_TYPE(op) ((op)->op2.u.EA.type)
#endif

#define SET_UNDEFINED(op) Ts[VAR_NUM((op).u.var)] = NULL;
#define SET_DEFINED(op)   Ts[VAR_NUM((op)->result.u.var)] = (op);
#define IS_DEFINED(op)    (Ts[VAR_NUM((op).u.var)] != NULL)
#define DEFINED_OP(op)    (Ts[VAR_NUM((op).u.var)])

static void optimize_bb(BB* bb, zend_op_array* op_array, char* global, int pass TSRMLS_DC)
{
  zend_op* prev = NULL;
  zend_op* op = bb->start;
  zend_op* end = op + bb->len;

  HashTable assigns;
  HashTable fetch_dim;

  zend_op** Ts = do_alloca(sizeof(zend_op*)*op_array->T);
  memset(Ts, 0, sizeof(zend_op*)*op_array->T);

  zend_hash_init(&assigns, 0, NULL, NULL, 0);
  zend_hash_init(&fetch_dim, 0, NULL, NULL, 0);

  while (op < end) {
    /* Constant Folding */
    if (op->op1.op_type == IS_TMP_VAR &&
        IS_DEFINED(op->op1) &&
        DEFINED_OP(op->op1)->opcode == ZEND_QM_ASSIGN &&
        DEFINED_OP(op->op1)->op1.op_type == IS_CONST) {
      zend_op *x = DEFINED_OP(op->op1);
      if (op->opcode != ZEND_CASE) {
        SET_UNDEFINED(op->op1);
        memcpy(&op->op1, &x->op1, sizeof(znode));
        SET_TO_NOP(x);
      }
    }
    if (op->op2.op_type == IS_TMP_VAR &&
        IS_DEFINED(op->op2) &&
        DEFINED_OP(op->op2)->opcode == ZEND_QM_ASSIGN &&
        DEFINED_OP(op->op2)->op1.op_type == IS_CONST) {
      zend_op *x = DEFINED_OP(op->op2);
      SET_UNDEFINED(op->op2);
      memcpy(&op->op2, &x->op1, sizeof(znode));
      SET_TO_NOP(x);
    }
/*
    if (op->result.op_type == IS_TMP_VAR &&
        Ts[VAR_NUM(op->result.u.var)].op_type == IS_CONST &&
        op->opcode != ZEND_ADD_CHAR &&
        op->opcode != ZEND_ADD_STRING &&
        op->opcode != ZEND_ADD_VAR &&
        op->opcode != ZEND_ADD_ARRAY_ELEMENT) {
      Ts[VAR_NUM(op->result.u.var)].op_type = IS_UNUSED;
    }
*/
    if (op->opcode == ZEND_IS_EQUAL) {
      if (op->op1.op_type == IS_CONST &&
          (op->op1.u.constant.type == IS_BOOL &&
           op->op1.u.constant.value.lval == 0)) {
        op->opcode = ZEND_BOOL_NOT;
        memcpy(&op->op1, &op->op2, sizeof(znode));
        op->op2.op_type = IS_UNUSED;
      } else if (op->op1.op_type == IS_CONST &&
                 op->op1.u.constant.type == IS_BOOL &&
                 op->op1.u.constant.value.lval == 1) {
        op->opcode = ZEND_BOOL;
        memcpy(&op->op1, &op->op2, sizeof(znode));
        op->op2.op_type = IS_UNUSED;
      } else if (op->op2.op_type == IS_CONST &&
                 op->op2.u.constant.type == IS_BOOL &&
                 op->op2.u.constant.value.lval == 0) {
        op->opcode = ZEND_BOOL_NOT;
        op->op2.op_type = IS_UNUSED;
      } else if (op->op2.op_type == IS_CONST &&
          op->op2.u.constant.type == IS_BOOL &&
          op->op2.u.constant.value.lval == 1) {
        op->opcode = ZEND_BOOL;
        op->op2.op_type = IS_UNUSED;
      } else if (op->op2.op_type == IS_CONST &&
          op->op2.u.constant.type == IS_LONG &&
          op->op2.u.constant.value.lval == 0 &&
          (op->op1.op_type == IS_TMP_VAR || op->op1.op_type == IS_VAR) &&
          IS_DEFINED(op->op1) &&
          opt_result_is_numeric(DEFINED_OP(op->op1))) {
        op->opcode = ZEND_BOOL_NOT;
        op->op2.op_type = IS_UNUSED;
      }
    } else if (op->opcode == ZEND_IS_NOT_EQUAL) {
      if (op->op1.op_type == IS_CONST &&
          op->op1.u.constant.type == IS_BOOL &&
          op->op1.u.constant.value.lval == 0) {
        op->opcode = ZEND_BOOL;
        memcpy(&op->op1, &op->op2, sizeof(znode));
        op->op2.op_type = IS_UNUSED;
      } else if (op->op1.op_type == IS_CONST &&
                 op->op1.u.constant.type == IS_BOOL &&
                 op->op1.u.constant.value.lval == 1) {
        op->opcode = ZEND_BOOL_NOT;
        memcpy(&op->op1, &op->op2, sizeof(znode));
        op->op2.op_type = IS_UNUSED;
      } else if (op->op2.op_type == IS_CONST &&
                 op->op2.u.constant.type == IS_BOOL &&
                 op->op2.u.constant.value.lval == 0) {
        op->opcode = ZEND_BOOL;
        op->op2.op_type = IS_UNUSED;
      } else if (op->op2.op_type == IS_CONST &&
          op->op2.u.constant.type == IS_BOOL &&
          op->op2.u.constant.value.lval == 1) {
        op->opcode = ZEND_BOOL_NOT;
        op->op2.op_type = IS_UNUSED;
      } else if (op->op2.op_type == IS_CONST &&
          op->op2.u.constant.type == IS_LONG &&
          op->op2.u.constant.value.lval == 0 &&
          (op->op1.op_type == IS_TMP_VAR || op->op1.op_type == IS_VAR) &&
          IS_DEFINED(op->op1) &&
          opt_result_is_numeric(DEFINED_OP(op->op1))) {
        op->opcode = ZEND_BOOL;
        op->op2.op_type = IS_UNUSED;
      }
    }
#if 0 
    /* doesn't work with newer php versions */
    if (op->opcode == ZEND_FETCH_CONSTANT &&
        op->result.op_type == IS_TMP_VAR) {
      zend_constant *c = NULL;
      if (op->op1.u.constant.value.str.val != NULL && 
          opt_get_constant(op->op1.u.constant.value.str.val, op->op1.u.constant.value.str.len, &c TSRMLS_CC) 
          && c != NULL && ((c->flags & CONST_PERSISTENT) != 0)) {
        STR_FREE(op->op1.u.constant.value.str.val);
        memcpy(&op->op1.u.constant, &c->value, sizeof(zval));
        zval_copy_ctor(&op->op1.u.constant);
        op->opcode = ZEND_QM_ASSIGN;
        op->extended_value = 0;
        op->op1.op_type = IS_CONST;
        op->op2.op_type = IS_UNUSED;
      }
    } else 
#endif

    if ((op->opcode == ZEND_ADD ||
                op->opcode == ZEND_SUB ||
                op->opcode == ZEND_MUL ||
                op->opcode == ZEND_DIV ||
                op->opcode == ZEND_MOD ||
                op->opcode == ZEND_SL ||
                op->opcode == ZEND_SR ||
                op->opcode == ZEND_CONCAT ||
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
               op->op1.op_type == IS_CONST &&
               op->op2.op_type == IS_CONST &&
               op->result.op_type == IS_TMP_VAR) {

      typedef int (*binary_op_type)(zval *, zval *, zval*  TSRMLS_DC);

      binary_op_type binary_op = (binary_op_type)get_binary_op(op->opcode);

      if (binary_op != NULL) {
        int old = EG(error_reporting);
        zval res;
        EG(error_reporting) = 0;
        if (binary_op(&res, &op->op1.u.constant, &op->op2.u.constant TSRMLS_CC) != FAILURE) {
          zval_dtor(&op->op1.u.constant);
          zval_dtor(&op->op2.u.constant);
          op->opcode = ZEND_QM_ASSIGN;
          op->extended_value = 0;
          op->op1.op_type = IS_CONST;
          memcpy(&op->op1.u.constant, &res, sizeof(zval));
          op->op2.op_type = IS_UNUSED;
        }
        EG(error_reporting) = old;
      }
    } else if ((op->opcode == ZEND_BW_NOT ||
                op->opcode == ZEND_BOOL_NOT) &&
               op->op1.op_type == IS_CONST &&
               op->result.op_type == IS_TMP_VAR) {
      int (*unary_op)(zval *result, zval *op1) =
        unary_op = get_unary_op(op->opcode);
      if (unary_op != NULL) {
        int old = EG(error_reporting);
        zval res;
        EG(error_reporting) = 0;
        if (unary_op(&res, &op->op1.u.constant) != FAILURE) {
          zval_dtor(&op->op1.u.constant);
          op->opcode = ZEND_QM_ASSIGN;
          op->extended_value = 0;
          op->op1.op_type = IS_CONST;
          memcpy(&op->op1.u.constant, &res, sizeof(zval));
          op->op2.op_type = IS_UNUSED;
        }
        EG(error_reporting) = old;
      }
    } else if ((op->opcode == ZEND_BOOL) &&
               op->op1.op_type == IS_CONST &&
               op->result.op_type == IS_TMP_VAR) {
      zval res;
      res.type = IS_BOOL;
      res.value.lval = zend_is_true(&op->op1.u.constant);
      zval_dtor(&op->op1.u.constant);
      op->opcode = ZEND_QM_ASSIGN;
      op->extended_value = 0;
      op->op1.op_type = IS_CONST;
      memcpy(&op->op1.u.constant, &res, sizeof(zval));
      op->op2.op_type = IS_UNUSED;
    } else if ((op->opcode == ZEND_CAST) &&
               op->op1.op_type == IS_CONST &&
               op->result.op_type == IS_TMP_VAR &&
               op->extended_value != IS_ARRAY &&
               op->extended_value != IS_OBJECT &&
               op->extended_value != IS_RESOURCE) {
      zval res;
      memcpy(&res,&op->op1.u.constant,sizeof(zval));
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
      zval_dtor(&op->op1.u.constant);
      op->opcode = ZEND_QM_ASSIGN;
      op->extended_value = 0;
      op->op1.op_type = IS_CONST;
      memcpy(&op->op1.u.constant, &res, sizeof(zval));
      op->op2.op_type = IS_UNUSED;

    /* FREE(CONST) => NOP
    */
    } else if (op->opcode == ZEND_FREE &&
               op->op1.op_type == IS_CONST) {
      zval_dtor(&op->op1.u.constant);
      SET_TO_NOP(op);

    /* INIT_STRING ADD_CAHR ADD_STRIN ADD_VAR folding */

    /* INIT_STRING($y) => QM_ASSIGN('',$y)
    */
    } else if (op->opcode == ZEND_INIT_STRING) {
      op->opcode = ZEND_QM_ASSIGN;
      op->op1.op_type = IS_CONST;
      op->op2.op_type = IS_UNUSED;
      op->op1.u.constant.type = IS_STRING;
      op->op1.u.constant.value.str.len = 0;
      op->op1.u.constant.value.str.val = empty_string;
    /* ADD_CHAR(CONST,CONST,$y) => QM_ASSIGN(CONST,$y)
    */
    } else if (op->opcode == ZEND_ADD_CHAR &&
               op->op1.op_type == IS_CONST) {
      size_t len;
      op->opcode = ZEND_QM_ASSIGN;
      op->op1.op_type = IS_CONST;
      op->op2.op_type = IS_UNUSED;
      convert_to_string(&op->op1.u.constant);
      len = op->op1.u.constant.value.str.len + 1;
      STR_REALLOC(op->op1.u.constant.value.str.val,len+1);
      op->op1.u.constant.value.str.val[len-1] = (char) op->op2.u.constant.value.lval;
      op->op1.u.constant.value.str.val[len] = 0;
      op->op1.u.constant.value.str.len = len;
    /* ADD_STRING(CONST,CONST,$y) => QM_ASSIGN(CONST,$y)
    */
    } else if (op->opcode == ZEND_ADD_STRING &&
               op->op1.op_type == IS_CONST) {
      size_t len;
      op->opcode = ZEND_QM_ASSIGN;
      op->op1.op_type = IS_CONST;
      op->op2.op_type = IS_UNUSED;
      convert_to_string(&op->op1.u.constant);
      convert_to_string(&op->op2.u.constant);
      len = op->op1.u.constant.value.str.len + op->op2.u.constant.value.str.len;
      STR_REALLOC(op->op1.u.constant.value.str.val,len+1);
      memcpy(op->op1.u.constant.value.str.val+op->op1.u.constant.value.str.len,
             op->op2.u.constant.value.str.val, op->op2.u.constant.value.str.len);
      op->op1.u.constant.value.str.val[len] = 0;
      op->op1.u.constant.value.str.len = len;
      STR_FREE(op->op2.u.constant.value.str.val);
    /* ADD_VAR(CONST,VAR,$y) => CONCAT(CONST,$y)
    */
    } else if (op->opcode == ZEND_ADD_VAR &&
               op->op1.op_type == IS_CONST) {
      op->opcode = ZEND_CONCAT;
    /* CONCAT('',$x,$y) + ADD_CHAR($y,CHAR,$z) => CONCAT($x, CONST, $z)
    */
    } else if (op->opcode == ZEND_ADD_CHAR &&
               op->op1.op_type == IS_TMP_VAR &&
               IS_DEFINED(op->op1) &&
               DEFINED_OP(op->op1)->opcode == ZEND_CONCAT &&
               DEFINED_OP(op->op1)->op1.op_type == IS_CONST &&
               DEFINED_OP(op->op1)->op1.u.constant.type == IS_STRING &&
               DEFINED_OP(op->op1)->op1.u.constant.value.str.len == 0) {
      char ch = (char) op->op2.u.constant.value.lval;
      zend_op *x = DEFINED_OP(op->op1);
      SET_UNDEFINED(op->op1);
      memcpy(&op->op1, &x->op2, sizeof(op->op2));
      op->opcode = ZEND_CONCAT;
      op->op2.u.constant.type = IS_STRING;
      op->op2.u.constant.value.str.val = emalloc(2);
      op->op2.u.constant.value.str.val[0] = ch;
      op->op2.u.constant.value.str.val[1] = '\000';
      op->op2.u.constant.value.str.len = 1;
      STR_FREE(x->op1.u.constant.value.str.val);
      SET_TO_NOP(x);
    /*
       CONCAT('',$x,$y) + ADD_STRING($y,$v,$z) => CONCAT($x, $v, $z)
       CONCAT('',$x,$y) + CONCAT($y,$v,$z)     => CONCAT($x, $v, $z)
       CONCAT('',$x,$y) + ADD_VAR($y,$v,$z)    => CONCAT($x, $v, $z)
    */
    } else if ((op->opcode == ZEND_ADD_STRING ||
                op->opcode == ZEND_CONCAT ||
                op->opcode == ZEND_ADD_VAR) &&
               op->op1.op_type == IS_TMP_VAR &&
               IS_DEFINED(op->op1) &&
               DEFINED_OP(op->op1)->opcode == ZEND_CONCAT &&
               DEFINED_OP(op->op1)->op1.op_type == IS_CONST &&
               DEFINED_OP(op->op1)->op1.u.constant.type == IS_STRING &&
               DEFINED_OP(op->op1)->op1.u.constant.value.str.len == 0) {
      zend_op *x = DEFINED_OP(op->op1);
      SET_UNDEFINED(op->op1);
      op->opcode = ZEND_CONCAT;
      memcpy(&op->op1, &x->op2, sizeof(op->op2));
      STR_FREE(x->op1.u.constant.value.str.val);
      SET_TO_NOP(x);
    /* ADD_CHAR($x,CONST,$y) + ADD_CHAR($y,CHAR,$z) => ADD_STRING($x, CONST, $z)
    */
    } else if (op->opcode == ZEND_ADD_CHAR &&
               op->op1.op_type == IS_TMP_VAR &&
               IS_DEFINED(op->op1) &&
               DEFINED_OP(op->op1)->opcode == ZEND_ADD_CHAR) {
      char ch1 = (char) DEFINED_OP(op->op1)->op2.u.constant.value.lval;
      char ch2 = (char) op->op2.u.constant.value.lval;
      DEFINED_OP(op->op1)->op2.u.constant.type = IS_STRING;
      DEFINED_OP(op->op1)->op2.u.constant.value.str.val = emalloc(3);
      DEFINED_OP(op->op1)->op2.u.constant.value.str.val[0] = ch1;
      DEFINED_OP(op->op1)->op2.u.constant.value.str.val[1] = ch2;
      DEFINED_OP(op->op1)->op2.u.constant.value.str.val[2] = '\000';
      DEFINED_OP(op->op1)->op2.u.constant.value.str.len = 2;
      memcpy(&DEFINED_OP(op->op1)->result, &op->result, sizeof(op->result));
      DEFINED_OP(op->op1)->opcode = ZEND_ADD_STRING;
      SET_DEFINED(DEFINED_OP(op->op1));
      SET_TO_NOP(op);
    /* CONCAT($x,CONST,$y) + ADD_CHAR($y,CONST,$z) => CONCAT($x, CONST, $z)
       ADD_STRING($x,CONST,$y) + ADD_CHAR($y,CONST,$z) => ADD_STRING($x, CONST, $z)
    */
    } else if (op->opcode == ZEND_ADD_CHAR &&
               op->op1.op_type == IS_TMP_VAR &&
               IS_DEFINED(op->op1) &&
               (DEFINED_OP(op->op1)->opcode == ZEND_CONCAT ||
                DEFINED_OP(op->op1)->opcode == ZEND_ADD_STRING) &&
               DEFINED_OP(op->op1)->op2.op_type == IS_CONST) {
      size_t len;
      convert_to_string(&DEFINED_OP(op->op1)->op2.u.constant);
      len = DEFINED_OP(op->op1)->op2.u.constant.value.str.len + 1;
      STR_REALLOC(DEFINED_OP(op->op1)->op2.u.constant.value.str.val,len+1);
      DEFINED_OP(op->op1)->op2.u.constant.value.str.val[len-1] = (char) op->op2.u.constant.value.lval;
      DEFINED_OP(op->op1)->op2.u.constant.value.str.val[len] = 0;
      DEFINED_OP(op->op1)->op2.u.constant.value.str.len = len;
      memcpy(&DEFINED_OP(op->op1)->result, &op->result, sizeof(op->result));
      if (DEFINED_OP(op->op1)->op1.op_type == DEFINED_OP(op->op1)->result.op_type &&
          DEFINED_OP(op->op1)->op1.u.var == DEFINED_OP(op->op1)->result.u.var) {
        DEFINED_OP(op->op1)->opcode = ZEND_ADD_STRING;
      }
      SET_DEFINED(DEFINED_OP(op->op1));
      SET_TO_NOP(op);
    /* ADD_CHAR($x,CONST,$y) + ADD_STRING($y,CONST,$z) => ADD_STRING($x, CONST, $z)
       ADD_CHAR($x,CONST,$y) + CONCAT($y,CONST,$z) => CONCAT($x, CONST, $z)
    */
    } else if ((op->opcode == ZEND_ADD_STRING ||
                op->opcode == ZEND_CONCAT) &&
               op->op2.op_type == IS_CONST &&
               op->op1.op_type == IS_TMP_VAR &&
               IS_DEFINED(op->op1) &&
               DEFINED_OP(op->op1)->opcode == ZEND_ADD_CHAR) {
      char ch = (char) DEFINED_OP(op->op1)->op2.u.constant.value.lval;
      size_t len;
      convert_to_string(&op->op2.u.constant);
      len = op->op2.u.constant.value.str.len + 1;
      DEFINED_OP(op->op1)->op2.u.constant.type = IS_STRING;
      DEFINED_OP(op->op1)->op2.u.constant.value.str.val = emalloc(len+1);
      DEFINED_OP(op->op1)->op2.u.constant.value.str.val[0] = ch;
      memcpy(DEFINED_OP(op->op1)->op2.u.constant.value.str.val+1,
             op->op2.u.constant.value.str.val, op->op2.u.constant.value.str.len);
      DEFINED_OP(op->op1)->op2.u.constant.value.str.val[len] = 0;
      DEFINED_OP(op->op1)->op2.u.constant.value.str.len = len;
      STR_FREE(op->op2.u.constant.value.str.val);
      memcpy(&DEFINED_OP(op->op1)->result, &op->result, sizeof(op->result));
      DEFINED_OP(op->op1)->opcode = op->opcode;
      if (DEFINED_OP(op->op1)->op1.op_type == DEFINED_OP(op->op1)->result.op_type &&
          DEFINED_OP(op->op1)->op1.u.var == DEFINED_OP(op->op1)->result.u.var) {
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
               op->op2.op_type == IS_CONST &&
               op->op1.op_type == IS_TMP_VAR &&
               IS_DEFINED(op->op1) &&
               (DEFINED_OP(op->op1)->opcode == ZEND_CONCAT ||
                DEFINED_OP(op->op1)->opcode == ZEND_ADD_STRING) &&
               DEFINED_OP(op->op1)->op2.op_type == IS_CONST) {
      size_t len;
      convert_to_string(&DEFINED_OP(op->op1)->op2.u.constant);
      convert_to_string(&op->op2.u.constant);
      len = DEFINED_OP(op->op1)->op2.u.constant.value.str.len + op->op2.u.constant.value.str.len;
      STR_REALLOC(DEFINED_OP(op->op1)->op2.u.constant.value.str.val,len+1);
      memcpy(DEFINED_OP(op->op1)->op2.u.constant.value.str.val+DEFINED_OP(op->op1)->op2.u.constant.value.str.len,
             op->op2.u.constant.value.str.val, op->op2.u.constant.value.str.len);
      DEFINED_OP(op->op1)->op2.u.constant.value.str.val[len] = 0;
      DEFINED_OP(op->op1)->op2.u.constant.value.str.len = len;
      STR_FREE(op->op2.u.constant.value.str.val);
      memcpy(&DEFINED_OP(op->op1)->result, &op->result, sizeof(op->result));
      if (op->opcode == ZEND_CONCAT) {
        DEFINED_OP(op->op1)->opcode = ZEND_CONCAT;
      }
      if (DEFINED_OP(op->op1)->op1.op_type == DEFINED_OP(op->op1)->result.op_type &&
          DEFINED_OP(op->op1)->op1.u.var == DEFINED_OP(op->op1)->result.u.var) {
        DEFINED_OP(op->op1)->opcode = ZEND_ADD_STRING;
      }
      SET_DEFINED(DEFINED_OP(op->op1));
      SET_TO_NOP(op);
    /* FETCH_X      local("GLOBALS"),$x => FETCH_X global($y),$z
       FETCH_DIM_X  $x,$y,$z               NOP
    */
    } else if (
#ifndef ZEND_ENGINE_2
               op_array->uses_globals &&
#endif
               ((op->opcode == ZEND_FETCH_DIM_R &&
                op->op1.op_type == IS_VAR &&
/*???               op->extended_value == ZEND_FETCH_STANDARD &&*/
                IS_DEFINED(op->op1) &&
                DEFINED_OP(op->op1)->opcode == ZEND_FETCH_R) ||
               (op->opcode == ZEND_FETCH_DIM_W &&
                op->op1.op_type == IS_VAR &&
                IS_DEFINED(op->op1) &&
                DEFINED_OP(op->op1)->opcode == ZEND_FETCH_W) ||
               (op->opcode == ZEND_FETCH_DIM_RW &&
                op->op1.op_type == IS_VAR &&
                IS_DEFINED(op->op1) &&
                DEFINED_OP(op->op1)->opcode == ZEND_FETCH_RW) ||
               (op->opcode == ZEND_FETCH_DIM_IS &&
                op->op1.op_type == IS_VAR &&
                IS_DEFINED(op->op1) &&
                DEFINED_OP(op->op1)->opcode == ZEND_FETCH_IS) ||
               (op->opcode == ZEND_FETCH_DIM_FUNC_ARG &&
                op->op1.op_type == IS_VAR &&
                IS_DEFINED(op->op1) &&
                DEFINED_OP(op->op1)->opcode == ZEND_FETCH_FUNC_ARG) ||
               (op->opcode == ZEND_FETCH_DIM_UNSET &&
                op->op1.op_type == IS_VAR &&
                IS_DEFINED(op->op1) &&
                DEFINED_OP(op->op1)->opcode == ZEND_FETCH_UNSET)) &&
#ifdef ZEND_ENGINE_2
                FETCH_TYPE(DEFINED_OP(op->op1)) == ZEND_FETCH_GLOBAL &&
#else
                FETCH_TYPE(DEFINED_OP(op->op1)) == ZEND_FETCH_LOCAL &&
#endif
                DEFINED_OP(op->op1)->op1.op_type == IS_CONST &&
                DEFINED_OP(op->op1)->op1.u.constant.type == IS_STRING &&
                DEFINED_OP(op->op1)->op1.u.constant.value.str.len == (sizeof("GLOBALS")-1) &&
                memcmp(DEFINED_OP(op->op1)->op1.u.constant.value.str.val, "GLOBALS", sizeof("GLOBALS")-1) == 0) {
      zend_op *x = DEFINED_OP(op->op1);
      SET_UNDEFINED(op->op1);
      STR_FREE(x->op1.u.constant.value.str.val);
      FETCH_TYPE(x) = ZEND_FETCH_GLOBAL;
      memcpy(&x->op1,&op->op2,sizeof(znode));
      memcpy(&x->result,&op->result,sizeof(znode));
      SET_DEFINED(x);
      SET_TO_NOP(op);
#ifndef ZEND_ENGINE_2
      if (x->op1.op_type == IS_VAR) {
        memcpy(&op->op1,&x->op1,sizeof(znode));
        op->opcode = ZEND_SWITCH_FREE;
        op->extended_value = 0;
      }
#endif
#ifdef ZEND_ENGINE_2
    /* FETCH_IS               local("GLOBALS"),$x    ISSET_ISEMPTY_VAR $y(global),res
       ISSET_ISEMPTY_DIM_OBJ  $x,$y,$res          => NOP
    */
    } else if (op->opcode == ZEND_ISSET_ISEMPTY_DIM_OBJ &&
               op->op1.op_type == IS_VAR &&
              IS_DEFINED(op->op1) &&
              DEFINED_OP(op->op1)->opcode == ZEND_FETCH_IS &&
              FETCH_TYPE(DEFINED_OP(op->op1)) == ZEND_FETCH_GLOBAL &&
              DEFINED_OP(op->op1)->op1.op_type == IS_CONST &&
              DEFINED_OP(op->op1)->op1.u.constant.type == IS_STRING &&
              DEFINED_OP(op->op1)->op1.u.constant.value.str.len == (sizeof("GLOBALS")-1) &&
              memcmp(DEFINED_OP(op->op1)->op1.u.constant.value.str.val, "GLOBALS", sizeof("GLOBALS")-1) == 0) {
      zend_op* x = DEFINED_OP(op->op1);
      STR_FREE(x->op1.u.constant.value.str.val);
      x->opcode = ZEND_ISSET_ISEMPTY_VAR;
      x->extended_value = op->extended_value;
      FETCH_TYPE(x) = ZEND_FETCH_GLOBAL;
      memcpy(&x->op1,&op->op2,sizeof(znode));
      memcpy(&x->result,&op->result,sizeof(znode));
      SET_DEFINED(x);
      SET_TO_NOP(op);
#endif
    } else if (op->opcode == ZEND_FREE &&
               op->op1.op_type == IS_TMP_VAR &&
               IS_DEFINED(op->op1)) {
      /* POST_INC + FREE => PRE_INC */
      if (DEFINED_OP(op->op1)->opcode == ZEND_POST_INC) {
        DEFINED_OP(op->op1)->opcode = ZEND_PRE_INC;
        DEFINED_OP(op->op1)->result.op_type = IS_VAR;
        DEFINED_OP(op->op1)->result.u.EA.type |= EXT_TYPE_UNUSED;
        SET_UNDEFINED(op->op1);
        SET_TO_NOP(op);
      /* POST_DEC + FREE => PRE_DEC */
      } else if (DEFINED_OP(op->op1)->opcode == ZEND_POST_DEC) {
        DEFINED_OP(op->op1)->opcode = ZEND_PRE_DEC;
        DEFINED_OP(op->op1)->result.op_type = IS_VAR;
        DEFINED_OP(op->op1)->result.u.EA.type |= EXT_TYPE_UNUSED;
        SET_UNDEFINED(op->op1);
        SET_TO_NOP(op);
      /* PRINT + FREE => ECHO */
      } else if (DEFINED_OP(op->op1)->opcode == ZEND_PRINT) {
        DEFINED_OP(op->op1)->opcode = ZEND_ECHO;
        DEFINED_OP(op->op1)->result.op_type = IS_UNUSED;
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
               op->op1.op_type == IS_TMP_VAR &&
               (!global[VAR_NUM(op->op1.u.var)] ||
                (op->result.op_type == IS_TMP_VAR &&
                 op->op1.u.var == op->result.u.var)) &&
               IS_DEFINED(op->op1) &&
               (DEFINED_OP(op->op1)->opcode == ZEND_IS_IDENTICAL ||
                DEFINED_OP(op->op1)->opcode == ZEND_IS_NOT_IDENTICAL ||
                DEFINED_OP(op->op1)->opcode == ZEND_IS_EQUAL ||
                DEFINED_OP(op->op1)->opcode == ZEND_IS_NOT_EQUAL ||
                DEFINED_OP(op->op1)->opcode == ZEND_IS_SMALLER ||
                DEFINED_OP(op->op1)->opcode == ZEND_IS_SMALLER_OR_EQUAL)) {
      memcpy(&DEFINED_OP(op->op1)->result, &op->result, sizeof(op->result));
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
                op->op1.op_type == IS_TMP_VAR &&
                (!global[VAR_NUM(op->op1.u.var)] ||
                 (op->result.op_type == IS_TMP_VAR &&
                  op->op1.u.var == op->result.u.var)) &&
                IS_DEFINED(op->op1) &&
                DEFINED_OP(op->op1)->opcode == ZEND_BOOL) {
      zend_op *x = DEFINED_OP(op->op1);
      SET_UNDEFINED(op->op1);
      memcpy(&op->op1, &x->op1, sizeof(op->op1));
      SET_TO_NOP(x);
    /* BOOL_NOT + BOOL     => NOP + BOOL_NOT
       BOOL_NOT + BOOL_NOT => NOP + BOOL
       BOOL_NOT + JMP...   => NOP + JMP[n]...
    */
    } else if ((op->opcode == ZEND_BOOL ||
                op->opcode == ZEND_BOOL_NOT ||
                op->opcode == ZEND_JMPZ||
                op->opcode == ZEND_JMPNZ) &&
                op->op1.op_type == IS_TMP_VAR &&
                (!global[VAR_NUM(op->op1.u.var)] ||
                 (op->result.op_type == IS_TMP_VAR &&
                  op->op1.u.var == op->result.u.var)) &&
                IS_DEFINED(op->op1) &&
                DEFINED_OP(op->op1)->opcode == ZEND_BOOL_NOT) {
      zend_op *x = DEFINED_OP(op->op1);
      switch (op->opcode) {
        case ZEND_BOOL:     op->opcode = ZEND_BOOL_NOT; break;
        case ZEND_BOOL_NOT: op->opcode = ZEND_BOOL;     break;
        case ZEND_JMPZ:     op->opcode = ZEND_JMPNZ;    break;
        case ZEND_JMPNZ:    op->opcode = ZEND_JMPZ;     break;
      }
      SET_UNDEFINED(op->op1);
      memcpy(&op->op1, &x->op1, sizeof(op->op1));
      SET_TO_NOP(x);
    /* function_exists(STR) or is_callable(STR) */
    } else if ((op->opcode == ZEND_BOOL ||
                op->opcode == ZEND_BOOL_NOT ||
                op->opcode == ZEND_JMPZ||
                op->opcode == ZEND_JMPNZ ||
                op->opcode == ZEND_JMPZNZ ||
                op->opcode == ZEND_JMPZ_EX ||
                op->opcode == ZEND_JMPNZ_EX) &&
                op->op1.op_type == IS_VAR &&
                !global[VAR_NUM(op->op1.u.var)] &&
                IS_DEFINED(op->op1) &&
                DEFINED_OP(op->op1)->opcode == ZEND_DO_FCALL &&
                DEFINED_OP(op->op1)->extended_value == 1 &&
                DEFINED_OP(op->op1)->op1.op_type == IS_CONST &&
                DEFINED_OP(op->op1)->op1.u.constant.type == IS_STRING) {
      zend_op* call = DEFINED_OP(op->op1);
      zend_op* send = call-1;
      if (send->opcode == ZEND_SEND_VAL &&
          send->extended_value == ZEND_DO_FCALL &&
          send->op1.op_type == IS_CONST &&
          send->op1.u.constant.type == IS_STRING &&
          (strcmp(call->op1.u.constant.value.str.val,"function_exists") == 0 ||
           strcmp(call->op1.u.constant.value.str.val,"is_callable") == 0)) {
        if (opt_function_exists(send->op1.u.constant.value.str.val, send->op1.u.constant.value.str.len  TSRMLS_CC)) {
          SET_UNDEFINED(op->op1);
          zval_dtor(&send->op1.u.constant);
          SET_TO_NOP(send);
          zval_dtor(&call->op1.u.constant);
          SET_TO_NOP(call);
          op->op1.op_type = IS_CONST;
          op->op1.u.constant.type = IS_BOOL;
          op->op1.u.constant.value.lval = 1;
        }
      } else if (send->opcode == ZEND_SEND_VAL &&
          send->extended_value == ZEND_DO_FCALL &&
          send->op1.op_type == IS_CONST &&
          send->op1.u.constant.type == IS_STRING &&
          strcmp(call->op1.u.constant.value.str.val,"extension_loaded") == 0) {
        if (opt_extension_loaded(send->op1.u.constant.value.str.val, send->op1.u.constant.value.str.len  TSRMLS_CC)) {
          SET_UNDEFINED(op->op1);
          zval_dtor(&send->op1.u.constant);
          SET_TO_NOP(send);
          zval_dtor(&call->op1.u.constant);
          SET_TO_NOP(call);
          op->op1.op_type = IS_CONST;
          op->op1.u.constant.type = IS_BOOL;
          op->op1.u.constant.value.lval = 1;
        }
      } else if (send->opcode == ZEND_SEND_VAL &&
          send->extended_value == ZEND_DO_FCALL &&
          send->op1.op_type == IS_CONST &&
          send->op1.u.constant.type == IS_STRING &&
          strcmp(call->op1.u.constant.value.str.val,"defined") == 0) {
        zend_constant *c = NULL;
        if (opt_get_constant(send->op1.u.constant.value.str.val, send->op1.u.constant.value.str.len, &c TSRMLS_CC) && c != NULL && ((c->flags & CONST_PERSISTENT) != 0)) {
          SET_UNDEFINED(op->op1);
          zval_dtor(&send->op1.u.constant);
          SET_TO_NOP(send);
          zval_dtor(&call->op1.u.constant);
          SET_TO_NOP(call);
          op->op1.op_type = IS_CONST;
          op->op1.u.constant.type = IS_BOOL;
          op->op1.u.constant.value.lval = 1;
        }
      }
    /* QM_ASSIGN($x,$x) => NOP */
    } else if (op->opcode == ZEND_QM_ASSIGN &&
               op->op1.op_type == IS_TMP_VAR &&
               op->result.op_type == IS_TMP_VAR &&
               op->op1.u.var == op->result.u.var) {
      SET_TO_NOP(op);
    /* ?(,,$tmp_x) +QM_ASSIGN($tmp_x,$tmp_y) => ?(,,$tmp_y) + NOP */
    } else if (op->opcode == ZEND_QM_ASSIGN &&
               op->op1.op_type == IS_TMP_VAR &&
               !global[VAR_NUM(op->op1.u.var)] &&
               op->op1.u.var != op->result.u.var &&
               IS_DEFINED(op->op1)) {
      zend_op *x = DEFINED_OP(op->op1);
      if (x->opcode != ZEND_ADD_ARRAY_ELEMENT &&
          x->opcode != ZEND_ADD_STRING &&
          x->opcode != ZEND_ADD_CHAR &&
          x->opcode != ZEND_ADD_VAR) {
        SET_UNDEFINED(op->op1);
        memcpy(&x->result, &op->result, sizeof(op->result));
        SET_DEFINED(x);
        SET_TO_NOP(op);
      }
    /* ECHO(const) + ECHO(const) => ECHO(const) */
    } else if (prev != NULL &&
               op->opcode == ZEND_ECHO &&
               op->op1.op_type == IS_CONST &&
               prev->opcode == ZEND_ECHO &&
               prev->op1.op_type == IS_CONST) {
      int len;
      convert_to_string(&prev->op1.u.constant);
      convert_to_string(&op->op1.u.constant);
      len = prev->op1.u.constant.value.str.len + op->op1.u.constant.value.str.len;
      STR_REALLOC(prev->op1.u.constant.value.str.val,len+1);
      memcpy(prev->op1.u.constant.value.str.val+prev->op1.u.constant.value.str.len,
             op->op1.u.constant.value.str.val, op->op1.u.constant.value.str.len);
      prev->op1.u.constant.value.str.val[len] = 0;
      prev->op1.u.constant.value.str.len = len;
      STR_FREE(op->op1.u.constant.value.str.val);
      SET_TO_NOP(op);
    /* END_SILENCE + BEGIN_SILENCE => NOP + NOP */
    } else if (prev != NULL &&
               prev->opcode == ZEND_END_SILENCE &&
               op->opcode == ZEND_BEGIN_SILENCE) {
      zend_op *x = op+1;
      while (x < end) {
        if (x->opcode == ZEND_END_SILENCE &&
            x->op1.u.var == op->result.u.var) {
          x->op1.u.var = prev->op1.u.var;
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
               prev->result.u.var == op->op1.u.var) {
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
               op->op1.op_type == IS_CONST &&
               op->op1.u.constant.type == IS_STRING &&
               prev->opcode == ZEND_INIT_FCALL_BY_NAME &&
               prev->op1.op_type == IS_UNUSED &&
               prev->op2.op_type == IS_CONST &&
               prev->op2.u.constant.type == IS_STRING &&
               op->op1.u.constant.value.str.len == prev->op2.u.constant.value.str.len &&
               (memcmp(op->op1.u.constant.value.str.val,prev->op2.u.constant.value.str.val,op->op1.u.constant.value.str.len) == 0)) {
       op->opcode = ZEND_DO_FCALL;
       STR_FREE(prev->op2.u.constant.value.str.val);
       SET_TO_NOP(prev);
    /* SEND_REF $x + FCALL "reset" => FE_RESET $x */
    }
#if 0
// this makes php 5 and php >= 4.3.11 go in an endless loop with reset(null)
else if (prev != NULL &&
               prev->opcode == ZEND_SEND_REF &&
               prev->extended_value == ZEND_DO_FCALL &&
               prev->op1.op_type == IS_VAR &&
               prev->op2.u.opline_num == 1 &&
               op->opcode == ZEND_DO_FCALL &&
               op->extended_value == 1 &&
               op->op1.op_type == IS_CONST &&
               op->op1.u.constant.type == IS_STRING &&
               op->op1.u.constant.value.str.len == (sizeof("reset")-1) &&
               (memcmp(op->op1.u.constant.value.str.val, "reset", sizeof("reset")-1) == 0) &&
               op->result.op_type == IS_VAR &&
               ((op->result.u.EA.type & EXT_TYPE_UNUSED) != 0)) {
      STR_FREE(op->op1.u.constant.value.str.val);
      prev->opcode = ZEND_FE_RESET;
      prev->extended_value = 1;
      prev->op2.op_type = IS_UNUSED;
      memcpy(&prev->result, &op->result, sizeof(op->result));
      if (op->result.op_type == IS_VAR &&
          (op->result.u.EA.type & EXT_TYPE_UNUSED) == 0) {
        SET_DEFINED(prev);
        SET_TO_NOP(op);
      } else {
        prev->result.u.EA.type &= ~EXT_TYPE_UNUSED;
        op->opcode = ZEND_SWITCH_FREE;
        op->extended_value = 1;
        memcpy(&op->op1,&prev->result,sizeof(prev->result));
        op->op2.op_type = IS_UNUSED;
        op->result.op_type = IS_UNUSED;
      }
    }
#endif

    /* $a = $a + ? => $a+= ? */
    if (op->opcode == ZEND_ASSIGN &&
        op->op1.op_type == IS_VAR &&
        op->op2.op_type == IS_TMP_VAR &&
        IS_DEFINED(op->op1) &&
        IS_DEFINED(op->op2)) {
      zend_op* l = DEFINED_OP(op->op1);
      zend_op* r = DEFINED_OP(op->op2);
      if (l->opcode == ZEND_FETCH_W &&
          l->op1.op_type == IS_CONST &&
          l->op1.u.constant.type == IS_STRING &&
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
         r->op1.op_type == IS_VAR &&
         IS_DEFINED(r->op1)) {
        zend_op* rl = DEFINED_OP(r->op1);
        if (rl->opcode == ZEND_FETCH_R &&
            rl->op1.op_type == IS_CONST &&
            rl->op1.u.constant.type == IS_STRING &&
            FETCH_TYPE(rl) == FETCH_TYPE(l) &&
            l->op1.u.constant.value.str.len ==
              rl->op1.u.constant.value.str.len &&
            memcmp(l->op1.u.constant.value.str.val,
              rl->op1.u.constant.value.str.val,
              l->op1.u.constant.value.str.len) == 0) {
          switch (r->opcode) {
            case ZEND_ADD:    op->opcode = ZEND_ASSIGN_ADD; break;
            case ZEND_SUB:    op->opcode = ZEND_ASSIGN_SUB; break;
            case ZEND_MUL:    op->opcode = ZEND_ASSIGN_MUL; break;
            case ZEND_DIV:    op->opcode = ZEND_ASSIGN_DIV; break;
            case ZEND_MOD:    op->opcode = ZEND_ASSIGN_MOD; break;
            case ZEND_SL:     op->opcode = ZEND_ASSIGN_SL;  break;
            case ZEND_SR:     op->opcode = ZEND_ASSIGN_SR;  break;
            case ZEND_CONCAT: op->opcode = ZEND_ASSIGN_CONCAT; break;
            case ZEND_BW_OR:  op->opcode = ZEND_ASSIGN_BW_OR;  break;
            case ZEND_BW_AND: op->opcode = ZEND_ASSIGN_BW_AND; break;
            case ZEND_BW_XOR: op->opcode = ZEND_ASSIGN_BW_XOR; break;
            default:
              break;
          }
          memcpy(&op->op2, &r->op2, sizeof(op->op2));
          l->opcode = ZEND_FETCH_RW;
          SET_TO_NOP(r);
          STR_FREE(rl->op1.u.constant.value.str.val);
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
#ifndef ZEND_ENGINE_2_1
/* Pre-PHP 5.1 only */
          op->opcode == ZEND_UNSET_DIM_OBJ ||
#else
          op->opcode == ZEND_UNSET_DIM ||
          op->opcode == ZEND_UNSET_OBJ ||
#endif
          op->opcode == ZEND_INCLUDE_OR_EVAL
#ifdef ZEND_ENGINE_2
          || op->opcode == ZEND_ASSIGN_DIM
          || op->opcode == ZEND_ASSIGN_OBJ
#endif
          ) {
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
        if ((op->result.u.EA.type & EXT_TYPE_UNUSED) != 0 &&
            op->op1.op_type == IS_VAR &&
#ifdef  ZEND_ENGINE_2
            op->extended_value != ZEND_ASSIGN_DIM &&
            op->extended_value != ZEND_ASSIGN_OBJ &&
#endif
            IS_DEFINED(op->op1)) {
          zend_op *x = DEFINED_OP(op->op1);
          if ((x->opcode == ZEND_FETCH_W || x->opcode == ZEND_FETCH_RW) &&
              x->op1.op_type == IS_CONST &&
              x->op1.u.constant.type == IS_STRING) {
            char *s = emalloc(x->op1.u.constant.value.str.len+2);
            memcpy(s,x->op1.u.constant.value.str.val,x->op1.u.constant.value.str.len);
            s[x->op1.u.constant.value.str.len] = (char)FETCH_TYPE(x);
            s[x->op1.u.constant.value.str.len+1] = '\0';
            zend_hash_update(&assigns,
                             s, x->op1.u.constant.value.str.len+2,
                             (void**)&op, sizeof(void*), NULL);
            efree(s);
          }
        }
      } else if ((op->opcode == ZEND_FETCH_R ||
                  op->opcode == ZEND_FETCH_IS) &&
                 !global[VAR_NUM(op->result.u.var)] &&
                 op->op1.op_type == IS_CONST &&
                 op->op1.u.constant.type == IS_STRING) {
        zend_op *x;
        char *s = emalloc(op->op1.u.constant.value.str.len+2);
        memcpy(s,op->op1.u.constant.value.str.val,op->op1.u.constant.value.str.len);
        s[op->op1.u.constant.value.str.len] = (char)FETCH_TYPE(op);
        s[op->op1.u.constant.value.str.len+1] = '\0';

        if (zend_hash_find(&assigns,
                           s, op->op1.u.constant.value.str.len+2,
                           (void**)&x) == SUCCESS) {
          x = *(zend_op**)x;
/*
          if (x->opcode == ZEND_ASSIGN && x->op2.op_type == IS_CONST) {
            zend_printf("possible const propogation in %s:%s (%s,%u:%u)<br>\n",op_array->filename, op_array->function_name, op->op1.u.constant.value.str.val, x-op_array->opcodes, op-op_array->opcodes);
          }
*/
          memcpy(&x->result, &op->result, sizeof(op->result));
          x->result.u.EA.type = 0;
          SET_DEFINED(x);
          zend_hash_del(&assigns,
                        s, op->op1.u.constant.value.str.len+2);
          STR_FREE(op->op1.u.constant.value.str.val);
          SET_TO_NOP(op);
        }
        efree(s);
      } else if (op->opcode == ZEND_FETCH_DIM_R &&
                 op->extended_value != ZEND_FETCH_ADD_LOCK &&
                 op->op1.op_type == IS_VAR &&
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
                x->op1.op_type == IS_VAR &&
                IS_DEFINED(x->op1)) {
          x = DEFINED_OP(x->op1);
        }
        if ((x->opcode == ZEND_FETCH_R ||
             x->opcode == ZEND_FETCH_W ||
             x->opcode == ZEND_FETCH_RW) &&
             x->op1.op_type == IS_CONST &&
             x->op1.u.constant.type == IS_STRING) {
          zend_op *y;
          char *s = emalloc(x->op1.u.constant.value.str.len+2);
          memcpy(s,x->op1.u.constant.value.str.val,x->op1.u.constant.value.str.len);
          s[x->op1.u.constant.value.str.len] = (char)FETCH_TYPE(x);
          s[x->op1.u.constant.value.str.len+1] = '\0';
          if (zend_hash_find(&fetch_dim,
                             s, x->op1.u.constant.value.str.len+2,
                             (void**)&y) == SUCCESS) {
            y = *(zend_op**)y;
            y->extended_value = ZEND_FETCH_ADD_LOCK;
            zend_hash_update(&fetch_dim,
                             s, x->op1.u.constant.value.str.len+2,
                             (void**)&op, sizeof(void*), NULL);
            SET_UNDEFINED(x->result);
            STR_FREE(x->op1.u.constant.value.str.val);
            SET_TO_NOP(x);
            memcpy(&op->op1,&y->op1,sizeof(op->op1));
          } else {
            zend_hash_update(&fetch_dim,
                             s, x->op1.u.constant.value.str.len+2,
                             (void**)&op, sizeof(void*), NULL);
          }
          efree(s);
        }
      }
    }

    if (op->opcode != ZEND_NOP) {
      prev = op;
    }
    if ((op->result.op_type == IS_VAR &&
#ifdef ZEND_ENGINE_2
        (op->opcode == ZEND_RECV || op->opcode == ZEND_RECV_INIT ||
         (op->result.u.EA.type & EXT_TYPE_UNUSED) == 0)) ||
#else
        (op->result.u.EA.type & EXT_TYPE_UNUSED) == 0) ||
#endif
        (op->result.op_type == IS_TMP_VAR)) {
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
      while (next < end && next->opcode == ZEND_NOP) next++;
      if (next < end) {
        memcpy(op,next,(end-next) * sizeof(zend_op));
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
  free_alloca(Ts);
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
	int remove_brk_cont_array = 1;
#ifndef ZEND_ENGINE_2
	int* overload_var = do_alloca(op_array->T * sizeof(int));
	memset(overload_var,-1,op_array->T * sizeof(int));
#else
	/* HOESH: Just to use later... */
	zend_uint innermost_ketchup;

	/* HOESH: Mark try & catch blocks */
	if (op_array->last_try_catch > 0)
	{
		int i;
		zend_try_catch_element* tc_element = op_array->try_catch_array;
		for (i=0; i<op_array->last_try_catch; i++, tc_element++)
		{
			bb[tc_element->try_op].start = &op_array->opcodes[tc_element->try_op];
			bb[tc_element->try_op].protect_merge = 1;

			bb[tc_element->catch_op].start = &op_array->opcodes[tc_element->catch_op];
			bb[tc_element->catch_op].protect_merge = 1;
		}
	}
#endif
	/* Find Starts of Basic Blocks */
	bb[0].start = op;
	for (line_num=0; line_num < len; op++,line_num++)
	{
#ifdef ZEND_ENGINE_2
		const opcode_dsc* dsc = get_opcode_dsc(op->opcode);
		if (dsc != NULL)
		{
			if ((dsc->ops & OP1_MASK) == OP1_UCLASS)
			{
				if (op->op1.op_type != IS_UNUSED)
				{
					op->op1.op_type = IS_VAR;
				}
			}
			else if ((dsc->ops & OP1_MASK) == OP1_CLASS)
			{
				op->op1.op_type = IS_VAR;
			}
			else if ((dsc->ops & OP1_MASK) == OP1_UNUSED)
			{
				op->op1.op_type = IS_UNUSED;
			}
			if ((dsc->ops & OP2_MASK) == OP2_CLASS)
			{
				op->op2.op_type = IS_VAR;
			}
			else if ((dsc->ops & OP2_MASK) == OP2_UNUSED)
			{
				op->op2.op_type = IS_UNUSED;
			}
			else if ((dsc->ops & OP2_MASK) == OP2_FETCH &&
					op->op2.u.EA.type == ZEND_FETCH_STATIC_MEMBER)
			{
				op->op2.op_type = IS_VAR;
			}
			if ((dsc->ops & RES_MASK) == RES_CLASS)
			{
				op->result.op_type = IS_VAR;
				op->result.u.EA.type &= ~EXT_TYPE_UNUSED;
			}
			else if ((dsc->ops & RES_MASK) == RES_UNUSED)
			{
				op->result.op_type = IS_UNUSED;
			}
		}
#endif
		switch(op->opcode)
		{
			case ZEND_RETURN:
			case ZEND_EXIT:
				bb[line_num+1].start = op+1;
				break;
			case ZEND_JMP:
				bb[op->op1.u.opline_num].start = &op_array->opcodes[op->op1.u.opline_num];
				bb[line_num+1].start = op+1;
				break;
			case ZEND_JMPZNZ:
				bb[op->extended_value].start = &op_array->opcodes[op->extended_value];
				bb[op->op2.u.opline_num].start = &op_array->opcodes[op->op2.u.opline_num];
				bb[line_num+1].start = op+1;
				break;
			case ZEND_JMPZ:
			case ZEND_JMPNZ:
			case ZEND_JMPZ_EX:
			case ZEND_JMPNZ_EX:
#ifndef ZEND_ENGINE_2_1
/* Pre-PHP 5.1 only */
			case ZEND_JMP_NO_CTOR:
#else
			case ZEND_NEW:
			case ZEND_FE_RESET:
#endif
			case ZEND_FE_FETCH:
				bb[line_num+1].start = op+1;
				bb[op->op2.u.opline_num].start = &op_array->opcodes[op->op2.u.opline_num];
				break;
			case ZEND_BRK:
				/* Replace BRK by JMP */
				if (op->op1.u.opline_num == -1)
				{
				}
				else if (op->op2.op_type == IS_CONST &&
						op->op2.u.constant.type == IS_LONG)
				{
					int level  = op->op2.u.constant.value.lval;
					zend_uint offset = op->op1.u.opline_num;
					zend_brk_cont_element *jmp_to;
					do
					{
						if (offset < 0 || offset >= op_array->last_brk_cont)
						{
							goto brk_failed;
						}
						jmp_to = &op_array->brk_cont_array[offset];
						if (level>1 &&
							(op_array->opcodes[jmp_to->brk].opcode == ZEND_SWITCH_FREE ||
							op_array->opcodes[jmp_to->brk].opcode == ZEND_FREE))
						{
							goto brk_failed;
						}
						offset = jmp_to->parent;
					}
					while (--level > 0);
					op->opcode = ZEND_JMP;
					op->op1.u.opline_num = jmp_to->brk;
					op->op2.op_type = IS_UNUSED;
					bb[op->op1.u.opline_num].start = &op_array->opcodes[jmp_to->brk];
				}
				else
				{
brk_failed:
				  remove_brk_cont_array = 0;
				}
				bb[line_num+1].start = op+1;
				break;
			case ZEND_CONT:
				/* Replace CONT by JMP */
				if (op->op1.u.opline_num == -1)
				{
				}
				else if (op->op2.op_type == IS_CONST &&
						op->op2.u.constant.type == IS_LONG)
				{
				int level  = op->op2.u.constant.value.lval;
				zend_uint offset = op->op1.u.opline_num;
				zend_brk_cont_element *jmp_to;
				do
				{
					if (offset < 0 || offset >= op_array->last_brk_cont)
					{
						goto cont_failed;
					}
					jmp_to = &op_array->brk_cont_array[offset];
					if (level>1 &&
						(op_array->opcodes[jmp_to->brk].opcode == ZEND_SWITCH_FREE ||
						op_array->opcodes[jmp_to->brk].opcode == ZEND_FREE))
					{
						goto cont_failed;
					}
					offset = jmp_to->parent;
				}
				while (--level > 0);
				op->opcode = ZEND_JMP;
				op->op1.u.opline_num = jmp_to->cont;
				op->op2.op_type = IS_UNUSED;
				bb[op->op1.u.opline_num].start = &op_array->opcodes[jmp_to->cont];
				}
				else
				{
cont_failed:
					remove_brk_cont_array = 0;
				}
				bb[line_num+1].start = op+1;
				break;
#ifdef ZEND_ENGINE_2
			case ZEND_CATCH:
				bb[op->extended_value].start = &op_array->opcodes[op->extended_value];
				bb[line_num+1].start = op+1;
				break;
			case ZEND_THROW:
				if (op->op2.u.opline_num != -1)
				{
					bb[op->op2.u.opline_num].start = &op_array->opcodes[op->op2.u.opline_num];
				}
				bb[line_num+1].start = op+1;
				break;
			case ZEND_DO_FCALL:
			case ZEND_DO_FCALL_BY_NAME:
				if (op->op2.u.opline_num != -1)
				{
					bb[op->op2.u.opline_num].start = &op_array->opcodes[op->op2.u.opline_num];
					bb[line_num+1].start = op+1;
				}
				break;
#else
			case ZEND_INIT_FCALL_BY_NAME:
				if (op->op1.op_type == IS_VAR && op->result.op_type == IS_VAR)
				{
					overload_var[op->result.u.var] = op->op1.u.var;
					op->result.u.var = op->op1.u.var;
				}
				break;
			case ZEND_DO_FCALL_BY_NAME:
				if (op->op1.op_type == IS_VAR && overload_var[op->op1.u.var] >= 0)
				{
					op->op1.u.var = overload_var[op->op1.u.var];
				}
				break;
#endif
			case ZEND_UNSET_VAR:
#ifndef ZEND_ENGINE_2_1
/* Pre-PHP 5.1 only */
			case ZEND_UNSET_DIM_OBJ:
				op->result.op_type = IS_UNUSED;
				break;
#else
			case ZEND_UNSET_DIM:
				op->result.op_type = IS_UNUSED;
				break;
			case ZEND_UNSET_OBJ:
				op->result.op_type = IS_UNUSED;
				break;
#endif
			default:
				break;
		}
	}

#ifndef ZEND_ENGINE_2
	free_alloca(overload_var);
#endif

	/* Find Lengths of Basic Blocks and build CFG */
	p = bb;
	for (line_num=1; line_num < len; line_num++)
	{
#ifdef ZEND_ENGINE_2
		/* Calculate innermost CATCH op */
		innermost_ketchup = 0;
		if (op_array->last_try_catch > 0)
		{
			int i;
			zend_try_catch_element* tc_element = op_array->try_catch_array;
			for (i=0; i<op_array->last_try_catch; i++, tc_element++)
			{
				// silence compile warnings. Line_num can't be negative here so casting is safe.
				if (tc_element->try_op <= (zend_uint)line_num-1 &&
					(zend_uint)line_num-1 < tc_element->catch_op &&
						(innermost_ketchup == 0 ||
						innermost_ketchup > tc_element->catch_op)
					)
				{
					innermost_ketchup = tc_element->catch_op;
				}
			}
		}
#endif
		if (bb[line_num].start != NULL)
		{
			p->len  = bb[line_num].start - p->start;
			p->next = &bb[line_num];
			op = &p->start[p->len-1];
			switch (op->opcode)
			{
				case ZEND_JMP:
					p->jmp_1 = &bb[op->op1.u.opline_num];
					break;
				case ZEND_JMPZNZ:
					p->jmp_2 = &bb[op->op2.u.opline_num];
					p->jmp_ext = &bb[op->extended_value];
					break;
				case ZEND_JMPZ:
				case ZEND_JMPNZ:
				case ZEND_JMPZ_EX:
				case ZEND_JMPNZ_EX:
#ifndef ZEND_ENGINE_2_1
/* Pre-PHP 5.1 only */
				case ZEND_JMP_NO_CTOR:
#else
				case ZEND_NEW:
				case ZEND_FE_RESET:
#endif
				case ZEND_FE_FETCH:
					p->jmp_2 = &bb[op->op2.u.opline_num];
					p->follow = &bb[line_num];
					break;
				case ZEND_RETURN:
				case ZEND_EXIT:
				case ZEND_BRK:
				case ZEND_CONT:
#ifdef ZEND_ENGINE_2
					/* HOESH: The control might flow to the innermost CATCH
					 * op if an exception thrown earlier. We can follow to CATCH
					 * to protect it against unnecessary K.O. In that case,
					 * the last RETURN will hold HANDLE_EXCEPTION.
					 * If no CATCH op toward, then glue it to the last opcode,
					 * that is HANDLE_EXCEPTION.
					 */
					p->follow = (innermost_ketchup > 0) ? &bb[innermost_ketchup] : &bb[len-1];
					break;
				case ZEND_DO_FCALL:
				case ZEND_DO_FCALL_BY_NAME:
					if (op->op2.u.opline_num != -1)
					{
						p->jmp_2 = &bb[op->op2.u.opline_num];
					}
					p->follow = &bb[line_num];
					break;
				case ZEND_CATCH:
					p->jmp_ext = &bb[op->extended_value];
					p->follow = &bb[line_num];
					break;
				case ZEND_THROW:
					if (op->op2.u.opline_num != -1)
					{
						p->jmp_2 = &bb[op->op2.u.opline_num];
					}
					p->follow = &bb[line_num];
#endif
					break;
				default:
					p->follow = &bb[line_num];
			}
			p = &bb[line_num];
		}
	}
	p->len = (op_array->opcodes + op_array->last) - p->start;

	/* Remove Unused brk_cont_array (BRK and COND instructions replaced by JMP)*/
	if (remove_brk_cont_array)
	{
		if (op_array->brk_cont_array != NULL)
		{
			efree(op_array->brk_cont_array);
			op_array->brk_cont_array = NULL;
		}
		op_array->last_brk_cont = 0;
	}
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
  while (p != NULL)
  {
    if (p->used)
	{
      if (p->len > 0 && op != p->start)
	  {
        memcpy(op, p->start, p->len * sizeof(zend_op));
      }
      p->start = op;
      op += p->len;
    }
    p = p->next;
  }
  op_array->last = op - start;
  op_array->start_op = NULL;
  while (op < end)
  {
    SET_TO_NOP(op);
    op++;
  }

  /* Set Branch Targets */
  p = bb;
  while (p != NULL) {
    if (p->used)
	{
      if (p->jmp_1 != NULL)
	  {
        p->start[p->len-1].op1.u.opline_num = p->jmp_1->start - start;
      }
      if (p->jmp_2 != NULL)
	  {
        p->start[p->len-1].op2.u.opline_num = p->jmp_2->start - start;
      }
      if (p->jmp_ext != NULL)
	  {
        p->start[p->len-1].extended_value = p->jmp_ext->start - start;
      }
    }
    p = p->next;
  }

#ifdef ZEND_ENGINE_2
	/*
	 * HOESH: Reassign try & catch blocks
	 */
	if (op_array->last_try_catch>0)
	{
		int i;
		int last_try_catch = op_array->last_try_catch;
		zend_try_catch_element* old_tc_element = op_array->try_catch_array;
		for (i=0; i<op_array->last_try_catch; i++, old_tc_element++)
		{
			if (bb[old_tc_element->try_op].used &&
				bb[old_tc_element->catch_op].used)
			{
				old_tc_element->try_op = bb[old_tc_element->try_op].start - start;
				old_tc_element->catch_op = bb[old_tc_element->catch_op].start - start;
			}
			else
			{
				old_tc_element->try_op = 0;
				old_tc_element->catch_op = 0;
				last_try_catch--;
			}
		}
		if (op_array->last_try_catch > last_try_catch)
		{
			zend_try_catch_element* new_tc_array = NULL;
			if (last_try_catch > 0)
			{
				/* Lost some try & catch blocks */
				zend_try_catch_element* new_tc_element = emalloc(sizeof(zend_try_catch_element)*last_try_catch);
				new_tc_array = new_tc_element;
				old_tc_element = op_array->try_catch_array;
				for (i=0; i<op_array->last_try_catch; i++, old_tc_element++)
				{
					if (old_tc_element->try_op != old_tc_element->catch_op)
					{
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
#endif
}

#define GET_REG(R) {\
                     if (assigned[(R)] < 0) {\
                       zend_uint j = 0;\
                       while (j < op_array->T) {\
                         if (reg_pool[j] == 0 &&\
                             (global[(R)] == 0 || used[j] == 0)) {\
                           reg_pool[j] = 1;\
                           assigned[(R)] = j;\
                           if (j+1 > n) {n = j+1;}\
                           break;\
                         }\
                         j++;\
                       }\
                     }\
                     used[assigned[(R)]] = 1;\
                   }

#define FREE_REG(R) reg_pool[(R)] = 0;


void reassign_registers(zend_op_array *op_array, BB* p, char *global) {
  zend_uint i;
  zend_uint n = 0;
  int uses_globals = 0;
  int* assigned = do_alloca(op_array->T * sizeof(int));
  char* reg_pool = do_alloca(op_array->T * sizeof(char));
  char* used     = do_alloca(op_array->T * sizeof(char));

  for (i = 0; i < op_array->T; i++) {
    assigned[i] = -1;
    reg_pool[i] = 0;
    used[i] = 0;
  }

  while (p != NULL) {
    if (p->used && p->len > 0) {
      zend_op* start = p->start;
      zend_op* op    = start + p->len;
#ifdef ZEND_ENGINE_2
      zend_op* op_data;
#endif

      for (i = 0; i < op_array->T; i++) {
        if (!global[i]) {
          if (assigned[i] >= 0) {reg_pool[assigned[i]] = 0;}
          assigned[i] = -1;
        }
      }

      while (start < op) {
        --op;
        /* zend_printf("op=%d\n", op-op_array->opcodes); */
#ifdef ZEND_ENGINE_2
        op_data = NULL;
#else
        if (op_array->uses_globals &&
            (op->opcode == ZEND_FETCH_R ||
             op->opcode == ZEND_FETCH_W ||
             op->opcode == ZEND_FETCH_RW ||
             op->opcode == ZEND_FETCH_IS ||
             op->opcode == ZEND_FETCH_FUNC_ARG ||
             op->opcode == ZEND_FETCH_UNSET) &&
             op->op1.op_type == IS_CONST &&
             op->op1.u.constant.type == IS_STRING &&
             op->op1.u.constant.value.str.len == (sizeof("GLOBALS")-1) &&
             memcmp(op->op1.u.constant.value.str.val, "GLOBALS", sizeof("GLOBALS")-1) == 0) {
          uses_globals = 1;
        }
#endif
        if (op->opcode == ZEND_DO_FCALL_BY_NAME &&
            op->op1.op_type == IS_CONST) {
          zval_dtor(&op->op1.u.constant);
          op->op1.op_type = IS_UNUSED;
        }
        if (op->op1.op_type == IS_VAR || op->op1.op_type == IS_TMP_VAR) {
          int r = VAR_NUM(op->op1.u.var);
          GET_REG(r);
          if (op->opcode == ZEND_DO_FCALL_BY_NAME) {
            op->op1.op_type = IS_UNUSED;
          } else if (op->opcode == ZEND_FETCH_CONSTANT && op->op1.op_type == IS_VAR) {
            op->op1.u.var = VAR_VAL(assigned[r]);
            /* restore op1 type from VAR to CONST (the opcode handler expects this or bombs out with invalid opcode) */
            op->op1.op_type = IS_CONST;
          } else {
            op->op1.u.var = VAR_VAL(assigned[r]);
          }
        }
        if (op->op2.op_type == IS_VAR || op->op2.op_type == IS_TMP_VAR) {
          int r = VAR_NUM(op->op2.u.var);
          GET_REG(r);
          op->op2.u.var = VAR_VAL(assigned[r]);
        }
#ifdef ZEND_ENGINE_2
        if (op->opcode == ZEND_DECLARE_INHERITED_CLASS) {
          int r = VAR_NUM(op->extended_value);
          GET_REG(r);
          op->extended_value = VAR_VAL(assigned[r]);
        }
#endif
        if (op->result.op_type == IS_VAR ||
            op->result.op_type == IS_TMP_VAR) {
          int r = VAR_NUM(op->result.u.var);
          GET_REG(r);
          op->result.u.var = VAR_VAL(assigned[r]);
          if (op->result.op_type == IS_VAR &&
#ifdef ZEND_ENGINE_2
              op->opcode != ZEND_RECV && op->opcode != ZEND_RECV_INIT &&
#endif
              ((op->result.u.EA.type & EXT_TYPE_UNUSED) != 0)) {
            FREE_REG(VAR_NUM(op->result.u.var))
          } else if (!(op->op1.op_type == op->result.op_type &&
              op->op1.u.var == op->result.u.var) &&
              !(op->op2.op_type == op->result.op_type &&
              op->op2.u.var == op->result.u.var) &&
              !global[r]) {
            switch (op->opcode) {
              case ZEND_RECV:
              case ZEND_RECV_INIT:
              case ZEND_ADD_ARRAY_ELEMENT:
                break;
              default:
                FREE_REG(VAR_NUM(op->result.u.var));
            }
          }
        }
      }
    }
    p = p->next;
  }
  op_array->T = n;
#ifndef ZEND_ENGINE_2
  if (op_array->uses_globals && !uses_globals) {
    op_array->uses_globals = 0;
  }
#endif
  free_alloca(used);
  free_alloca(reg_pool);
  free_alloca(assigned);
}

void restore_operand_types(zend_op_array *op_array) {
	zend_op* op = op_array->opcodes;
	int len = op_array->last;
	int line_num;

	for (line_num=0; line_num < len; op++,line_num++)
	{
          if (op->opcode == ZEND_FETCH_CONSTANT && op->op1.op_type == IS_VAR) {
            /* restore op1 type from VAR to CONST (the opcode handler expects this or bombs out with invalid opcode) */
            op->op1.op_type = IS_CONST;
	  }
	}
}

/*
 * Main Optimization Routine
 */
void eaccelerator_optimize(zend_op_array *op_array)
{
  BB* p;
  int i;
  BB* bb;

  TSRMLS_FETCH();
/*???
#ifdef ZEND_ENGINE_2
  return;
#endif
*/
  if (!EAG(compiler) || op_array->type != ZEND_USER_FUNCTION) {
    return;
  }

  /* Allocate memory for CFG */
  if ((bb = do_alloca(sizeof(BB)*(op_array->last+1))) == NULL) return;
  memset(bb, 0, sizeof(BB)*(op_array->last+1));

  /* Find All Basic Blocks and build CFG */
  if (build_cfg(op_array, bb)) {
    char *global = do_alloca(op_array->T * sizeof(char));
    if (global == NULL) return;

    for (i=0; i<2; i++) {
      /* Determine Used Blocks and its Predcessors */
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

      /* Mark All Basik Blocks as Unused. Free Predcessors Links. */
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
    /* dump_bb(bb, op_array); */

    free_alloca(global);
  }
#  ifdef ZEND_ENGINE_2_1
  else {
    /* build_cfg encountered some nested ZEND_BRK or ZEND_CONT's
       which it could not replace with JMP's

       now restore the operand type changes that build_cfg had
       already applied, to prevent 'invalid opcode' errors
       on opcode handlers that expect a strict set of operand
       types since php-5.1 (like ZEND_FETCH_CONSTANT)
    */
    restore_operand_types(op_array);
  }
#  endif
  free_alloca(bb);
}
#endif
#endif /* #ifdef HAVE_EACCELERATOR */
