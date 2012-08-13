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
   $Id: debug.c 375 2010-01-19 15:49:13Z bart $
*/

#include "eaccelerator.h"

#ifdef HAVE_EACCELERATOR

#include "debug.h"
#include <ctype.h>
#include <stdio.h>

static FILE *F_fp = NULL;
static int file_no = 0;
long ea_debug = 0;

/**
 * Init the debug system. This must be called before any debug
 * functions are used.
 */
void ea_debug_init (TSRMLS_D)
{
    F_fp = fopen (EAG(ea_log_file), "a");
    if (!F_fp) {
        F_fp = stderr;
    }
    file_no = fileno(F_fp);
}

/**
 * Close the debug system.
 */
void ea_debug_shutdown ()
{
    fflush (F_fp);
    if (F_fp != stderr) {
        fclose (F_fp);
    }
    F_fp = NULL;
}

/**
 * Print a log message that will be print when the debug level is
 * equal to EA_LOG. This function is always called even if ea isn't
 * compiled with DEBUG and the log level is not equal to EA_LOG.
 */
void ea_debug_log (char *format, ...)
{
    if (ea_debug & EA_LOG) {
        char output_buf[512];
        va_list args;

        va_start (args, format);
        vsnprintf (output_buf, sizeof (output_buf), format, args);
        va_end (args);

        if (F_fp != stderr) {
            EACCELERATOR_FLOCK(file_no, LOCK_EX);
        }
        fputs (output_buf, F_fp);
        fflush (F_fp);
        if (F_fp != stderr) {
            EACCELERATOR_FLOCK(file_no, LOCK_UN);
        }
    }
}

/**
 * Output an error message to stderr. This message are always printed
 * no matter what log level is used.
 */
void ea_debug_error (char *format, ...)
{
    char output_buf[512];
    va_list args;

    va_start (args, format);
    vsnprintf (output_buf, sizeof (output_buf), format, args);
    va_end (args);

    fputs (output_buf, stderr);
    fflush (stderr);
}

/*
 * All these functions aren't compiled when eA isn't compiled with DEBUG. They
 * are replaced with function with no body, so it's optimized away by the compiler.
 * Even if the debug level is ok.
 */

/**
 * Print a debug message
 */
void ea_debug_printf (long debug_level, char *format, ...)
{
    if (ea_debug & debug_level) {
        char output_buf[512];
        va_list args;

        va_start (args, format);
        vsnprintf (output_buf, sizeof (output_buf), format, args);
        va_end (args);

        if (F_fp != stderr) {
            EACCELERATOR_FLOCK(file_no, LOCK_EX);
        }
        fputs (output_buf, F_fp);
        fflush (F_fp);
        if (F_fp != stderr) {
            EACCELERATOR_FLOCK(file_no, LOCK_UN);
        }
    }
}

/**
 * Put a debug message
 */
void ea_debug_put (long debug_level, char *message)
{
    if (debug_level & ea_debug) {
        if (F_fp != stderr) {
            EACCELERATOR_FLOCK(file_no, LOCK_EX);
        }
        fputs (message, F_fp);
        fflush (F_fp);
        if (F_fp != stderr) {
            EACCELERATOR_FLOCK(file_no, LOCK_UN);
        }
    }
}

/**
 * Print a binary message
 */
void ea_debug_binary_print (long debug_level, const char *p, int len)
{
    if (ea_debug & debug_level) {
        if (F_fp != stderr) {
            EACCELERATOR_FLOCK(file_no, LOCK_EX);
        }
        while (len--) {
            if (*p == 0) {
                fputs ("\\0", F_fp);
            } else {
                fputc (*p, F_fp);
            }
            p++;
        }
        fputc ('\n', F_fp);
        fflush (F_fp);
        if (F_fp != stderr) {
            EACCELERATOR_FLOCK(file_no, LOCK_UN);
        }
    }
}

/**
 * Log a hashkey
 */
void ea_debug_log_hashkeys (char *p, HashTable * ht)
{
    if (ea_debug & EA_LOG_HASHKEYS) {
        Bucket *b;
        int i = 0;

        b = ht->pListHead;

        if (F_fp != stderr) {
            EACCELERATOR_FLOCK(file_no, LOCK_EX);
        }
        fputs(p, F_fp);
        fflush(F_fp);
        while (b) {
            fprintf (F_fp, "[%d] ", i);
            ea_debug_binary_print (EA_LOG_HASHKEYS, b->arKey, b->nKeyLength);

            b = b->pListNext;
            i++;
        }
        if (F_fp != stderr) {
            EACCELERATOR_FLOCK(file_no, LOCK_UN);
        }
    }
}

/**
 * Pad the message with the current pad level.
 */
void ea_debug_pad (long debug_level TSRMLS_DC)
{
#ifdef DEBUG /* This ifdef is still req'd because xpad is N/A in a non-debug compile */
    if (ea_debug & debug_level) {
        int i;
        if (F_fp != stderr) {
            EACCELERATOR_FLOCK(file_no, LOCK_EX);
        }
        i = EAG (xpad);
        while (i-- > 0) {
            fputc ('\t', F_fp);
        }
        if (F_fp != stderr) {
            EACCELERATOR_FLOCK(file_no, LOCK_UN);
        }
    }
#endif
}

void ea_debug_start_time (struct timeval *tvstart)
{
    gettimeofday (tvstart, NULL);
}

long ea_debug_elapsed_time (struct timeval *tvstart)
{
    struct timeval tvend;
    int sec, usec;
    gettimeofday (&tvend, NULL);
    sec = tvend.tv_sec - tvstart->tv_sec;
    usec = tvend.tv_usec - tvstart->tv_usec;
    return sec * 1000000 + usec;
}

/*
 * This dumps a HashTable to debug output. Taken from zend_hash.c and slightly adapted.
 */
void ea_debug_hash_display(HashTable * ht)
{
    Bucket *p;
    uint i;

    fprintf(F_fp, "ht->nTableSize: %d\n", ht->nTableSize);
    fprintf(F_fp, "ht->nNumOfElements: %d\n", ht->nNumOfElements);

    for (i = 0; i < ht->nTableSize; i++) {
        p = ht->arBuckets[i];
        while (p != NULL) {
            fprintf(F_fp, "\t%s <==> 0x%lX\n", p->arKey, p->h);
            p = p->pNext;
        }
    }

    fflush(F_fp);
}

/*
 *  Dump an eaccelerator class entry structure
 */
void ea_debug_dump_ea_class_entry(ea_class_entry *ce)
{
    fprintf(F_fp, "ea class entry: '%s' (len = %u)\n", ce->name, ce->name_length);
    fprintf(F_fp, "\tparent: '%s'\n", ce->parent);
    fprintf(F_fp, "\ttype: %d\n", ce->type);
    fprintf(F_fp, "\tfunction_table: %u entries\n", ce->function_table.nNumOfElements);
    fprintf(F_fp, "\tproperties_info: %u entries\n", ce->properties_info.nNumOfElements);
#ifdef ZEND_ENGINE_2_4
    fprintf(F_fp, "\tdefault_properties: %u entries\n", ce->default_properties_count);
    fprintf(F_fp, "\tdefault_static_members: %u entries\n", ce->default_static_members_count);
#else
    fprintf(F_fp, "\tdefault_properties: %u entries\n", ce->default_properties.nNumOfElements);
    fprintf(F_fp, "\tdefault_static_members: %u entries\n", ce->default_static_members.nNumOfElements);
    fprintf(F_fp, "\tstatic_members: %u entries\n", ce->static_members->nNumOfElements);
#endif
    fprintf(F_fp, "\tconstants_Table: %u entries\n", ce->constants_table.nNumOfElements);
    fprintf(F_fp, "\tce_flags: %u\n", ce->ce_flags);
    fprintf(F_fp, "\tnum_interfaces: %u\n", ce->num_interfaces);
    fprintf(F_fp, "\tfilename: %s\n", ce->filename);
    fprintf(F_fp, "\tline_start: %u\n", ce->line_start);
    fprintf(F_fp, "\tline_end: %u\n", ce->line_end);
#  ifdef INCLUDE_DOC_COMMENTS
    fprintf(F_fp, "\tdoc_comment: %s\n", ce->doc_comment);
    fprintf(F_fp, "\tdoc_comment_len: %u\n", ce->doc_comment_len);
#  endif
    fflush(F_fp);
}

/*
 *  Dump a zend class entry structure
 */
void ea_debug_dump_zend_class_entry(zend_class_entry *ce)
{
    fprintf(F_fp, "zend class entry: '%s' (len = %u)\n", ce->name, ce->name_length);
    fprintf(F_fp, "\tparent: '%s'\n", (ce->parent == NULL) ? "none" : ce->parent->name);
    fprintf(F_fp, "\ttype: %d\n", ce->type);
    fprintf(F_fp, "\tfunction_table: %u entries\n", ce->function_table.nNumOfElements);
    fprintf(F_fp, "\tproperties_info: %u entries\n", ce->properties_info.nNumOfElements);
#  ifdef ZEND_ENGINE_2_4
    fprintf(F_fp, "\tdefault_properties: %u entries\n", ce->default_properties_count);
    fprintf(F_fp, "\tdefault_static_members: %u entries\n", ce->default_static_members_count);
#  else
    fprintf(F_fp, "\tdefault_properties: %u entries\n", ce->default_properties.nNumOfElements);
    fprintf(F_fp, "\tdefault_static_members: %u entries\n", ce->default_static_members.nNumOfElements);
    fprintf(F_fp, "\tstatic_members: %u entries\n", ce->static_members->nNumOfElements);
#  endif
    fprintf(F_fp, "\tconstants_Table: %u entries\n", ce->constants_table.nNumOfElements);
    fprintf(F_fp, "\tce_flags: %u\n", ce->ce_flags);
    fprintf(F_fp, "\tnum_interfaces: %u\n", ce->num_interfaces);
#  ifdef ZEND_ENGINE_2_4
    fprintf(F_fp, "\tfilename: %s\n", ce->info.user.filename);
    fprintf(F_fp, "\tline_start: %u\n", ce->info.user.line_start);
    fprintf(F_fp, "\tline_end: %u\n", ce->info.user.line_end);
#  else
    fprintf(F_fp, "\tfilename: %s\n", ce->filename);
    fprintf(F_fp, "\tline_start: %u\n", ce->line_start);
    fprintf(F_fp, "\tline_end: %u\n", ce->line_end);
#  endif
#  ifdef INCLUDE_DOC_COMMENTS
#    ifdef ZEND_ENGINE_2_4
    fprintf(F_fp, "\tdoc_comment: %s\n", ce->info.user.doc_comment);
    fprintf(F_fp, "\tdoc_comment_len: %u\n", ce->info.user.doc_comment_len);
#    else
    fprintf(F_fp, "\tdoc_comment: %s\n", ce->doc_comment);
    fprintf(F_fp, "\tdoc_comment_len: %u\n", ce->doc_comment_len);
#    endif
#  endif
    fflush(F_fp);
}

#endif /* #ifdef HAVE_EACCELERATOR */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim>600: expandtab sw=4 ts=4 sts=4 fdm=marker
 * vim<600: expandtab sw=4 ts=4 sts=4
 */
