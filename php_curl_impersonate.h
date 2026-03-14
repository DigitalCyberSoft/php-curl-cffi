/*
 * php-curl-impersonate - PHP extension for libcurl-impersonate
 * Browser impersonation (TLS/HTTP2 fingerprinting) for PHP
 */

#ifndef PHP_CURL_IMPERSONATE_H
#define PHP_CURL_IMPERSONATE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/standard/url.h"
#include "ext/standard/php_http.h"
#include "ext/standard/php_string.h"
#include "zend_smart_str.h"
#include "zend_exceptions.h"
#include "ext/spl/spl_exceptions.h"
#include "ext/json/php_json.h"
#include "php_streams.h"
#include "php_network.h"

#include <curl/curl.h>

#define PHP_CURL_IMPERSONATE_VERSION "0.1.0"

extern zend_module_entry curl_impersonate_module_entry;
#define phpext_curl_impersonate_ptr &curl_impersonate_module_entry

PHP_MINIT_FUNCTION(curl_impersonate);
PHP_MSHUTDOWN_FUNCTION(curl_impersonate);
PHP_MINFO_FUNCTION(curl_impersonate);

/* Not in standard curl headers - declared by curl-impersonate */
extern CURLcode curl_easy_impersonate(CURL *curl, const char *target, int default_headers);

/* Compat macros */
#if PHP_VERSION_ID < 80000
#define RETURN_THROWS() return
#define ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(pass_by_ref, name, type_hint, allow_null, default_value) \
    ZEND_ARG_TYPE_INFO(pass_by_ref, name, type_hint, allow_null)
#endif

#if PHP_VERSION_ID < 80200
#ifndef ZEND_ACC_NO_DYNAMIC_PROPERTIES
#define ZEND_ACC_NO_DYNAMIC_PROPERTIES 0
#endif
#endif

#endif /* PHP_CURL_IMPERSONATE_H */
