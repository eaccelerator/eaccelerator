/*???
#ifdef HAVE_SCHED_H
#  include <sched.h>
#endif
*/

typedef struct { volatile unsigned int lock;
                 volatile pid_t pid;
                 volatile int locked;
               } spinlock_t;

#define spinlock_init(rw)  do { (rw)->lock = 0x00000001; (rw)->pid=-1; (rw)->locked=0;} while(0)

#define spinlock_try_lock(rw)  asm volatile("lock ; decl %0" :"=m" ((rw)->lock) : : "memory")
#define _spinlock_unlock(rw)   asm volatile("lock ; incl %0" :"=m" ((rw)->lock) : : "memory")


/*???
#ifdef HAVE_SCHED_YIELD
#  define yield sched_yield
#else
*/
static inline void yield()
{
  struct timeval t;

  t.tv_sec = 0;
  t.tv_usec = 100;
  select(0, NULL, NULL, NULL, &t);
}
/*???
#endif
*/
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
