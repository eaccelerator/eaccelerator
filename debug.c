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
#include "debug.h"
#include <ctype.h>
#include <stdio.h>

#if defined(DEBUG) || defined(TEST_PERFORMANCE)  || defined(PROFILE_OPCODES)

extern FILE *F_fp;

void
binary_print (char *p, int len)
{
	while (len--)
	{
		fputc (*p++, F_fp);
	}
	fputc ('\n', F_fp);
}

void
log_hashkeys (char *p, HashTable * ht)
{
	Bucket *b;
	int i = 0;

	b = ht->pListHead;

	fputs (p, F_fp);
	while (b)
	{
		fprintf (F_fp, "[%d] ", i);
		binary_print (b->arKey, b->nKeyLength);

		b = b->pListNext;
		i++;
	}
}

void
pad (TSRMLS_D)
{
	int i = MMCG (xpad);
	while (i-- > 0)
	{
		fputc ('\t', F_fp);
	}
}

void
start_time (struct timeval *tvstart)
{
	gettimeofday (tvstart, NULL);
}

long
elapsed_time (struct timeval *tvstart)
{
	struct timeval tvend;
	int sec, usec;
	gettimeofday (&tvend, NULL);
	sec = tvend.tv_sec - tvstart->tv_sec;
	usec = tvend.tv_usec - tvstart->tv_usec;
	return sec * 1000000 + usec;
}
#endif /* #if defined(DEBUG) || defined(TEST_PERFORMANCE)  || defined(PROFILE_OPCODES) */

void
debug_printf (char *format, ...)
{
	char output_buf[512];
	va_list args;

	va_start (args, format);
	vsnprintf (output_buf, sizeof (output_buf), format, args);
	va_end (args);

#ifdef ZEND_WIN32
	OutputDebugString (output_buf);
/*  zend_printf("EACCELERATOR: %s<br>\n",output_buf);*/
#else
	fputs (output_buf, stderr);
#endif
}
