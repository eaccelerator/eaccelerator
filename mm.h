/*
   +----------------------------------------------------------------------+
   | eAccelerator project                                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) 2004 - 2010 eAccelerator                               |
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
   $Id$
*/

/* libmm replacement */

#ifndef INCLUDED_MM_H
#define INCLUDED_MM_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MM_PRIVATE
#  ifdef MM
#    undef MM
#  endif
#  define MM void
#endif

#define MM_LOCK_RW 1
#define MM_LOCK_RD 0

MM*    _mm_create(size_t size, const char* key);
void   _mm_set_attach(MM* mm, void* attach_addr);
void*  _mm_attach(size_t size, const char* key);
size_t _mm_size(MM* mm);
void   _mm_destroy(MM* mm);
int    _mm_lock(MM* mm, int kind);
int    _mm_unlock(MM* mm);
size_t _mm_available(MM* mm);
size_t _mm_maxsize(MM* mm);
void*  _mm_malloc_lock(MM* mm, size_t size);
void   _mm_free_lock(MM* mm, void* p);
void*  _mm_malloc_nolock(MM* mm, size_t size);
void   _mm_free_nolock(MM* mm, void* p);
size_t _mm_sizeof(MM* mm, void* x);


const char* mm_shm_type();
const char* mm_sem_type();
void mm_check_mem(void *x);

#define MM_PROT_NONE  1
#define MM_PROT_READ  2
#define MM_PROT_WRITE 4
#define MM_PROT_EXEC  8

int mm_protect(MM* mm, int mode);

#define mm_create(A, B)        _mm_create(A, B)
#define mm_set_attach(A, B)    _mm_set_attach(A, B)
#define mm_attach(A, B)        _mm_attach(A, B)
#define mm_size(A)             _mm_size(A)
#define mm_destroy(A)          _mm_destroy(A)
#define mm_lock(A, B)          _mm_lock(A, B)
#define mm_unlock(A)           _mm_unlock(A)
#define mm_available(A)        _mm_available(A)
#define mm_maxsize(A)          _mm_maxsize(A)
#define mm_malloc_lock(A, B)   _mm_malloc_lock(A, B)
#define mm_free_lock(A, B)     _mm_free_lock(A, B)
#define mm_malloc_nolock(A, B) _mm_malloc_nolock(A, B)
#define mm_free_nolock(A, B)   _mm_free_nolock(A, B)
#define mm_sizeof(A, B)        _mm_sizeof(A, B)

#ifdef __cplusplus
}
#endif

#endif
