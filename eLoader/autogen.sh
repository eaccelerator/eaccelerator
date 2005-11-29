#! /bin/sh

ln -sf ../loader.c .
ln -sf ../opcodes.c .
ln -sf ../opcodes.h .
ln -sf ../eaccelerator.h .
ln -sf ../eaccelerator_version.h .
ln -sf ../ea_restore.c .
ln -sf ../ea_restore.h .
ln -sf ../debug.h .
ln -sf ../debug.c .

if test x$PHP_PREFIX = x; then
  phpize
else
  $PHP_PREFIX/bin/phpize
fi
