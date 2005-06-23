/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 - 2005 eAccelerator                               |
   | http://eaccelerator.net                                  			  |
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
#ifdef WITH_EACCELERATOR_WEBUI

#include "webui.h"
#include "opcodes.h"
#include "cache.h"
#include "mm.h"

#include "zend.h"
#include "zend_API.h"
#include "zend_extensions.h"

#include <sys/types.h>
#include <sys/stat.h>
#ifdef ZEND_WIN32
#  include "win32/time.h"
#  include <time.h>
#  include <sys/utime.h>
#else
#  include <sys/file.h>
#  include <sys/time.h>
#  include <utime.h>
#endif
#include <fcntl.h>

#ifndef O_BINARY
#  define O_BINARY 0
#endif

#include "SAPI.h"

extern eaccelerator_mm *eaccelerator_mm_instance;
PHPAPI char *php_get_uname();

/******************************************************************************/

#ifdef WITH_EACCELERATOR_DISASSEMBLER

static const char *extopnames_declare[] = {
	"",							/* 0 */
	"DECLARE_CLASS",			/* 1 */
	"DECLARE_FUNCTION",			/* 2 */
	"DECLARE_INHERITED_CLASS"	/* 3 */
};

static const char *extopnames_cast[] = {
	"IS_NULL",					/* 0 */
	"IS_LONG",					/* 1 */
	"IS_DOUBLE",				/* 2 */
	"IS_STRING",				/* 3 */
	"IS_ARRAY",					/* 4 */
	"IS_OBJECT",				/* 5 */
	"IS_BOOL",					/* 6 */
	"IS_RESOURCE",				/* 7 */
	"IS_CONSTANT",				/* 8 */
	"IS_CONSTANT_ARRAY"			/* 9 */
};

static const char *extopnames_fetch[] = {
	"FETCH_STANDARD",			/* 0 */
	"FETCH_ADD_LOCK"			/* 1 */
};

static const char *extopnames_fetch_class[] = {
	"FETCH_CLASS_DEFAULT",		/* 0 */
	"FETCH_CLASS_SELF",			/* 1 */
	"FETCH_CLASS_PARENT",		/* 2 */
	"FETCH_CLASS_MAIN",			/* 3 */
	"FETCH_CLASS_GLOBAL",		/* 4 */
	"FETCH_CLASS_AUTO"			/* 5 */
};

static const char *extopnames_init_fcall[] = {
	"&nbsp;",					/* 0 */
	"MEMBER_FUNC_CALL",			/* 1 */
	"CTOR_CALL",				/* 2 */
	"CTOR_CALL"					/* 3 */
};

static const char *extopnames_sendnoref[] = {
	"&nbsp;",					/* 0 */
	"ARG_SEND_BY_REF",			/* 1 */
	"ARG_COMPILE_TIME_BOUND",	/* 2 */
	"ARG_SEND_BY_REF | ZEND_ARG_COMPILE_TIME_BOUND"	/* 3 */
};

static const char *fetchtypename[] = {
	"FETCH_GLOBAL",				/* 0 */
	"FETCH_LOCAL",				/* 1 */
	"FETCH_STATIC"				/* 2 */
#ifdef ZEND_ENGINE_2
		,
	"FETCH_STATIC_MEMBER"		/* 3 */
#endif
};

static const char *extopnames_fe[] = {
	"",							/* 0 */
	"FE_FETCH_BYREF",			/* 1 */
	"FE_FETCH_WITH_KEY"			/* 2 */
};

static void dump_write (const char *s, uint len)
{
	uint i = 0;
	while (i < len) {
		if (!s[i])
			ZEND_PUTS ("\\000");
		else if (s[i] == '\n')
			ZEND_PUTS ("\\n");
		else if (s[i] == '\r')
			ZEND_PUTS ("\\r");
		else if (s[i] < ' ')
			zend_printf ("\\%03o", (unsigned char) s[i]);
		else if (s[i] == '<')
			ZEND_PUTS ("&lt;");
		else if (s[i] == '>')
			ZEND_PUTS ("&gt;");
		else if (s[i] == '&')
			ZEND_PUTS ("&amp;");
		else if (s[i] == '\'')
			ZEND_PUTS ("\\'");
		else if (s[i] == '\\')
			ZEND_PUTS ("\\\\");
		else
			zend_write (&s[i], 1);
		++i;
	}
}

static void dump_zval (zval * v, int compress)
{
	switch (v->type & ~IS_CONSTANT_INDEX) {
	case IS_NULL:
		ZEND_PUTS ("null");
		break;
	case IS_LONG:
		zend_printf ("long(%ld)", v->value.lval);
		break;
	case IS_DOUBLE:
		zend_printf ("double(%e)", v->value.dval);
/*
      zend_printf("double(%.*G)", v->value.dval);
*/
		break;
	case IS_STRING:
		ZEND_PUTS ("string('");
	  string_dump:
		dump_write (v->value.str.val, v->value.str.len);
		ZEND_PUTS ("')");
		break;
	case IS_BOOL:
		zend_printf ("bool(%s)", v->value.lval ? "true" : "false");
		break;
	case IS_ARRAY:
		ZEND_PUTS ("array(");
	  array_dump:
		{
			Bucket *p = v->value.ht->pListHead;
			while (p != NULL) {
				if (p->nKeyLength == 0) {
					zend_printf ("%lu", p->h);
				} else {
					int is_const = 0;
					if (((zval *) p->pDataPtr)->type & IS_CONSTANT_INDEX) {
						is_const = 1;
					}
					if (is_const) {
						ZEND_PUTS ("constant(");
					}
					ZEND_PUTS (p->arKey);
					if (is_const) {
						ZEND_PUTS (")");
					}
				}
				ZEND_PUTS (" => ");
				dump_zval ((zval *) p->pDataPtr, 1);
				p = p->pListNext;
				if (p != NULL)
					ZEND_PUTS (", ");
			}
		}
		ZEND_PUTS (")");
		break;
	case IS_OBJECT:
#ifdef ZEND_ENGINE_2
		ZEND_PUTS ("object(?)");
#else
		ZEND_PUTS ("object(");
		if (v->value.obj.ce != NULL) {
			zend_printf ("class: '%s' properties(", v->value.obj.ce);
		} else {
			ZEND_PUTS ("class: ? properties(");
		}
		if (v->value.obj.properties != NULL) {
			Bucket *p = v->value.obj.properties->pListHead;
			while (p != NULL) {
				if (p->nKeyLength == 0) {
					zend_printf ("%lu", p->h);
				} else {
					int is_const = 0;
					if ((compress
						 && (((zval *) p->pData)->type & IS_CONSTANT_INDEX))
						|| (!compress
							&& ((*(zval **) p->pData)->
								type & IS_CONSTANT_INDEX))) {
						is_const = 1;
					}
					if (is_const) {
						ZEND_PUTS ("constant(");
					}
					ZEND_PUTS (p->arKey);
					if (is_const) {
						ZEND_PUTS (")");
					}
				}
				ZEND_PUTS (" => ");
				dump_zval ((zval *) p->pDataPtr, 1);
				p = p->pListNext;
				if (p != NULL)
					ZEND_PUTS (", ");
			}
		}
		ZEND_PUTS ("))");
#endif
		break;
	case IS_RESOURCE:
		ZEND_PUTS ("resource(?)");
		break;
	case IS_CONSTANT:
		ZEND_PUTS ("constant('");
		goto string_dump;
	case IS_CONSTANT_ARRAY:
		ZEND_PUTS ("constatnt_array(");
		goto array_dump;
	default:
		zend_printf ("unknown(%d)", v->type);
	}
}

static const char *color_list[] = {
	"#FF0000",
	"#00FF00",
	"#0000FF",
	"#FFFF00",
	"#00FFFF",
	"#FF00FF",
	"#800000",
	"#008000",
	"#000080",
	"#808000",
	"#008080",
	"#800080"
};

static char const *color (int num)
{
	return color_list[num % (sizeof (color_list) / sizeof (char *))];
}

static char *get_file_contents (char *filename)
{
	struct stat st;
	char *buf;
	FILE *fp;

	if (stat (filename, &st) == -1)
		return NULL;

	buf = emalloc (st.st_size);
	if (buf == NULL)
		return NULL;

	fp = fopen (filename, "rb");
	fread (buf, 1, st.st_size, fp);
	fclose (fp);

	return buf;
}

static void print_file_line (char *p, int line)
{
	char *s;

	if (p == NULL) {
		zend_printf ("..can't open file..");
		return;
	}

	while (line > 0 && *p) {
		if (*p == '\n') {
			line--;
		} else if (line == 1) {
			if (*p == '<')
				zend_printf ("&lt;");
			else if (*p == '>')
				zend_printf ("&gt;");
			else
				zend_printf ("%c", *p);
		}

		p++;
	}
}

static void dump_op_array (eaccelerator_op_array * p TSRMLS_DC)
{
	zend_op *opline;
	zend_op *end;
	char *filebuf;
	unsigned last_line = 0;

#ifdef ZEND_ENGINE_2
	zend_printf ("T = %u, size = %u\n, brk_count = %u, file = %s<br>\n",
				 p->T, p->last, p->last_brk_cont, p->filename);
#else
	zend_printf
		("T = %u, size = %u\n, uses_globals = %d, brk_count = %u, file = %s<br>\n",
		 p->T, p->last, p->uses_globals, p->last_brk_cont, p->filename);
#endif

	if (p->static_variables) {
		Bucket *q = p->static_variables->pListHead;

		ZEND_PUTS
			("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
		ZEND_PUTS
			("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"200\">Static variable</th><th width=\"400\">Value</th></tr></thead>\n");
		ZEND_PUTS
			("<tbody valign=\"top\" bgcolor=\"#cccccc\" style=\"word-break:break-all\">\n");

		while (q) {
			zend_printf ("<tr><td bgcolor=\"#ccccff\">$%s</td><td>", q->arKey);
			dump_zval ((zval *) q->pDataPtr, 1);
			ZEND_PUTS ("&nbsp;</td></tr>\n");
			q = q->pListNext;
		}
		ZEND_PUTS ("<tbody></table><br>\n");
	}

	if (p->opcodes) {
		int n = 0;
		opline = p->opcodes;
		end = opline + p->last;

		filebuf = get_file_contents (p->filename);

		ZEND_PUTS
			("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"900\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
		ZEND_PUTS
			("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"40\">N</th><th width=\"160\">OPCODE</th><th width=\"160\">EXTENDED_VALUE</th><th width=\"220\">OP1</th><th width=\"220\">OP2</th><th width=\"80\">RESULT</th></tr></thead>\n");
		ZEND_PUTS
			("<tbody valign=\"top\" bgcolor=\"#cccccc\" style=\"word-break:break-all; font-size: x-small\">\n");
		for (; opline < end; opline++) {
			const opcode_dsc *op = get_opcode_dsc (opline->opcode);

			while (last_line < opline->lineno) {
				last_line++;
				zend_printf
					("<tr><td colspan=6 bgcolor=black><pre><font color=#80ff80>");
				print_file_line (filebuf, last_line);
				zend_printf ("</font></pre></td></tr>\n");
			}

			if (op != NULL) {
				zend_printf
					("<tr><td><font color=%s>%d</font> </td><td>%s </td>",
					 color (n), n, op->opname);
				if ((op->ops & EXT_MASK) == EXT_OPLINE) {
					zend_printf
						("<td><font color=%s>opline(%lu)</font> </td>",
						 color (opline->
								extended_value), opline->extended_value);
				} else if ((op->ops & EXT_MASK) == EXT_FCALL) {
					zend_printf ("<td>args(%lu) </td>", opline->extended_value);
				} else if ((op->ops & EXT_MASK) == EXT_ARG) {
					zend_printf ("<td>arg(%lu) </td>", opline->extended_value);
				} else if ((op->ops & EXT_MASK) == EXT_SEND) {
					zend_printf ("<td>%s </td>",
								 get_opcode_dsc (opline->
												 extended_value)->opname);
				} else if ((op->ops & EXT_MASK) == EXT_CAST) {
					zend_printf ("<td>%s </td>",
								 extopnames_cast[opline->extended_value]);
				} else if ((op->ops & EXT_MASK) == EXT_INIT_FCALL) {
					zend_printf ("<td>%s </td>",
								 extopnames_init_fcall[opline->extended_value]);
				} else if ((op->ops & EXT_MASK) == EXT_FETCH) {
					zend_printf ("<td>%s </td>",
								 extopnames_fetch[opline->extended_value]);
				} else if ((op->ops & EXT_MASK) == EXT_FE) {
					zend_printf ("<td>%s </td>",
								 extopnames_fe[opline->extended_value]);
				} else if ((op->ops & EXT_MASK) == EXT_DECLARE) {
					zend_printf ("<td>%s </td>",
								 extopnames_declare[opline->extended_value]);
				} else if ((op->ops & EXT_MASK) == EXT_SEND_NOREF) {
					zend_printf ("<td>%s </td>",
								 extopnames_sendnoref[opline->extended_value]);
				} else if ((op->ops & EXT_MASK) == EXT_FCLASS) {
					zend_printf ("<td>%s </td>",
								 extopnames_fetch_class
								 [opline->extended_value]);
				} else if ((op->ops & EXT_MASK) == EXT_IFACE) {
					zend_printf
						("<td>interface(%lu) </td>", opline->extended_value);
				} else if ((op->ops & EXT_MASK) == EXT_CLASS) {
					zend_printf ("<td>$class%u </td>",
								 VAR_NUM (opline->extended_value));
				} else if ((op->ops & EXT_MASK) == EXT_BIT) {
					zend_printf ("<td>%s </td>",
								 opline->extended_value ? "true" : "false");
				} else if ((op->ops & EXT_MASK) == EXT_ISSET) {
					if (opline->extended_value == ZEND_ISSET) {
						ZEND_PUTS ("<td>ZEND_ISSET </td>");
					} else if (opline->extended_value == ZEND_ISEMPTY) {
						ZEND_PUTS ("<td>ZEND_ISEMPTY </td>");
					} else {
						ZEND_PUTS ("<td>&nbsp; </td>");
					}
#ifdef ZEND_ENGINE_2
				} else if ((op->ops & EXT_MASK) == EXT_ASSIGN) {
					if (opline->extended_value == ZEND_ASSIGN_OBJ) {
						ZEND_PUTS ("<td>ZEND_ASSIGN_OBJ </td>");
					} else if (opline->extended_value == ZEND_ASSIGN_DIM) {
						ZEND_PUTS ("<td>ZEND_ASSIGN_DIM </td>");
					} else {
						ZEND_PUTS ("<td>&nbsp; </td>");
					}
				} else if (opline->opcode == ZEND_UNSET_DIM_OBJ) {
					if (opline->extended_value == ZEND_UNSET_DIM) {
						ZEND_PUTS ("<td>ZEND_UNSET_DIM </td>");
					} else if (opline->extended_value == ZEND_UNSET_OBJ) {
						ZEND_PUTS ("<td>ZEND_UNSET_OBJ </td>");
					} else {
						ZEND_PUTS ("<td>&nbsp; </td>");
					}
#endif
				} else if (opline->extended_value != 0) {
					zend_printf ("<td>%ld </td>", opline->extended_value);
				} else {
					ZEND_PUTS ("<td>&nbsp;</td>");
				}
			} else {
				zend_printf
					("<tr><td>%d </td><td>UNKNOWN_OPCODE %d </td><td>%lu </td>",
					 n, opline->opcode, opline->extended_value);
				op = get_opcode_dsc (0);
			}

			if ((op->ops & OP1_MASK) == OP1_OPLINE) {
				zend_printf
					("<td><font color=%s>opline(%d)</font> </td>",
					 color (opline->op1.u.opline_num),
					 opline->op1.u.opline_num);
#ifdef ZEND_ENGINE_2
			} else if ((op->ops & OP1_MASK) == OP1_JMPADDR) {
				zend_printf
					("<td><font color=%s>opline(%u)</font> </td>",
					 color ((unsigned int) (opline->op1.u.
											jmp_addr -
											p->opcodes)),
					 (unsigned int) (opline->op1.u.jmp_addr - p->opcodes));
			} else if ((op->ops & OP1_MASK) == OP1_CLASS) {
				zend_printf ("<td>$class%u </td>", VAR_NUM (opline->op1.u.var));
			} else if ((op->ops & OP1_MASK) == OP1_UCLASS) {
				if (opline->op1.op_type == IS_UNUSED) {
					zend_printf ("<td>&nbsp; </td>");
				} else {
					zend_printf ("<td>$class%u </td>",
								 VAR_NUM (opline->op1.u.var));
				}
#endif
			} else if ((op->ops & OP1_MASK) == OP1_BRK) {
				if (opline->op1.u.opline_num != -1 &&
					opline->op2.op_type == IS_CONST &&
					opline->op2.u.constant.type == IS_LONG) {
					int level = opline->op2.u.constant.value.lval;
					zend_uint offset = opline->op1.u.opline_num;
					zend_brk_cont_element *jmp_to;
					do {
						if (offset >= p->last_brk_cont) {
							goto brk_failed;
						}
						jmp_to = &p->brk_cont_array[offset];
						offset = jmp_to->parent;
					}
					while (--level > 0);
					zend_printf
						("<td><font color=%s>opline(%d)</font> </td>",
						 color (jmp_to->brk), jmp_to->brk);
				} else {
				  brk_failed:
					zend_printf ("<td>brk_cont(%u) </td>",
								 opline->op1.u.opline_num);
				}
			} else if ((op->ops & OP1_MASK) == OP1_CONT) {
				if (opline->op1.u.opline_num != -1 &&
					opline->op2.op_type == IS_CONST &&
					opline->op2.u.constant.type == IS_LONG) {
					int level = opline->op2.u.constant.value.lval;
					zend_uint offset = opline->op1.u.opline_num;
					zend_brk_cont_element *jmp_to;
					do {
						if (offset >= p->last_brk_cont) {
							goto cont_failed;
						}
						jmp_to = &p->brk_cont_array[offset];
						offset = jmp_to->parent;
					}
					while (--level > 0);
					zend_printf
						("<td><font color=%s>opline(%d)</font> </td>",
						 color (jmp_to->cont), jmp_to->cont);
				} else {
				  cont_failed:
					zend_printf ("<td>brk_cont(%u) </td>",
								 opline->op1.u.opline_num);
				}
			} else if ((op->ops & OP1_MASK) == OP1_ARG) {
				zend_printf ("<td>arg(%ld) </td>",
							 opline->op1.u.constant.value.lval);
			} else if ((op->ops & OP1_MASK) == OP1_VAR) {
				zend_printf
					("<td><font color=%s>$var%u</font> </td>",
					 color (VAR_NUM (opline->op1.u.var)),
					 VAR_NUM (opline->op1.u.var));
			} else if ((op->ops & OP1_MASK) == OP1_TMP) {
				zend_printf
					("<td><font color=%s>$tmp%u</font> </td>",
					 color (VAR_NUM (opline->op1.u.var)),
					 VAR_NUM (opline->op1.u.var));
			} else {
				if (opline->op1.op_type == IS_CONST) {
					ZEND_PUTS ("<td>");
					dump_zval (&opline->op1.u.constant, 0);
					ZEND_PUTS (" </td>");
				} else if (opline->op1.op_type == IS_TMP_VAR) {
					zend_printf
						("<td><font color=%s>$tmp%u</font> </td>",
						 color (VAR_NUM
								(opline->op1.u.var)),
						 VAR_NUM (opline->op1.u.var));
				} else if (opline->op1.op_type == IS_VAR) {
					zend_printf
						("<td><font color=%s>$var%u</font> </td>",
						 color (VAR_NUM
								(opline->op1.u.var)),
						 VAR_NUM (opline->op1.u.var));
				} else if (opline->op1.op_type == IS_UNUSED) {
					ZEND_PUTS ("<td>&nbsp;</td>");
				} else {
					zend_printf
						("<td>UNKNOWN NODE %d </td>", opline->op1.op_type);
				}
			}

			if ((op->ops & OP2_MASK) == OP2_OPLINE) {
				zend_printf
					("<td><font color=%s>opline(%d)</font> </td>",
					 color (opline->op2.u.opline_num),
					 opline->op2.u.opline_num);
#ifdef ZEND_ENGINE_2
			} else if ((op->ops & OP2_MASK) == OP2_JMPADDR) {
				zend_printf
					("<td><font color=%s>opline(%u)</font> </td>",
					 color ((unsigned int) (opline->op2.u.
											jmp_addr -
											p->opcodes)),
					 (unsigned int) (opline->op2.u.jmp_addr - p->opcodes));
			} else if ((op->ops & OP2_MASK) == OP2_CLASS) {
				zend_printf ("<td>$class%u </td>", VAR_NUM (opline->op2.u.var));
#endif
			} else if ((op->ops & OP2_MASK) == OP2_VAR) {
				zend_printf
					("<td><font color=%s>$var%u</font> </td>",
					 color (VAR_NUM (opline->op2.u.var)),
					 VAR_NUM (opline->op2.u.var));
			} else if ((op->ops & OP2_MASK) == OP2_FETCH) {
#ifdef ZEND_ENGINE_2
				if (opline->op2.u.EA.type == ZEND_FETCH_STATIC_MEMBER) {
					zend_printf ("<td>%s $class%u</td>",
								 fetchtypename[opline->
											   op2.u.EA.
											   type],
								 VAR_NUM (opline->op2.u.var));
				} else {
					zend_printf ("<td>%s </td>",
								 fetchtypename[opline->op2.u.EA.type]);
				}
#else
				zend_printf ("<td>%s </td>",
							 fetchtypename[opline->op2.u.fetch_type]);
#endif
			} else if ((op->ops & OP2_MASK) == OP2_INCLUDE) {
				if (opline->op2.u.constant.value.lval == ZEND_EVAL) {
					ZEND_PUTS ("<td>ZEND_EVAL </td>");
				} else if (opline->op2.u.constant.value.lval == ZEND_INCLUDE) {
					ZEND_PUTS ("<td>ZEND_INCLUDE </td>");
				} else if (opline->op2.u.constant.value.lval ==
						   ZEND_INCLUDE_ONCE) {
					ZEND_PUTS ("<td>ZEND_INCLUDE_ONCE </td>");
				} else if (opline->op2.u.constant.value.lval == ZEND_REQUIRE) {
					ZEND_PUTS ("<td>ZEND_REQUIRE </td>");
				} else if (opline->op2.u.constant.value.lval ==
						   ZEND_REQUIRE_ONCE) {
					ZEND_PUTS ("<td>ZEND_REQUIRE_ONCE </td>");
				} else {
					ZEND_PUTS ("<td>&nbsp;</td>");
				}
			} else if ((op->ops & OP2_MASK) == OP2_ARG) {
				zend_printf ("<td>arg(%u) </td>", opline->op2.u.opline_num);
			} else if ((op->ops & OP2_MASK) == OP2_ISSET) {
				if (opline->op2.u.constant.value.lval == ZEND_ISSET) {
					ZEND_PUTS ("<td>ZEND_ISSET </td>");
				} else if (opline->op2.u.constant.value.lval == ZEND_ISEMPTY) {
					ZEND_PUTS ("<td>ZEND_ISEMPTY </td>");
				} else {
					ZEND_PUTS ("<td>&nbsp; </td>");
				}
			} else {
				if (opline->op2.op_type == IS_CONST) {
					ZEND_PUTS ("<td>");
					dump_zval (&opline->op2.u.constant, 0);
					ZEND_PUTS (" </td>");
				} else if (opline->op2.op_type == IS_TMP_VAR) {
					zend_printf
						("<td><font color=%s>$tmp%u</font> </td>",
						 color (VAR_NUM
								(opline->op2.u.var)),
						 VAR_NUM (opline->op2.u.var));
				} else if (opline->op2.op_type == IS_VAR) {
					zend_printf
						("<td><font color=%s>$var%u</font> </td>",
						 color (VAR_NUM
								(opline->op2.u.var)),
						 VAR_NUM (opline->op2.u.var));
				} else if (opline->op2.op_type == IS_UNUSED) {
					ZEND_PUTS ("<td>&nbsp; </td>");
				} else {
					zend_printf
						("<td>UNKNOWN NODE %d </td>", opline->op2.op_type);
				}
			}

			switch (op->ops & RES_MASK) {
			case RES_STD:
				if (opline->result.op_type == IS_CONST) {
					ZEND_PUTS ("<td>");
					dump_zval (&opline->result.u.constant, 0);
					ZEND_PUTS ("</td>");
				} else if (opline->result.op_type == IS_TMP_VAR) {
					zend_printf
						("<td><font color=%s>$tmp%u</font> </td>",
						 color (VAR_NUM
								(opline->result.u.var)),
						 VAR_NUM (opline->result.u.var));
				} else if (opline->result.op_type == IS_VAR) {
					if ((opline->result.u.EA.type & EXT_TYPE_UNUSED) != 0)
						zend_printf
							("<td><font color=%s>$var%u <small>(unused)</small></font> </td>",
							 color (VAR_NUM
									(opline->
									 result.u.
									 var)), VAR_NUM (opline->result.u.var));
					else
						zend_printf
							("<td><font color=%s>$var%u</font> </td>",
							 color (VAR_NUM
									(opline->
									 result.u.
									 var)), VAR_NUM (opline->result.u.var));
				} else if (opline->result.op_type == IS_UNUSED) {
					ZEND_PUTS ("<td>&nbsp;</td>");
				} else {
					zend_printf
						("<td>UNKNOWN NODE %d</td>", opline->result.op_type);
				}
				break;
			case RES_CLASS:
				zend_printf ("<td>$class%u</td>",
							 VAR_NUM (opline->result.u.var));
				break;
			case RES_TMP:
				zend_printf
					("<td><font color=%s>$tmp%u</font> </td>",
					 color (VAR_NUM
							(opline->result.u.var)),
					 VAR_NUM (opline->result.u.var));
				break;
			case RES_VAR:
				if ((opline->result.u.EA.type & EXT_TYPE_UNUSED) != 0) {
					zend_printf
						("<td><font color=%s>$var%u <small>(unused)</small></font> </td>",
						 color (VAR_NUM
								(opline->result.u.
								 var)), VAR_NUM (opline->result.u.var));
				} else {
					zend_printf
						("<td><font color=%s>$var%u</font> </td>",
						 color (VAR_NUM
								(opline->result.u.
								 var)), VAR_NUM (opline->result.u.var));
				}
				break;
			case RES_UNUSED:
				ZEND_PUTS ("<td>&nbsp;</td>");
				break;
			default:
				zend_printf ("<td>UNKNOWN NODE %d</td>",
							 opline->result.op_type);
				break;
			}
			ZEND_PUTS ("</tr>\n");
			n++;
		}
		ZEND_PUTS ("</tbody></table>\n");

		efree (filebuf);
	}
}

static void dump_cache_entry (mm_cache_entry * p TSRMLS_DC)
{
	mm_fc_entry *fc = p->c_head;
	if (fc != NULL) {
		ZEND_PUTS
			("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
		ZEND_PUTS
			("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"600\">Classes</th></tr></thead>\n");
		ZEND_PUTS
			("<tbody valign=\"top\" bgcolor=\"#ccccff\" style=\"word-break:break-all\">\n");
		while (fc != NULL) {
			eaccelerator_class_entry *x = (eaccelerator_class_entry *) fc->fc;
			char class[1024];
			memcpy (class, fc->htabkey, fc->htablen);
			class[fc->htablen] = '\0';
			if (class[0] == '\000')
				class[0] = '-';
			if (x->type == ZEND_USER_CLASS) {
#ifdef ZEND_ENGINE_2
				zend_printf
					("<tr><td><a href=\"?file=%s&class=%s\">%s</a> [\n",
					 p->realfilename, class, class);
				if (x->ce_flags & ZEND_ACC_FINAL_CLASS) {
					ZEND_PUTS ("final ");
				}
				if (x->ce_flags & ZEND_ACC_IMPLICIT_ABSTRACT_CLASS) {
					ZEND_PUTS ("implicit abstract ");
				}
				if (x->ce_flags & ZEND_ACC_EXPLICIT_ABSTRACT_CLASS) {
					ZEND_PUTS ("explicit abstract ");
				}
				if (x->ce_flags & ZEND_ACC_INTERFACE) {
					ZEND_PUTS ("interface");
				} else {
					ZEND_PUTS ("class ");
				}
				ZEND_PUTS ("]</td></tr>");
#else
				zend_printf
					("<tr><td><a href=\"?file=%s&class=%s\">%s</a></td></tr>\n",
					 p->realfilename, class, class);
#endif
			} else {
				zend_printf ("<tr><td>%s [internal]</td></tr>\n", class);
			}
			fc = fc->next;
		}
		ZEND_PUTS ("</tbody></table><br>\n");
	}
	fc = p->f_head;
	if (fc != NULL) {
		ZEND_PUTS
			("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
		ZEND_PUTS
			("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"600\">Functions</th></tr></thead>\n");
		ZEND_PUTS
			("<tbody valign=\"top\" bgcolor=\"#ccccff\" style=\"word-break:break-all\">\n");
		while (fc != NULL) {
			char func[1024];
			memcpy (func, fc->htabkey, fc->htablen);
			func[fc->htablen] = '\0';
			if (func[0] == '\000' && fc->htablen > 0)
				func[0] = '-';
			if (((zend_function *) (fc->fc))->type == ZEND_USER_FUNCTION) {
				zend_printf
					("<tr><td><a href=\"?file=%s&func=%s\">%s</a></td></tr>\n",
					 p->realfilename, func, func);
			} else {
				zend_printf ("<tr><td>%s [internal]</td></tr>\n", func);
			}
			fc = fc->next;
		}
		ZEND_PUTS ("</tbody></table><br>\n");
	}
	if (p->op_array) {
		dump_op_array (p->op_array TSRMLS_CC);
	}
}

static void dump_class (mm_cache_entry * p, char *class TSRMLS_DC)
{
	mm_fc_entry *fc = p->c_head;
	int len;
	if (class[0] == '-') {
		len = strlen (class);
		class[0] = '\0';
	} else {
		len = strlen (class) + 1;
	}
	while (fc != NULL) {
		if (len == fc->htablen && memcmp (fc->htabkey, class, fc->htablen) == 0) {
			break;
		}
		fc = fc->next;
	}
	if (class[0] == '\0')
		class[0] = '-';
	if (fc && fc->fc) {
		eaccelerator_class_entry *c = (eaccelerator_class_entry *) fc->fc;
		Bucket *q;
		if (c->parent) {
			zend_printf ("<h4>extends: %s</h4>\n", (const char *) c->parent);
		}
#ifdef ZEND_ENGINE_2
		if (c->properties_info.nNumOfElements > 0) {
			q = c->properties_info.pListHead;
			ZEND_PUTS
				("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
			ZEND_PUTS
				("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"200\">Property</th><th width=\"400\">Value</th></tr></thead>\n");
			ZEND_PUTS
				("<tbody valign=\"top\" bgcolor=\"#cccccc\" style=\"word-break:break-all\">\n");
			while (q) {
				zend_property_info *x = (zend_property_info *) q->pData;
				Bucket *y = NULL;

				zend_printf ("<tr><td bgcolor=\"#ccccff\">$%s [", q->arKey);
				if (x->flags & ZEND_ACC_STATIC) {
					ZEND_PUTS ("static ");
				}
				if ((x->flags & ZEND_ACC_PPP_MASK) == ZEND_ACC_PRIVATE) {
					ZEND_PUTS ("private ");
				} else if ((x->flags & ZEND_ACC_PPP_MASK) == ZEND_ACC_PROTECTED) {
					ZEND_PUTS ("protected ");
				} else if ((x->flags & ZEND_ACC_PPP_MASK) == ZEND_ACC_PUBLIC) {
					ZEND_PUTS ("public ");
				}
				if (x->flags & ZEND_ACC_FINAL) {
					ZEND_PUTS ("final ");
				}
				ZEND_PUTS ("]</td><td>");
				if ((x->flags & ZEND_ACC_STATIC)
					&& c->static_members != NULL
					&& c->static_members->nNumOfElements > 0) {
					y = c->static_members->pListHead;
				} else if ((x->flags & ZEND_ACC_STATIC) == 0
						   && c->default_properties.nNumOfElements > 0) {
					y = c->default_properties.pListHead;
				}
				while (y) {
					if (y->h == x->h &&
						(int) y->nKeyLength ==
						x->name_length + 1
						&& memcmp (y->arKey, x->name,
								   x->name_length + 1) == 0) {
						dump_zval ((zval *) y->pDataPtr, 1);
						break;
					}
					y = y->pListNext;
				}
				ZEND_PUTS ("&nbsp;</td></tr>\n");
				q = q->pListNext;
			}
			ZEND_PUTS ("<tbody></table><br>\n");
		}

		if (c->constants_table.nNumOfElements > 0) {
			q = c->constants_table.pListHead;
			ZEND_PUTS
				("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
			ZEND_PUTS
				("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"200\">Constant</th><th width=\"400\">Value</th></tr></thead>\n");
			ZEND_PUTS
				("<tbody valign=\"top\" bgcolor=\"#cccccc\" style=\"word-break:break-all\">\n");
			while (q) {
				zend_printf
					("<tr><td bgcolor=\"#ccccff\">%s</td><td>", q->arKey);
				dump_zval ((zval *) q->pDataPtr, 1);
				ZEND_PUTS ("&nbsp;</td></tr>\n");
				q = q->pListNext;
			}
			ZEND_PUTS ("<tbody></table><br>\n");
		}
#else
		if (c->default_properties.nNumOfElements > 0) {
			q = c->default_properties.pListHead;
			ZEND_PUTS
				("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
			ZEND_PUTS
				("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"200\">Property</th><th width=\"400\">Value</th></tr></thead>\n");
			ZEND_PUTS
				("<tbody valign=\"top\" bgcolor=\"#cccccc\" style=\"word-break:break-all\">\n");
			while (q) {
				zend_printf
					("<tr><td bgcolor=\"#ccccff\">$%s</td><td>", q->arKey);
				dump_zval ((zval *) q->pDataPtr, 1);
				ZEND_PUTS ("&nbsp;</td></tr>\n");
				q = q->pListNext;
			}
			ZEND_PUTS ("<tbody></table><br>\n");
		}
#endif

		q = c->function_table.pListHead;
		ZEND_PUTS
			("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed\">\n");
		ZEND_PUTS
			("<thead valign=\"middle\" bgcolor=\"#9999cc\"><tr><th width=\"600\">Methods</th></tr></thead>\n");
		ZEND_PUTS
			("<tbody valign=\"top\" bgcolor=\"#ccccff\" style=\"word-break:break-all\">\n");
		while (q) {
			eaccelerator_op_array *x = (eaccelerator_op_array *) q->pData;
			if (x->type == ZEND_USER_FUNCTION) {
#ifdef ZEND_ENGINE_2
				zend_printf
					("<tr><td><a href=\"?file=%s&class=%s&func=%s\">%s</a> [",
					 p->realfilename, class, q->arKey, q->arKey);
				if (x->fn_flags & ZEND_ACC_STATIC) {
					ZEND_PUTS ("static ");
				}
				if ((x->fn_flags & ZEND_ACC_PPP_MASK) == ZEND_ACC_PRIVATE) {
					ZEND_PUTS ("private ");
				} else if ((x->fn_flags & ZEND_ACC_PPP_MASK) ==
						   ZEND_ACC_PROTECTED) {
					ZEND_PUTS ("protected ");
				} else if ((x->fn_flags & ZEND_ACC_PPP_MASK) == ZEND_ACC_PUBLIC) {
					ZEND_PUTS ("public ");
				}
				if (x->fn_flags & ZEND_ACC_ABSTRACT) {
					ZEND_PUTS ("abstract ");
				}
				if (x->fn_flags & ZEND_ACC_FINAL) {
					ZEND_PUTS ("final ");
				}
				ZEND_PUTS ("]</td></tr>");
#else
				zend_printf
					("<tr><td><a href=\"?file=%s&class=%s&func=%s\">%s</a></td></tr>\n",
					 p->realfilename, class, q->arKey, q->arKey);
#endif
			} else {
				zend_printf ("<tr><td>%s [internal]</td></tr>\n", q->arKey);
			}
			q = q->pListNext;
		}
		ZEND_PUTS ("</tbody></table><br>\n");
		return;
	}
	ZEND_PUTS ("<h5>NOT FOUND</h5>\n");
}

static void
dump_method (mm_cache_entry * p, char *class, const char *func TSRMLS_DC)
{
	mm_fc_entry *fc = p->c_head;
	int len;
	if (class[0] == '-') {
		len = strlen (class);
		class[0] = '\0';
	} else {
		len = strlen (class) + 1;
	}
	while (fc != NULL) {
		if (len == fc->htablen && memcmp (fc->htabkey, class, fc->htablen) == 0) {
			break;
		}
		fc = fc->next;
	}
	if (class[0] == '\0')
		class[0] = '-';
	if (fc && fc->fc) {
		unsigned int len = strlen (func) + 1;
		eaccelerator_class_entry *c = (eaccelerator_class_entry *) fc->fc;
		Bucket *q = c->function_table.pListHead;
		while (q != NULL) {
			if (len == q->nKeyLength && memcmp (func, q->arKey, len) == 0) {
				dump_op_array ((eaccelerator_op_array *) q->pData TSRMLS_CC);
				return;
			}
			q = q->pListNext;
		}
	}
	ZEND_PUTS ("<h5>NOT FOUND</h5>\n");
}

static void dump_function (mm_cache_entry * p, char *func TSRMLS_DC)
{
	mm_fc_entry *fc = p->f_head;
	int len;
	if (func[0] == '-') {
		len = strlen (func);
		func[0] = '\0';
	} else {
		len = strlen (func) + 1;
	}
	while (fc != NULL) {
		if (len == fc->htablen && memcmp (fc->htabkey, func, fc->htablen) == 0) {
			break;
		}
		fc = fc->next;
	}
	if (func[0] == '\0')
		func[0] = '-';
	if (fc && fc->fc) {
		dump_op_array ((eaccelerator_op_array *) fc->fc TSRMLS_CC);
		return;
	}
	ZEND_PUTS ("<h5>NOT FOUND</h5>\n");
}

static int eaccelerator_dump (char *file, char *func, char *class TSRMLS_DC)
{
	unsigned int slot;
	mm_cache_entry *p;

	if (file != NULL) {
		EACCELERATOR_UNPROTECT ();
		EACCELERATOR_LOCK_RD ();
		EACCELERATOR_PROTECT ();
		for (slot = 0; slot < MM_HASH_SIZE; slot++) {
			p = eaccelerator_mm_instance->hash[slot];
			while (p != NULL) {
				if (strcmp (p->realfilename, file) == 0) {
					goto found;
				}
				p = p->next;
			}
		}
	  found:
		zend_printf ("<h2>FILE: %s</h2>\n", file);
		if (!p) {
			EACCELERATOR_UNPROTECT ();
			EACCELERATOR_UNLOCK_RD ();
			EACCELERATOR_PROTECT ();
			ZEND_PUTS ("<h5>NOT FOUND</h5>\n");
			return 0;
		}
		if (class != NULL) {
			if (func != NULL) {
				zend_printf ("<h3>CLASS: %s</h3>\n", class);
				zend_printf ("<h4>METHOD: %s</h4>\n", func);
				dump_method (p, class, func TSRMLS_CC);
			} else {
				zend_printf ("<h3>CLASS: %s</h3>\n", class);
				dump_class (p, class TSRMLS_CC);
			}
		} else if (func != NULL) {
			zend_printf ("<h3>FUNCTION: %s</h3>\n", func);
			dump_function (p, func TSRMLS_CC);
		} else {
			dump_cache_entry (p TSRMLS_CC);
		}
		EACCELERATOR_UNPROTECT ();
		EACCELERATOR_UNLOCK_RD ();
		EACCELERATOR_PROTECT ();
		return 1;
	}
	return 0;
}

static int eaccelerator_dump_all (TSRMLS_D)
{
	unsigned int i;
	mm_cache_entry *p;
	char s[1024];

	if (eaccelerator_mm_instance == NULL) {
		return 0;
	}
	EACCELERATOR_UNPROTECT ();
	EACCELERATOR_LOCK_RD ();
	EACCELERATOR_PROTECT ();
	for (i = 0; i < MM_HASH_SIZE; i++) {
		p = eaccelerator_mm_instance->hash[i];
		while (p != NULL) {
			mm_fc_entry *fc = p->c_head;
			zend_printf ("<h2>FILE: %s</h2>\n", p->realfilename);
			if (p->op_array);
			dump_op_array (p->op_array TSRMLS_CC);

			while (fc != NULL) {
				eaccelerator_class_entry *c =
					(eaccelerator_class_entry *) fc->fc;
				Bucket *q = c->function_table.pListHead;
				memcpy (s, fc->htabkey, fc->htablen);
				s[fc->htablen] = '\0';
				if (s[0] == '\000')
					s[0] = '-';
				zend_printf ("<h3>CLASS: %s</h3>\n", s);
				while (q) {
					zend_printf ("<h4>METHOD: %s</h4>\n", q->arKey);
					dump_op_array ((eaccelerator_op_array
									*) q->pData TSRMLS_CC);
					q = q->pListNext;
				}
				fc = fc->next;
			}

			fc = p->f_head;
			while (fc != NULL) {
				memcpy (s, fc->htabkey, fc->htablen);
				s[fc->htablen] = '\0';
				if (s[0] == '\000')
					s[0] = '-';
				zend_printf ("<h3>FUNCTION: %s</h3>\n", s);
				dump_op_array ((eaccelerator_op_array *) fc->fc TSRMLS_CC);
				fc = fc->next;
			}
			p = p->next;
		}
	}
	EACCELERATOR_UNPROTECT ();
	EACCELERATOR_UNLOCK_RD ();
	EACCELERATOR_PROTECT ();
	return 1;
}

static void eaccelerator_purge ()
{
	if (eaccelerator_mm_instance != NULL) {
		mm_cache_entry *p, *q;
		EACCELERATOR_UNPROTECT ();
		EACCELERATOR_LOCK_RW ();
		p = eaccelerator_mm_instance->removed;
		eaccelerator_mm_instance->rem_cnt = 0;
		eaccelerator_mm_instance->removed = NULL;
		while (p != NULL) {
			q = p->next;
			eaccelerator_free_nolock (p);
			p = q;
		}
		EACCELERATOR_UNLOCK_RW ();
		EACCELERATOR_PROTECT ();
	}
}
#endif


static void eaccelerator_clean (TSRMLS_D)
{
	time_t t;

	t = time (0);
	/* Remove expired scripts from shared memory */
	eaccelerator_prune (t);

	/* Remove expired keys (session data, content) from disk cache */
#ifndef ZEND_WIN32
	/* clear file cache */
	{
		DIR *dp;
		struct dirent *entry;
		char s[MAXPATHLEN];

		if ((dp = opendir (MMCG (cache_dir))) != NULL) {
			while ((entry = readdir (dp)) != NULL) {
				if (strstr
					(entry->d_name, "eaccelerator-user") == entry->d_name) {
					int f;
					strncpy (s, MMCG (cache_dir), MAXPATHLEN - 1);
					strlcat (s, "/", MAXPATHLEN);
					strlcat (s, entry->d_name, MAXPATHLEN);
					if ((f = open (s, O_RDONLY | O_BINARY)) > 0) {
						mm_file_header hdr;
						EACCELERATOR_FLOCK (f, LOCK_SH);
						if (read (f, &hdr, sizeof (hdr)) != sizeof (hdr)
							|| strncmp (hdr.magic,
										"EACCELERATOR",
										8) != 0
							|| (hdr.mtime != 0 && hdr.mtime < t)) {
							EACCELERATOR_FLOCK (f, LOCK_UN);
							close (f);
							unlink (s);
						} else {
							EACCELERATOR_FLOCK (f, LOCK_UN);
							close (f);
						}
					}
				}
			}
			closedir (dp);
		}
	}
#else
	{
		HANDLE hList;
		TCHAR szDir[MAXPATHLEN];
		WIN32_FIND_DATA FileData;
		char s[MAXPATHLEN];

		snprintf (szDir, MAXPATHLEN, "%s\\eaccelerator-user*",
				  MMCG (cache_dir));

		if ((hList = FindFirstFile (szDir, &FileData)) != INVALID_HANDLE_VALUE) {
			do {
				int f;
				strncpy (s, MMCG (cache_dir), MAXPATHLEN - 1);
				strlcat (s, "\\", MAXPATHLEN);
				strlcat (s, FileData.cFileName, MAXPATHLEN);
				if ((f = open (s, O_RDONLY | O_BINARY)) > 0) {
					mm_file_header hdr;
					EACCELERATOR_FLOCK (f, LOCK_SH);
					if (read (f, &hdr, sizeof (hdr)) != sizeof (hdr)
						|| strncmp (hdr.magic,
									"EACCELERATOR",
									8) != 0
						|| (hdr.mtime != 0 && hdr.mtime < t)) {
						EACCELERATOR_FLOCK (f, LOCK_UN);
						close (f);
						unlink (s);
					} else {
						EACCELERATOR_FLOCK (f, LOCK_UN);
						close (f);
					}
				}
			}
			while (FindNextFile (hList, &FileData));
		}

		FindClose (hList);
	}
#endif
	/* Remove expired keys (session data, content) from shared memory */
	eaccelerator_gc (TSRMLS_C);
}

static void eaccelerator_clear (TSRMLS_D)
{
	unsigned int i;
	mm_cache_entry *p;

	EACCELERATOR_UNPROTECT ();
	EACCELERATOR_LOCK_RW ();
	for (i = 0; i < MM_HASH_SIZE; i++) {
		p = eaccelerator_mm_instance->hash[i];
		while (p != NULL) {
			mm_cache_entry *r = p;
			p = p->next;
			eaccelerator_mm_instance->hash_cnt--;
			if (r->use_cnt <= 0) {
				eaccelerator_free_nolock (r);
			} else {
				r->removed = 1;
				r->next = eaccelerator_mm_instance->removed;
				eaccelerator_mm_instance->removed = r;
				eaccelerator_mm_instance->rem_cnt++;
			}
		}
		eaccelerator_mm_instance->hash[i] = NULL;
	}
	for (i = 0; i < MM_USER_HASH_SIZE; i++) {
		mm_user_cache_entry *p = eaccelerator_mm_instance->user_hash[i];
		while (p != NULL) {
			mm_user_cache_entry *r = p;
			p = p->next;
			eaccelerator_mm_instance->user_hash_cnt--;
			eaccelerator_free_nolock (r);
		}
		eaccelerator_mm_instance->user_hash[i] = NULL;
	}
	EACCELERATOR_UNLOCK_RW ();
	EACCELERATOR_PROTECT ();
#ifndef ZEND_WIN32
	/* clear file cache */
	{
		DIR *dp;
		struct dirent *entry;
		char s[MAXPATHLEN];

		if ((dp = opendir (MMCG (cache_dir))) != NULL) {
			while ((entry = readdir (dp)) != NULL) {
				if (strstr (entry->d_name, "eaccelerator") == entry->d_name) {
					strncpy (s, MMCG (cache_dir), MAXPATHLEN - 1);
					strlcat (s, "/", MAXPATHLEN);
					strlcat (s, entry->d_name, MAXPATHLEN);
					unlink (s);
				}
			}
			closedir (dp);
		}
	}
#else
	{
		HANDLE hList;
		TCHAR szDir[MAXPATHLEN];
		WIN32_FIND_DATA FileData;
		char s[MAXPATHLEN];

		snprintf (szDir, MAXPATHLEN, "%s\\eaccelerator*", MMCG (cache_dir));

		if ((hList = FindFirstFile (szDir, &FileData)) != INVALID_HANDLE_VALUE) {
			do {
				strncpy (s, MMCG (cache_dir), MAXPATHLEN - 1);
				strlcat (s, "\\", MAXPATHLEN);
				strlcat (s, FileData.cFileName, MAXPATHLEN);
				unlink (s);
			}
			while (FindNextFile (hList, &FileData));
		}

		FindClose (hList);
	}
#endif
}

static int cache_entry_compare (const void *p, const void *q)
{
	return strcmp ((*((mm_cache_entry **) p))->realfilename,
				   (*((mm_cache_entry **) q))->realfilename);
}

static int eaccelerator_login (TSRMLS_D)
{
	zval **http_vars = NULL;
	zval **name = NULL;
	zval **pass = NULL;
	char *admin_name;
	char *admin_password;

	if (cfg_get_string ("eaccelerator.admin.name", &admin_name) == FAILURE
		|| *admin_name == '\0') {
		admin_name = NULL;
	}
	if (cfg_get_string ("eaccelerator.admin.password", &admin_password) ==
		FAILURE || *admin_password == '\0') {
		admin_password = NULL;
	}
	if (admin_name == NULL && admin_password == NULL) {
		return 1;
	}
	if (zend_hash_find
		(&EG (symbol_table), "_SERVER", sizeof ("_SERVER"),
		 (void **) & http_vars) != FAILURE && (*http_vars)->type == IS_ARRAY) {
		if (zend_hash_find
			((*http_vars)->value.ht, "PHP_AUTH_USER",
			 sizeof ("PHP_AUTH_USER"), (void **) & name) == FAILURE
			|| (*name)->type != IS_STRING) {
			name = NULL;
		}
		if (zend_hash_find
			((*http_vars)->value.ht, "PHP_AUTH_PW",
			 sizeof ("PHP_AUTH_PW"), (void **) & pass) == FAILURE
			|| (*pass)->type != IS_STRING) {
			pass = NULL;
		}
	}
	if (name != NULL && pass != NULL) {
		if (admin_name == NULL || strcmp (admin_name, Z_STRVAL_PP (name)) == 0) {
			if (admin_password != NULL) {
				zval retval;
				zval crypt;
				zval param1;
				zval *params[2];

				ZVAL_STRING (&crypt, "crypt", 0);
				params[0] = *pass;
				INIT_ZVAL (param1);
				params[1] = &param1;
				ZVAL_STRING (params[1], admin_password, 0);
				if (call_user_function
					(CG (function_table), (zval **) NULL,
					 &crypt, &retval, 2,
					 params TSRMLS_CC) == SUCCESS
					&& retval.type == IS_STRING
					&& Z_STRLEN (retval) == Z_STRLEN_P (params[1])
					&& strcmp (Z_STRVAL (retval), Z_STRVAL_P (params[1])) == 0) {
					zval_dtor (&retval);
					return 1;
				}
				zval_dtor (&retval);
			} else {
				return 1;
			}
		}
	}
	sapi_add_header_ex ("WWW-authenticate: basic realm='eAccelerator'",
						sizeof
						("WWW-authenticate: basic realm='eAccelerator'") -
						1, 1, 1 TSRMLS_CC);
	sapi_add_header_ex ("HTTP/1.0 401 Unauthorized",
						sizeof ("HTTP/1.0 401 Unauthorized") - 1, 1,
						1 TSRMLS_CC);
	ZEND_PUTS
		("You must enter a valid login ID and password to access this resource\n");
	return 0;
}

static void eaccelerator_disable_caching (TSRMLS_D)
{
	struct tm tmbuf;
	time_t curtime;
	char s[256];

	time (&curtime);
	strftime (s, 255, "Last-Modified: %a, %d %b %Y %H:%M:%S GMT",
			  php_gmtime_r (&curtime, &tmbuf));

	sapi_add_header_ex ("Expires: Thu, 19 Nov 1981 08:52:00 GMT",
						sizeof ("Expires: Thu, 19 Nov 1981 08:52:00 GMT")
						- 1, 1, 1 TSRMLS_CC);
	sapi_add_header_ex (s, strlen (s), 1, 1 TSRMLS_CC);
	sapi_add_header_ex
		("Cache-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0",
		 sizeof
		 ("Cache-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0")
		 - 1, 1, 1 TSRMLS_CC);
	sapi_add_header_ex ("Pragma: no-cache",
						sizeof ("Pragma: no-cache") - 1, 1, 1 TSRMLS_CC);
}

static void eaccelerator_puts_filename (const char *s)
{
	int i = 0;
	while (s[i] != '\0') {
		ZEND_PUTC (s[i]);
		if (s[i] == '/' || s[i] == '\\') {
			ZEND_PUTS ("<wbr>");
		}
		i++;
	}
}

PHP_FUNCTION (eaccelerator)
{
	unsigned int i, j;
	unsigned int available;
	mm_cache_entry *p;
	mm_cache_entry **slots;
	char s[MAXPATHLEN];
	zval **php_self = NULL;
	zval **serv_soft = NULL;
	zval **http_vars = NULL;

	eaccelerator_disable_caching (TSRMLS_C);
	if (eaccelerator_mm_instance == NULL) {
		ZEND_PUTS ("eAccelerator ");
		ZEND_PUTS (EACCELERATOR_VERSION);
		ZEND_PUTS
			(" is not active!\nIt doesn't work in CGI or command line mode!\n\n");
		RETURN_NULL ();
	}
	if (!eaccelerator_login (TSRMLS_C)) {
		RETURN_NULL ();
	}

	if (zend_hash_find
		(&EG (symbol_table), "_SERVER", sizeof ("_SERVER"),
		 (void **) & http_vars) != FAILURE && (*http_vars)->type == IS_ARRAY) {
		if (zend_hash_find
			((*http_vars)->value.ht, "PHP_SELF", sizeof ("PHP_SELF"),
			 (void **) & php_self) == FAILURE
			|| (*php_self)->type != IS_STRING) {
			php_self = NULL;
		}
		if (zend_hash_find
			((*http_vars)->value.ht, "SERVER_SOFTWARE",
			 sizeof ("SERVER_SOFTWARE"),
			 (void **) & serv_soft) == FAILURE
			|| (*serv_soft)->type != IS_STRING) {
			serv_soft = NULL;
		}
	}
	if (zend_hash_find
		(&EG (symbol_table), "_POST", sizeof ("_POST"),
		 (void **) & http_vars) != FAILURE && (*http_vars)->type == IS_ARRAY) {
		if (zend_hash_exists
			((*http_vars)->value.ht, "enable", sizeof ("enable"))) {
			EACCELERATOR_UNPROTECT ();
			eaccelerator_mm_instance->enabled = 1;
			EACCELERATOR_PROTECT ();
			snprintf (s, MAXPATHLEN, "Location: %s",
					  php_self ? (*php_self)->value.str.
					  val : "eaccelerator.php");
			sapi_add_header_ex (s, strlen (s), 1, 1 TSRMLS_CC);
			RETURN_NULL ();
		} else if (zend_hash_exists
				   ((*http_vars)->value.ht, "disable", sizeof ("disable"))) {
			EACCELERATOR_UNPROTECT ();
			eaccelerator_mm_instance->enabled = 0;
			EACCELERATOR_PROTECT ();
			snprintf (s, MAXPATHLEN, "Location: %s",
					  php_self ? (*php_self)->value.str.
					  val : "eaccelerator.php");
			sapi_add_header_ex (s, strlen (s), 1, 1 TSRMLS_CC);
			RETURN_NULL ();
#ifdef WITH_EACCELERATOR_OPTIMIZER
		} else if (zend_hash_exists
				   ((*http_vars)->value.ht, "enable_opt",
					sizeof ("enable_opt"))) {
			EACCELERATOR_UNPROTECT ();
			eaccelerator_mm_instance->optimizer_enabled = 1;
			EACCELERATOR_PROTECT ();
			snprintf (s, MAXPATHLEN, "Location: %s",
					  php_self ? (*php_self)->value.str.
					  val : "eaccelerator.php");
			sapi_add_header_ex (s, strlen (s), 1, 1 TSRMLS_CC);
			RETURN_NULL ();
		} else if (zend_hash_exists
				   ((*http_vars)->value.ht, "disable_opt",
					sizeof ("disable_opt"))) {
			EACCELERATOR_UNPROTECT ();
			eaccelerator_mm_instance->optimizer_enabled = 0;
			EACCELERATOR_PROTECT ();
			snprintf (s, MAXPATHLEN, "Location: %s",
					  php_self ? (*php_self)->value.str.
					  val : "eaccelerator.php");
			sapi_add_header_ex (s, strlen (s), 1, 1 TSRMLS_CC);
			RETURN_NULL ();
#endif
		} else if (zend_hash_exists
				   ((*http_vars)->value.ht, "clear", sizeof ("clear"))) {
			eaccelerator_clear (TSRMLS_C);
			snprintf (s, MAXPATHLEN, "Location: %s",
					  php_self ? (*php_self)->value.str.
					  val : "eaccelerator.php");
			sapi_add_header_ex (s, strlen (s), 1, 1 TSRMLS_CC);
			RETURN_NULL ();
		} else if (zend_hash_exists
				   ((*http_vars)->value.ht, "clean", sizeof ("clean"))) {
			eaccelerator_clean (TSRMLS_C);
			snprintf (s, MAXPATHLEN, "Location: %s",
					  php_self ? (*php_self)->value.str.
					  val : "eaccelerator.php");
			sapi_add_header_ex (s, strlen (s), 1, 1 TSRMLS_CC);
			RETURN_NULL ();
#ifdef WITH_EACCELERATOR_DISASSEMBLER
		} else if (zend_hash_exists
				   ((*http_vars)->value.ht, "purge", sizeof ("purge"))) {
			eaccelerator_purge ();
			snprintf (s, MAXPATHLEN, "Location: %s",
					  php_self ? (*php_self)->value.str.
					  val : "eaccelerator.php");
			sapi_add_header_ex (s, strlen (s), 1, 1 TSRMLS_CC);
			RETURN_NULL ();
		} else if (zend_hash_exists
				   ((*http_vars)->value.ht, "dump", sizeof ("dump"))) {
			snprintf (s, MAXPATHLEN, "Location: %s?dump=",
					  php_self ? (*php_self)->value.str.
					  val : "eaccelerator.php");
			sapi_add_header_ex (s, strlen (s), 1, 1 TSRMLS_CC);
			RETURN_NULL ();
#endif
		}
	}

	ZEND_PUTS
		("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\">\n<html>\n<head>\n  <title>eAccelerator</title>\n</head>\n<body>\n");
	ZEND_PUTS ("<h1 align=\"center\">eAccelerator ");
	ZEND_PUTS (EACCELERATOR_VERSION);
	ZEND_PUTS ("</h1>\n");

#ifdef WITH_EACCELERATOR_DISASSEMBLER
	if (zend_hash_find
		(&EG (symbol_table), "_GET", sizeof ("_GET"),
		 (void **) & http_vars) != FAILURE && (*http_vars)->type == IS_ARRAY) {
		if (zend_hash_exists ((*http_vars)->value.ht, "dump", sizeof ("dump"))) {
			eaccelerator_dump_all (TSRMLS_C);
			ZEND_PUTS ("</body></html>");
			RETURN_NULL ();
		} else {
			zval **data;
			char *file = NULL;
			char *func = NULL;
			char *class = NULL;
			if (zend_hash_find
				((*http_vars)->value.ht, "file", sizeof ("file"),
				 (void **) & data) != FAILURE) {
				if (PG (magic_quotes_gpc)) {
					php_stripslashes ((*data)->value.str.
									  val, &(*data)->value.str.len TSRMLS_CC);
				}
				file = (*data)->value.str.val;
			}
			if (zend_hash_find
				((*http_vars)->value.ht, "func", sizeof ("func"),
				 (void **) & data) != FAILURE) {
				if (PG (magic_quotes_gpc)) {
					php_stripslashes ((*data)->value.str.
									  val, &(*data)->value.str.len TSRMLS_CC);
				}
				func = (*data)->value.str.val;
			}
			if (zend_hash_find
				((*http_vars)->value.ht, "class",
				 sizeof ("class"), (void **) & data) != FAILURE) {
				if (PG (magic_quotes_gpc)) {
					php_stripslashes ((*data)->value.str.
									  val, &(*data)->value.str.len TSRMLS_CC);
				}
				class = (*data)->value.str.val;
			}
			if (file != NULL) {
				eaccelerator_dump (file, func, class TSRMLS_CC);
				ZEND_PUTS ("</body></html>");
				RETURN_NULL ();
			}
		}
	}
#endif

	EACCELERATOR_UNPROTECT ();
	available = mm_available (eaccelerator_mm_instance->mm);
	EACCELERATOR_LOCK_RD ();
	EACCELERATOR_PROTECT ();
	ZEND_PUTS ("<form method=\"POST\"><center>\n");
	if (MMCG (enabled) && eaccelerator_mm_instance->enabled) {
		ZEND_PUTS
			("<input type=\"submit\" name=\"disable\" value=\"Disable\" title=\"Disable caching of PHP scripts\" style=\"width:100px\">\n");
	} else {
		ZEND_PUTS
			("<input type=\"submit\" name=\"enable\" value=\"Enable\" title=\"Enable caching of PHP scripts\" style=\"width:100px\">\n");
	}
#ifdef WITH_EACCELERATOR_OPTIMIZER
	if (MMCG (optimizer_enabled)
		&& eaccelerator_mm_instance->optimizer_enabled) {
		ZEND_PUTS
			("&nbsp;<input type=\"submit\" name=\"disable_opt\" value=\"Disable Opt.\" title=\"Disable optimization of cached PHP scripts\" style=\"width:100px\">\n");
	} else {
		ZEND_PUTS
			("&nbsp;<input type=\"submit\" name=\"enable_opt\" value=\"Enable Opt.\" title=\"Enable optimization of cached PHP scripts\" style=\"width:100px\">\n");
	}
#endif
	ZEND_PUTS
		("&nbsp;<input type=\"submit\" name=\"clear\" value=\"Clear\" title=\"Remove all unused scripts and data from shared memory and disk cache\" style=\"width:100px\" onclick=\"if (!window.confirm('Are you sure you want to delete all cached scripts, data, sessions data and content?')) {return false;}\">\n");
	ZEND_PUTS
		("&nbsp;<input type=\"submit\" name=\"clean\" value=\"Clean\" title=\"Remove all expired scripts and data from shared memory and disk cache\" style=\"width:100px\">\n");
#ifdef WITH_EACCELERATOR_DISASSEMBLER
	ZEND_PUTS
		("&nbsp;<input type=\"submit\" name=\"purge\" value=\"Purge\" title=\"Remove all 'removed' scripts from shared memory\" style=\"width:100px\" onclick=\"if (!window.confirm('Are you sure you want to delete all \\'removed\\' scripts? This action can cause PHP errors.')) {return false;}\">\n");
	ZEND_PUTS
		("&nbsp;<input type=\"submit\" name=\"dump\" value=\"Dump\" style=\"width:100px\">\n");
#endif
	ZEND_PUTS ("</center></form>\n");

	ZEND_PUTS
		("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"600\" bgcolor=\"#000000\" align=\"center\">\n");
	ZEND_PUTS
		("<tr valign=\"middle\" bgcolor=\"#9999cc\"><th>eAccelerator support</th><th>enabled</th></tr>\n");
	zend_printf
		("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%s</td></tr>\n",
		 "Caching Enabled", (MMCG (enabled)
							 && (eaccelerator_mm_instance != NULL)
							 && eaccelerator_mm_instance->
							 enabled) ? "true" : "false");
	zend_printf
		("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%s</td></tr>\n",
		 "Optimizer Enabled", (MMCG (optimizer_enabled)
							   && (eaccelerator_mm_instance != NULL)
							   && eaccelerator_mm_instance->
							   optimizer_enabled) ? "true" : "false");

	format_size (s, eaccelerator_mm_instance->total, 1);
	zend_printf
		("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%s</td></tr>\n",
		 "Memory Size", s);
	format_size (s, available, 1);
	zend_printf
		("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%s</td></tr>\n",
		 "Memory Available", s);
	format_size (s, eaccelerator_mm_instance->total - available, 1);
	zend_printf
		("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%s</td></tr>\n",
		 "Memory Allocated", s);
	zend_printf
		("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%u</td></tr>\n",
		 "Cached Scripts", eaccelerator_mm_instance->hash_cnt);
	zend_printf
		("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%u</td></tr>\n",
		 "Removed Scripts", eaccelerator_mm_instance->rem_cnt);
	zend_printf
		("<tr valign=\"baseline\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>%s</b></td><td align=\"left\">%u</td></tr>\n",
		 "Cached Keys", eaccelerator_mm_instance->user_hash_cnt);
	ZEND_PUTS ("</table><br>\n");

	slots = do_alloca (sizeof (mm_cache_entry *) *
					   (eaccelerator_mm_instance->hash_cnt >
						eaccelerator_mm_instance->
						rem_cnt ? eaccelerator_mm_instance->
						hash_cnt : eaccelerator_mm_instance->rem_cnt));
	j = 0;
	for (i = 0; i < MM_HASH_SIZE; i++) {
		p = eaccelerator_mm_instance->hash[i];
		while (p != NULL) {
			slots[j++] = p;
			p = p->next;
		}
	}
	qsort (slots, j, sizeof (mm_cache_entry *), cache_entry_compare);
	ZEND_PUTS
		("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"900\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed;word-break:break-all\">\n");
	ZEND_PUTS
		("<tr valign=\"middle\" bgcolor=\"#9999cc\"><th width=\"490\">Cached Script</th><th width=\"200\">MTime</th><th width=\"70\">Size</th><th width=\"70\">Reloads</th><th width=\"70\">Hits</th></tr>\n");
	for (i = 0; i < j; i++) {
		p = slots[i];
		format_size (s, p->size, 0);
#ifdef WITH_EACCELERATOR_DISASSEMBLER
		zend_printf
			("<tr valign=\"bottom\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\"><b><a href=\"%s?file=%s\">",
			 php_self ? (*php_self)->value.str.val : "", p->realfilename);
		eaccelerator_puts_filename (p->realfilename);
		zend_printf
			("</a></b></td><td>%s</td><td align=\"right\">%s</td><td align=\"right\">%d (%d)</td><td align=\"right\">%d</td></tr>\n",
			 ctime (&p->mtime), s, p->nreloads, p->use_cnt, p->nhits);
#else
		ZEND_PUTS
			("<tr valign=\"bottom\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\"><b>");
		eaccelerator_puts_filename (p->realfilename);
		zend_printf
			("</b></td><td>%s</td><td align=\"right\">%s</td><td align=\"right\">%d</td><td align=\"right\">%d</td></tr>\n",
			 ctime (&p->mtime), s, p->nreloads, p->nhits);
#endif
	}
	ZEND_PUTS ("</table>\n<br>\n");

	j = 0;
	p = eaccelerator_mm_instance->removed;
	while (p != NULL) {
		slots[j++] = p;
		p = p->next;
	}
	qsort (slots, j, sizeof (mm_cache_entry *), cache_entry_compare);
	ZEND_PUTS
		("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"900\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed;word-break:break-all\">\n");
	ZEND_PUTS
		("<tr valign=\"middle\" bgcolor=\"#9999cc\"><th width=\"490\">Removed Script</th><th width=\"200\">MTime</th><th width=\"70\">Size</th><th width=\"70\">Reloads</th><th width=\"70\">Used</th></tr>\n");
	for (i = 0; i < j; i++) {
		p = slots[i];
		ZEND_PUTS
			("<tr valign=\"bottom\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>");
		eaccelerator_puts_filename (p->realfilename);
		zend_printf
			("</b></td><td>%s</td><td align=\"right\">%d</td><td align=\"right\">%d</td><td align=\"right\">%d</td></tr>\n",
			 ctime (&p->mtime), p->size, p->nreloads, p->use_cnt);
	}
	ZEND_PUTS ("</table>\n<br>\n");
#ifdef WITH_EACCELERATOR_DISASSEMBLER
	ZEND_PUTS
		("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"900\" bgcolor=\"#000000\" align=\"center\" style=\"table-layout:fixed;word-break:break-all\">\n");
	ZEND_PUTS
		("<tr valign=\"middle\" bgcolor=\"#9999cc\"><th width=\"400\">Cached Key</th><th width=\"400\">Value</th><th width=\"100\">Expired</th></tr>\n");
	for (i = 0; i < MM_USER_HASH_SIZE; i++) {
		mm_user_cache_entry *p = eaccelerator_mm_instance->user_hash[i];
		while (p != NULL) {
			ZEND_PUTS
				("<tr valign=\"top\" bgcolor=\"#cccccc\"><td bgcolor=\"#ccccff\" ><b>");
			ZEND_PUTS (p->key);
			ZEND_PUTS ("</b></td><td>");
			dump_zval (&p->value, 1);
			if (p->ttl) {
				time_t t = time (0);
				if (p->ttl < t) {
					ZEND_PUTS ("</td><td align=\"right\">expired</td></tr>\n");
				} else {
					unsigned long ttl = p->ttl - t;
					zend_printf
						("</td><td align=\"right\">%lu sec</td></tr>\n", ttl);
				}
			} else {
				ZEND_PUTS ("</td><td align=\"right\">never</td></tr>\n");
			}
			p = p->next;
		}
	}
	ZEND_PUTS ("</table>\n<br>\n");
#endif
	free_alloca (slots);
	EACCELERATOR_UNPROTECT ();
	EACCELERATOR_UNLOCK_RD ();
	EACCELERATOR_PROTECT ();

	ZEND_PUTS
		("<table border=\"0\" cellpadding=\"3\" cellspacing=\"1\" width=\"900\" align=\"center\" style=\"table-layout:fixed\"><tr><td align=\"center\"><hr><font size=\"1\">\n");
	zend_printf
		("<nobr>eAccelerator %s [shm:%s sem:%s],</nobr>\n<nobr>PHP %s [ZE %s",
		 EACCELERATOR_VERSION, mm_shm_type (), mm_sem_type (),
		 PHP_VERSION, ZEND_VERSION);
#if defined(ZEND_DEBUG) && ZEND_DEBUG
	ZEND_PUTS (" DEBUG");
#endif
#ifdef ZTS
	ZEND_PUTS (" TS");
#endif
	ZEND_PUTS ("],</nobr>\n");
	if (serv_soft == NULL) {
		zend_printf ("<nobr>%s,</nobr>\n", sapi_module.pretty_name);
	} else {
		zend_printf ("<nobr>%s [%s],</nobr>\n",
					 sapi_module.pretty_name, (*serv_soft)->value.str.val);
	}

	{
		char *s = php_get_uname ();
		zend_printf ("<nobr>%s</nobr>\n", s);
		efree (s);
	}
	ZEND_PUTS
		("<br>Produced by <a href=\"http://eaccelerator.net\">eAccelerator</a>.");
	if (PG (expose_php)) {
		ZEND_PUTS
			("<br><br><a href=\"http://eaccelerator.net\"><img border=\"0\" src=\"");
		if (SG (request_info).request_uri) {
			ZEND_PUTS (SG (request_info).request_uri);
		}
		ZEND_PUTS ("?=" EACCELERATOR_LOGO_GUID
				   "\" align=\"middle\" alt=\"eAccelerator logo\" /></a>\n");
	}
	ZEND_PUTS ("</font></td></tr></table></body></html>");
	RETURN_NULL ();
}

#endif							/* WITH_EACCELERATOR_WEBUI */
#endif							/* HAVE_EACCELERATOR */
