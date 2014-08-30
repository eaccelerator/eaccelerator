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

dnl 
dnl configure options for eAccelerator
dnl 
AC_ARG_WITH(eaccelerator,[],[enable_eaccelerator=$withval])

PHP_ARG_ENABLE(eaccelerator, whether to enable eaccelerator support,
[  --enable-eaccelerator                    Enable eaccelerator support])

AC_ARG_WITH(eaccelerator-crash-detection,
[  --without-eaccelerator-crash-detection   Do not include eAccelerator crash detection],[
  eaccelerator_crash_detection=$withval
],[
  eaccelerator_crash_detection=yes
])

AC_ARG_WITH(eaccelerator-info,
[  --without-eaccelerator-info              Do not compile the eAccelerator information functions],[
  eaccelerator_info=$withval
],[
  eaccelerator_info=yes
])

AC_ARG_WITH(eaccelerator-doc-comment-inclusion,
[  --without-eaccelerator-doc-comment-inclusion  If you want eAccelerator to strip doc-comments from internal php structures.],[
    enable_doc_comment_inclusion=$withval
],[
    enable_doc_comment_inclusion=yes
])

AC_ARG_WITH(eaccelerator-disassembler,
[  --with-eaccelerator-disassembler         Include disassembler],[
  eaccelerator_disassembler=$withval
],[
  eaccelerator_disassemmbler=no
])

AC_ARG_WITH(eaccelerator-debug,
[  --with-eaccelerator-debug                Enable the debug code so eAccelerator logs verbose.],[
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
  if test "$eaccelerator_disassembler" = "yes"; then
    AC_DEFINE(WITH_EACCELERATOR_DISASSEMBLER, 1, [Define if you like to explore Zend bytecode])
  fi
  if test "$eaccelerator_debug" = "yes"; then
    AC_DEFINE(DEBUG, 1, [Undef when you want to enable eaccelerator debug code])
  fi

  AC_REQUIRE_CPP()

dnl
dnl Do some tests for OS support
dnl

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

dnl Test for union semun
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

dnl sysvipc shared memory
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

dnl mmap shared memory
  AC_MSG_CHECKING(for mmap shared memory support)
  AC_TRY_RUN([#define MM_SEM_NONE
#define MM_SHM_MMAP_FILE
#define MM_TEST_SHM
#include "$ext_srcdir/mm.c"
],dnl
    mm_shm_mmap_file=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

dnl mmap zero shared memory
  AC_MSG_CHECKING(for mmap on /dev/zero shared memory support)
  AC_TRY_RUN([#define MM_SEM_NONE
#define MM_SHM_MMAP_ZERO
#define MM_TEST_SHM
#include "$ext_srcdir/mm.c"
],dnl
    mm_shm_mmap_zero=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

dnl mmap anonymous shared memory
  AC_MSG_CHECKING(for anonymous mmap shared memory support)
  AC_TRY_RUN([#define MM_SEM_NONE
#define MM_SHM_MMAP_ANON
#define MM_TEST_SHM
#include "$ext_srcdir/mm.c"
],dnl
    mm_shm_mmap_anon=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

dnl posix mmap shared memory support
  AC_MSG_CHECKING(for posix mmap shared memory support)
  AC_TRY_RUN([#define MM_SEM_NONE
#define MM_SHM_MMAP_POSIX
#define MM_TEST_SHM
#include "$ext_srcdir/mm.c"
],dnl
    mm_shm_mmap_posix=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

dnl determine the best type
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
  else
    msg="no"
  fi
  AC_MSG_RESULT([$msg])
  if test "$msg" = "no" ; then
    AC_MSG_ERROR([eaccelerator couldn't detect the shared memory type])
  fi

dnl
dnl

dnl spinlock test
  AC_MSG_CHECKING(for spinlock semaphores support)
  AC_TRY_RUN([#define MM_SEM_SPINLOCK
#define MM_TEST_SEM
#include "$ext_srcdir/mm.c"
],dnl
    mm_sem_spinlock=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

oldLIBS="$LIBS"
LIBS="-lpthread"
dnl pthread support
  AC_MSG_CHECKING(for pthread semaphores support)
  AC_TRY_RUN([#define MM_SEM_PTHREAD
#define MM_TEST_SEM
#include "$ext_srcdir/mm.c"
],dnl
    mm_sem_pthread=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

dnl posix semaphore support
  AC_MSG_CHECKING(for posix semaphores support)
  AC_TRY_RUN([#define MM_SEM_POSIX
#define MM_TEST_SEM
#include "$ext_srcdir/mm.c"
],dnl
    mm_sem_posix=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

LIBS="$oldLIBS"
dnl sysvipc semaphore support
  AC_MSG_CHECKING(for sysvipc semaphores support)
  AC_TRY_RUN([#define MM_SEM_IPC
#define MM_TEST_SEM
#include "$ext_srcdir/mm.c"
],dnl
    mm_sem_ipc=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])
  EA_REMOVE_IPC_TEST()

dnl fnctl semaphore support
  AC_MSG_CHECKING(for fcntl semaphores support)
  AC_TRY_RUN([#define MM_SEM_FCNTL
#define MM_TEST_SEM
#include "$ext_srcdir/mm.c"
],dnl
    mm_sem_fcntl=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

dnl flock semaphore support
  AC_MSG_CHECKING(for flock semaphores support)
  AC_TRY_RUN([#define MM_SEM_FLOCK
#define MM_TEST_SEM
#include "$ext_srcdir/mm.c"
],dnl
    mm_sem_flock=yes
    msg=yes,msg=no,msg=no)
  AC_MSG_RESULT([$msg])

dnl Determine the best type
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
    AC_MSG_ERROR([eaccelerator cannot determine semaphore type, which is required])
  fi

fi
