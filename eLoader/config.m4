PHP_ARG_ENABLE(eloader, whether to enable mmcache support,
[  --enable-eloader             Enable eLoader support])

dnl PHP_BUILD_SHARED
if test "$PHP_MMCACHE" != "no"; then
  PHP_EXTENSION(eloader, $ext_shared)
  AC_DEFINE(HAVE_EACCELERATOR, 1, [Define if you like to use eAccelerator])
  AC_DEFINE(WITH_EACCELERATOR_LOADER, 1, [Define if you like to use eAccelerator loader])
  AC_DEFINE(HAVE_EACCELERATOR_STANDALONE_LOADER, 1, [Define if you like to use eAccelerator standolone loader])
fi
