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
   $Id: x86_spinlocks.h 178 2006-03-06 09:08:40Z bart $
*/

typedef struct { volatile unsigned int lock;
                 volatile pid_t pid;
                 volatile int locked;
               } spinlock_t;

#define spinlock_init(rw)  do { (rw)->lock = 0x00000001; (rw)->pid=-1; (rw)->locked=0;} while(0)

#define spinlock_try_lock(rw)  asm volatile("lock ; decl %0" :"=m" ((rw)->lock) : : "memory")
#define _spinlock_unlock(rw)   asm volatile("lock ; incl %0" :"=m" ((rw)->lock) : : "memory")

static inline void yield()
{
  struct timeval t;

  t.tv_sec = 0;
  t.tv_usec = 100;
  select(0, NULL, NULL, NULL, &t);
}

static inline void spinlock_unlock(spinlock_t* rw) {
  if (rw->locked && (rw->pid == getpid())) {
    rw->pid = 0;
    rw->locked = 0;
    _spinlock_unlock(rw);
  }
}

static inline void spinlock_lock(spinlock_t* rw)
{
  while (1) {
    spinlock_try_lock(rw);
    if (rw->lock == 0) {
      rw->pid = getpid();
      rw->locked = 1;
      return;
    }
    _spinlock_unlock(rw);
    yield();
  }
}

