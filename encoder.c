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
#ifdef WITH_EACCELERATOR_ENCODER

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

#ifndef WITHOUT_FILE_FILTER
#define IEQ(need)  (ch == (need) || \
                    ((need) >= 'a' && (need) <= 'z' && ch == ((need)-'a'+'A')))

#define SKIP_WHITESPACES() do {\
                             ch = fgetc(yyin);\
                             while (ch == ' ' || ch == '\t' ||\
                                    ch == '\r' || ch == '\n') {\
                               fputc(ch,yyout);\
                               ch = fgetc(yyin);\
                             }\
                           } while(0)

static void filter_script(FILE *yyin, FILE *yyout TSRMLS_DC) {
  register int ch = EOF;
  int repeat = 0;
  int allow = 1;

  int label_len, pos;
  char label[256];

  while (1) {
    if (!repeat) {
      ch = fgetc(yyin);
    } else {
      repeat = 0;
    }
    if (ch == EOF) break;
    fputc(ch,yyout);
    if (ch == '\'' || ch == '"' || ch == '`') {
      /* skip strings */
      register int start = ch;
      do {
        ch = fgetc(yyin);
        if (ch == EOF) break;
        fputc(ch,yyout);
        if (ch == '\\') {
          ch = fgetc(yyin);
          if (ch == EOF) break;
          fputc(ch,yyout);
          ch = fgetc(yyin);
          if (ch == EOF) break;
          fputc(ch,yyout);
        }
      } while (ch != start);
      allow = 1;
    } else if (ch == '#') {
      /* skip one line comments */
one_line_comment:
      do {
        ch = fgetc(yyin);
        if (ch == EOF) break;
        fputc(ch,yyout);
      } while (ch != '\r' && ch != '\n');
      allow = 1;
    } else if (ch == '/') {
      ch = fgetc(yyin);
      if (ch == EOF) break;
      if (ch == '/') {
        fputc(ch,yyout);
        /* skip one line comments */
        goto one_line_comment;
      } else if (ch == '*') {
        fputc(ch,yyout);
        /* skip multiline comments */
        while (1) {
          do {
            ch = fgetc(yyin);
            if (ch == EOF) break;
            fputc(ch,yyout);
          } while (ch != '*');
          if (ch == EOF) break;
          while (ch == '*') {
            ch = fgetc(yyin);
            if (ch == EOF) break;
            fputc(ch,yyout);
          }
          if (ch == EOF || ch == '/') break;
        }
      } else {
        repeat = 1;
      }
      allow = 1;
    } else if (ch == '?' || (ch == '%' && CG(asp_tags))) {
      /* end of script */
      ch = fgetc(yyin);
      if (ch == EOF) break;
      if (ch == '>') {
        fputc(ch,yyout);
        return;
      } else {
        repeat = 1;
      }
      allow = 1;
    } else if (ch == '<') {
      repeat = 1;
      /* </script> */
      ch = fgetc(yyin);
      if (ch == EOF) break;
      if (ch == '/') {
        fputc(ch,yyout);
        ch = fgetc(yyin);
        if (ch == EOF) break;
        if (IEQ('s')) {
          fputc(ch,yyout);
          ch = fgetc(yyin);
          if (ch == EOF) break;
          if (IEQ('c')) {
            fputc(ch,yyout);
            ch = fgetc(yyin);
            if (ch == EOF) break;
            if (IEQ('r')) {
              fputc(ch,yyout);
              ch = fgetc(yyin);
              if (ch == EOF) break;
              if (IEQ('i')) {
                fputc(ch,yyout);
                ch = fgetc(yyin);
                if (ch == EOF) break;
                if (IEQ('p')) {
                  fputc(ch,yyout);
                  ch = fgetc(yyin);
                  if (ch == EOF) break;
                  if (IEQ('t')) {
                    fputc(ch,yyout);
                    SKIP_WHITESPACES();
                    if (ch == '>') {
                      fputc(ch,yyout);
                      return;
                    }
                  }
                }
              }
            }
          }
        }
      } else if (ch == '<') {
        repeat = 1;
        fputc(ch,yyout);
        ch = fgetc(yyin);
        if (ch == EOF) break;
        if (ch == '<') {
          /* heredoc */
          fputc(ch,yyout);
          do {
            ch = fgetc(yyin);
            if (ch == EOF) break;
            fputc(ch,yyout);
          } while (ch == '\t' || ch == ' ');
          if ((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              ch == '_' ||
              (ch >= '\x7f' && ch <= '\xff')) {
            label[0] = ch;
            label_len = 1;
            while(1) {
              ch = fgetc(yyin);
              if (ch == EOF) break;
              fputc(ch,yyout);
              if ((ch >= 'a' && ch <= 'z') ||
                  (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') ||
                  ch == '_' ||
                  (ch >= '\x7f' && ch <= '\xff')) {
                label[label_len] = ch;
                label_len++;
                if (label_len >= sizeof(label)-1) break;
              } else {
                break;
              }
            }
            if (ch == '\r' || ch =='\n') {
              label[label_len] = '\000';
              while (1) {
                if (ch == '\r' || ch == '\n') {
                  ch = fgetc(yyin);
                  if (ch == EOF) break;
                  fputc(ch,yyout);
                  pos = 0;
                  while (1) {
                    if (pos == label_len && ch == ';') {
                      ch = fgetc(yyin);
                      if (ch == EOF) break;
                      fputc(ch,yyout);
                    }
                    if (pos == label_len && (ch == '\r' || ch == '\n')) {
                      break;
                    }
                    if (pos > label_len || label[pos] != ch) {
                      break;
                    }
                    pos++;
                    ch = fgetc(yyin);
                    if (ch == EOF) break;
                    fputc(ch,yyout);
                  }
                  if (pos == label_len && (ch == '\r' || ch == '\n')) {
                    break;
                  }
                } else {
                  ch = fgetc(yyin);
                  if (ch == EOF) break;
                  fputc(ch,yyout);
                }
              }
            }
          }
          repeat = 0;
        }
      }
      allow = 1;
    } else if (allow && ch == '_') {
      label[0] = ch = fgetc(yyin);
      if (ch == EOF) {break;}
      if (ch == '_') {
        label[1] = ch = fgetc(yyin);
        if (ch == EOF) {fwrite(label,1,1,yyout); break;}
        if (IEQ('f')) {
          label[2] = ch = fgetc(yyin);
          if (ch == EOF) {fwrite(label,2,1,yyout); break;}
          if (IEQ('i')) {
            label[3] = ch = fgetc(yyin);
            if (ch == EOF) {fwrite(label,3,1,yyout); break;}
            if (IEQ('l')) {
              label[4] = ch = fgetc(yyin);
              if (ch == EOF) {fwrite(label,4,1,yyout); break;}
              if (IEQ('e')) {
                label[5] = ch = fgetc(yyin);
                if (ch == EOF) {fwrite(label,5,1,yyout); break;}
                if (ch == '_') {
                  label[6] = ch = fgetc(yyin);
                  if (ch == EOF) {fwrite(label,6,1,yyout); break;}
                  if (ch == '_') {
                    ch = fgetc(yyin);
                    repeat = 1;
                    if ((ch >= 'a' && ch <= 'z') ||
                        (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') ||
                        (ch >= '\x7f' && ch <= '\xff') ||
                        ch == '_') {
                      fwrite(label,7,1,yyout);
                    } else {
                      fputs("eaccelerator_loader_file()",yyout);
                    }
                  } else {
                    fwrite(label,7,1,yyout);
                  }
                } else {
                  fwrite(label,6,1,yyout);
                }
              } else {
                fwrite(label,5,1,yyout);
              }
            } else {
              fwrite(label,4,1,yyout);
            }
          } else {
            fwrite(label,3,1,yyout);
          }
        } else if (IEQ('l')) {
          label[2] = ch = fgetc(yyin);
          if (ch == EOF) {fwrite(label,2,1,yyout); break;}
          if (IEQ('i')) {
            label[3] = ch = fgetc(yyin);
            if (ch == EOF) {fwrite(label,3,1,yyout); break;}
            if (IEQ('n')) {
              label[4] = ch = fgetc(yyin);
              if (ch == EOF) {fwrite(label,4,1,yyout); break;}
              if (IEQ('e')) {
                label[5] = ch = fgetc(yyin);
                if (ch == EOF) {fwrite(label,5,1,yyout); break;}
                if (ch == '_') {
                  label[6] = ch = fgetc(yyin);
                  if (ch == EOF) {fwrite(label,6,1,yyout); break;}
                  if (ch == '_') {
                    ch = fgetc(yyin);
                    repeat = 1;
                    if ((ch >= 'a' && ch <= 'z') ||
                        (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') ||
                        (ch >= '\x7f' && ch <= '\xff') ||
                        ch == '_') {
                      fwrite(label,7,1,yyout);
                    } else {
                      fputs("eaccelerator_loader_line()",yyout);
                    }
                  } else {
                    fwrite(label,7,1,yyout);
                  }
                } else {
                  fwrite(label,6,1,yyout);
                }
              } else {
                fwrite(label,5,1,yyout);
              }
            } else {
              fwrite(label,4,1,yyout);
            }
          } else {
            fwrite(label,3,1,yyout);
          }
        } else {
          fwrite(label,2,1,yyout);
        }
      } else {
        fwrite(label,1,1,yyout);
      }
      allow = 0;
    } else if ((ch >= 'a' && ch <= 'z') ||
               (ch >= 'A' && ch <= 'Z') ||
               (ch >= '\x7f' && ch <= '\xff') ||
               ch == '_' || ch == '$') {
      allow = 0;
    } else if (ch == '-') {
      ch = fgetc(yyin);
      if (ch == EOF) break;
      if (ch == '>') {
        fputc(ch,yyout);
        allow = 0;
      } else {
        repeat = 1;
        allow = 1;
      }
    } else {
      allow = 1;
    }
  }
}

static void filter_file(FILE *yyin, FILE *yyout TSRMLS_DC) {
  register int ch = EOF;
  int repeat = 0;

  while (1) {
    if (!repeat) {
      ch = fgetc(yyin);
    } else {
      repeat = 0;
    }
    if (ch == EOF) break;
    fputc(ch,yyout);
    if (ch == '<') {
      ch = fgetc(yyin);
      if (ch == EOF) break;
      if (ch == '?') {
        fputc(ch,yyout);
        if (CG(short_tags)) {
          filter_script(yyin, yyout TSRMLS_CC);
        } else {
          repeat = 1;
          ch = fgetc(yyin);
          if (ch == EOF) break;
          if (IEQ('p')) {
            fputc(ch,yyout);
            ch = fgetc(yyin);
            if (ch == EOF) break;
            if (IEQ('h')) {
              fputc(ch,yyout);
              ch = fgetc(yyin);
              if (ch == EOF) break;
              if (IEQ('p')) {
                fputc(ch,yyout);
                ch = fgetc(yyin);
                if (ch == EOF) break;
                if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') {
                  fputc(ch,yyout);
                  filter_script(yyin, yyout TSRMLS_CC);
                  repeat = 0;
                }
              }
            }
          }
        }
      } else if (ch == '%' && CG(asp_tags)) {
        fputc(ch,yyout);
        filter_script(yyin, yyout TSRMLS_CC);
      } else if (IEQ('s')) {
        repeat = 1;
        fputc(ch,yyout);
        ch = fgetc(yyin);
        if (ch == EOF) break;
        if (IEQ('c')) {
          fputc(ch,yyout);
          ch = fgetc(yyin);
          if (ch == EOF) break;
          if (IEQ('r')) {
            fputc(ch,yyout);
            ch = fgetc(yyin);
            if (ch == EOF) break;
            if (IEQ('i')) {
              fputc(ch,yyout);
              ch = fgetc(yyin);
              if (ch == EOF) break;
              if (IEQ('p')) {
                fputc(ch,yyout);
                ch = fgetc(yyin);
                if (ch == EOF) break;
                if (IEQ('t')) {
                  fputc(ch,yyout);
                  SKIP_WHITESPACES();
                  if (ch == EOF) break;
                  if (IEQ('l')) {
                    fputc(ch,yyout);
                    ch = fgetc(yyin);
                    if (ch == EOF) break;
                    if (IEQ('a')) {
                      fputc(ch,yyout);
                      ch = fgetc(yyin);
                      if (ch == EOF) break;
                      if (IEQ('n')) {
                        fputc(ch,yyout);
                        ch = fgetc(yyin);
                        if (ch == EOF) break;
                        if (IEQ('g')) {
                          fputc(ch,yyout);
                          ch = fgetc(yyin);
                          if (ch == EOF) break;
                          if (IEQ('u')) {
                            fputc(ch,yyout);
                            ch = fgetc(yyin);
                            if (ch == EOF) break;
                            if (IEQ('a')) {
                              fputc(ch,yyout);
                              ch = fgetc(yyin);
                              if (ch == EOF) break;
                              if (IEQ('g')) {
                                fputc(ch,yyout);
                                ch = fgetc(yyin);
                                if (ch == EOF) break;
                                if (IEQ('e')) {
                                  register int start = '\000';
                                  fputc(ch,yyout);
                                  SKIP_WHITESPACES();
                                  if (ch == EOF) break;
                                  if (ch == '=') {
                                    fputc(ch,yyout);
                                    SKIP_WHITESPACES();
                                    if (ch == EOF) break;
                                    if (ch == '\'' || ch == '"') {
                                      fputc(ch,yyout);
                                      start = ch;
                                      ch = fgetc(yyin);
                                      if (ch == EOF) break;
                                    }
                                    if (IEQ('p')) {
                                      fputc(ch,yyout);
                                      ch = fgetc(yyin);
                                      if (ch == EOF) break;
                                      if (IEQ('h')) {
                                        fputc(ch,yyout);
                                        ch = fgetc(yyin);
                                        if (ch == EOF) break;
                                        if (IEQ('p')) {
                                          fputc(ch,yyout);
                                          if (start != '\000') {
                                            ch = fgetc(yyin);
                                            if (ch == EOF) break;
                                            if (ch == start) {
                                              fputc(ch,yyout);
                                              start = '\000';
                                            }
                                          }
                                          if (start == '\000') {
                                            SKIP_WHITESPACES();
                                            if (ch == '>') {
                                              fputc(ch,yyout);
                                              filter_script(yyin, yyout TSRMLS_CC);
                                              repeat = 0;
                                            }
                                          }
                                        }
                                      }
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      } else {
        repeat = 1;
      }
    }
  }
}
#endif

static inline void encode(unsigned char c) {
  zend_write((char*)&c, 1);
}

static inline void encode32(unsigned int i) {
  encode((unsigned char)(i & 0xff));
  encode((unsigned char)((i >> 8) & 0xff));
  encode((unsigned char)((i >> 16) & 0xff));
  encode((unsigned char)((i >> 24) & 0xff));
}

static inline void encode16(unsigned short i) {
  encode((unsigned char)(i & 0xff));
  encode((unsigned char)((i >> 8) & 0xff));
}

static void encode_var(unsigned int var, unsigned int count) {
  unsigned int v = VAR_NUM(var);
  if (v >= count) {
    zend_bailout();
  }
  if (count < 0xff) {
    encode((unsigned char)v);
  } else if (count < 0xffff) {
    encode16((unsigned short)v);
  } else {
    encode32(v);
  }
}

static void encode_opline(unsigned int opline, unsigned int last) {
  if (opline >= last && opline != (unsigned int)-1) {
    zend_bailout();
  }
  if (last < 0xff-1) {
    encode((unsigned char)opline);
  } else if (last < 0xffff-1) {
    encode16((unsigned short)opline);
  } else {
    encode32(opline);
  }
}

static void encode_zstr(const char* str) {
  if (str != NULL) {
    int len = strlen(str);
    ZEND_WRITE(str,len+1);
  } else {
    encode(0);
  }
}

static void encode_lstr(const char* str, unsigned int len) {
  if (str != NULL && len > 0) {
    encode32(len);
    ZEND_WRITE(str,len);
  } else {
    encode32(0);
  }
}

static inline void encode_pstr(const unsigned char* str) {
  if (str != NULL) {
    unsigned int len = str[0];
    ZEND_WRITE((const char*)str,len+1);
  } else {
    encode(0);
  }
}

static void encode_double(double d) {
  char sign = 0;
  int  exp;
  unsigned long i1, i2;

  if (d < 0.0) {
    sign = 1;
    d = -d;
  }
  d = frexp(d, &exp);
  d = d * 4294967296.0;
  i1 = (unsigned long)floor(d);
  d = (d - i1) * 4294967296.0;
  i2 = (unsigned long)floor(d);

  encode(sign);
  encode32(exp);
  encode32(i1);
  encode32(i2);
}

typedef void (*encode_bucket_t)(void*);

#define encode_zval_hash(from) encode_hash(from, (encode_bucket_t)encode_zval_ptr)

static void encode_zval_ptr(zval** from);
static void encode_hash(HashTable* from, encode_bucket_t encode_bucket);

static void encode_zval(zval* from, int refs) {
  encode(from->type);
  if (refs) {
    encode(from->is_ref);
    encode32(from->refcount);
  } else if (!from->is_ref || from->refcount != 2) {
    zend_bailout();
  }

  switch (from->type & ~IS_CONSTANT_INDEX) {
    case IS_NULL:
      break;
    case IS_BOOL:
      encode((unsigned char)from->value.lval);
      break;
    case IS_LONG:
      encode32(from->value.lval);
      break;
    case IS_DOUBLE:
      encode_double(from->value.dval);
      break;
    case IS_CONSTANT:
    case IS_STRING:
/*???    case FLAG_IS_BC:*/
      encode_lstr(from->value.str.val, from->value.str.len);
      break;
    case IS_ARRAY:
    case IS_CONSTANT_ARRAY:
      encode_zval_hash(from->value.ht);
      break;
    case IS_OBJECT:
    case IS_RESOURCE:
      /*???*/
    default:
      zend_bailout();
      break;
  }
}

static void encode_znode(znode* from, unsigned int vars_count) {
  encode((unsigned char)from->op_type);
  if (from->op_type == IS_CONST) {
    encode_zval(&from->u.constant, 0);
  } else if (from->op_type == IS_VAR ||
             from->op_type == IS_TMP_VAR) {
    encode_var(from->u.var, vars_count);
  } else if (from->op_type != IS_UNUSED) {
    zend_bailout();
  }
}


static void encode_zval_ptr(zval** from) {
  encode_zval(*from, 1);
}

#ifdef ZEND_ENGINE_2
static void encode_property_info(zend_property_info* from) {
  encode32(from->flags);
  encode_lstr(from->name, from->name_length);
}

static void encode_class_entry(zend_class_entry* from);

static void encode_class_entry_ptr(zend_class_entry** from) {
  encode_class_entry(*from);
}
#endif

static void encode_hash(HashTable* from, encode_bucket_t encode_bucket) {
  if (from != NULL &&
      from->nNumOfElements > 0) {
    Bucket* p;
    encode32(from->nNumOfElements);
    p = from->pListHead;
    while (p != NULL) {
      encode_lstr(p->arKey, p->nKeyLength);
      if (p->nKeyLength == 0) {
        encode32(p->h);
      }
      encode_bucket(p->pData);
      p = p->pListNext;
    }
  } else {
    encode32(0);
  }
}

#ifdef ZEND_ENGINE_2
#define encode_zval_hash_ex(from,p) encode_hash_ex(from, p, (encode_bucket_t)encode_zval_ptr)

static void encode_hash_ex(HashTable* from, Bucket* p, encode_bucket_t encode_bucket) {
  if (from != NULL &&
      from->nNumOfElements > 0) {
    unsigned int n = 0;
    Bucket* q = p;
    while (q != NULL) {
      ++n;
      q = q->pListNext;
    }
    encode32(n);
    while (p != NULL) {
      encode_lstr(p->arKey, p->nKeyLength);
      if (p->nKeyLength == 0) {
        encode32(p->h);
      }
      encode_bucket(p->pData);
      p = p->pListNext;
    }
  } else {
    encode32(0);
  }
}
#endif

static void encode_op_array(zend_op_array* from) {
  zend_op *opline;
  zend_op *end;

  if (from->type == ZEND_INTERNAL_FUNCTION) {
  } else if (from->type == ZEND_USER_FUNCTION) {
  } else {
    zend_bailout();
  }
  encode(from->type);
#ifdef ZEND_ENGINE_2
  encode32(from->num_args);
  if (from->num_args > 0) {
    zend_uint i;
    for (i = 0; i < from->num_args; i++) {
      encode_lstr(from->arg_info[i].name,from->arg_info[i].name_len);
      encode_lstr(from->arg_info[i].class_name,from->arg_info[i].class_name_len);
      encode(from->arg_info[i].allow_null);
      encode(from->arg_info[i].pass_by_reference);
    }
  }
  encode(from->pass_rest_by_reference);
#else
  encode_pstr(from->arg_types);
#endif
  encode_zstr(from->function_name);
#ifdef ZEND_ENGINE_2
  encode32(from->fn_flags);
  if (from->scope != NULL) {
    TSRMLS_FETCH();
    {
      Bucket* q = CG(class_table)->pListHead;
      while (q != NULL) {
        if (*(zend_class_entry**)q->pData == from->scope) {
          encode_lstr(q->arKey, q->nKeyLength);
          goto scope_stored;
        }
        q = q->pListNext;
      }
    }
  }
  encode32(0);
scope_stored:
#endif
  if (from->type == ZEND_INTERNAL_FUNCTION) {
    return;
  }
  encode32(from->T);
#ifdef ZEND_ENGINE_2
  encode(from->uses_this);
#else
  encode(from->uses_globals);
#endif
  encode(from->return_reference);

  if (from->opcodes != NULL && from->last > 0) {
    encode32(from->last);
    if (from->brk_cont_array != NULL && from->last_brk_cont > 0) {
      zend_uint i;
      encode32(from->last_brk_cont);
      for (i = 0; i < from->last_brk_cont; i++) {
        encode_opline(from->brk_cont_array[i].brk, from->last);
        encode_opline(from->brk_cont_array[i].cont, from->last);
        encode_opline(from->brk_cont_array[i].parent, from->last_brk_cont);
      }
    } else {
      encode32(0);
    }
#ifdef ZEND_ENGINE_2
	if (from->try_catch_array != NULL && from->last_try_catch > 0)
	{
		zend_uint i;
		encode32(from->last_try_catch);
		for (i = 0; i < from->last_try_catch; i++)
		{
			encode_opline(from->try_catch_array[i].try_op, from->last);
			encode_opline(from->try_catch_array[i].catch_op, from->last);
//			encode_opline(from->try_catch_array[i].parent, from->last_try_catch);
		}
	}
	else
	{
		encode32(0);
	}
#endif
	opline = from->opcodes;
    end = opline + from->last;
    for (;opline < end; opline++) {
      const opcode_dsc* op_dsc = get_opcode_dsc(opline->opcode);
      if (op_dsc == NULL) {
        zend_bailout();
      } else {
        unsigned int ops = op_dsc->ops;
        encode(opline->opcode);
#if MMC_ENCODER_VERSION < 2
        encode32(opline->lineno);
#endif
        switch (ops & EXT_MASK) {
          case EXT_UNUSED:
             break;
          case EXT_STD:
          case EXT_FCALL:
          case EXT_ARG:
          case EXT_IFACE:
            encode32(opline->extended_value);
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
            encode((unsigned char)opline->extended_value);
            break;
          case EXT_OPLINE:
            encode_opline(opline->extended_value, from->last);
            break;
          case EXT_CLASS:
            encode_var(opline->extended_value, from->T);
            break;
          default:
            zend_bailout();
            break;
        }
        switch (ops & RES_MASK) {
          case RES_UNUSED:
            break;
          case RES_TMP:
          case RES_CLASS:
            encode_var(opline->result.u.var, from->T);
            break;
          case RES_VAR:
            encode_var(opline->result.u.var, from->T);
            if ((opline->result.u.EA.type & EXT_TYPE_UNUSED) != 0) {
              encode(1);
            } else {
              encode(0);
            }
            break;
          case RES_STD:
            encode_znode(&opline->result, from->T);
            if (opline->result.op_type == IS_VAR) {
              if ((opline->result.u.EA.type & EXT_TYPE_UNUSED) != 0) {
                encode(1);
              } else {
                encode(0);
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
            encode_opline(opline->op1.u.opline_num,from->last);
            break;
          case OP1_BRK:
          case OP1_CONT:
            encode_opline(opline->op1.u.opline_num, from->last_brk_cont);
            break;
          case OP1_CLASS:
          case OP1_TMP:
          case OP1_VAR:
            encode_var(opline->op1.u.var, from->T);
            break;
          case OP1_UCLASS:
            encode((unsigned char)opline->op1.op_type);
            if (opline->op1.op_type != IS_UNUSED) {
              encode_var(opline->op1.u.var, from->T);
            }
            break;
          case OP1_ARG:
            encode32(opline->op1.u.constant.value.lval);
            break;
#ifdef ZEND_ENGINE_2
          case OP1_JMPADDR:
            encode_opline(opline->op1.u.jmp_addr - from->opcodes, from->last);
            break;
#endif
          case OP1_STD:
            encode_znode(&opline->op1, from->T);
            break;
          default:
            zend_bailout();
            break;
        }
        switch (ops & OP2_MASK) {
          case OP2_UNUSED:
            break;
          case OP2_OPLINE:
            encode_opline(opline->op2.u.opline_num, from->last);
            break;
          case OP2_ARG:
            encode32(opline->op2.u.opline_num);
            break;
          case OP2_ISSET:
          case OP2_INCLUDE:
            encode((unsigned char)opline->op2.u.constant.value.lval);
            break;
          case OP2_FETCH:
#ifdef ZEND_ENGINE_2
            encode((unsigned char)opline->op2.u.EA.type);
            if (opline->op2.u.EA.type == ZEND_FETCH_STATIC_MEMBER) {
              encode_var(opline->op2.u.var, from->T);
            }
#else
            encode((unsigned char)opline->op2.u.fetch_type);
#endif
            break;
          case OP2_CLASS:
          case OP2_TMP:
          case OP2_VAR:
            encode_var(opline->op2.u.var, from->T);
            break;
#ifdef ZEND_ENGINE_2
          case OP2_JMPADDR:
            encode_opline(opline->op2.u.jmp_addr - from->opcodes, from->last);
            break;
#endif
          case OP2_STD:
            encode_znode(&opline->op2, from->T);
            break;
          default:
            zend_bailout();
            break;
        }
      }
    }
  } else {
    encode32(0);
  }
  encode_zval_hash(from->static_variables);
#if MMC_ENCODER_VERSION < 2
  encode_zstr(from->filename);
#endif
#ifdef ZEND_ENGINE_2
  encode32(from->line_start);
  encode32(from->line_end);
  encode_lstr(from->doc_comment, from->doc_comment_len);
#endif
}

static void encode_class_entry(zend_class_entry* from) {
  encode(from->type);
  encode_lstr(from->name,from->name_length);
#ifdef ZEND_ENGINE_2
  encode32(from->ce_flags);
  encode32(from->num_interfaces);
#endif

  if (from->parent != NULL && from->parent->name) {
    encode_lstr(from->parent->name, from->parent->name_length);
  } else {
    encode32(0);
  }

#ifdef ZEND_ENGINE_2
#if MMC_ENCODER_VERSION < 2
  encode32(from->line_start);
  encode32(from->line_end);
  encode_zstr(from->filename);
#endif
  encode_lstr(from->doc_comment, from->doc_comment_len);

  encode_zval_hash(&from->constants_table);
  encode_zval_hash(&from->default_properties);
  encode_hash(&from->properties_info, (encode_bucket_t)encode_property_info);
  encode_zval_hash(from->static_members);
#else
  encode_zval_hash(&from->default_properties);
#endif
  encode_hash(&from->function_table, (encode_bucket_t)encode_op_array);
}

static int eaccelerator_encode(char* key, zend_op_array* op_array,
                          Bucket* f, Bucket *c) {
  encode_zstr("EACCELERATOR");
  encode32(MMC_ENCODER_VERSION);
#ifdef ZEND_ENGINE_2
  encode(2);
#else
  encode(1);
#endif
  while (c != NULL) {
    zend_class_entry *ce;
#ifdef ZEND_ENGINE_2
    ce = *(zend_class_entry**)c->pData;
    encode(MMC_ENCODER_CLASS);
    encode_lstr(c->arKey, c->nKeyLength);
    encode_class_entry(ce);
#else
    encode(MMC_ENCODER_CLASS);
    encode_lstr(c->arKey, c->nKeyLength);
    ce = (zend_class_entry*)c->pData;
    encode_class_entry(ce);
#endif
    c = c->pListNext;
  }
  encode(MMC_ENCODER_END);

  while (f != NULL) {
    encode(MMC_ENCODER_FUNCTION);
    encode_lstr(f->arKey, f->nKeyLength);
    encode_op_array((zend_op_array*)f->pData);
    f = f->pListNext;
  }
  encode(MMC_ENCODER_END);
  encode_op_array(op_array);
  return 1;
}

#ifdef ZEND_ENGINE_2

ZEND_DLIMPORT int isatty(int fd);

static size_t eaccelerator_stream_stdio_reader(void *handle, char *buf, size_t len TSRMLS_DC)
{
	return fread(buf, 1, len, (FILE*)handle);
}

static void eaccelerator_stream_stdio_closer(void *handle TSRMLS_DC)
{
	if ((FILE*)handle != stdin)
		fclose((FILE*)handle);
}

#endif

PHP_FUNCTION(eaccelerator_encode)
{
	zend_op_array *t;
	Bucket        *f;
	Bucket        *c;
	zval          *src;
	zval          *prefix = NULL;
	char          *pre_content = NULL;
	int           pre_content_len = 0;
	char          *post_content = NULL;
	int           post_content_len = 0;
	int welldone = 0;
	char *opened_path=NULL;
	zend_file_handle file_handle;
	zend_bool old_enabled;

	FILE* src_fp;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC,
		"z|zss", &src, &prefix,
		&pre_content, &pre_content_len,
		&post_content, &post_content_len) == FAILURE)
	{
		return;
	}
	if (Z_TYPE_P(src) == IS_STRING)
	{
#ifndef ZEND_ENGINE_2
		file_handle.handle.fp = NULL;
		file_handle.type = ZEND_HANDLE_FILENAME;
		file_handle.filename = Z_STRVAL_P(src);
		file_handle.opened_path = NULL;
		file_handle.free_filename = 0;
#endif
	}
	else
	{
		zend_error(E_WARNING, "eaccelerator_encode() expects parameter 1 to be string\n");
		RETURN_FALSE;
	}
	if (!zend_hash_exists(EG(function_table), "gzcompress", sizeof("gzcompress")))
	{
		zend_error(E_ERROR, "eAccelerator Encoder requires php_zlib extension.\n");
		RETURN_FALSE;
	}
	if (prefix != NULL)
	{
		ZVAL_EMPTY_STRING(prefix);
	}

	/* Storing global pre-compiled functions and classes */
	f = CG(function_table)->pListTail;
	c = CG(class_table)->pListTail;
	MMCG(compiler) = 1;
	MMCG(encoder) = 1;
	old_enabled = MMCG(enabled);
	MMCG(enabled) = 0;
	zend_try
	{
		char *opened_path;

#ifdef ZEND_ENGINE_2
		zend_stream_open(Z_STRVAL_P(src), &file_handle TSRMLS_CC);
		src_fp = fopen(file_handle.opened_path, "r");
		opened_path = file_handle.opened_path;
#else
		file_handle.handle.fp = zend_fopen(file_handle.filename, &opened_path);
		src_fp = file_handle.handle.fp;
#endif

		if (src_fp)
		{
			/* #!php support */
			long pos = 0;
			char c;

			c = fgetc(src_fp);
			pos++;
			if (c == '#')
			{
				while (c != 10 && c != 13)
				{
					c = fgetc(src_fp); /* skip to end of line */
					pos++;
				}
				/* handle situations where line is terminated by \r\n */
				/* HOESH: easy rider.. tricky half sised */
				if (c == 13 || c == 10)
				{
					if (fgetc(src_fp)+c != 10 + 13)
					{
						if (prefix == NULL) fseek(src_fp, pos, SEEK_SET);
					}
					else pos++;
				}
				if (prefix != NULL)
				{
					prefix->type = IS_STRING;
					prefix->value.str.len = pos;
					prefix->value.str.val = emalloc(pos+1);
					rewind(src_fp);
					fread(prefix->value.str.val, pos, 1, src_fp);
					prefix->value.str.val[prefix->value.str.len] = '\000';
				}
			}
			else
			{
				rewind(src_fp);
			}
#ifndef ZEND_ENGINE_2
			file_handle.type = ZEND_HANDLE_FP;
			file_handle.opened_path = opened_path;
#endif
		    if (1)
			{
				FILE *tmp_fp = tmpfile();
				if (tmp_fp)
				{
					if (pre_content_len > 0)
					{
						fwrite(pre_content, pre_content_len, 1, tmp_fp);
					}
#ifndef WITHOUT_FILE_FILTER
					filter_file(src_fp, tmp_fp TSRMLS_CC);
#else
					while (1)
					{
						int c = fgetc(src_fp);
						if (c == EOF) break;
						fputc(c, tmp_fp);
					}
#endif
					if (post_content_len > 0)
					{
						fwrite(post_content, post_content_len, 1, tmp_fp);
					}
					rewind(tmp_fp);
					fclose(src_fp);
#ifndef ZEND_ENGINE_2
					file_handle.handle.fp = tmp_fp;
#else
					/* HOESH: change stream */
					file_handle.handle.stream.closer(file_handle.handle.stream.handle TSRMLS_CC);
					file_handle.handle.stream.handle = tmp_fp;
					file_handle.handle.stream.reader = eaccelerator_stream_stdio_reader;
					file_handle.handle.stream.closer = eaccelerator_stream_stdio_closer;
					file_handle.type = ZEND_HANDLE_STREAM;
					file_handle.handle.stream.interactive = isatty(fileno((FILE *)file_handle.handle.stream.handle));
#endif
				}
			}
		}
		t = zend_compile_file(&file_handle, ZEND_INCLUDE TSRMLS_CC);
	}
	zend_catch
	{
		t = NULL;
		/* restoring some globals to default values */
		CG(active_class_entry) = NULL;
	}
	zend_end_try();

	MMCG(encoder) = 0;
	MMCG(compiler) = 0;
	MMCG(enabled) = old_enabled;

	f = f ? f->pListNext : CG(function_table)->pListHead;
	c = c ? c->pListNext : CG(class_table)->pListHead;

	if (t != NULL)
	{
		opened_path = file_handle.opened_path;
#ifdef PHP_OUTPUT_HANDLER_USER
		/* PHP 4.2.0 and above */
		if (php_start_ob_buffer(NULL, 0, 0 TSRMLS_CC) != FAILURE)
		{
#else
		/* PHP 4.1.2 and before */
		if (php_start_ob_buffer(NULL, 0 TSRMLS_CC) != FAILURE)
		{
#endif
/*???
			zend_error(E_ERROR, "Cann't encode %s\n", opened_path);
*/
			zend_try
			{
				if (eaccelerator_encode(opened_path, t, f, c) &&
					php_ob_get_buffer(return_value TSRMLS_CC) != FAILURE)
				{
					zval func;
					zval gzstring;
					zval *params[1];

					php_end_ob_buffer(0, 0 TSRMLS_CC);
					ZVAL_STRING(&func, "gzcompress", 0);
					params[0] = return_value;
					if (call_user_function(CG(function_table), (zval**)NULL, &func, &gzstring, 1, params TSRMLS_CC) == SUCCESS &&
						gzstring.type == IS_STRING)
					{
						zval_dtor(return_value);
						ZVAL_STRING(&func, "base64_encode", 0);
						params[0] = &gzstring;
						if (call_user_function(CG(function_table), (zval**)NULL, &func, return_value, 1, params TSRMLS_CC) == SUCCESS &&
							return_value->type == IS_STRING)
						{
							zval_dtor(&gzstring);
							welldone = 1;
						}
					}
				}
			}
			zend_catch
			{
				php_end_ob_buffer(0, 0 TSRMLS_CC);
			}
			zend_end_try();
		}
	}
	/* Clear compiled code */
	if (t != NULL)
	{
#ifdef ZEND_ENGINE_2
		destroy_op_array(t TSRMLS_CC);
#else
		destroy_op_array(t);
#endif
		efree(t);
	}
	while (f != NULL)
	{
		Bucket* q = f->pListNext;
		zend_hash_del(CG(function_table), f->arKey, f->nKeyLength);
		f = q;
	}
	while (c != NULL)
	{
		Bucket* q = c->pListNext;
		zend_hash_del(CG(class_table), c->arKey, c->nKeyLength);
		c = q;
	}
	zend_destroy_file_handle(&file_handle TSRMLS_CC);
	if (welldone)
	{
		return;
	}
	else RETURN_FALSE;
}

#endif
#endif
