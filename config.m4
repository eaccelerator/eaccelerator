AC_DEFUN([EA_REMOVE_IPC_TEST], [
  # for cygwin ipc error
  if test -f conftest* ; then
    echo $ECHO_N "Wait for conftest* to exit$ECHO_C"
    while ! rm -f conftest* 2>/dev/null ; do
      echo $ECHO_N ".$ECHO_C"
      sleep 1
    done
    echo
  fi
])

AC_ARG_WITH(eaccelerator,[],[enable_eaccelerator=$withval])

PHP_ARG_ENABLE(eaccelerator, whether to enable eaccelerator support,
[  --enable-eaccelerator                    Enable eaccelerator support])

AC_ARG_WITH(eaccelerator-crash-detection,
[  --without-eaccelerator-crash-detection   Do not include eaccelerator crash detection],[
  eaccelerator_crash_detection=$withval
],[
  eaccelerator_crash_detection=yes
])

AC_ARG_WITH(eaccelerator-optimizer,
[  --without-eaccelerator-optimizer         Do not include eaccelerator optimizer],[
  eaccelerator_optimizer=$withval
],[
  eaccelerator_optimizer=yes
])

AC_ARG_WITH(eaccelerator-encoder,
[  --without-eaccelerator-encoder           Do not include eaccelerator encoder],[
  eaccelerator_encoder=$withval
],[
  eaccelerator_encoder=yes
])

AC_ARG_WITH(eaccelerator-loader,
[  --without-eaccelerator-loader            Do not include eaccelerator loader],[
  eaccelerator_loader=$withval
],[
  eaccelerator_loader=yes
])

AC_ARG_WITH(eaccelerator-shared-memory,
[  --with-eaccelerator-shared-memory        Include eaccelerator shared memory functions],[
  eaccelerator_shm=$withval
],[
  eaccelerator_shm=no
])

AC_ARG_WITH(eaccelerator-sessions,
[  --with-eaccelerator-sessions             Include eaccelerator sessions],[
  eaccelerator_sessions=$withval
],[
  eaccelerator_sessions=no
])

AC_ARG_WITH(eaccelerator-content-caching,
[  --with-eaccelerator-content-caching      Include eaccelerator content caching],[
  eaccelerator_content_caching=$withval
],[
  eaccelerator_content_caching=no
])

AC_ARG_WITH(eaccelerator-info,
[  --without-eaccelerator-info              Do not compile the eAccelerator information functions],[
  eaccelerator_info=$withval
],[
  eaccelerator_info=yes
])

AC_ARG_WITH(eaccelerator-disassembler,
[  --with-eaccelerator-disassembler         Include disassembler],[
  eaccelerator_disassembler=$withval
],[
  eaccelerator_disassemmbler=no
])

AC_ARG_WITH(eaccelerator-use-inode,
[  --without-eaccelerator-use-inode         Don't use inodes to determine hash keys (never used on win32)],[
  eaccelerator_inode=$withval
],[
  eaccelerator_inode=yes
])

AC_ARG_WITH(eaccelerator-debug,
[  --with-eaccelerator-debug                Enable the debug code so eaccelerator logs verbose.],[
  eaccelerator_debug=$withval
],[
  eaccelerator_debug=no
])

AC_ARG_WITH(eaccelerator-userid,
[  --with-eaccelerator-userid               eAccelerator runs under this userid, only needed when using sysvipc semaphores.],[
  ea_userid=$withval
],[
  ea_userid=0
])

AC_ARG_WITH(eaccelerator-doc-comment-inclusion,
[  --with-eaccelerator-doc-comment-inclusion  If you want eAccelerator to retain doc-comments in  internal php structures.],[
    enable_doc_comment_inclusion=$withval
],[
    enable_doc_comment_inclusion=no
])

dnl PHP_BUILD_SHARED
if test "$PHP_EACCELERATOR" != "no"; then
  PHP_EXTENSION(eaccelerator, $ext_shared)
  AC_DEFINE(HAVE_EACCELERATOR, 1, [Define if you like to use eAccelerator])

  AC_DEFINE(WITH_EACCELERATOR_INFO, 1, [Define to be able to get information about eAccelerator])

  AC_DEFINE_UNQUOTED(EA_USERID, $ea_userid, [The userid eAccelerator will be running under.]) 

  if test "$enable_doc_comment_inclusion" = "yes"; then
    AC_DEFINE(INCLUDE_DOC_COMMENTS, 1, [If you want eAccelerator to retain doc-comments in internal php structures (meta-programming)])
  fi
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
  if test "$eaccelerator_info" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_INFO, 1, [Define if you want the information functions])
  fi
  if test "$eaccelerator_sessions" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_SESSIONS, 1, [Define if you like to use eAccelerator session handlers to store session's information in shared memory])
  fi
  if test "$eaccelerator_content_caching" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_CONTENT_CACHING, 1, [Define if you like to use eAccelerator content cachin API])
    AC_DEFINE(WITH_EACCELERATOR_SHM, 1, [Define if you like to use the eAccelerator functions to store keys in shared memory])
  fi
  if test "$eaccelerator_disassembler" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_DISASSEMBLER, 1, [Define if you like to explore Zend bytecode])
  fi
  if test "$eaccelerator_inode" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_USE_INODE, 1, [Undef if you don't wan't to use inodes to determine hash keys])
  fi
  if test "$eaccelerator_debug" = "yes"; then
    AC_DEFINE(DEBUG, 1, [Undef when you want to enable eaccelerator debug code])
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
#include "$ext_srcdir/mm.c"
],dnl
    mm_shm_ipc=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])
  EA_REMOVE_IPC_TEST()

  AC_MSG_CHECKING(for mmap shared memory support)
  AC_TRY_RUN([#define MM_SEM_NONE
#define MM_SHM_MMAP_FILE
#define MM_TEST_SHM
#include "$ext_srcdir/mm.c"
],dnl
    mm_shm_mmap_file=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for mmap on /dev/zero shared memory support)
  AC_TRY_RUN([#define MM_SEM_NONE
#define MM_SHM_MMAP_ZERO
#define MM_TEST_SHM
#include "$ext_srcdir/mm.c"
],dnl
    mm_shm_mmap_zero=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for anonymous mmap shared memory support)
  AC_TRY_RUN([#define MM_SEM_NONE
#define MM_SHM_MMAP_ANON
#define MM_TEST_SHM
#include "$ext_srcdir/mm.c"
],dnl
    mm_shm_mmap_anon=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for posix mmap shared memory support)
  AC_TRY_RUN([#define MM_SEM_NONE
#define MM_SHM_MMAP_POSIX
#define MM_TEST_SHM
#include "$ext_srcdir/mm.c"
],dnl
    mm_shm_mmap_posix=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for best shared memory type)
  if test "$mm_shm_mmap_anon" = "yes"; then
    AC_DEFINE(MM_SHM_MMAP_ANON, 1, [Define if you like to use anonymous mmap based shared memory])
    msg="anonymous mmap"
  elif test "$mm_shm_ipc" = "yes"; then
    AC_DEFINE(MM_SHM_IPC, 1, [Define if you like to use sysvipc based shared memory])
    msg="sysvipc"
  elif test "$mm_shm_mmap_zero" = "yes"; then
    AC_DEFINE(MM_SHM_MMAP_ZERO, 1, [Define if you like to use mmap on /dev/zero based shared memory])
    msg="mmap on /dev/zero"
  elif test "$mm_shm_mmap_posix" = "yes"; then
    AC_DEFINE(MM_SHM_MMAP_POSIX, 1, [Define if you like to use posix mmap based shared memory])
    msg="posix mmap"
  elif test "$mm_shm_mmap_file" = "yes"; then
    AC_DEFINE(MM_SHM_MMAP_FILE, 1, [Define if you like to use mmap on temporary file shared memory])
    msg="mmap"
  else
    msg="no"
  fi
  AC_MSG_RESULT([$msg])
  if test "$msg" = "no" ; then
    AC_MSG_WARN([eaccelerator cannot detect shared memory type, which is required])
  fi

  AC_MSG_CHECKING(for spinlock semaphores support)
  AC_TRY_RUN([#define MM_SEM_SPINLOCK
#define MM_TEST_SEM
#include "$ext_srcdir/mm.c"
],dnl
    mm_sem_spinlock=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for pthread semaphores support)
  AC_TRY_RUN([#define MM_SEM_PTHREAD
#define MM_TEST_SEM
#include "$ext_srcdir/mm.c"
],dnl
    mm_sem_pthread=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for posix semaphores support)
  AC_TRY_RUN([#define MM_SEM_POSIX
#define MM_TEST_SEM
#include "$ext_srcdir/mm.c"
],dnl
    mm_sem_posix=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for sysvipc semaphores support)
  AC_TRY_RUN([#define MM_SEM_IPC
#define MM_TEST_SEM
#include "$ext_srcdir/mm.c"
],dnl
    mm_sem_ipc=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])
  EA_REMOVE_IPC_TEST()

  AC_MSG_CHECKING(for fcntl semaphores support)
  AC_TRY_RUN([#define MM_SEM_FCNTL
#define MM_TEST_SEM
#include "$ext_srcdir/mm.c"
],dnl
    mm_sem_fcntl=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for flock semaphores support)
  AC_TRY_RUN([#define MM_SEM_FLOCK
#define MM_TEST_SEM
#include "$ext_srcdir/mm.c"
],dnl
    mm_sem_flock=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

  AC_MSG_CHECKING(for best semaphores type)
  if test "$mm_sem_spinlock" = "yes"; then
    AC_DEFINE(MM_SEM_SPINLOCK, 1, [Define if you like to use spinlock based semaphores])
    msg="spinlock"
  elif test "$mm_sem_ipc" = "yes"; then
    if test $ea_userid = 0; then
        AC_MSG_ERROR("You need to pass the user id eaccelerator will be running under when using sysvipc semaphores")
    else
        AC_DEFINE(MM_SEM_IPC, 1, [Define if you like to use sysvipc based semaphores])
        msg="sysvipc"
    fi
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
  else
    msg="no"
  fi
  AC_MSG_RESULT([$msg])
  if test "$msg" = "no" ; then
    AC_MSG_WARN([eaccelerator cannot semaphores type, which is required])
  fi

  AC_CHECK_FUNC(sched_yield,[
      AC_DEFINE(HAVE_SCHED_YIELD, 1, [Define if ou have sched_yield function])
    ])

  AC_CHECK_FUNC(mprotect,[
      AC_DEFINE(HAVE_MPROTECT, 1, [Define if ou have mprotect function])
    ])

  old_cppflags="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $INCLUDES -I$abs_srcdir"
  AC_MSG_CHECKING(for ext/session/php_session.h)
  AC_TRY_CPP([#include "ext/session/php_session.h"],msg="yes",msg="no")
  if test "$msg" = "yes"; then
    AC_DEFINE(HAVE_EXT_SESSION_PHP_SESSION_H, 1, [Define if you have the <ext/session/php_session.h> header file.])
  fi
  AC_MSG_RESULT([$msg])
  CPPFLAGS="$old_cppflags"

fi
