dnl $Id$
dnl config.m4 for extension pcache

dnl Comments in this file start with the string 'dnl'.
dnl Remove where necessary. This file will not work
dnl without editing.

dnl If your extension references something external, use with:

dnl PHP_ARG_WITH(pcache, for pcache support,
dnl Make sure that the comment is aligned:
dnl [  --with-pcache             Include pcache support])

dnl Otherwise use enable:

PHP_ARG_ENABLE(pcache, whether to enable pcache support,
dnl Make sure that the comment is aligned:
[  --enable-pcache           Enable pcache support])

if test "$PHP_PCACHE" != "no"; then
  dnl Write more examples of tests here...

  dnl # --with-pcache -> check with-path
  dnl SEARCH_PATH="/usr/local /usr"     # you might want to change this
  dnl SEARCH_FOR="/include/pcache.h"  # you most likely want to change this
  dnl if test -r $PHP_PCACHE/$SEARCH_FOR; then # path given as parameter
  dnl   PCACHE_DIR=$PHP_PCACHE
  dnl else # search default path list
  dnl   AC_MSG_CHECKING([for pcache files in default path])
  dnl   for i in $SEARCH_PATH ; do
  dnl     if test -r $i/$SEARCH_FOR; then
  dnl       PCACHE_DIR=$i
  dnl       AC_MSG_RESULT(found in $i)
  dnl     fi
  dnl   done
  dnl fi
  dnl
  dnl if test -z "$PCACHE_DIR"; then
  dnl   AC_MSG_RESULT([not found])
  dnl   AC_MSG_ERROR([Please reinstall the pcache distribution])
  dnl fi

  dnl # --with-pcache -> add include path
  dnl PHP_ADD_INCLUDE($PCACHE_DIR/include)

  dnl # --with-pcache -> check for lib and symbol presence
  dnl LIBNAME=pcache # you may want to change this
  dnl LIBSYMBOL=pcache # you most likely want to change this

  dnl PHP_CHECK_LIBRARY($LIBNAME,$LIBSYMBOL,
  dnl [
  dnl   PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $PCACHE_DIR/lib, PCACHE_SHARED_LIBADD)
  dnl   AC_DEFINE(HAVE_PCACHELIB,1,[ ])
  dnl ],[
  dnl   AC_MSG_ERROR([wrong pcache lib version or lib not found])
  dnl ],[
  dnl   -L$PCACHE_DIR/lib -lm
  dnl ])
  dnl
  dnl PHP_SUBST(PCACHE_SHARED_LIBADD)

  PHP_NEW_EXTENSION(pcache, pcache.c ncx_lock.c ncx_shm.c ncx_slab.c, $ext_shared)
fi
