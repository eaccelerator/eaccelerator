AC_ARG_WITH(eaccelerator,[],[enable_eaccelerator=$withval])

PHP_ARG_ENABLE(eaccelerator, whether to enable eaccelerator support,
[  --enable-eaccelerator        Enable eaccelerator support])

AC_ARG_WITH(eaccelerator-crash-detection,
[  --without-eaccelerator-crash-detection  Do not include eaccelerator crash detection],[
  eaccelerator_crash_detection=$withval
],[
  eaccelerator_crash_detection=yes
])

AC_ARG_WITH(eaccelerator-optimizer,
[  --without-eaccelerator-optimizer        Do not include eaccelerator optimizer],[
  eaccelerator_optimizer=$withval
],[
  eaccelerator_optimizer=yes
])

AC_ARG_WITH(eaccelerator-encoder,
[  --without-eaccelerator-encoder          Do not include eaccelerator encoder],[
  eaccelerator_encoder=$withval
],[
  eaccelerator_encoder=yes
])

AC_ARG_WITH(eaccelerator-loader,
[  --without-eaccelerator-loader           Do not include eaccelerator loader],[
  eaccelerator_loader=$withval
],[
  eaccelerator_loader=yes
])

AC_ARG_WITH(eaccelerator-shared-memory,
[  --without-eaccelerator-shared-memory	   Do not include eaccelerator shared memory functions],[
  eaccelerator_shm=$withval
],[
  eaccelerator_shm=yes
])

AC_ARG_WITH(eaccelerator-webui,
[  --without-eaccelerator-webui        Do not include the eaccelerator WebUI],[
  eaccelerator_webui=$withval
],[
  eaccelerator_webui=yes
])

AC_ARG_WITH(eaccelerator-sessions,
[  --without-eaccelerator-sessions         Do not include eaccelerator sessions],[
  eaccelerator_sessions=$withval
],[
  eaccelerator_sessions=yes
])

AC_ARG_WITH(eaccelerator-content-caching,
[  --without-eaccelerator-content-caching  Do not include eaccelerator content caching],[
  eaccelerator_content_caching=$withval
],[
  eaccelerator_content_caching=yes
])

AC_ARG_WITH(eaccelerator-disassembler,
[  --with-eaccelerator-disassembler        Include disassembler],[
  eaccelerator_disassembler=$withval
],[
  eaccelerator_disassemmbler=no
])

AC_ARG_WITH(eaccelerator-executor,
[  --with-eaccelerator-executor            Include optimized executor (not implemented yet)],[
  eaccelerator_executor=$withval
],[
  eaccelerator_executor=no
])

dnl PHP_BUILD_SHARED
if test "$PHP_EACCELERATOR" != "no"; then
  PHP_EXTENSION(eaccelerator, $ext_shared)
  AC_DEFINE(HAVE_EACCELERATOR, 1, [Define if you like to use eAccelerator])

  if test "$eaccelerator_crash_detection" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_CRASH_DETECTION, 1, [Define if you like to release eAccelerator resources on PHP crash])
  fi
  if test "$eaccelerator_optimizer" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_OPTIMIZER, 1, [Define if you like to use peephole opcode optimization])
  fi
  if test "$eaccelerator_encoder" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_ENCODER, 1, [Define if you like to use eAccelerator enoder])
  fi
  if test "$eaccelerator_loader" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_LOADER, 1, [Define if you like to load files encoded by eAccelerator encoder])
  fi
  if test "$eaccelerator_shm" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_SHM, 1, [Define if you like to use the eAccelerator functions to store keys in shared memory])
  fi
  if test "$eaccelerator_webui" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_WEBUI, 1, [Define if you like to use the eAccelerator WebUI])
  fi
  if test "$eaccelerator_sessions" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_SESSIONS, 1, [Define if you like to use eAccelerator session handlers to store session's information in shared memory])
  fi
  if test "$eaccelerator_content_caching" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_CONTENT_CACHING, 1, [Define if you like to use eAccelerator content cachin API])
  fi
  if test "$eaccelerator_disassembler" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_DISASSEMBLER, 1, [Define if you like to explore Zend bytecode])
  fi
  if test "$eaccelerator_executor" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_EXECUTOR, 1, [Define if you like use optimized executor (not implemented yet)])
  fi

  AC_REQUIRE_CPP()

  AC_HAVE_HEADERS(unistd.h limits.h sys/param.h sched.h)

  AC_MSG_CHECKING(mandatory system headers)
  AC_TRY_CPP([#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>],msg=yes,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(whether union semun is defined in sys/sem.h)
  AC_TRY_COMPILE([
  #include <sys/types.h>
  #include <sys/ipc.h>
  #include <sys/sem.h>
  ],[
  union semun arg;
  semctl(0, 0, 0, arg);
  ],
  AC_DEFINE(HAVE_UNION_SEMUN, 1, [Define if you have semun union in sys/sem.h])
  msg=yes,msg=no)
  AC_MSG_RESULT([$msg])

  mm_shm_ipc=no
  mm_shm_mmap_anon=no
  mm_shm_mmap_zero=no
  mm_shm_mmap_file=no
  mm_shm_mmap_posix=no

  AC_MSG_CHECKING(for sysvipc shared memory support)
  AC_TRY_RUN([#define MM_SEM_NONE
#define MM_SHM_IPC
#define MM_TEST_SHM
#include "mm.c"
],dnl
    mm_shm_ipc=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for mmap shared memory support)
  AC_TRY_RUN([#define MM_SEM_NONE
#define MM_SHM_MMAP_FILE
#define MM_TEST_SHM
#include "mm.c"
],dnl
    mm_shm_mmap_file=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for mmap on /dev/zero shared memory support)
  AC_TRY_RUN([#define MM_SEM_NONE
#define MM_SHM_MMAP_ZERO
#define MM_TEST_SHM
#include "mm.c"
],dnl
    mm_shm_mmap_zero=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for anonymous mmap shared memory support)
  AC_TRY_RUN([#define MM_SEM_NONE
#define MM_SHM_MMAP_ANON
#define MM_TEST_SHM
#include "mm.c"
],dnl
    mm_shm_mmap_anon=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for posix mmap shared memory support)
  AC_TRY_RUN([#define MM_SEM_NONE
#define MM_SHM_MMAP_POSIX
#define MM_TEST_SHM
#include "mm.c"
],dnl
    mm_shm_mmap_posix=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for best shared memory type)
  if test "$mm_shm_ipc" = "yes"; then
    AC_DEFINE(MM_SHM_IPC, 1, [Define if you like to use sysvipc based shared memory])
    msg="sysvipc"
  elif test "$mm_shm_mmap_anon" = "yes"; then
    AC_DEFINE(MM_SHM_MMAP_ANON, 1, [Define if you like to use anonymous mmap based shared memory])
    msg="anonymous mmap"
  elif test "$mm_shm_mmap_zero" = "yes"; then
    AC_DEFINE(MM_SHM_MMAP_ZERO, 1, [Define if you like to use mmap on /dev/zero based shared memory])
    msg="mmap on /dev/zero"
  elif test "$mm_shm_mmap_posix" = "yes"; then
    AC_DEFINE(MM_SHM_MMAP_POSIX, 1, [Define if you like to use posix mmap based shared memory])
    msg="posix mmap"
  elif test "$mm_shm_mmap_file" = "yes"; then
    AC_DEFINE(MM_SHM_MMAP_FILE, 1, [Define if you like to use mmap on temporary file shared memory])
    msg="mmap"
  fi
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for spinlock semaphores support)
  AC_TRY_RUN([#define MM_SEM_SPINLOCK
#define MM_TEST_SEM
#include "mm.c"
],dnl
    mm_sem_spinlock=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for pthread semaphores support)
  AC_TRY_RUN([#define MM_SEM_PTHREAD
#define MM_TEST_SEM
#include "mm.c"
],dnl
    mm_sem_pthread=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for posix semaphores support)
  AC_TRY_RUN([#define MM_SEM_POSIX
#define MM_TEST_SEM
#include "mm.c"
],dnl
    mm_sem_posix=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for sysvipc semaphores support)
  AC_TRY_RUN([#define MM_SEM_IPC
#define MM_TEST_SEM
#include "mm.c"
],dnl
    mm_sem_ipc=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for fcntl semaphores support)
  AC_TRY_RUN([#define MM_SEM_FCNTL
#define MM_TEST_SEM
#include "mm.c"
],dnl
    mm_sem_fcntl=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for flock semaphores support)
  AC_TRY_RUN([#define MM_SEM_FLOCK
#define MM_TEST_SEM
#include "mm.c"
],dnl
    mm_sem_flock=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for best semaphores type)
  if test "$mm_sem_spinlock" = "yes"; then
    AC_DEFINE(MM_SEM_SPINLOCK, 1, [Define if you like to use spinlock based semaphores])
    msg="spinlock"
  elif test "$mm_sem_ipc" = "yes"; then
    AC_DEFINE(MM_SEM_IPC, 1, [Define if you like to use sysvipc based semaphores])
    msg="sysvipc"
  elif test "$mm_sem_fcntl" = "yes"; then
    AC_DEFINE(MM_SEM_FCNTL, 1, [Define if you like to use fcntl based semaphores])
    msg="fcntl"
  elif test "$mm_sem_flock" = "yes"; then
    AC_DEFINE(MM_SEM_FLOCK, 1, [Define if you like to use flock based semaphores])
    msg="flock"
  elif test "$mm_sem_pthread" = "yes"; then
    AC_DEFINE(MM_SEM_PTHREAD, 1, [Define if you like to use pthread based semaphores])
    msg="pthread"
  elif test "$mm_sem_posix" = "yes"; then
    AC_DEFINE(MM_SEM_POSIX, 1, [Define if you like to use posix based semaphores])
    msg="posix"
  fi
  AC_MSG_RESULT([$msg])

  AC_CHECK_FUNC(sched_yield,[
      AC_DEFINE(HAVE_SCHED_YIELD, 1, [Define if ou have sched_yield function])
    ])

  AC_CHECK_FUNC(mprotect,[
      AC_DEFINE(HAVE_MPROTECT, 1, [Define if ou have mprotect function])
    ])

  old_cppflags="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $INCLUDES"
  AC_MSG_CHECKING(for ext/session/php_session.h)
  AC_TRY_CPP([#include "ext/session/php_session.h"],msg="yes",msg="no")
  if test "$msg" = "yes"; then
    AC_DEFINE(HAVE_EXT_SESSION_PHP_SESSION_H, 1, [Define if you have the <ext/session/php_session.h> header file.])
  fi
  AC_MSG_RESULT([$msg])
  CPPFLAGS="$old_cppflags"

fi
