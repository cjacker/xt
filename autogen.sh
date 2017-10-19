#!/bin/sh
set -x
aclocal
autoconf
libtoolize --force
automake --add-missing --foreign
