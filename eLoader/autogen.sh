#! /bin/sh

ln -sf ../loader.c loader.c
ln -sf ../opcodes.c opcodes.c
ln -sf ../opcodes.h opcodes.h
ln -sf ../eaccelerator.h eaccelerator.h
ln -sf ../eaccelerator_version.h eaccelerator_version.h

if test x$PHP_PREFIX = x; then
  phpize
else
  $PHP_PREFIX/bin/phpize
fi