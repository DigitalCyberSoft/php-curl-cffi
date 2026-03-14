dnl config.m4 for extension curl_impersonate

PHP_ARG_WITH([curl-impersonate],
  [for curl-impersonate support],
  [AS_HELP_STRING([--with-curl-impersonate@<:@=FILE@:>@],
    [Include curl-impersonate support. FILE is path to libcurl-impersonate.a])])

if test "$PHP_CURL_IMPERSONATE" != "no"; then

  AC_CHECK_HEADER([curl/curl.h], [], [
    AC_MSG_ERROR([curl/curl.h not found. Install libcurl-devel.])
  ])

  if test "$PHP_CURL_IMPERSONATE" = "yes"; then
    CURL_IMPERSONATE_LIB="/usr/local/lib/libcurl-impersonate.a"
  else
    CURL_IMPERSONATE_LIB="$PHP_CURL_IMPERSONATE"
  fi

  if test ! -f "$CURL_IMPERSONATE_LIB"; then
    AC_MSG_ERROR([libcurl-impersonate.a not found at $CURL_IMPERSONATE_LIB])
  fi

  AC_MSG_RESULT([using libcurl-impersonate at $CURL_IMPERSONATE_LIB])

  PHP_ADD_LIBRARY(z, 1, CURL_IMPERSONATE_SHARED_LIBADD)
  PHP_ADD_LIBRARY(pthread, 1, CURL_IMPERSONATE_SHARED_LIBADD)
  PHP_ADD_LIBRARY(dl, 1, CURL_IMPERSONATE_SHARED_LIBADD)
  PHP_ADD_LIBRARY(m, 1, CURL_IMPERSONATE_SHARED_LIBADD)
  PHP_ADD_LIBRARY(stdc++, 1, CURL_IMPERSONATE_SHARED_LIBADD)

  dnl Prepend the static library so linker resolves our symbols from it
  CURL_IMPERSONATE_SHARED_LIBADD="$CURL_IMPERSONATE_LIB $CURL_IMPERSONATE_SHARED_LIBADD"

  dnl Allow text relocations since the static lib may not be compiled with -fPIC
  LDFLAGS="$LDFLAGS -Wl,-z,notext"

  PHP_SUBST(CURL_IMPERSONATE_SHARED_LIBADD)
  PHP_NEW_EXTENSION(curl_impersonate, curl_impersonate.c, $ext_shared,, -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1)
fi
