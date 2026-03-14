/*
 * php-curl-impersonate - PHP extension wrapping libcurl-impersonate
 * Provides browser TLS/HTTP2 fingerprint impersonation for PHP
 *
 * Classes:
 *   CurlImpersonate\Curl      - Low-level curl handle
 *   CurlImpersonate\Session   - High-level HTTP client (requests-like)
 *   CurlImpersonate\Response  - HTTP response object
 *   CurlImpersonate\CurlOpt   - Option constants
 *   CurlImpersonate\CurlInfo  - Info constants
 *   CurlImpersonate\CurlException - Exception class
 */

#include "php_curl_impersonate.h"
#include <string.h>

/* ========================================================================
 * Compatibility
 * ======================================================================== */

#if PHP_VERSION_ID < 80200
#ifndef ZEND_ACC_NO_DYNAMIC_PROPERTIES
#define ZEND_ACC_NO_DYNAMIC_PROPERTIES 0
#endif
#endif

#if PHP_VERSION_ID < 80000
#define RETURN_THROWS() return
#endif

/* ========================================================================
 * Object structures
 * ======================================================================== */

typedef struct {
    CURL *handle;
    struct curl_slist *req_headers;
    struct curl_slist *proxy_headers;
    struct curl_slist *resolve;
    char errbuf[CURL_ERROR_SIZE];
    zval write_cb;
    zval header_cb;
    smart_str body_buf;
    smart_str header_buf;
    int return_transfer;    /* CURLOPT_RETURNTRANSFER emulation */
    CURLcode last_errno;    /* last error code for curl_cffi_errno() */
    zval private_data;      /* CURLOPT_PRIVATE storage */
    zend_string *header_out;/* Captured outgoing request headers */
    zend_bool header_out_enabled;
    zval progress_cb;       /* CURLOPT_PROGRESSFUNCTION */
    zval xferinfo_cb;       /* CURLOPT_XFERINFOFUNCTION */
    zval debug_cb;          /* CURLOPT_DEBUGFUNCTION */
    zval read_cb;           /* CURLOPT_READFUNCTION */
    zval fnmatch_cb;        /* CURLOPT_FNMATCH_FUNCTION */
    zval postfields;        /* Keep reference to POSTFIELDS array for MIME */
    zval infile;            /* CURLOPT_INFILE resource reference */
    curl_mime *mime;         /* MIME handle for multipart POSTFIELDS */
    FILE *devnull;           /* /dev/null handle for suppressing debug output */
    zend_bool closed;
    zend_object std;
} ci_curl_obj;

typedef struct {
    CURL *handle;
    struct curl_slist *req_headers;
    char errbuf[CURL_ERROR_SIZE];
    smart_str body_buf;
    smart_str header_buf;
    zval cookies;           /* HashTable: cookie name => value */
    zval def_headers;       /* HashTable: header name => value */
    zval def_params;        /* HashTable: param name => value */
    char *base_url;
    char *impersonate_target;
    double timeout;
    long max_redirects;
    zend_bool allow_redirects;
    zend_bool verify;
    zend_bool raise_for_status;
    char *proxy;
    zend_bool closed;
    zend_object std;
} ci_session_obj;

/* ========================================================================
 * Global class entries and handlers
 * ======================================================================== */

static zend_class_entry *ci_curl_ce;
static zend_class_entry *ci_session_ce;
static zend_class_entry *ci_response_ce;
static zend_class_entry *ci_exception_ce;
static zend_class_entry *ci_curlopt_ce;
static zend_class_entry *ci_curlinfo_ce;

static zend_object_handlers ci_curl_handlers;
static zend_object_handlers ci_session_handlers;

/* Flag to track if we're inside setopt_array (for error messages) */
static zend_bool ci_in_setopt_array = 0;
#define CI_SETOPT_FN (ci_in_setopt_array ? "curl_cffi_setopt_array" : "curl_cffi_setopt")
#define CI_SETOPT_ARG_CB (ci_in_setopt_array ? "Argument #2 ($options) must be a valid callback" : "Argument #3 ($value) must be a valid callback")

/* ========================================================================
 * Object fetch macros
 * ======================================================================== */

/* Forward declarations for share handle */
static zend_class_entry *ci_share_ce;
static zend_class_entry *ci_curlfile_ce;
static zend_class_entry *ci_curlstringfile_ce;
typedef struct {
    CURLSH *share;
    CURLSHcode last_errno;
    zend_object std;
} ci_share_obj;
static inline ci_share_obj *ci_share_from_obj(zend_object *obj) {
    return (ci_share_obj *)((char *)obj - XtOffsetOf(ci_share_obj, std));
}

static inline ci_curl_obj *ci_curl_from_obj(zend_object *obj) {
    return (ci_curl_obj *)((char *)obj - XtOffsetOf(ci_curl_obj, std));
}

static inline ci_session_obj *ci_session_from_obj(zend_object *obj) {
    return (ci_session_obj *)((char *)obj - XtOffsetOf(ci_session_obj, std));
}

/* ========================================================================
 * Utility functions
 * ======================================================================== */

static int ci_is_slist_option(CURLoption opt) {
    switch (opt) {
        case CURLOPT_HTTPHEADER:
        case CURLOPT_PROXYHEADER:
        case CURLOPT_RESOLVE:
#ifdef CURLOPT_HTTP200ALIASES
        case CURLOPT_HTTP200ALIASES:
#endif
#ifdef CURLOPT_MAIL_RCPT
        case CURLOPT_MAIL_RCPT:
#endif
#ifdef CURLOPT_CONNECT_TO
        case CURLOPT_CONNECT_TO:
#endif
        case CURLOPT_QUOTE:
        case CURLOPT_POSTQUOTE:
#ifdef CURLOPT_PREQUOTE
        case CURLOPT_PREQUOTE:
#endif
            return 1;
        default:
            return 0;
    }
}

/* Find the last HTTP status line in raw headers (for redirect chains) */
static const char *ci_find_last_status_line(const char *raw, size_t len) {
    const char *last = NULL;
    const char *p = raw;
    const char *end = raw + len;

    while (p < end - 5) {
        if (memcmp(p, "HTTP/", 5) == 0 && (p == raw || *(p - 1) == '\n')) {
            last = p;
        }
        p++;
    }

    return last ? last : raw;
}

/* Parse HTTP response headers into structured arrays.
 * headers_arr: {"Header-Name" => ["value1", "value2"]}
 * cookies_arr: {"name" => "value"}
 * reason: extracted from status line (e.g. "OK", "Not Found")
 */
static void ci_parse_headers(const char *raw, size_t len,
                             zval *headers_arr, zval *cookies_arr,
                             zend_string **reason_out) {
    const char *block_start = ci_find_last_status_line(raw, len);
    const char *end = raw + len;
    const char *line_start = block_start;
    int is_status_line = 1;

    while (line_start < end) {
        const char *line_end = line_start;
        while (line_end < end && *line_end != '\r' && *line_end != '\n') {
            line_end++;
        }

        size_t line_len = line_end - line_start;

        if (line_len == 0) {
            /* Skip empty line */
            if (line_end < end && *line_end == '\r') line_end++;
            if (line_end < end && *line_end == '\n') line_end++;
            line_start = line_end;
            continue;
        }

        if (is_status_line) {
            /* Status line: "HTTP/1.1 200 OK" */
            const char *p = line_start;
            int spaces = 0;
            while (p < line_end && spaces < 2) {
                if (*p == ' ') spaces++;
                p++;
            }
            if (spaces >= 2 && p < line_end) {
                *reason_out = zend_string_init(p, line_end - p, 0);
            }
            is_status_line = 0;
        } else {
            /* Header line: "Name: Value" */
            const char *colon = memchr(line_start, ':', line_len);
            if (colon) {
                size_t name_len = colon - line_start;
                const char *value = colon + 1;
                while (value < line_end && *value == ' ') value++;
                size_t value_len = line_end - value;

                zend_string *name = zend_string_init(line_start, name_len, 0);
                zval *existing = zend_hash_find(Z_ARRVAL_P(headers_arr), name);

                if (existing && Z_TYPE_P(existing) == IS_ARRAY) {
                    add_next_index_stringl(existing, value, value_len);
                } else {
                    zval arr;
                    array_init(&arr);
                    add_next_index_stringl(&arr, value, value_len);
                    zend_hash_update(Z_ARRVAL_P(headers_arr), name, &arr);
                }

                /* Extract Set-Cookie */
                if (name_len == 10 && strncasecmp(line_start, "set-cookie", 10) == 0) {
                    const char *eq = memchr(value, '=', value_len);
                    if (eq) {
                        size_t cname_len = eq - value;
                        const char *cval = eq + 1;
                        const char *semi = memchr(cval, ';', value_len - (cval - value));
                        size_t cval_len = semi ? (size_t)(semi - cval) : value_len - (cval - value);
                        add_assoc_stringl_ex(cookies_arr, value, cname_len, (char *)cval, cval_len);
                    }
                }

                zend_string_release(name);
            }
        }

        if (line_end < end && *line_end == '\r') line_end++;
        if (line_end < end && *line_end == '\n') line_end++;
        line_start = line_end;
    }
}

/* Build "name=value; name2=value2" cookie string from a PHP array.
 * Strips \r, \n, and ; from values to prevent cookie/header injection. */
static zend_string *ci_build_cookie_string(HashTable *cookies) {
    smart_str buf = {0};
    zend_string *key;
    zval *val;
    int first = 1;

    ZEND_HASH_FOREACH_STR_KEY_VAL(cookies, key, val) {
        if (!key) continue;
        if (!first) smart_str_appends(&buf, "; ");
        first = 0;
        smart_str_append(&buf, key);
        smart_str_appendc(&buf, '=');
        zend_string *sv = zval_get_string(val);
        /* Sanitize: strip characters that could cause cookie/header injection */
        const char *p = ZSTR_VAL(sv);
        size_t len = ZSTR_LEN(sv);
        for (size_t i = 0; i < len; i++) {
            if (p[i] != '\r' && p[i] != '\n' && p[i] != ';') {
                smart_str_appendc(&buf, p[i]);
            }
        }
        zend_string_release(sv);
    } ZEND_HASH_FOREACH_END();

    if (buf.s) {
        smart_str_0(&buf);
        return buf.s;
    }
    return NULL;
}

/* URL-encode a PHP array into "key=val&key2=val2" form data */
static zend_string *ci_urlencode_array(zval *data) {
    smart_str buf = {0};
    int first = 1;

    if (Z_TYPE_P(data) != IS_ARRAY) return NULL;

    /* Check if indexed array of pairs: [[key,val], [key,val]] */
    zval *first_entry = zend_hash_index_find(Z_ARRVAL_P(data), 0);
    if (first_entry && Z_TYPE_P(first_entry) == IS_ARRAY) {
        zval *entry;
        ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(data), entry) {
            if (Z_TYPE_P(entry) != IS_ARRAY) continue;
            zval *k = zend_hash_index_find(Z_ARRVAL_P(entry), 0);
            zval *v = zend_hash_index_find(Z_ARRVAL_P(entry), 1);
            if (!k || !v) continue;

            if (!first) smart_str_appendc(&buf, '&');
            first = 0;

            zend_string *ks = zval_get_string(k);
            zend_string *vs = zval_get_string(v);
            zend_string *ek = php_url_encode(ZSTR_VAL(ks), ZSTR_LEN(ks));
            zend_string *ev = php_url_encode(ZSTR_VAL(vs), ZSTR_LEN(vs));

            smart_str_append(&buf, ek);
            smart_str_appendc(&buf, '=');
            smart_str_append(&buf, ev);

            zend_string_release(ks);
            zend_string_release(vs);
            zend_string_release(ek);
            zend_string_release(ev);
        } ZEND_HASH_FOREACH_END();
    } else {
        /* Associative array: [key => val] */
        zend_string *key;
        zval *val;
        ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(data), key, val) {
            if (!key) continue;
            if (!first) smart_str_appendc(&buf, '&');
            first = 0;

            zend_string *vs = zval_get_string(val);
            zend_string *ek = php_url_encode(ZSTR_VAL(key), ZSTR_LEN(key));
            zend_string *ev = php_url_encode(ZSTR_VAL(vs), ZSTR_LEN(vs));

            smart_str_append(&buf, ek);
            smart_str_appendc(&buf, '=');
            smart_str_append(&buf, ev);

            zend_string_release(vs);
            zend_string_release(ek);
            zend_string_release(ev);
        } ZEND_HASH_FOREACH_END();
    }

    if (buf.s) {
        smart_str_0(&buf);
        return buf.s;
    }
    return ZSTR_EMPTY_ALLOC();
}

/* ========================================================================
 * Curl write/header callbacks
 * ======================================================================== */

static size_t ci_curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    ci_curl_obj *obj = (ci_curl_obj *)userdata;
    if (size != 0 && nmemb > SIZE_MAX / size) return 0;
    size_t total = size * nmemb;

    if (Z_TYPE(obj->write_cb) != IS_UNDEF) {
        /* PHP's curl passes ($ch, $data) - 2 args */
        zval retval, args[2];
        GC_ADDREF(&obj->std);
        ZVAL_OBJ(&args[0], &obj->std);
        ZVAL_STRINGL(&args[1], ptr, total);

        if (call_user_function(NULL, NULL, &obj->write_cb, &retval, 2, args) == SUCCESS) {
            size_t ret = (size_t)zval_get_long(&retval);
            zval_ptr_dtor(&retval);
            zval_ptr_dtor(&args[0]);
            zval_ptr_dtor(&args[1]);
            return ret;
        }
        zval_ptr_dtor(&args[0]);
        zval_ptr_dtor(&args[1]);
        return 0;
    }

    if (obj->return_transfer) {
        smart_str_appendl(&obj->body_buf, ptr, total);
    } else {
        /* Write directly to stdout when not returning transfer */
        PHPWRITE(ptr, total);
    }
    return total;
}

static size_t ci_curl_header_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    ci_curl_obj *obj = (ci_curl_obj *)userdata;
    if (size != 0 && nmemb > SIZE_MAX / size) return 0;
    size_t total = size * nmemb;

    if (Z_TYPE(obj->header_cb) != IS_UNDEF) {
        /* PHP's curl passes ($ch, $data) - 2 args */
        zval retval, args[2];
        GC_ADDREF(&obj->std);
        ZVAL_OBJ(&args[0], &obj->std);
        ZVAL_STRINGL(&args[1], ptr, total);

        if (call_user_function(NULL, NULL, &obj->header_cb, &retval, 2, args) == SUCCESS) {
            size_t ret = (size_t)zval_get_long(&retval);
            zval_ptr_dtor(&retval);
            zval_ptr_dtor(&args[0]);
            zval_ptr_dtor(&args[1]);
            return ret;
        }
        zval_ptr_dtor(&args[0]);
        zval_ptr_dtor(&args[1]);
        return 0;
    }

    smart_str_appendl(&obj->header_buf, ptr, total);
    return total;
}

static size_t ci_session_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    ci_session_obj *obj = (ci_session_obj *)userdata;
    if (size != 0 && nmemb > SIZE_MAX / size) return 0;
    size_t total = size * nmemb;
    smart_str_appendl(&obj->body_buf, ptr, total);
    return total;
}

static size_t ci_session_header_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    ci_session_obj *obj = (ci_session_obj *)userdata;
    if (size != 0 && nmemb > SIZE_MAX / size) return 0;
    size_t total = size * nmemb;
    smart_str_appendl(&obj->header_buf, ptr, total);
    return total;
}

/* Debug callback for capturing outgoing request headers (CURLINFO_HEADER_OUT) */
static int ci_curl_debug_cb(CURL *handle, curl_infotype type, char *data, size_t size, void *userdata) {
    ci_curl_obj *obj = (ci_curl_obj *)userdata;
    if (type == CURLINFO_HEADER_OUT && obj->header_out_enabled) {
        if (obj->header_out) {
            zend_string_release(obj->header_out);
        }
        obj->header_out = zend_string_init(data, size, 0);
    }
    return 0;
}

/* Progress callback: ($ch, $dltotal, $dlnow, $ultotal, $ulnow) -> int */
static int ci_curl_progress_cb(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
    ci_curl_obj *obj = (ci_curl_obj *)clientp;
    if (Z_TYPE(obj->progress_cb) == IS_UNDEF) return 0;

    zval retval, args[5];
    GC_ADDREF(&obj->std);
    ZVAL_OBJ(&args[0], &obj->std);
    ZVAL_LONG(&args[1], (zend_long)dltotal);
    ZVAL_LONG(&args[2], (zend_long)dlnow);
    ZVAL_LONG(&args[3], (zend_long)ultotal);
    ZVAL_LONG(&args[4], (zend_long)ulnow);

    int rval = 0;
    if (call_user_function(NULL, NULL, &obj->progress_cb, &retval, 5, args) == SUCCESS) {
        if (!Z_ISUNDEF(retval) && zval_get_long(&retval) != 0) rval = 1;
        zval_ptr_dtor(&retval);
    }
    zval_ptr_dtor(&args[0]);
    return rval;
}

/* Xferinfo callback: ($ch, $dltotal, $dlnow, $ultotal, $ulnow) -> int */
static int ci_curl_xferinfo_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    ci_curl_obj *obj = (ci_curl_obj *)clientp;
    if (Z_TYPE(obj->xferinfo_cb) == IS_UNDEF) return 0;

    zval retval, args[5];
    GC_ADDREF(&obj->std);
    ZVAL_OBJ(&args[0], &obj->std);
    ZVAL_LONG(&args[1], (zend_long)dltotal);
    ZVAL_LONG(&args[2], (zend_long)dlnow);
    ZVAL_LONG(&args[3], (zend_long)ultotal);
    ZVAL_LONG(&args[4], (zend_long)ulnow);

    int rval = 0;
    if (call_user_function(NULL, NULL, &obj->xferinfo_cb, &retval, 5, args) == SUCCESS) {
        if (!Z_ISUNDEF(retval) && zval_get_long(&retval) != 0) rval = 1;
        zval_ptr_dtor(&retval);
    }
    zval_ptr_dtor(&args[0]);
    return rval;
}

/* User debug callback: ($ch, $type, $data) -> 0 */
static int ci_curl_user_debug_cb(CURL *handle, curl_infotype type, char *data, size_t size, void *userdata) {
    ci_curl_obj *obj = (ci_curl_obj *)userdata;

    /* Always capture outgoing headers for CURLINFO_HEADER_OUT */
    if (type == CURLINFO_HEADER_OUT && obj->header_out_enabled) {
        if (obj->header_out) zend_string_release(obj->header_out);
        obj->header_out = zend_string_init(data, size, 0);
    }

    if (Z_TYPE(obj->debug_cb) == IS_UNDEF) return 0;

    zval retval, args[3];
    GC_ADDREF(&obj->std);
    ZVAL_OBJ(&args[0], &obj->std);
    ZVAL_LONG(&args[1], type);
    ZVAL_STRINGL(&args[2], data, size);

    if (call_user_function(NULL, NULL, &obj->debug_cb, &retval, 3, args) == SUCCESS) {
        zval_ptr_dtor(&retval);
    }
    zval_ptr_dtor(&args[0]);
    zval_ptr_dtor(&args[2]);

    /* If the callback threw an exception, abort the transfer */
    if (EG(exception)) {
        return 1; /* non-zero return aborts the transfer */
    }
    return 0;
}

/* Read callback: ($ch, $stream, $length) -> string */
static size_t ci_curl_read_cb(char *buffer, size_t size, size_t nitems, void *userdata) {
    ci_curl_obj *obj = (ci_curl_obj *)userdata;
    if (Z_TYPE(obj->read_cb) == IS_UNDEF) return 0;

    size_t length = size * nitems;
    zval retval, args[3];
    GC_ADDREF(&obj->std);
    ZVAL_OBJ(&args[0], &obj->std);
    /* Pass the INFILE resource if available, NULL otherwise */
    if (Z_TYPE(obj->infile) != IS_UNDEF) {
        ZVAL_COPY_VALUE(&args[1], &obj->infile);
    } else {
        ZVAL_NULL(&args[1]);
    }
    ZVAL_LONG(&args[2], (zend_long)length);

    size_t ret = CURL_READFUNC_ABORT;
    if (call_user_function(NULL, NULL, &obj->read_cb, &retval, 3, args) == SUCCESS) {
        if (Z_TYPE(retval) == IS_STRING) {
            size_t copy_len = Z_STRLEN(retval) < length ? Z_STRLEN(retval) : length;
            memcpy(buffer, Z_STRVAL(retval), copy_len);
            ret = copy_len;
        } else if (Z_TYPE(retval) == IS_LONG) {
            ret = (size_t)Z_LVAL(retval);
        }
        zval_ptr_dtor(&retval);
    }
    zval_ptr_dtor(&args[0]);
    return ret;
}

/* ========================================================================
 * CurlHandle lifecycle
 * ======================================================================== */

static zend_object *ci_curl_create(zend_class_entry *ce) {
    ci_curl_obj *obj = zend_object_alloc(sizeof(ci_curl_obj), ce);
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &ci_curl_handlers;

    obj->handle = NULL;
    obj->req_headers = NULL;
    obj->proxy_headers = NULL;
    obj->resolve = NULL;
    memset(obj->errbuf, 0, CURL_ERROR_SIZE);
    ZVAL_UNDEF(&obj->write_cb);
    ZVAL_UNDEF(&obj->header_cb);
    memset(&obj->body_buf, 0, sizeof(smart_str));
    memset(&obj->header_buf, 0, sizeof(smart_str));
    obj->return_transfer = 0;
    obj->last_errno = CURLE_OK;
    ZVAL_UNDEF(&obj->private_data);
    ZVAL_UNDEF(&obj->progress_cb);
    ZVAL_UNDEF(&obj->xferinfo_cb);
    ZVAL_UNDEF(&obj->debug_cb);
    ZVAL_UNDEF(&obj->read_cb);
    ZVAL_UNDEF(&obj->fnmatch_cb);
    ZVAL_UNDEF(&obj->postfields);
    ZVAL_UNDEF(&obj->infile);
    obj->mime = NULL;
    obj->devnull = NULL;
    obj->header_out = NULL;
    obj->header_out_enabled = 0;
    obj->closed = 0;

    return &obj->std;
}

static void ci_curl_free(zend_object *object) {
    ci_curl_obj *obj = ci_curl_from_obj(object);

    if (obj->handle && !obj->closed) {
        curl_easy_cleanup(obj->handle);
        obj->handle = NULL;
    }
    if (obj->req_headers) { curl_slist_free_all(obj->req_headers); obj->req_headers = NULL; }
    if (obj->proxy_headers) { curl_slist_free_all(obj->proxy_headers); obj->proxy_headers = NULL; }
    if (obj->resolve) { curl_slist_free_all(obj->resolve); obj->resolve = NULL; }

    zval_ptr_dtor(&obj->write_cb);
    zval_ptr_dtor(&obj->header_cb);
    zval_ptr_dtor(&obj->private_data);
    zval_ptr_dtor(&obj->progress_cb);
    zval_ptr_dtor(&obj->xferinfo_cb);
    zval_ptr_dtor(&obj->debug_cb);
    zval_ptr_dtor(&obj->read_cb);
    zval_ptr_dtor(&obj->fnmatch_cb);
    zval_ptr_dtor(&obj->postfields);
    zval_ptr_dtor(&obj->infile);
    if (obj->mime) { curl_mime_free(obj->mime); obj->mime = NULL; }
    if (obj->devnull) { fclose(obj->devnull); obj->devnull = NULL; }
    if (obj->header_out) { zend_string_release(obj->header_out); obj->header_out = NULL; }
    smart_str_free(&obj->body_buf);
    smart_str_free(&obj->header_buf);

    zend_object_std_dtor(&obj->std);
}

/* ========================================================================
 * CurlHandle methods
 * ======================================================================== */

PHP_METHOD(Curl, __construct) {
    ZEND_PARSE_PARAMETERS_NONE();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(ZEND_THIS));
    obj->handle = curl_easy_init();
    if (!obj->handle) {
        zend_throw_exception(spl_ce_RuntimeException, "Failed to initialize curl handle", 0);
        RETURN_THROWS();
    }
    curl_easy_setopt(obj->handle, CURLOPT_ERRORBUFFER, obj->errbuf);
    curl_easy_setopt(obj->handle, CURLOPT_WRITEFUNCTION, ci_curl_write_cb);
    curl_easy_setopt(obj->handle, CURLOPT_WRITEDATA, obj);
    curl_easy_setopt(obj->handle, CURLOPT_HEADERFUNCTION, ci_curl_header_cb);
    curl_easy_setopt(obj->handle, CURLOPT_HEADERDATA, obj);

    /* OOP API defaults to capturing body for getBody() */
    obj->return_transfer = 1;
}

PHP_METHOD(Curl, setOpt) {
    zend_long option;
    zval *value;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_LONG(option)
        Z_PARAM_ZVAL(value)
    ZEND_PARSE_PARAMETERS_END();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(ZEND_THIS));
    if (!obj->handle || obj->closed) {
        zend_throw_exception(ci_exception_ce, "Curl handle is not initialized or is closed", 0);
        RETURN_THROWS();
    }

    CURLoption opt = (CURLoption)option;
    CURLcode res = CURLE_OK;

    /* --- slist options --- */
    if (ci_is_slist_option(opt)) {
        if (Z_TYPE_P(value) != IS_ARRAY) {
            zend_throw_exception(spl_ce_InvalidArgumentException,
                "Value must be an array for list options (HTTPHEADER, RESOLVE, etc.)", 0);
            RETURN_THROWS();
        }

        struct curl_slist *slist = NULL;
        zval *item;
        ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(value), item) {
            zend_string *str = zval_get_string(item);
            slist = curl_slist_append(slist, ZSTR_VAL(str));
            zend_string_release(str);
        } ZEND_HASH_FOREACH_END();

        /* Free previous and store new */
        if (opt == CURLOPT_HTTPHEADER) {
            if (obj->req_headers) curl_slist_free_all(obj->req_headers);
            obj->req_headers = slist;
        } else if (opt == CURLOPT_PROXYHEADER) {
            if (obj->proxy_headers) curl_slist_free_all(obj->proxy_headers);
            obj->proxy_headers = slist;
        } else if (opt == CURLOPT_RESOLVE) {
            if (obj->resolve) curl_slist_free_all(obj->resolve);
            obj->resolve = slist;
        }

        res = curl_easy_setopt(obj->handle, opt, slist);
    }
    /* --- write callback --- */
    else if (opt == CURLOPT_WRITEFUNCTION) {
        if (!zend_is_callable(value, 0, NULL)) {
            zend_throw_exception(spl_ce_InvalidArgumentException, "Value must be callable for WRITEFUNCTION", 0);
            RETURN_THROWS();
        }
        zval_ptr_dtor(&obj->write_cb);
        ZVAL_COPY(&obj->write_cb, value);
        return;
    }
    /* --- header callback --- */
    else if (opt == CURLOPT_HEADERFUNCTION) {
        if (!zend_is_callable(value, 0, NULL)) {
            zend_throw_exception(spl_ce_InvalidArgumentException, "Value must be callable for HEADERFUNCTION", 0);
            RETURN_THROWS();
        }
        zval_ptr_dtor(&obj->header_cb);
        ZVAL_COPY(&obj->header_cb, value);
        return;
    }
    /* --- RETURNTRANSFER (PHP-specific, not a real curl option) --- */
    else if (option == 19913) { /* CURLOPT_RETURNTRANSFER from PHP */
        obj->return_transfer = zval_is_true(value);
        return;
    }
    /* --- CURLOPT_PRIVATE --- */
    else if (option == CURLOPT_PRIVATE) {
        zval_ptr_dtor(&obj->private_data);
        ZVAL_COPY(&obj->private_data, value);
        return;
    }
    /* --- POSTFIELDS special handling --- */
    else if (opt == CURLOPT_POSTFIELDS) {
        if (Z_TYPE_P(value) == IS_STRING) {
            curl_easy_setopt(obj->handle, CURLOPT_POSTFIELDSIZE, (long)Z_STRLEN_P(value));
            res = curl_easy_setopt(obj->handle, CURLOPT_COPYPOSTFIELDS, Z_STRVAL_P(value));
        } else if (Z_TYPE_P(value) == IS_NULL) {
            res = curl_easy_setopt(obj->handle, opt, NULL);
        } else {
            zend_throw_exception(spl_ce_InvalidArgumentException,
                "POSTFIELDS value must be a string (use Session for array encoding)", 0);
            RETURN_THROWS();
        }
    }
    /* --- string value --- */
    else if (Z_TYPE_P(value) == IS_STRING) {
        if (memchr(Z_STRVAL_P(value), '\0', Z_STRLEN_P(value))) {
            zend_throw_exception(spl_ce_InvalidArgumentException,
                "Option value must not contain any null bytes", 0);
            RETURN_THROWS();
        }
        res = curl_easy_setopt(obj->handle, opt, Z_STRVAL_P(value));
    }
    /* --- long/bool value --- */
    else if (Z_TYPE_P(value) == IS_LONG || Z_TYPE_P(value) == IS_TRUE || Z_TYPE_P(value) == IS_FALSE) {
        res = curl_easy_setopt(obj->handle, opt, (long)zval_get_long(value));
    }
    /* --- double value --- */
    else if (Z_TYPE_P(value) == IS_DOUBLE) {
        res = curl_easy_setopt(obj->handle, opt, (long)Z_DVAL_P(value));
    }
    /* --- null --- */
    else if (Z_TYPE_P(value) == IS_NULL) {
        res = curl_easy_setopt(obj->handle, opt, NULL);
    }
    else {
        zend_throw_exception(spl_ce_InvalidArgumentException, "Unsupported value type for setOpt", 0);
        RETURN_THROWS();
    }

    if (res != CURLE_OK) {
        zend_throw_exception_ex(ci_exception_ce, (long)res,
            "setOpt failed: (%d) %s", res, curl_easy_strerror(res));
        RETURN_THROWS();
    }
}

PHP_METHOD(Curl, getInfo) {
    zend_long option;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(option)
    ZEND_PARSE_PARAMETERS_END();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(ZEND_THIS));
    if (!obj->handle || obj->closed) {
        zend_throw_exception(ci_exception_ce, "Curl handle is not initialized or is closed", 0);
        RETURN_THROWS();
    }

    CURL *ch = obj->handle;

    /* CURLINFO_PRIVATE */
    if (option == CURLINFO_PRIVATE) {
        if (Z_TYPE(obj->private_data) != IS_UNDEF) {
            RETURN_ZVAL(&obj->private_data, 1, 0);
        }
        RETURN_FALSE;
    }

    int type = (int)option & CURLINFO_TYPEMASK;

    switch (type) {
        case CURLINFO_STRING: {
            char *str = NULL;
            curl_easy_getinfo(ch, (CURLINFO)option, &str);
            if (str) {
                RETURN_STRING(str);
            }
            RETURN_NULL();
        }
        case CURLINFO_LONG: {
            long lval = 0;
            curl_easy_getinfo(ch, (CURLINFO)option, &lval);
            RETURN_LONG(lval);
        }
        case CURLINFO_DOUBLE: {
            double dval = 0.0;
            curl_easy_getinfo(ch, (CURLINFO)option, &dval);
            RETURN_DOUBLE(dval);
        }
        case CURLINFO_SLIST: {
            struct curl_slist *slist = NULL;
            curl_easy_getinfo(ch, (CURLINFO)option, &slist);
            array_init(return_value);
            while (slist) {
                add_next_index_string(return_value, slist->data);
                slist = slist->next;
            }
            curl_slist_free_all(slist);
            return;
        }
#ifdef CURLINFO_OFF_T
        case CURLINFO_OFF_T: {
            curl_off_t oval = 0;
            curl_easy_getinfo(ch, (CURLINFO)option, &oval);
            RETURN_LONG((zend_long)oval);
        }
#endif
        default:
            zend_throw_exception_ex(spl_ce_InvalidArgumentException, 0,
                "Unknown CURLINFO type for option %ld", option);
            RETURN_THROWS();
    }
}

PHP_METHOD(Curl, perform) {
    ZEND_PARSE_PARAMETERS_NONE();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(ZEND_THIS));
    if (!obj->handle || obj->closed) {
        zend_throw_exception(ci_exception_ce, "Curl handle is not initialized or is closed", 0);
        RETURN_THROWS();
    }

    /* Clear internal buffers */
    smart_str_free(&obj->body_buf);
    smart_str_free(&obj->header_buf);
    memset(&obj->body_buf, 0, sizeof(smart_str));
    memset(&obj->header_buf, 0, sizeof(smart_str));
    memset(obj->errbuf, 0, CURL_ERROR_SIZE);

    CURLcode res = curl_easy_perform(obj->handle);
    obj->last_errno = res;
    if (res != CURLE_OK) {
        const char *err = obj->errbuf[0] ? obj->errbuf : curl_easy_strerror(res);
        zend_throw_exception_ex(ci_exception_ce, (long)res,
            "curl: (%d) %s", (int)res, err);
        RETURN_THROWS();
    }
}

PHP_METHOD(Curl, impersonate) {
    char *target;
    size_t target_len;
    zend_bool default_headers = 1;

    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(target, target_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(default_headers)
    ZEND_PARSE_PARAMETERS_END();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(ZEND_THIS));
    if (!obj->handle || obj->closed) {
        zend_throw_exception(ci_exception_ce, "Curl handle is not initialized or is closed", 0);
        RETURN_THROWS();
    }

    CURLcode res = curl_easy_impersonate(obj->handle, target, default_headers ? 1 : 0);
    if (res != CURLE_OK) {
        zend_throw_exception_ex(ci_exception_ce, (long)res,
            "Failed to impersonate '%s': %s", target, curl_easy_strerror(res));
        RETURN_THROWS();
    }
    /* Enable automatic decompression to match browser behavior;
     * impersonation sets Accept-Encoding headers but not the decode side.
     * Only when default_headers is on, since that's what injects Accept-Encoding. */
    if (default_headers) {
        curl_easy_setopt(obj->handle, CURLOPT_ENCODING, "");
    }
}

PHP_METHOD(Curl, close) {
    ZEND_PARSE_PARAMETERS_NONE();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(ZEND_THIS));
    if (obj->handle && !obj->closed) {
        curl_easy_cleanup(obj->handle);
        obj->handle = NULL;
        obj->closed = 1;
    }
}

PHP_METHOD(Curl, reset) {
    ZEND_PARSE_PARAMETERS_NONE();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(ZEND_THIS));
    if (!obj->handle || obj->closed) {
        zend_throw_exception(ci_exception_ce, "Curl handle is not initialized or is closed", 0);
        RETURN_THROWS();
    }

    curl_easy_reset(obj->handle);

    /* Free tracked slists */
    if (obj->req_headers) { curl_slist_free_all(obj->req_headers); obj->req_headers = NULL; }
    if (obj->proxy_headers) { curl_slist_free_all(obj->proxy_headers); obj->proxy_headers = NULL; }
    if (obj->resolve) { curl_slist_free_all(obj->resolve); obj->resolve = NULL; }

    /* Clear callbacks */
    zval_ptr_dtor(&obj->write_cb);
    ZVAL_UNDEF(&obj->write_cb);
    zval_ptr_dtor(&obj->header_cb);
    ZVAL_UNDEF(&obj->header_cb);

    /* Clear buffers */
    smart_str_free(&obj->body_buf);
    smart_str_free(&obj->header_buf);
    memset(&obj->body_buf, 0, sizeof(smart_str));
    memset(&obj->header_buf, 0, sizeof(smart_str));

    /* Re-set default callbacks */
    curl_easy_setopt(obj->handle, CURLOPT_ERRORBUFFER, obj->errbuf);
    curl_easy_setopt(obj->handle, CURLOPT_WRITEFUNCTION, ci_curl_write_cb);
    curl_easy_setopt(obj->handle, CURLOPT_WRITEDATA, obj);
    curl_easy_setopt(obj->handle, CURLOPT_HEADERFUNCTION, ci_curl_header_cb);
    curl_easy_setopt(obj->handle, CURLOPT_HEADERDATA, obj);
}

PHP_METHOD(Curl, dupHandle) {
    ZEND_PARSE_PARAMETERS_NONE();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(ZEND_THIS));
    if (!obj->handle || obj->closed) {
        zend_throw_exception(ci_exception_ce, "Curl handle is not initialized or is closed", 0);
        RETURN_THROWS();
    }

    object_init_ex(return_value, ci_curl_ce);
    ci_curl_obj *new_obj = ci_curl_from_obj(Z_OBJ_P(return_value));
    new_obj->handle = curl_easy_duphandle(obj->handle);

    if (!new_obj->handle) {
        zend_throw_exception(spl_ce_RuntimeException, "Failed to duplicate curl handle", 0);
        RETURN_THROWS();
    }

    /* Re-point callbacks to the new object */
    curl_easy_setopt(new_obj->handle, CURLOPT_ERRORBUFFER, new_obj->errbuf);
    curl_easy_setopt(new_obj->handle, CURLOPT_WRITEFUNCTION, ci_curl_write_cb);
    curl_easy_setopt(new_obj->handle, CURLOPT_WRITEDATA, new_obj);
    curl_easy_setopt(new_obj->handle, CURLOPT_HEADERFUNCTION, ci_curl_header_cb);
    curl_easy_setopt(new_obj->handle, CURLOPT_HEADERDATA, new_obj);
}

PHP_METHOD(Curl, getBody) {
    ZEND_PARSE_PARAMETERS_NONE();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(ZEND_THIS));
    if (obj->body_buf.s) {
        smart_str_0(&obj->body_buf);
        RETURN_STRINGL(ZSTR_VAL(obj->body_buf.s), ZSTR_LEN(obj->body_buf.s));
    }
    RETURN_EMPTY_STRING();
}

PHP_METHOD(Curl, getResponseHeaders) {
    ZEND_PARSE_PARAMETERS_NONE();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(ZEND_THIS));
    if (obj->header_buf.s) {
        smart_str_0(&obj->header_buf);
        RETURN_STRINGL(ZSTR_VAL(obj->header_buf.s), ZSTR_LEN(obj->header_buf.s));
    }
    RETURN_EMPTY_STRING();
}

PHP_METHOD(Curl, version) {
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_STRING(curl_version());
}

/* ========================================================================
 * CurlHandle arginfo
 * ======================================================================== */

ZEND_BEGIN_ARG_INFO_EX(arginfo_curl_construct, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_curl_setopt, 0, 2, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, option, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, value, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_curl_getinfo, 0, 1, IS_MIXED, 0)
    ZEND_ARG_TYPE_INFO(0, option, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_curl_perform, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_curl_impersonate, 0, 1, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, target, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, defaultHeaders, _IS_BOOL, 0, "true")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_curl_close, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_curl_reset, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_curl_duphandle, 0, 0, CurlImpersonate\\Curl, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_curl_getbody, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_curl_version, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry ci_curl_methods[] = {
    PHP_ME(Curl, __construct,        arginfo_curl_construct,   ZEND_ACC_PUBLIC)
    PHP_ME(Curl, setOpt,             arginfo_curl_setopt,      ZEND_ACC_PUBLIC)
    PHP_ME(Curl, getInfo,            arginfo_curl_getinfo,     ZEND_ACC_PUBLIC)
    PHP_ME(Curl, perform,            arginfo_curl_perform,     ZEND_ACC_PUBLIC)
    PHP_ME(Curl, impersonate,        arginfo_curl_impersonate, ZEND_ACC_PUBLIC)
    PHP_ME(Curl, close,              arginfo_curl_close,       ZEND_ACC_PUBLIC)
    PHP_ME(Curl, reset,              arginfo_curl_reset,       ZEND_ACC_PUBLIC)
    PHP_ME(Curl, dupHandle,          arginfo_curl_duphandle,   ZEND_ACC_PUBLIC)
    PHP_ME(Curl, getBody,            arginfo_curl_getbody,     ZEND_ACC_PUBLIC)
    PHP_ME(Curl, getResponseHeaders, arginfo_curl_getbody,     ZEND_ACC_PUBLIC)
    PHP_ME(Curl, version,            arginfo_curl_version,     ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_FE_END
};

/* ========================================================================
 * Response class
 * ======================================================================== */

PHP_METHOD(Response, json) {
    zend_bool assoc = 1;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(assoc)
    ZEND_PARSE_PARAMETERS_END();

    zval rv;
    zval *content = zend_read_property(ci_response_ce, Z_OBJ_P(ZEND_THIS),
        "content", sizeof("content") - 1, 1, &rv);

    if (content && Z_TYPE_P(content) == IS_STRING && Z_STRLEN_P(content) > 0) {
        php_json_decode_ex(return_value, Z_STRVAL_P(content), Z_STRLEN_P(content),
            assoc ? PHP_JSON_OBJECT_AS_ARRAY : 0, 512);
    } else {
        RETURN_NULL();
    }
}

PHP_METHOD(Response, text) {
    ZEND_PARSE_PARAMETERS_NONE();

    zval rv;
    zval *content = zend_read_property(ci_response_ce, Z_OBJ_P(ZEND_THIS),
        "content", sizeof("content") - 1, 1, &rv);

    if (content && Z_TYPE_P(content) == IS_STRING) {
        RETURN_STR_COPY(Z_STR_P(content));
    }
    RETURN_EMPTY_STRING();
}

PHP_METHOD(Response, raiseForStatus) {
    ZEND_PARSE_PARAMETERS_NONE();

    zval rv;
    zval *status = zend_read_property(ci_response_ce, Z_OBJ_P(ZEND_THIS),
        "statusCode", sizeof("statusCode") - 1, 1, &rv);

    if (status && Z_TYPE_P(status) == IS_LONG && Z_LVAL_P(status) >= 400) {
        zend_throw_exception_ex(ci_exception_ce, Z_LVAL_P(status),
            "HTTP Error: %ld", Z_LVAL_P(status));
        RETURN_THROWS();
    }
}

PHP_METHOD(Response, getHeader) {
    char *name;
    size_t name_len;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(name, name_len)
    ZEND_PARSE_PARAMETERS_END();

    zval rv;
    zval *headers = zend_read_property(ci_response_ce, Z_OBJ_P(ZEND_THIS),
        "headers", sizeof("headers") - 1, 1, &rv);

    if (!headers || Z_TYPE_P(headers) != IS_ARRAY) {
        RETURN_NULL();
    }

    /* Case-insensitive header lookup */
    zend_string *key;
    zval *val;
    ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(headers), key, val) {
        if (key && ZSTR_LEN(key) == name_len &&
            strncasecmp(ZSTR_VAL(key), name, name_len) == 0) {
            if (Z_TYPE_P(val) == IS_ARRAY) {
                /* Return first value */
                zval *first = zend_hash_index_find(Z_ARRVAL_P(val), 0);
                if (first) {
                    RETURN_ZVAL(first, 1, 0);
                }
            }
            RETURN_ZVAL(val, 1, 0);
        }
    } ZEND_HASH_FOREACH_END();

    RETURN_NULL();
}

/* Response arginfo */
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_response_json, 0, 0, IS_MIXED, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, assoc, _IS_BOOL, 0, "true")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_response_text, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_response_raise, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_response_getheader, 0, 1, IS_STRING, 1)
    ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry ci_response_methods[] = {
    PHP_ME(Response, json,           arginfo_response_json,      ZEND_ACC_PUBLIC)
    PHP_ME(Response, text,           arginfo_response_text,      ZEND_ACC_PUBLIC)
    PHP_ME(Response, raiseForStatus, arginfo_response_raise,     ZEND_ACC_PUBLIC)
    PHP_ME(Response, getHeader,      arginfo_response_getheader, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* ========================================================================
 * Session lifecycle
 * ======================================================================== */

static zend_object *ci_session_create(zend_class_entry *ce) {
    ci_session_obj *obj = zend_object_alloc(sizeof(ci_session_obj), ce);
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &ci_session_handlers;

    obj->handle = NULL;
    obj->req_headers = NULL;
    memset(obj->errbuf, 0, CURL_ERROR_SIZE);
    memset(&obj->body_buf, 0, sizeof(smart_str));
    memset(&obj->header_buf, 0, sizeof(smart_str));
    ZVAL_UNDEF(&obj->cookies);
    ZVAL_UNDEF(&obj->def_headers);
    ZVAL_UNDEF(&obj->def_params);
    obj->base_url = NULL;
    obj->impersonate_target = NULL;
    obj->timeout = 0;
    obj->max_redirects = 30;
    obj->allow_redirects = 1;
    obj->verify = 1;
    obj->raise_for_status = 0;
    obj->proxy = NULL;
    obj->closed = 0;

    return &obj->std;
}

static void ci_session_free(zend_object *object) {
    ci_session_obj *obj = ci_session_from_obj(object);

    if (obj->handle && !obj->closed) {
        curl_easy_cleanup(obj->handle);
        obj->handle = NULL;
    }
    if (obj->req_headers) { curl_slist_free_all(obj->req_headers); obj->req_headers = NULL; }
    zval_ptr_dtor(&obj->cookies);
    zval_ptr_dtor(&obj->def_headers);
    zval_ptr_dtor(&obj->def_params);
    smart_str_free(&obj->body_buf);
    smart_str_free(&obj->header_buf);

    if (obj->base_url) { efree(obj->base_url); obj->base_url = NULL; }
    if (obj->impersonate_target) { efree(obj->impersonate_target); obj->impersonate_target = NULL; }
    if (obj->proxy) { efree(obj->proxy); obj->proxy = NULL; }

    zend_object_std_dtor(&obj->std);
}

/* ========================================================================
 * Session internal request helper
 * ======================================================================== */

static void ci_session_do_request(INTERNAL_FUNCTION_PARAMETERS, const char *method) {
    char *url;
    size_t url_len;
    zval *options = NULL;

    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(url, url_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_EX(options, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    ci_session_obj *session = ci_session_from_obj(Z_OBJ_P(ZEND_THIS));
    if (session->closed) {
        zend_throw_exception(ci_exception_ce, "Session is closed", 0);
        RETURN_THROWS();
    }

    CURL *ch = session->handle;
    HashTable *opts = options ? Z_ARRVAL_P(options) : NULL;

    /* --- Clear previous request state --- */
    smart_str_free(&session->body_buf);
    smart_str_free(&session->header_buf);
    memset(&session->body_buf, 0, sizeof(smart_str));
    memset(&session->header_buf, 0, sizeof(smart_str));
    memset(session->errbuf, 0, CURL_ERROR_SIZE);

    if (session->req_headers) {
        curl_slist_free_all(session->req_headers);
        session->req_headers = NULL;
    }

    /* --- Build URL --- */
    smart_str full_url = {0};

    if (url_len == 0 && session->base_url) {
        /* Empty URL: use base_url as-is */
        smart_str_appends(&full_url, session->base_url);
    } else if (session->base_url && url_len > 0 && !memchr(url, ':', url_len > 10 ? 10 : url_len)) {
        /* Relative URL (no scheme): resolve against base_url */
        char *base = session->base_url;
        size_t base_len = strlen(base);

        if (url[0] == '/') {
            /* Absolute path: use scheme + host from base */
            php_url *parsed = php_url_parse(base);
            if (parsed) {
                if (parsed->scheme) {
                    smart_str_append(&full_url, parsed->scheme);
                } else {
                    smart_str_appends(&full_url, "http");
                }
                smart_str_appends(&full_url, "://");
                if (parsed->host) smart_str_append(&full_url, parsed->host);
                if (parsed->port) {
                    smart_str_appendc(&full_url, ':');
                    smart_str_append_long(&full_url, parsed->port);
                }
                smart_str_appends(&full_url, url);
                php_url_free(parsed);
            } else {
                smart_str_appends(&full_url, url);
            }
        } else {
            /* Relative path: replace last segment */
            const char *last_slash = strrchr(base, '/');
            /* Find the slash after :// */
            const char *scheme_end = strstr(base, "://");
            if (scheme_end) {
                const char *path_start = strchr(scheme_end + 3, '/');
                if (path_start && last_slash > path_start) {
                    smart_str_appendl(&full_url, base, last_slash - base + 1);
                } else {
                    smart_str_appendl(&full_url, base, base_len);
                    /* Strip query string from base */
                    const char *q = memchr(ZSTR_VAL(full_url.s), '?', ZSTR_LEN(full_url.s));
                    if (q) {
                        ZSTR_LEN(full_url.s) = q - ZSTR_VAL(full_url.s);
                    }
                    if (ZSTR_VAL(full_url.s)[ZSTR_LEN(full_url.s) - 1] != '/') {
                        smart_str_appendc(&full_url, '/');
                    }
                }
            } else {
                smart_str_appendl(&full_url, base, base_len);
                smart_str_appendc(&full_url, '/');
            }
            smart_str_appends(&full_url, url);
        }
    } else {
        smart_str_appendl(&full_url, url, url_len);
    }

    /* --- Append query params --- */
    zval *params_opt = opts ? zend_hash_str_find(opts, "params", sizeof("params") - 1) : NULL;
    HashTable *merged_params = NULL;
    int free_merged = 0;

    if ((params_opt && Z_TYPE_P(params_opt) == IS_ARRAY) ||
        Z_TYPE(session->def_params) == IS_ARRAY) {
        ALLOC_HASHTABLE(merged_params);
        zend_hash_init(merged_params, 8, NULL, ZVAL_PTR_DTOR, 0);
        free_merged = 1;

        /* Add session default params */
        if (Z_TYPE(session->def_params) == IS_ARRAY) {
            zend_string *key;
            zval *val;
            ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL(session->def_params), key, val) {
                if (key) {
                    Z_TRY_ADDREF_P(val);
                    zend_hash_update(merged_params, key, val);
                }
            } ZEND_HASH_FOREACH_END();
        }

        /* Override with request params */
        if (params_opt && Z_TYPE_P(params_opt) == IS_ARRAY) {
            zend_string *key;
            zval *val;
            ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(params_opt), key, val) {
                if (key) {
                    Z_TRY_ADDREF_P(val);
                    zend_hash_update(merged_params, key, val);
                }
            } ZEND_HASH_FOREACH_END();
        }

        if (zend_hash_num_elements(merged_params) > 0) {
            smart_str_0(&full_url);
            /* Check if URL already has query string */
            int has_query = (full_url.s && memchr(ZSTR_VAL(full_url.s), '?', ZSTR_LEN(full_url.s)));
            smart_str_appendc(&full_url, has_query ? '&' : '?');

            zend_string *key;
            zval *val;
            int first = 1;
            ZEND_HASH_FOREACH_STR_KEY_VAL(merged_params, key, val) {
                if (!key) continue;
                if (!first) smart_str_appendc(&full_url, '&');
                first = 0;
                zend_string *vs = zval_get_string(val);
                zend_string *ek = php_url_encode(ZSTR_VAL(key), ZSTR_LEN(key));
                zend_string *ev = php_url_encode(ZSTR_VAL(vs), ZSTR_LEN(vs));
                smart_str_append(&full_url, ek);
                smart_str_appendc(&full_url, '=');
                smart_str_append(&full_url, ev);
                zend_string_release(vs);
                zend_string_release(ek);
                zend_string_release(ev);
            } ZEND_HASH_FOREACH_END();
        }
    }

    smart_str_0(&full_url);
    curl_easy_setopt(ch, CURLOPT_URL, ZSTR_VAL(full_url.s));

    /* --- Handle request body --- */
    zval *data_opt = opts ? zend_hash_str_find(opts, "data", sizeof("data") - 1) : NULL;
    zval *json_opt = opts ? zend_hash_str_find(opts, "json", sizeof("json") - 1) : NULL;
    int has_content_type = 0;
    smart_str post_body = {0};

    /* Clear any previous POST data before setting method, since
     * CURLOPT_POSTFIELDS can re-enable POST mode on a reused handle */
    curl_easy_setopt(ch, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(ch, CURLOPT_POSTFIELDSIZE, -1L);

    if (json_opt && Z_TYPE_P(json_opt) != IS_NULL) {
        /* JSON body */
        php_json_encode(&post_body, json_opt, 0);
        smart_str_0(&post_body);
        curl_easy_setopt(ch, CURLOPT_POSTFIELDSIZE, (long)ZSTR_LEN(post_body.s));
        curl_easy_setopt(ch, CURLOPT_COPYPOSTFIELDS, ZSTR_VAL(post_body.s));
        has_content_type = 1; /* Will add Content-Type: application/json */
    } else if (data_opt && Z_TYPE_P(data_opt) != IS_NULL) {
        if (Z_TYPE_P(data_opt) == IS_STRING) {
            curl_easy_setopt(ch, CURLOPT_POSTFIELDSIZE, (long)Z_STRLEN_P(data_opt));
            curl_easy_setopt(ch, CURLOPT_COPYPOSTFIELDS, Z_STRVAL_P(data_opt));
        } else if (Z_TYPE_P(data_opt) == IS_ARRAY) {
            zend_string *encoded = ci_urlencode_array(data_opt);
            if (encoded) {
                curl_easy_setopt(ch, CURLOPT_POSTFIELDSIZE, (long)ZSTR_LEN(encoded));
                curl_easy_setopt(ch, CURLOPT_COPYPOSTFIELDS, ZSTR_VAL(encoded));
                zend_string_release(encoded);
            }
        }
    }

    /* --- Set HTTP method (after body, so HTTPGET overrides any POST state) --- */
    if (strcmp(method, "GET") == 0) {
        curl_easy_setopt(ch, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(ch, CURLOPT_NOBODY, 0L);
        curl_easy_setopt(ch, CURLOPT_CUSTOMREQUEST, NULL);
        /* If body was set for GET, use CUSTOMREQUEST to preserve GET method */
        if ((json_opt && Z_TYPE_P(json_opt) != IS_NULL) ||
            (data_opt && Z_TYPE_P(data_opt) != IS_NULL)) {
            curl_easy_setopt(ch, CURLOPT_CUSTOMREQUEST, "GET");
        }
    } else if (strcmp(method, "POST") == 0) {
        curl_easy_setopt(ch, CURLOPT_POST, 1L);
        curl_easy_setopt(ch, CURLOPT_NOBODY, 0L);
        curl_easy_setopt(ch, CURLOPT_CUSTOMREQUEST, NULL);
    } else if (strcmp(method, "HEAD") == 0) {
        curl_easy_setopt(ch, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(ch, CURLOPT_CUSTOMREQUEST, NULL);
    } else {
        curl_easy_setopt(ch, CURLOPT_NOBODY, 0L);
        curl_easy_setopt(ch, CURLOPT_CUSTOMREQUEST, method);
    }

    /* --- Build headers --- */
    zval *headers_opt = opts ? zend_hash_str_find(opts, "headers", sizeof("headers") - 1) : NULL;
    int user_set_content_type = 0;

    /* Add default session headers */
    if (Z_TYPE(session->def_headers) == IS_ARRAY) {
        zend_string *key;
        zval *val;
        ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL(session->def_headers), key, val) {
            if (!key) continue;
            smart_str hdr = {0};
            smart_str_append(&hdr, key);
            smart_str_appends(&hdr, ": ");
            zend_string *sv = zval_get_string(val);
            smart_str_append(&hdr, sv);
            smart_str_0(&hdr);
            session->req_headers = curl_slist_append(session->req_headers, ZSTR_VAL(hdr.s));
            smart_str_free(&hdr);
            zend_string_release(sv);
        } ZEND_HASH_FOREACH_END();
    }

    /* Add request-specific headers */
    if (headers_opt && Z_TYPE_P(headers_opt) == IS_ARRAY) {
        zend_string *key;
        zval *val;
        ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(headers_opt), key, val) {
            if (!key) continue;

            /* Check if user set Content-Type */
            if (ZSTR_LEN(key) == 12 && strncasecmp(ZSTR_VAL(key), "content-type", 12) == 0) {
                user_set_content_type = 1;
            }

            /* NULL value means remove the header */
            if (Z_TYPE_P(val) == IS_NULL) {
                smart_str hdr = {0};
                smart_str_append(&hdr, key);
                smart_str_appendc(&hdr, ':');
                smart_str_0(&hdr);
                session->req_headers = curl_slist_append(session->req_headers, ZSTR_VAL(hdr.s));
                smart_str_free(&hdr);
                continue;
            }

            smart_str hdr = {0};
            smart_str_append(&hdr, key);
            smart_str_appends(&hdr, ": ");
            zend_string *sv = zval_get_string(val);
            smart_str_append(&hdr, sv);
            smart_str_0(&hdr);
            session->req_headers = curl_slist_append(session->req_headers, ZSTR_VAL(hdr.s));
            smart_str_free(&hdr);
            zend_string_release(sv);
        } ZEND_HASH_FOREACH_END();
    }

    /* Add Content-Type for JSON if not already set */
    if (has_content_type && !user_set_content_type) {
        session->req_headers = curl_slist_append(session->req_headers, "Content-Type: application/json");
    }

    if (session->req_headers) {
        curl_easy_setopt(ch, CURLOPT_HTTPHEADER, session->req_headers);
    } else {
        curl_easy_setopt(ch, CURLOPT_HTTPHEADER, NULL);
    }

    /* --- Handle cookies --- */
    zval *cookies_opt = opts ? zend_hash_str_find(opts, "cookies", sizeof("cookies") - 1) : NULL;
    HashTable *merged_cookies = NULL;
    int free_cookies = 0;

    if ((cookies_opt && Z_TYPE_P(cookies_opt) == IS_ARRAY) ||
        (Z_TYPE(session->cookies) == IS_ARRAY && zend_hash_num_elements(Z_ARRVAL(session->cookies)) > 0)) {
        ALLOC_HASHTABLE(merged_cookies);
        zend_hash_init(merged_cookies, 8, NULL, ZVAL_PTR_DTOR, 0);
        free_cookies = 1;

        /* Session cookies first */
        if (Z_TYPE(session->cookies) == IS_ARRAY) {
            zend_string *key;
            zval *val;
            ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL(session->cookies), key, val) {
                if (key) { Z_TRY_ADDREF_P(val); zend_hash_update(merged_cookies, key, val); }
            } ZEND_HASH_FOREACH_END();
        }

        /* Request cookies override */
        if (cookies_opt && Z_TYPE_P(cookies_opt) == IS_ARRAY) {
            zend_string *key;
            zval *val;
            ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(cookies_opt), key, val) {
                if (key) { Z_TRY_ADDREF_P(val); zend_hash_update(merged_cookies, key, val); }
            } ZEND_HASH_FOREACH_END();
        }

        zend_string *cookie_str = ci_build_cookie_string(merged_cookies);
        if (cookie_str) {
            curl_easy_setopt(ch, CURLOPT_COOKIE, ZSTR_VAL(cookie_str));
            zend_string_release(cookie_str);
        }
    } else {
        curl_easy_setopt(ch, CURLOPT_COOKIE, NULL);
    }

    /* --- Handle auth --- */
    zval *auth_opt = opts ? zend_hash_str_find(opts, "auth", sizeof("auth") - 1) : NULL;
    if (auth_opt && Z_TYPE_P(auth_opt) == IS_ARRAY) {
        zval *user = zend_hash_index_find(Z_ARRVAL_P(auth_opt), 0);
        zval *pass = zend_hash_index_find(Z_ARRVAL_P(auth_opt), 1);
        if (user) {
            zend_string *us = zval_get_string(user);
            curl_easy_setopt(ch, CURLOPT_USERNAME, ZSTR_VAL(us));
            zend_string_release(us);
        }
        if (pass) {
            zend_string *ps = zval_get_string(pass);
            curl_easy_setopt(ch, CURLOPT_PASSWORD, ZSTR_VAL(ps));
            zend_string_release(ps);
        }
    }

    /* --- Timeout --- */
    double timeout = session->timeout;
    zval *timeout_opt = opts ? zend_hash_str_find(opts, "timeout", sizeof("timeout") - 1) : NULL;
    if (timeout_opt) timeout = zval_get_double(timeout_opt);
    if (timeout > 0) {
        curl_easy_setopt(ch, CURLOPT_TIMEOUT_MS, (long)(timeout * 1000));
    } else {
        curl_easy_setopt(ch, CURLOPT_TIMEOUT_MS, 0L);
    }

    /* --- Redirects --- */
    zend_bool allow_redir = session->allow_redirects;
    zval *redir_opt = opts ? zend_hash_str_find(opts, "allow_redirects", sizeof("allow_redirects") - 1) : NULL;
    if (redir_opt) allow_redir = zval_is_true(redir_opt);
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, allow_redir ? 1L : 0L);

    long max_redir = session->max_redirects;
    zval *maxredir_opt = opts ? zend_hash_str_find(opts, "max_redirects", sizeof("max_redirects") - 1) : NULL;
    if (maxredir_opt) max_redir = zval_get_long(maxredir_opt);
    curl_easy_setopt(ch, CURLOPT_MAXREDIRS, max_redir);

    /* --- SSL verify --- */
    zend_bool verify = session->verify;
    zval *verify_opt = opts ? zend_hash_str_find(opts, "verify", sizeof("verify") - 1) : NULL;
    if (verify_opt) verify = zval_is_true(verify_opt);
    curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, verify ? 1L : 0L);
    curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, verify ? 2L : 0L);

    /* --- Proxy --- */
    char *proxy = session->proxy;
    zval *proxy_opt = opts ? zend_hash_str_find(opts, "proxy", sizeof("proxy") - 1) : NULL;
    if (proxy_opt && Z_TYPE_P(proxy_opt) == IS_STRING) proxy = Z_STRVAL_P(proxy_opt);
    if (proxy) {
        curl_easy_setopt(ch, CURLOPT_PROXY, proxy);
    }

    /* --- Referer --- */
    zval *referer_opt = opts ? zend_hash_str_find(opts, "referer", sizeof("referer") - 1) : NULL;
    if (referer_opt && Z_TYPE_P(referer_opt) == IS_STRING) {
        curl_easy_setopt(ch, CURLOPT_REFERER, Z_STRVAL_P(referer_opt));
    }

    /* --- Impersonate (per-request override) --- */
    zval *imp_opt = opts ? zend_hash_str_find(opts, "impersonate", sizeof("impersonate") - 1) : NULL;
    if (imp_opt && Z_TYPE_P(imp_opt) == IS_STRING) {
        curl_easy_impersonate(ch, Z_STRVAL_P(imp_opt), 1);
        curl_easy_setopt(ch, CURLOPT_ENCODING, "");
    }

    /* --- Set callbacks --- */
    curl_easy_setopt(ch, CURLOPT_ERRORBUFFER, session->errbuf);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, ci_session_write_cb);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, session);
    curl_easy_setopt(ch, CURLOPT_HEADERFUNCTION, ci_session_header_cb);
    curl_easy_setopt(ch, CURLOPT_HEADERDATA, session);

    /* --- Perform --- */
    CURLcode res = curl_easy_perform(ch);

    /* Collect response info before potential error handling */
    long status_code = 0;
    char *effective_url = NULL;
    double total_time = 0;
    long redirect_count = 0;

    curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &status_code);
    curl_easy_getinfo(ch, CURLINFO_EFFECTIVE_URL, &effective_url);
    curl_easy_getinfo(ch, CURLINFO_TOTAL_TIME, &total_time);
    curl_easy_getinfo(ch, CURLINFO_REDIRECT_COUNT, &redirect_count);

    /* Handle errors */
    if (res != CURLE_OK) {
        const char *err = session->errbuf[0] ? session->errbuf : curl_easy_strerror(res);

        /* For redirect errors, create a partial response and attach it */
        if (res == CURLE_TOO_MANY_REDIRECTS) {
            zval resp_obj;
            object_init_ex(&resp_obj, ci_response_ce);
            zend_update_property_long(ci_response_ce, Z_OBJ(resp_obj), "statusCode", sizeof("statusCode") - 1, status_code);
            if (effective_url) {
                zend_update_property_string(ci_response_ce, Z_OBJ(resp_obj), "url", sizeof("url") - 1, effective_url);
            }
            zend_update_property_long(ci_response_ce, Z_OBJ(resp_obj), "redirectCount", sizeof("redirectCount") - 1, redirect_count);

            zval ex;
            object_init_ex(&ex, ci_exception_ce);
            zend_update_property_string(ci_exception_ce, Z_OBJ(ex), "message", sizeof("message") - 1, err);
            zend_update_property_long(ci_exception_ce, Z_OBJ(ex), "code", sizeof("code") - 1, (long)res);
            zend_update_property(ci_exception_ce, Z_OBJ(ex), "response", sizeof("response") - 1, &resp_obj);
            zval_ptr_dtor(&resp_obj);
            zend_throw_exception_object(&ex);
        } else {
            zend_throw_exception_ex(ci_exception_ce, (long)res,
                "curl: (%d) %s", (int)res, err);
        }

        /* Cleanup */
        smart_str_free(&post_body);
        smart_str_free(&full_url);
        if (free_merged) { zend_hash_destroy(merged_params); FREE_HASHTABLE(merged_params); }
        if (free_cookies) { zend_hash_destroy(merged_cookies); FREE_HASHTABLE(merged_cookies); }
        RETURN_THROWS();
    }

    /* --- Create Response object --- */
    object_init_ex(return_value, ci_response_ce);
    zend_object *resp = Z_OBJ_P(return_value);

    zend_update_property_long(ci_response_ce, resp, "statusCode", sizeof("statusCode") - 1, status_code);
    if (effective_url) {
        zend_update_property_string(ci_response_ce, resp, "url", sizeof("url") - 1, effective_url);
    }
    zend_update_property_double(ci_response_ce, resp, "elapsed", sizeof("elapsed") - 1, total_time);
    zend_update_property_long(ci_response_ce, resp, "redirectCount", sizeof("redirectCount") - 1, redirect_count);

    /* Set body */
    smart_str_0(&session->body_buf);
    if (session->body_buf.s) {
        zend_update_property_stringl(ci_response_ce, resp, "content", sizeof("content") - 1,
            ZSTR_VAL(session->body_buf.s), ZSTR_LEN(session->body_buf.s));
    } else {
        zend_update_property_string(ci_response_ce, resp, "content", sizeof("content") - 1, "");
    }

    /* Parse headers */
    smart_str_0(&session->header_buf);
    zval headers_arr, resp_cookies;
    zend_string *reason = NULL;
    array_init(&headers_arr);
    array_init(&resp_cookies);

    if (session->header_buf.s && ZSTR_LEN(session->header_buf.s) > 0) {
        ci_parse_headers(ZSTR_VAL(session->header_buf.s), ZSTR_LEN(session->header_buf.s),
                         &headers_arr, &resp_cookies, &reason);
    }

    zend_update_property(ci_response_ce, resp, "headers", sizeof("headers") - 1, &headers_arr);
    zend_update_property(ci_response_ce, resp, "cookies", sizeof("cookies") - 1, &resp_cookies);

    if (reason) {
        zend_update_property_str(ci_response_ce, resp, "reason", sizeof("reason") - 1, reason);
        zend_string_release(reason);
    } else {
        zend_update_property_string(ci_response_ce, resp, "reason", sizeof("reason") - 1, "");
    }

    /* Update session cookies from response Set-Cookie headers */
    if (Z_TYPE(session->cookies) == IS_ARRAY && zend_hash_num_elements(Z_ARRVAL(resp_cookies)) > 0) {
        zend_string *key;
        zval *val;
        ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL(resp_cookies), key, val) {
            if (key) {
                Z_TRY_ADDREF_P(val);
                zend_hash_update(Z_ARRVAL(session->cookies), key, val);
            }
        } ZEND_HASH_FOREACH_END();
    }

    zval_ptr_dtor(&headers_arr);
    zval_ptr_dtor(&resp_cookies);

    /* Cleanup */
    smart_str_free(&post_body);
    smart_str_free(&full_url);
    if (free_merged) { zend_hash_destroy(merged_params); FREE_HASHTABLE(merged_params); }
    if (free_cookies) { zend_hash_destroy(merged_cookies); FREE_HASHTABLE(merged_cookies); }

    /* Auto raise for status */
    zend_bool do_raise = session->raise_for_status;
    zval *raise_opt = opts ? zend_hash_str_find(opts, "raise_for_status", sizeof("raise_for_status") - 1) : NULL;
    if (raise_opt) do_raise = zval_is_true(raise_opt);

    if (do_raise && status_code >= 400) {
        zval ex;
        object_init_ex(&ex, ci_exception_ce);
        zend_string *msg = strpprintf(0, "HTTP Error: %ld", status_code);
        zend_update_property_str(ci_exception_ce, Z_OBJ(ex), "message", sizeof("message") - 1, msg);
        zend_update_property_long(ci_exception_ce, Z_OBJ(ex), "code", sizeof("code") - 1, status_code);
        zend_update_property(ci_exception_ce, Z_OBJ(ex), "response", sizeof("response") - 1, return_value);
        zend_string_release(msg);
        zval_ptr_dtor(return_value);
        ZVAL_UNDEF(return_value);
        zend_throw_exception_object(&ex);
        RETURN_THROWS();
    }
}

/* ========================================================================
 * Session methods
 * ======================================================================== */

PHP_METHOD(Session, __construct) {
    zval *options = NULL;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_EX(options, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    ci_session_obj *session = ci_session_from_obj(Z_OBJ_P(ZEND_THIS));

    /* Initialize curl handle */
    session->handle = curl_easy_init();
    if (!session->handle) {
        zend_throw_exception(spl_ce_RuntimeException, "Failed to initialize curl handle for session", 0);
        RETURN_THROWS();
    }

    /* Initialize cookies array */
    array_init(&session->cookies);
    array_init(&session->def_headers);
    array_init(&session->def_params);

    HashTable *opts = options ? Z_ARRVAL_P(options) : NULL;
    if (!opts) return;

    /* Parse options */
    zval *val;

    if ((val = zend_hash_str_find(opts, "impersonate", sizeof("impersonate") - 1)) && Z_TYPE_P(val) == IS_STRING) {
        session->impersonate_target = estrndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
        curl_easy_impersonate(session->handle, session->impersonate_target, 1);
        curl_easy_setopt(session->handle, CURLOPT_ENCODING, "");
    }

    if ((val = zend_hash_str_find(opts, "cookies", sizeof("cookies") - 1)) && Z_TYPE_P(val) == IS_ARRAY) {
        zend_string *key;
        zval *v;
        ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(val), key, v) {
            if (key) { Z_TRY_ADDREF_P(v); zend_hash_update(Z_ARRVAL(session->cookies), key, v); }
        } ZEND_HASH_FOREACH_END();
    }

    if ((val = zend_hash_str_find(opts, "headers", sizeof("headers") - 1)) && Z_TYPE_P(val) == IS_ARRAY) {
        zend_string *key;
        zval *v;
        ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(val), key, v) {
            if (key) { Z_TRY_ADDREF_P(v); zend_hash_update(Z_ARRVAL(session->def_headers), key, v); }
        } ZEND_HASH_FOREACH_END();
    }

    if ((val = zend_hash_str_find(opts, "params", sizeof("params") - 1)) && Z_TYPE_P(val) == IS_ARRAY) {
        zend_string *key;
        zval *v;
        ZEND_HASH_FOREACH_STR_KEY_VAL(Z_ARRVAL_P(val), key, v) {
            if (key) { Z_TRY_ADDREF_P(v); zend_hash_update(Z_ARRVAL(session->def_params), key, v); }
        } ZEND_HASH_FOREACH_END();
    }

    if ((val = zend_hash_str_find(opts, "base_url", sizeof("base_url") - 1)) && Z_TYPE_P(val) == IS_STRING) {
        session->base_url = estrndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
    }

    if ((val = zend_hash_str_find(opts, "timeout", sizeof("timeout") - 1))) {
        session->timeout = zval_get_double(val);
    }

    if ((val = zend_hash_str_find(opts, "max_redirects", sizeof("max_redirects") - 1))) {
        session->max_redirects = zval_get_long(val);
    }

    if ((val = zend_hash_str_find(opts, "allow_redirects", sizeof("allow_redirects") - 1))) {
        session->allow_redirects = zval_is_true(val);
    }

    if ((val = zend_hash_str_find(opts, "verify", sizeof("verify") - 1))) {
        session->verify = zval_is_true(val);
    }

    if ((val = zend_hash_str_find(opts, "raise_for_status", sizeof("raise_for_status") - 1))) {
        session->raise_for_status = zval_is_true(val);
    }

    if ((val = zend_hash_str_find(opts, "proxy", sizeof("proxy") - 1)) && Z_TYPE_P(val) == IS_STRING) {
        session->proxy = estrndup(Z_STRVAL_P(val), Z_STRLEN_P(val));
    }
}

PHP_METHOD(Session, get)     { ci_session_do_request(INTERNAL_FUNCTION_PARAM_PASSTHRU, "GET"); }
PHP_METHOD(Session, post)    { ci_session_do_request(INTERNAL_FUNCTION_PARAM_PASSTHRU, "POST"); }
PHP_METHOD(Session, put)     { ci_session_do_request(INTERNAL_FUNCTION_PARAM_PASSTHRU, "PUT"); }
PHP_METHOD(Session, delete_) { ci_session_do_request(INTERNAL_FUNCTION_PARAM_PASSTHRU, "DELETE"); }
PHP_METHOD(Session, head)    { ci_session_do_request(INTERNAL_FUNCTION_PARAM_PASSTHRU, "HEAD"); }
PHP_METHOD(Session, options) { ci_session_do_request(INTERNAL_FUNCTION_PARAM_PASSTHRU, "OPTIONS"); }
PHP_METHOD(Session, patch)   { ci_session_do_request(INTERNAL_FUNCTION_PARAM_PASSTHRU, "PATCH"); }

PHP_METHOD(Session, close) {
    ZEND_PARSE_PARAMETERS_NONE();

    ci_session_obj *session = ci_session_from_obj(Z_OBJ_P(ZEND_THIS));
    if (session->handle && !session->closed) {
        curl_easy_cleanup(session->handle);
        session->handle = NULL;
        session->closed = 1;
    }
}

/* Session arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_session_construct, 0, 0, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, options, IS_ARRAY, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_session_request, 0, 1, CurlImpersonate\\Response, 0)
    ZEND_ARG_TYPE_INFO(0, url, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, options, IS_ARRAY, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_session_close, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry ci_session_methods[] = {
    PHP_ME(Session, __construct, arginfo_session_construct, ZEND_ACC_PUBLIC)
    PHP_ME(Session, get,         arginfo_session_request,   ZEND_ACC_PUBLIC)
    PHP_ME(Session, post,        arginfo_session_request,   ZEND_ACC_PUBLIC)
    PHP_ME(Session, put,         arginfo_session_request,   ZEND_ACC_PUBLIC)
    PHP_ME(Session, delete_,     arginfo_session_request,   ZEND_ACC_PUBLIC)
    PHP_ME(Session, head,        arginfo_session_request,   ZEND_ACC_PUBLIC)
    PHP_ME(Session, options,     arginfo_session_request,   ZEND_ACC_PUBLIC)
    PHP_ME(Session, patch,       arginfo_session_request,   ZEND_ACC_PUBLIC)
    PHP_ME(Session, close,       arginfo_session_close,     ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* ========================================================================
 * Module-level functions
 * ======================================================================== */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_ci_version, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

PHP_FUNCTION(curl_cffi_version) {
    ZEND_PARSE_PARAMETERS_NONE();

    curl_version_info_data *d = curl_version_info(CURLVERSION_NOW);
    array_init(return_value);

    add_assoc_long(return_value, "age", d->age);
    add_assoc_string(return_value, "version", (char *)(d->version ? d->version : ""));
    add_assoc_long(return_value, "version_number", d->version_num);
    add_assoc_string(return_value, "host", (char *)(d->host ? d->host : ""));
    add_assoc_long(return_value, "features", d->features);
    add_assoc_string(return_value, "ssl_version", (char *)(d->ssl_version ? d->ssl_version : ""));
    add_assoc_long(return_value, "ssl_version_number", 0);
    add_assoc_string(return_value, "libz_version", (char *)(d->libz_version ? d->libz_version : ""));

    /* Protocols array */
    zval protocols;
    array_init(&protocols);
    if (d->protocols) {
        const char * const *p = d->protocols;
        while (*p) {
            add_next_index_string(&protocols, (char *)*p);
            p++;
        }
    }
    add_assoc_zval(return_value, "protocols", &protocols);

    /* Feature list array */
    zval feature_list;
    array_init(&feature_list);
    /* Feature names matching PHP's curl_version() output exactly */
    struct { const char *name; int flag; } features[] = {
        {"AsynchDNS", CURL_VERSION_ASYNCHDNS},
#ifdef CURL_VERSION_CHARCONV
        {"CharConv", CURL_VERSION_CHARCONV},
#else
        {"CharConv", 0}, /* Removed in newer curl, always false */
#endif
        {"Debug", CURL_VERSION_DEBUG},
#ifdef CURL_VERSION_GSSNEGOTIATE
        {"GSS-Negotiate", CURL_VERSION_GSSNEGOTIATE},
#endif
        {"IDN", CURL_VERSION_IDN},
        {"IPv6", CURL_VERSION_IPV6},
        {"krb4", CURL_VERSION_KERBEROS4},
        {"Largefile", CURL_VERSION_LARGEFILE},
        {"libz", CURL_VERSION_LIBZ},
        {"NTLM", CURL_VERSION_NTLM},
#ifdef CURL_VERSION_NTLM_WB
        {"NTLMWB", CURL_VERSION_NTLM_WB},
#else
        {"NTLMWB", 0},
#endif
#ifdef CURL_VERSION_SPNEGO
        {"SPNEGO", CURL_VERSION_SPNEGO},
#else
        {"SPNEGO", 0},
#endif
        {"SSL", CURL_VERSION_SSL},
        {"SSPI", CURL_VERSION_SSPI},
#ifdef CURL_VERSION_TLSAUTH_SRP
        {"TLS-SRP", CURL_VERSION_TLSAUTH_SRP},
#endif
#ifdef CURL_VERSION_HTTP2
        {"HTTP2", CURL_VERSION_HTTP2},
#endif
#ifdef CURL_VERSION_GSSAPI
        {"GSSAPI", CURL_VERSION_GSSAPI},
#endif
#ifdef CURL_VERSION_KERBEROS5
        {"KERBEROS5", CURL_VERSION_KERBEROS5},
#endif
#ifdef CURL_VERSION_UNIX_SOCKETS
        {"UNIX_SOCKETS", CURL_VERSION_UNIX_SOCKETS},
#endif
#ifdef CURL_VERSION_PSL
        {"PSL", CURL_VERSION_PSL},
#endif
#ifdef CURL_VERSION_HTTPS_PROXY
        {"HTTPS_PROXY", CURL_VERSION_HTTPS_PROXY},
#endif
#ifdef CURL_VERSION_MULTI_SSL
        {"MULTI_SSL", CURL_VERSION_MULTI_SSL},
#endif
#ifdef CURL_VERSION_BROTLI
        {"BROTLI", CURL_VERSION_BROTLI},
#endif
        {NULL, 0}
    };
    for (int i = 0; features[i].name; i++) {
        add_assoc_bool(&feature_list, features[i].name, (d->features & features[i].flag) ? 1 : 0);
    }
    add_assoc_zval(return_value, "feature_list", &feature_list);
}

/* ========================================================================
 * Procedural API (matching PHP's curl_* function style)
 * ======================================================================== */

/* {{{ curl_cffi_init() */
PHP_FUNCTION(curl_cffi_init) {
    char *url = NULL;
    size_t url_len = 0;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING_OR_NULL(url, url_len)
    ZEND_PARSE_PARAMETERS_END();

    object_init_ex(return_value, ci_curl_ce);
    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(return_value));
    obj->handle = curl_easy_init();

    if (!obj->handle) {
        zval_ptr_dtor(return_value);
        RETURN_FALSE;
    }

    curl_easy_setopt(obj->handle, CURLOPT_ERRORBUFFER, obj->errbuf);
    curl_easy_setopt(obj->handle, CURLOPT_WRITEFUNCTION, ci_curl_write_cb);
    curl_easy_setopt(obj->handle, CURLOPT_WRITEDATA, obj);
    curl_easy_setopt(obj->handle, CURLOPT_HEADERFUNCTION, ci_curl_header_cb);
    curl_easy_setopt(obj->handle, CURLOPT_HEADERDATA, obj);

    /* Suppress curl's default stderr output (progress meter, verbose) */
    obj->devnull = fopen("/dev/null", "w");
    if (obj->devnull) {
        curl_easy_setopt(obj->handle, CURLOPT_STDERR, obj->devnull);
    }

    if (url && url_len > 0) {
        curl_easy_setopt(obj->handle, CURLOPT_URL, url);
    }
}
/* }}} */

/* {{{ curl_cffi_setopt($ch, $option, $value) */
PHP_FUNCTION(curl_cffi_setopt) {
    zval *handle;
    zend_long option;
    zval *value;

    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_OBJECT_OF_CLASS(handle, ci_curl_ce)
        Z_PARAM_LONG(option)
        Z_PARAM_ZVAL(value)
    ZEND_PARSE_PARAMETERS_END();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(handle));
    if (!obj->handle || obj->closed) {
        php_error_docref(NULL, E_WARNING, "Curl handle is not initialized or closed");
        RETURN_FALSE;
    }

    CURLoption opt = (CURLoption)option;
    CURLcode res = CURLE_OK;

    /* RETURNTRANSFER */
    if (option == 19913) {
        obj->return_transfer = zval_is_true(value);
        RETURN_TRUE;
    }

    /* CURLINFO_HEADER_OUT (treated as a setopt by PHP's curl) */
    if (option == CURLINFO_HEADER_OUT) {
        if (zval_is_true(value)) {
            /* Check if DEBUGFUNCTION is active - they conflict */
            if (Z_TYPE(obj->debug_cb) != IS_UNDEF) {
                zend_value_error("CURLINFO_HEADER_OUT option must not be set when the CURLOPT_DEBUGFUNCTION option is set");
                RETURN_THROWS();
            }
            obj->header_out_enabled = 1;
            curl_easy_setopt(obj->handle, CURLOPT_DEBUGFUNCTION, ci_curl_debug_cb);
            curl_easy_setopt(obj->handle, CURLOPT_DEBUGDATA, obj);
            curl_easy_setopt(obj->handle, CURLOPT_VERBOSE, 1L);
            curl_easy_setopt(obj->handle, CURLOPT_NOPROGRESS, 1L);
            /* Suppress stderr debug text output - reuse existing handle */
            if (!obj->devnull) {
                obj->devnull = fopen("/dev/null", "w");
            }
            if (obj->devnull) {
                curl_easy_setopt(obj->handle, CURLOPT_STDERR, obj->devnull);
            }
        } else {
            obj->header_out_enabled = 0;
            curl_easy_setopt(obj->handle, CURLOPT_DEBUGFUNCTION, NULL);
            curl_easy_setopt(obj->handle, CURLOPT_VERBOSE, 0L);
        }
        RETURN_TRUE;
    }

    /* PRIVATE */
    if (opt == CURLOPT_PRIVATE) {
        zval_ptr_dtor(&obj->private_data);
        ZVAL_COPY(&obj->private_data, value);
        RETURN_TRUE;
    }

    /* SHARE */
    if (opt == CURLOPT_SHARE) {
        if (Z_TYPE_P(value) == IS_OBJECT && instanceof_function(Z_OBJCE_P(value), ci_share_ce)) {
            ci_share_obj *so = ci_share_from_obj(Z_OBJ_P(value));
            res = curl_easy_setopt(obj->handle, CURLOPT_SHARE, so->share);
            RETURN_BOOL(res == CURLE_OK);
        }
        RETURN_FALSE;
    }

    /* File handle options: CURLOPT_FILE, CURLOPT_WRITEHEADER, CURLOPT_STDERR, CURLOPT_INFILE */
    if (opt == CURLOPT_FILE || opt == CURLOPT_WRITEHEADER ||
        opt == CURLOPT_STDERR || opt == CURLOPT_INFILE) {
        /* Accept stream resources - convert to FILE* */
        php_stream *stream = NULL;
        if (Z_TYPE_P(value) == IS_RESOURCE) {
            php_stream_from_zval_no_verify(stream, value);
        }
        if (!stream) {
            php_error_docref(NULL, E_WARNING, "supplied argument is not a valid File-Handle resource");
            RETURN_FALSE;
        }
        FILE *fp = NULL;
        if (php_stream_cast(stream, PHP_STREAM_AS_STDIO, (void**)&fp, REPORT_ERRORS) == FAILURE) {
            RETURN_FALSE;
        }
        /* For CURLOPT_INFILE: if we have a read callback, don't set CURLOPT_READDATA
         * (which is what CURLOPT_INFILE maps to in libcurl), just store the reference */
        if (opt == CURLOPT_INFILE) {
            zval_ptr_dtor(&obj->infile);
            ZVAL_COPY(&obj->infile, value);
            if (Z_TYPE(obj->read_cb) != IS_UNDEF) {
                /* Read callback is set - keep READDATA pointing to our obj */
                res = CURLE_OK;
            } else {
                res = curl_easy_setopt(obj->handle, opt, fp);
            }
        } else {
            res = curl_easy_setopt(obj->handle, opt, fp);
        }
        /* For CURLOPT_FILE, also clear our internal write callback so curl writes to the file */
        if (opt == CURLOPT_FILE) {
            curl_easy_setopt(obj->handle, CURLOPT_WRITEFUNCTION, NULL);
            curl_easy_setopt(obj->handle, CURLOPT_WRITEDATA, fp);
            /* Keep track that we have a file write handler, not our callback */
            zval_ptr_dtor(&obj->write_cb);
            ZVAL_UNDEF(&obj->write_cb);
        }
        if (opt == CURLOPT_WRITEHEADER) {
            curl_easy_setopt(obj->handle, CURLOPT_HEADERFUNCTION, NULL);
            curl_easy_setopt(obj->handle, CURLOPT_HEADERDATA, fp);
            zval_ptr_dtor(&obj->header_cb);
            ZVAL_UNDEF(&obj->header_cb);
        }
        RETURN_BOOL(res == CURLE_OK);
    }

    /* slist options */
    if (ci_is_slist_option(opt)) {
        if (Z_TYPE_P(value) != IS_ARRAY) {
            /* Match PHP's error format for specific options */
            if (opt == CURLOPT_HTTPHEADER) {
                zend_type_error("curl_cffi_setopt(): The CURLOPT_HTTPHEADER option must have an array value");
                RETURN_THROWS();
            }
            RETURN_FALSE;
        }
        struct curl_slist *slist = NULL;
        zval *item;
        ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(value), item) {
            zend_string *str = zval_get_string(item);
            slist = curl_slist_append(slist, ZSTR_VAL(str));
            zend_string_release(str);
        } ZEND_HASH_FOREACH_END();

        if (opt == CURLOPT_HTTPHEADER) {
            if (obj->req_headers) curl_slist_free_all(obj->req_headers);
            obj->req_headers = slist;
        } else if (opt == CURLOPT_PROXYHEADER) {
            if (obj->proxy_headers) curl_slist_free_all(obj->proxy_headers);
            obj->proxy_headers = slist;
        } else if (opt == CURLOPT_RESOLVE) {
            if (obj->resolve) curl_slist_free_all(obj->resolve);
            obj->resolve = slist;
        }
        res = curl_easy_setopt(obj->handle, opt, slist);
        RETURN_BOOL(res == CURLE_OK);
    }

    /* Callbacks */
    if (opt == CURLOPT_WRITEFUNCTION) {
        if (Z_TYPE_P(value) == IS_NULL) {
            /* Reset to default write behavior */
            zval_ptr_dtor(&obj->write_cb);
            ZVAL_UNDEF(&obj->write_cb);
            curl_easy_setopt(obj->handle, CURLOPT_WRITEFUNCTION, ci_curl_write_cb);
            curl_easy_setopt(obj->handle, CURLOPT_WRITEDATA, obj);
            RETURN_TRUE;
        }
        if (!zend_is_callable(value, 0, NULL)) {
            zend_type_error("%s(): %s for option CURLOPT_WRITEFUNCTION, function \"%s\" not found or invalid function name", CI_SETOPT_FN, CI_SETOPT_ARG_CB,
                Z_TYPE_P(value) == IS_STRING ? Z_STRVAL_P(value) : "");
            RETURN_THROWS();
        }
        zval_ptr_dtor(&obj->write_cb);
        ZVAL_COPY(&obj->write_cb, value);
        RETURN_TRUE;
    }
    if (opt == CURLOPT_HEADERFUNCTION) {
        if (Z_TYPE_P(value) == IS_NULL) {
            zval_ptr_dtor(&obj->header_cb);
            ZVAL_UNDEF(&obj->header_cb);
            curl_easy_setopt(obj->handle, CURLOPT_HEADERFUNCTION, ci_curl_header_cb);
            curl_easy_setopt(obj->handle, CURLOPT_HEADERDATA, obj);
            RETURN_TRUE;
        }
        if (!zend_is_callable(value, 0, NULL)) {
            zend_type_error("%s(): %s for option CURLOPT_HEADERFUNCTION, function \"%s\" not found or invalid function name", CI_SETOPT_FN, CI_SETOPT_ARG_CB,
                Z_TYPE_P(value) == IS_STRING ? Z_STRVAL_P(value) : "");
            RETURN_THROWS();
        }
        zval_ptr_dtor(&obj->header_cb);
        ZVAL_COPY(&obj->header_cb, value);
        RETURN_TRUE;
    }

    /* Progress callback */
    if (opt == CURLOPT_PROGRESSFUNCTION) {
        if (Z_TYPE_P(value) == IS_NULL) {
            zval_ptr_dtor(&obj->progress_cb); ZVAL_UNDEF(&obj->progress_cb);
            RETURN_TRUE;
        }
        if (!zend_is_callable(value, 0, NULL)) {
            zend_type_error("%s(): %s for option CURLOPT_PROGRESSFUNCTION, function \"%s\" not found or invalid function name", CI_SETOPT_FN, CI_SETOPT_ARG_CB,
                Z_TYPE_P(value) == IS_STRING ? Z_STRVAL_P(value) : "");
            RETURN_THROWS();
        }
        zval_ptr_dtor(&obj->progress_cb);
        ZVAL_COPY(&obj->progress_cb, value);
        curl_easy_setopt(obj->handle, CURLOPT_PROGRESSFUNCTION, ci_curl_progress_cb);
        curl_easy_setopt(obj->handle, CURLOPT_PROGRESSDATA, obj);
        RETURN_TRUE;
    }

    /* Xferinfo callback */
    if (opt == CURLOPT_XFERINFOFUNCTION) {
        if (Z_TYPE_P(value) == IS_NULL) {
            zval_ptr_dtor(&obj->xferinfo_cb); ZVAL_UNDEF(&obj->xferinfo_cb);
            RETURN_TRUE;
        }
        if (!zend_is_callable(value, 0, NULL)) {
            zend_type_error("%s(): %s for option CURLOPT_XFERINFOFUNCTION, function \"%s\" not found or invalid function name", CI_SETOPT_FN, CI_SETOPT_ARG_CB,
                Z_TYPE_P(value) == IS_STRING ? Z_STRVAL_P(value) : "");
            RETURN_THROWS();
        }
        zval_ptr_dtor(&obj->xferinfo_cb);
        ZVAL_COPY(&obj->xferinfo_cb, value);
        curl_easy_setopt(obj->handle, CURLOPT_XFERINFOFUNCTION, ci_curl_xferinfo_cb);
        curl_easy_setopt(obj->handle, CURLOPT_XFERINFODATA, obj);
        RETURN_TRUE;
    }

    /* Debug callback */
    if (opt == CURLOPT_DEBUGFUNCTION) {
        if (Z_TYPE_P(value) == IS_NULL) {
            zval_ptr_dtor(&obj->debug_cb); ZVAL_UNDEF(&obj->debug_cb);
            /* Restore default debug handler if header_out is still enabled */
            if (obj->header_out_enabled) {
                curl_easy_setopt(obj->handle, CURLOPT_DEBUGFUNCTION, ci_curl_debug_cb);
                curl_easy_setopt(obj->handle, CURLOPT_DEBUGDATA, obj);
                curl_easy_setopt(obj->handle, CURLOPT_VERBOSE, 1L);
            } else {
                curl_easy_setopt(obj->handle, CURLOPT_DEBUGFUNCTION, NULL);
                curl_easy_setopt(obj->handle, CURLOPT_VERBOSE, 0L);
            }
            RETURN_TRUE;
        }
        if (!zend_is_callable(value, 0, NULL)) {
            zend_type_error("%s(): %s for option CURLOPT_DEBUGFUNCTION, function \"%s\" not found or invalid function name", CI_SETOPT_FN, CI_SETOPT_ARG_CB,
                Z_TYPE_P(value) == IS_STRING ? Z_STRVAL_P(value) : "");
            RETURN_THROWS();
        }
        zval_ptr_dtor(&obj->debug_cb);
        ZVAL_COPY(&obj->debug_cb, value);
        curl_easy_setopt(obj->handle, CURLOPT_DEBUGFUNCTION, ci_curl_user_debug_cb);
        curl_easy_setopt(obj->handle, CURLOPT_DEBUGDATA, obj);
        /* Don't force verbose - let user control it separately */
        RETURN_TRUE;
    }

    /* Read callback */
    if (opt == CURLOPT_READFUNCTION) {
        if (Z_TYPE_P(value) == IS_NULL) {
            zval_ptr_dtor(&obj->read_cb); ZVAL_UNDEF(&obj->read_cb);
            RETURN_TRUE;
        }
        if (!zend_is_callable(value, 0, NULL)) {
            zend_type_error("%s(): %s for option CURLOPT_READFUNCTION, function \"%s\" not found or invalid function name", CI_SETOPT_FN, CI_SETOPT_ARG_CB,
                Z_TYPE_P(value) == IS_STRING ? Z_STRVAL_P(value) : "");
            RETURN_THROWS();
        }
        zval_ptr_dtor(&obj->read_cb);
        ZVAL_COPY(&obj->read_cb, value);
        curl_easy_setopt(obj->handle, CURLOPT_READFUNCTION, ci_curl_read_cb);
        /* Always set READDATA to our obj (overrides any CURLOPT_INFILE FILE*) */
        curl_easy_setopt(obj->handle, CURLOPT_READDATA, obj);
        RETURN_TRUE;
    }

    /* CURLOPT_READDATA/CURLOPT_INFILE when set directly as data pointer */
    if (opt == CURLOPT_READDATA) {
        /* Handle same as CURLOPT_INFILE */
        opt = CURLOPT_INFILE;
    }

    /* CURLOPT_FNMATCH_FUNCTION (value 20200) */
    if (option == 20200) {
        if (Z_TYPE_P(value) == IS_NULL) {
            zval_ptr_dtor(&obj->fnmatch_cb); ZVAL_UNDEF(&obj->fnmatch_cb);
            RETURN_TRUE;
        }
        if (!zend_is_callable(value, 0, NULL)) {
            zend_type_error("%s(): %s for option CURLOPT_FNMATCH_FUNCTION, function \"%s\" not found or invalid function name", CI_SETOPT_FN, CI_SETOPT_ARG_CB,
                Z_TYPE_P(value) == IS_STRING ? Z_STRVAL_P(value) : "");
            RETURN_THROWS();
        }
        zval_ptr_dtor(&obj->fnmatch_cb);
        ZVAL_COPY(&obj->fnmatch_cb, value);
        RETURN_TRUE;
    }

    /* CURLOPT_PREREQFUNCTION (value 20312) */
    if (option == 20312) {
        if (Z_TYPE_P(value) == IS_NULL) { RETURN_TRUE; }
        if (!zend_is_callable(value, 0, NULL)) {
            zend_type_error("%s(): %s for option CURLOPT_PREREQFUNCTION, function \"%s\" not found or invalid function name", CI_SETOPT_FN, CI_SETOPT_ARG_CB,
                Z_TYPE_P(value) == IS_STRING ? Z_STRVAL_P(value) : "");
            RETURN_THROWS();
        }
        /* Store and wire up the prereq callback */
        /* TODO: Implement actual prereq C callback */
        RETURN_TRUE;
    }

    /* CURLOPT_SSH_HOSTKEYFUNCTION (value 20316) */
    if (option == 20316) {
        if (Z_TYPE_P(value) == IS_NULL) { RETURN_TRUE; }
        if (!zend_is_callable(value, 0, NULL)) {
            zend_type_error("%s(): %s for option CURLOPT_SSH_HOSTKEYFUNCTION, function \"%s\" not found or invalid function name", CI_SETOPT_FN, CI_SETOPT_ARG_CB,
                Z_TYPE_P(value) == IS_STRING ? Z_STRVAL_P(value) : "");
            RETURN_THROWS();
        }
        RETURN_TRUE;
    }

    /* CURLOPT_BINARYTRANSFER (19914) - deprecated no-op in PHP */
    if (option == 19914) {
        RETURN_TRUE;
    }

    /* CURLOPT_SAFE_UPLOAD = -1 in PHP: always true, disabling is not supported */
    if (option == -1) {
        if (!zval_is_true(value)) {
            zend_value_error("%s(): Disabling safe uploads is no longer supported", CI_SETOPT_FN);
            RETURN_THROWS();
        }
        RETURN_TRUE;
    }

    /* Validate option value - negative or in unreasonable range */
    if (option < 0) {
        zend_value_error("%s(): Argument #2 ($option) is not a valid cURL option", CI_SETOPT_FN);
        RETURN_THROWS();
    }

    /* POSTFIELDS */
    if (opt == CURLOPT_POSTFIELDS) {
        if (Z_TYPE_P(value) == IS_STRING) {
            curl_easy_setopt(obj->handle, CURLOPT_POSTFIELDSIZE, (long)Z_STRLEN_P(value));
            res = curl_easy_setopt(obj->handle, CURLOPT_COPYPOSTFIELDS, Z_STRVAL_P(value));
        } else if (Z_TYPE_P(value) == IS_ARRAY) {
            /* Empty array: send as empty POST body */
            if (zend_hash_num_elements(Z_ARRVAL_P(value)) == 0) {
                if (obj->mime) { curl_mime_free(obj->mime); obj->mime = NULL; }
                curl_easy_setopt(obj->handle, CURLOPT_POSTFIELDSIZE, 0L);
                res = curl_easy_setopt(obj->handle, CURLOPT_COPYPOSTFIELDS, "");
                RETURN_BOOL(res == CURLE_OK);
            }
            /* Build multipart MIME from array */
            if (obj->mime) { curl_mime_free(obj->mime); obj->mime = NULL; }
            curl_mime *mime = curl_mime_init(obj->handle);
            zend_string *key;
            zend_ulong num_key;
            zval *val;
            ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(value), num_key, key, val) {
                curl_mimepart *part = curl_mime_addpart(mime);
                if (key) {
                    curl_mime_name(part, ZSTR_VAL(key));
                } else {
                    char numbuf[32];
                    snprintf(numbuf, sizeof(numbuf), "%lu", (unsigned long)num_key);
                    curl_mime_name(part, numbuf);
                }

                /* Check if value is a CURLFile */
                if (Z_TYPE_P(val) == IS_OBJECT && instanceof_function(Z_OBJCE_P(val), ci_curlfile_ce)) {
                    zval rv;
                    zval *fname = zend_read_property(ci_curlfile_ce, Z_OBJ_P(val), "name", sizeof("name")-1, 1, &rv);
                    zval *mime_type = zend_read_property(ci_curlfile_ce, Z_OBJ_P(val), "mime", sizeof("mime")-1, 1, &rv);
                    zval *posted_name = zend_read_property(ci_curlfile_ce, Z_OBJ_P(val), "postname", sizeof("postname")-1, 1, &rv);

                    curl_mime_filedata(part, Z_STRVAL_P(fname));
                    if (Z_TYPE_P(mime_type) == IS_STRING && Z_STRLEN_P(mime_type) > 0) {
                        curl_mime_type(part, Z_STRVAL_P(mime_type));
                    } else {
                        /* PHP's curl defaults to application/octet-stream */
                        curl_mime_type(part, "application/octet-stream");
                    }
                    if (Z_TYPE_P(posted_name) == IS_STRING && Z_STRLEN_P(posted_name) > 0) {
                        curl_mime_filename(part, Z_STRVAL_P(posted_name));
                    }
                } else if (Z_TYPE_P(val) == IS_OBJECT && instanceof_function(Z_OBJCE_P(val), ci_curlstringfile_ce)) {
                    zval rv;
                    zval *data_val = zend_read_property(ci_curlstringfile_ce, Z_OBJ_P(val), "data", sizeof("data")-1, 1, &rv);
                    zval *mime_type = zend_read_property(ci_curlstringfile_ce, Z_OBJ_P(val), "mime", sizeof("mime")-1, 1, &rv);
                    zval *posted_name = zend_read_property(ci_curlstringfile_ce, Z_OBJ_P(val), "postname", sizeof("postname")-1, 1, &rv);

                    curl_mime_data(part, Z_STRVAL_P(data_val), Z_STRLEN_P(data_val));
                    if (Z_TYPE_P(mime_type) == IS_STRING && Z_STRLEN_P(mime_type) > 0) {
                        curl_mime_type(part, Z_STRVAL_P(mime_type));
                    }
                    if (Z_TYPE_P(posted_name) == IS_STRING && Z_STRLEN_P(posted_name) > 0) {
                        curl_mime_filename(part, Z_STRVAL_P(posted_name));
                    }
                } else if (Z_TYPE_P(val) == IS_ARRAY) {
                    /* Multi-value field: each array element becomes a separate part */
                    zval *sub;
                    int idx = 0;
                    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(val), sub) {
                        curl_mimepart *subpart;
                        if (idx == 0) {
                            subpart = part; /* first uses already-created part */
                        } else {
                            subpart = curl_mime_addpart(mime);
                            if (key) {
                                curl_mime_name(subpart, ZSTR_VAL(key));
                            } else {
                                char numbuf[32];
                                snprintf(numbuf, sizeof(numbuf), "%lu", (unsigned long)num_key);
                                curl_mime_name(subpart, numbuf);
                            }
                        }
                        zend_string *sv = zval_get_string(sub);
                        curl_mime_data(subpart, ZSTR_VAL(sv), ZSTR_LEN(sv));
                        zend_string_release(sv);
                        idx++;
                    } ZEND_HASH_FOREACH_END();
                } else {
                    zend_string *sv = zval_get_string(val);
                    curl_mime_data(part, ZSTR_VAL(sv), ZSTR_LEN(sv));
                    zend_string_release(sv);
                }
            } ZEND_HASH_FOREACH_END();

            res = curl_easy_setopt(obj->handle, CURLOPT_MIMEPOST, mime);
            obj->mime = mime; /* Store to free later */
            /* Store reference to keep array alive for mime part data */
            zval_ptr_dtor(&obj->postfields);
            ZVAL_COPY(&obj->postfields, value);
        } else if (Z_TYPE_P(value) == IS_NULL) {
            res = curl_easy_setopt(obj->handle, opt, NULL);
        } else {
            /* Object passed that isn't CURLFile/CURLStringFile/array - try string conversion */
            zend_string *sv = zval_get_string(value);
            if (EG(exception)) {
                RETURN_THROWS();
            }
            curl_easy_setopt(obj->handle, CURLOPT_POSTFIELDSIZE, (long)ZSTR_LEN(sv));
            res = curl_easy_setopt(obj->handle, CURLOPT_COPYPOSTFIELDS, ZSTR_VAL(sv));
            zend_string_release(sv);
        }
        RETURN_BOOL(res == CURLE_OK);
    }

    /* String */
    if (Z_TYPE_P(value) == IS_STRING) {
        /* Check for null bytes in URL and string options */
        if (memchr(Z_STRVAL_P(value), '\0', Z_STRLEN_P(value))) {
            zend_argument_value_error(3, "must not contain any null bytes");
            RETURN_THROWS();
        }
        res = curl_easy_setopt(obj->handle, opt, Z_STRVAL_P(value));
    }
    /* Long/Bool */
    else if (Z_TYPE_P(value) == IS_LONG || Z_TYPE_P(value) == IS_TRUE || Z_TYPE_P(value) == IS_FALSE) {
        long lval = (long)zval_get_long(value);
        /* SSL_VERIFYHOST=1 deprecation notice (matches PHP's curl behavior) */
        if (opt == CURLOPT_SSL_VERIFYHOST && lval == 1) {
            php_error_docref(NULL, E_NOTICE,
                "CURLOPT_SSL_VERIFYHOST no longer accepts the value 1, value 2 will be used instead");
            lval = 2;
        }
        res = curl_easy_setopt(obj->handle, opt, lval);
    }
    /* Null */
    else if (Z_TYPE_P(value) == IS_NULL) {
        res = curl_easy_setopt(obj->handle, opt, NULL);
    }
    else if (Z_TYPE_P(value) == IS_OBJECT || Z_TYPE_P(value) == IS_ARRAY) {
        /* Object/Array passed to a non-callback, non-postfields option */
        php_error_docref(NULL, E_WARNING, "Unsupported value type");
        RETURN_FALSE;
    }
    else {
        php_error_docref(NULL, E_WARNING, "Unsupported value type");
        RETURN_FALSE;
    }

    if (res == CURLE_UNKNOWN_OPTION || res == CURLE_BAD_FUNCTION_ARGUMENT) {
        zend_value_error("%s(): Argument #2 ($option) is not a valid cURL option", CI_SETOPT_FN);
        RETURN_THROWS();
    }

    RETURN_BOOL(res == CURLE_OK);
}
/* }}} */

/* {{{ curl_cffi_setopt_array($ch, $options) */
PHP_FUNCTION(curl_cffi_setopt_array) {
    zval *handle;
    zval *options;

    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_OBJECT_OF_CLASS(handle, ci_curl_ce)
        Z_PARAM_ARRAY(options)
    ZEND_PARSE_PARAMETERS_END();

    zend_ulong option;
    zval *value;

    ci_in_setopt_array = 1;
    ZEND_HASH_FOREACH_NUM_KEY_VAL(Z_ARRVAL_P(options), option, value) {
        zval zopt, retval, args[3];
        ZVAL_COPY_VALUE(&args[0], handle);
        ZVAL_LONG(&args[1], option);
        ZVAL_COPY_VALUE(&args[2], value);

        ZVAL_STRING(&zopt, "curl_cffi_setopt");
        if (call_user_function(NULL, NULL, &zopt, &retval, 3, args) != SUCCESS) {
            zval_ptr_dtor(&zopt);
            ci_in_setopt_array = 0;
            RETURN_FALSE;
        }
        zval_ptr_dtor(&zopt);

        /* Propagate exceptions (TypeErrors from callback validation) */
        if (EG(exception)) {
            ci_in_setopt_array = 0;
            RETURN_THROWS();
        }

        if (Z_TYPE(retval) == IS_FALSE) {
            zval_ptr_dtor(&retval);
            ci_in_setopt_array = 0;
            RETURN_FALSE;
        }
        zval_ptr_dtor(&retval);
    } ZEND_HASH_FOREACH_END();
    ci_in_setopt_array = 0;

    RETURN_TRUE;
}
/* }}} */

/* {{{ curl_cffi_exec($ch) */
PHP_FUNCTION(curl_cffi_exec) {
    zval *handle;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(handle, ci_curl_ce)
    ZEND_PARSE_PARAMETERS_END();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(handle));
    if (!obj->handle || obj->closed) {
        php_error_docref(NULL, E_WARNING, "Curl handle is not initialized or closed");
        RETURN_FALSE;
    }

    /* Clear buffers */
    smart_str_free(&obj->body_buf);
    smart_str_free(&obj->header_buf);
    memset(&obj->body_buf, 0, sizeof(smart_str));
    memset(&obj->header_buf, 0, sizeof(smart_str));
    memset(obj->errbuf, 0, CURL_ERROR_SIZE);

    CURLcode res = curl_easy_perform(obj->handle);
    obj->last_errno = res;

    /* Check if a PHP exception was thrown from a callback */
    if (EG(exception)) {
        RETURN_THROWS();
    }

    if (res != CURLE_OK) {
        RETURN_FALSE;
    }

    if (obj->return_transfer) {
        smart_str_0(&obj->body_buf);
        if (obj->body_buf.s) {
            RETURN_STR_COPY(obj->body_buf.s);
        }
        RETURN_EMPTY_STRING();
    }

    /* If not RETURNTRANSFER and no write callback, output to stdout */
    if (Z_TYPE(obj->write_cb) == IS_UNDEF) {
        if (obj->body_buf.s) {
            smart_str_0(&obj->body_buf);
            PHPWRITE(ZSTR_VAL(obj->body_buf.s), ZSTR_LEN(obj->body_buf.s));
        }
    }

    RETURN_TRUE;
}
/* }}} */

/* {{{ curl_cffi_getinfo($ch, $option = null) */
PHP_FUNCTION(curl_cffi_getinfo) {
    zval *handle;
    zend_long option = 0;
    zend_bool option_is_null = 1;

    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_OBJECT_OF_CLASS(handle, ci_curl_ce)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG_OR_NULL(option, option_is_null)
    ZEND_PARSE_PARAMETERS_END();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(handle));
    if (!obj->handle || obj->closed) {
        RETURN_FALSE;
    }

    CURL *ch = obj->handle;

    if (option_is_null) {
        /* Return all info as array (like PHP's curl_getinfo with no option) */
        char *s_code;
        long l_code;
        double d_code;

        array_init(return_value);

        curl_easy_getinfo(ch, CURLINFO_EFFECTIVE_URL, &s_code);
        add_assoc_string(return_value, "url", s_code ? s_code : "");

        curl_easy_getinfo(ch, CURLINFO_CONTENT_TYPE, &s_code);
        if (s_code) { add_assoc_string(return_value, "content_type", s_code); }
        else { add_assoc_null(return_value, "content_type"); }

        curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &l_code);
        add_assoc_long(return_value, "http_code", l_code);

        curl_easy_getinfo(ch, CURLINFO_HEADER_SIZE, &l_code);
        add_assoc_long(return_value, "header_size", l_code);

        curl_easy_getinfo(ch, CURLINFO_REQUEST_SIZE, &l_code);
        add_assoc_long(return_value, "request_size", l_code);

        curl_easy_getinfo(ch, CURLINFO_REDIRECT_COUNT, &l_code);
        add_assoc_long(return_value, "redirect_count", l_code);

        curl_easy_getinfo(ch, CURLINFO_TOTAL_TIME, &d_code);
        add_assoc_double(return_value, "total_time", d_code);

        curl_easy_getinfo(ch, CURLINFO_NAMELOOKUP_TIME, &d_code);
        add_assoc_double(return_value, "namelookup_time", d_code);

        curl_easy_getinfo(ch, CURLINFO_CONNECT_TIME, &d_code);
        add_assoc_double(return_value, "connect_time", d_code);

        curl_easy_getinfo(ch, CURLINFO_PRETRANSFER_TIME, &d_code);
        add_assoc_double(return_value, "pretransfer_time", d_code);

        curl_easy_getinfo(ch, CURLINFO_STARTTRANSFER_TIME, &d_code);
        add_assoc_double(return_value, "starttransfer_time", d_code);

        curl_easy_getinfo(ch, CURLINFO_REDIRECT_TIME, &d_code);
        add_assoc_double(return_value, "redirect_time", d_code);

        curl_easy_getinfo(ch, CURLINFO_PRIMARY_IP, &s_code);
        add_assoc_string(return_value, "primary_ip", s_code ? s_code : "");

        curl_easy_getinfo(ch, CURLINFO_PRIMARY_PORT, &l_code);
        add_assoc_long(return_value, "primary_port", l_code);

        curl_easy_getinfo(ch, CURLINFO_REDIRECT_URL, &s_code);
        add_assoc_string(return_value, "redirect_url", s_code ? s_code : "");

        /* Time _us fields (microseconds as int) */
        curl_off_t off_t_code;
#ifdef CURLINFO_TOTAL_TIME_T
        curl_easy_getinfo(ch, CURLINFO_TOTAL_TIME_T, &off_t_code);
        add_assoc_long(return_value, "total_time_us", (zend_long)off_t_code);
#endif
#ifdef CURLINFO_NAMELOOKUP_TIME_T
        curl_easy_getinfo(ch, CURLINFO_NAMELOOKUP_TIME_T, &off_t_code);
        add_assoc_long(return_value, "namelookup_time_us", (zend_long)off_t_code);
#endif
#ifdef CURLINFO_CONNECT_TIME_T
        curl_easy_getinfo(ch, CURLINFO_CONNECT_TIME_T, &off_t_code);
        add_assoc_long(return_value, "connect_time_us", (zend_long)off_t_code);
#endif
#ifdef CURLINFO_PRETRANSFER_TIME_T
        curl_easy_getinfo(ch, CURLINFO_PRETRANSFER_TIME_T, &off_t_code);
        add_assoc_long(return_value, "pretransfer_time_us", (zend_long)off_t_code);
#endif
#ifdef CURLINFO_STARTTRANSFER_TIME_T
        curl_easy_getinfo(ch, CURLINFO_STARTTRANSFER_TIME_T, &off_t_code);
        add_assoc_long(return_value, "starttransfer_time_us", (zend_long)off_t_code);
#endif
#ifdef CURLINFO_REDIRECT_TIME_T
        curl_easy_getinfo(ch, CURLINFO_REDIRECT_TIME_T, &off_t_code);
        add_assoc_long(return_value, "redirect_time_us", (zend_long)off_t_code);
#endif
#ifdef CURLINFO_APPCONNECT_TIME_T
        curl_easy_getinfo(ch, CURLINFO_APPCONNECT_TIME_T, &off_t_code);
        add_assoc_long(return_value, "appconnect_time_us", (zend_long)off_t_code);
#endif
#ifdef CURLINFO_POSTTRANSFER_TIME_T
        curl_easy_getinfo(ch, CURLINFO_POSTTRANSFER_TIME_T, &off_t_code);
        add_assoc_long(return_value, "posttransfer_time_us", (zend_long)off_t_code);
#endif

        /* Add captured request headers if available */
        if (obj->header_out) {
            add_assoc_str(return_value, "request_header", zend_string_copy(obj->header_out));
        }
        return;
    }

    /* CURLINFO_PRIVATE (special: stored in PHP, not in curl) */
    if (option == CURLINFO_PRIVATE) {
        if (Z_TYPE(obj->private_data) != IS_UNDEF) {
            RETURN_ZVAL(&obj->private_data, 1, 0);
        }
        RETURN_FALSE;
    }

    /* CURLINFO_HEADER_OUT - return captured request headers */
    if (option == CURLINFO_HEADER_OUT) {
        if (obj->header_out) {
            RETURN_STR_COPY(obj->header_out);
        }
        RETURN_FALSE;
    }

    /* Single option */
    int type = (int)option & CURLINFO_TYPEMASK;
    switch (type) {
        case CURLINFO_STRING: {
            char *str = NULL;
            curl_easy_getinfo(ch, (CURLINFO)option, &str);
            if (str) { RETURN_STRING(str); }
            RETURN_FALSE;
        }
        case CURLINFO_LONG: {
            long lval = 0;
            curl_easy_getinfo(ch, (CURLINFO)option, &lval);
            RETURN_LONG(lval);
        }
        case CURLINFO_DOUBLE: {
            double dval = 0.0;
            curl_easy_getinfo(ch, (CURLINFO)option, &dval);
            RETURN_DOUBLE(dval);
        }
        case CURLINFO_SLIST: {
            struct curl_slist *slist = NULL;
            curl_easy_getinfo(ch, (CURLINFO)option, &slist);
            array_init(return_value);
            struct curl_slist *current = slist;
            while (current) {
                add_next_index_string(return_value, current->data);
                current = current->next;
            }
            curl_slist_free_all(slist);
            return;
        }
#ifdef CURLINFO_OFF_T
        case CURLINFO_OFF_T: {
            curl_off_t oval = 0;
            curl_easy_getinfo(ch, (CURLINFO)option, &oval);
            RETURN_LONG((zend_long)oval);
        }
#endif
        default:
            RETURN_FALSE;
    }
}
/* }}} */

/* {{{ curl_cffi_error($ch) */
PHP_FUNCTION(curl_cffi_error) {
    zval *handle;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(handle, ci_curl_ce)
    ZEND_PARSE_PARAMETERS_END();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(handle));
    if (obj->errbuf[0]) {
        RETURN_STRING(obj->errbuf);
    }
    RETURN_EMPTY_STRING();
}
/* }}} */

/* {{{ curl_cffi_errno($ch) */
PHP_FUNCTION(curl_cffi_errno) {
    zval *handle;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(handle, ci_curl_ce)
    ZEND_PARSE_PARAMETERS_END();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(handle));
    RETURN_LONG((long)obj->last_errno);
}
/* }}} */

/* {{{ curl_cffi_close($ch) */
PHP_FUNCTION(curl_cffi_close) {
    zval *handle;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(handle, ci_curl_ce)
    ZEND_PARSE_PARAMETERS_END();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(handle));
    if (obj->handle && !obj->closed) {
        curl_easy_cleanup(obj->handle);
        obj->handle = NULL;
        obj->closed = 1;
    }
}
/* }}} */

/* {{{ curl_cffi_reset($ch) */
PHP_FUNCTION(curl_cffi_reset) {
    zval *handle;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(handle, ci_curl_ce)
    ZEND_PARSE_PARAMETERS_END();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(handle));
    if (!obj->handle || obj->closed) {
        return;
    }

    curl_easy_reset(obj->handle);
    if (obj->req_headers) { curl_slist_free_all(obj->req_headers); obj->req_headers = NULL; }
    if (obj->proxy_headers) { curl_slist_free_all(obj->proxy_headers); obj->proxy_headers = NULL; }
    if (obj->resolve) { curl_slist_free_all(obj->resolve); obj->resolve = NULL; }
    zval_ptr_dtor(&obj->write_cb); ZVAL_UNDEF(&obj->write_cb);
    zval_ptr_dtor(&obj->header_cb); ZVAL_UNDEF(&obj->header_cb);
    smart_str_free(&obj->body_buf); memset(&obj->body_buf, 0, sizeof(smart_str));
    smart_str_free(&obj->header_buf); memset(&obj->header_buf, 0, sizeof(smart_str));
    obj->return_transfer = 0;
    obj->last_errno = CURLE_OK;

    curl_easy_setopt(obj->handle, CURLOPT_ERRORBUFFER, obj->errbuf);
    curl_easy_setopt(obj->handle, CURLOPT_WRITEFUNCTION, ci_curl_write_cb);
    curl_easy_setopt(obj->handle, CURLOPT_WRITEDATA, obj);
    curl_easy_setopt(obj->handle, CURLOPT_HEADERFUNCTION, ci_curl_header_cb);
    curl_easy_setopt(obj->handle, CURLOPT_HEADERDATA, obj);
}
/* }}} */

/* {{{ curl_cffi_copy_handle($ch) */
PHP_FUNCTION(curl_cffi_copy_handle) {
    zval *handle;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(handle, ci_curl_ce)
    ZEND_PARSE_PARAMETERS_END();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(handle));
    if (!obj->handle || obj->closed) {
        RETURN_FALSE;
    }

    object_init_ex(return_value, ci_curl_ce);
    ci_curl_obj *new_obj = ci_curl_from_obj(Z_OBJ_P(return_value));
    new_obj->handle = curl_easy_duphandle(obj->handle);

    if (!new_obj->handle) {
        zval_ptr_dtor(return_value);
        RETURN_FALSE;
    }

    new_obj->return_transfer = obj->return_transfer;
    new_obj->header_out_enabled = obj->header_out_enabled;

    /* Copy private data */
    if (Z_TYPE(obj->private_data) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->private_data, &obj->private_data);
    }

    /* Copy callbacks */
    if (Z_TYPE(obj->write_cb) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->write_cb, &obj->write_cb);
    }
    if (Z_TYPE(obj->header_cb) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->header_cb, &obj->header_cb);
    }
    if (Z_TYPE(obj->progress_cb) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->progress_cb, &obj->progress_cb);
    }
    if (Z_TYPE(obj->xferinfo_cb) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->xferinfo_cb, &obj->xferinfo_cb);
    }
    if (Z_TYPE(obj->debug_cb) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->debug_cb, &obj->debug_cb);
    }
    if (Z_TYPE(obj->read_cb) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->read_cb, &obj->read_cb);
    }
    if (Z_TYPE(obj->fnmatch_cb) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->fnmatch_cb, &obj->fnmatch_cb);
    }
    if (Z_TYPE(obj->postfields) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->postfields, &obj->postfields);
    }
    if (obj->header_out) {
        new_obj->header_out = zend_string_copy(obj->header_out);
    }

    /* Copy slist options */
    if (obj->req_headers) {
        struct curl_slist *src = obj->req_headers;
        struct curl_slist *dst = NULL;
        while (src) { dst = curl_slist_append(dst, src->data); src = src->next; }
        new_obj->req_headers = dst;
        curl_easy_setopt(new_obj->handle, CURLOPT_HTTPHEADER, dst);
    }

    curl_easy_setopt(new_obj->handle, CURLOPT_ERRORBUFFER, new_obj->errbuf);
    curl_easy_setopt(new_obj->handle, CURLOPT_WRITEFUNCTION, ci_curl_write_cb);
    curl_easy_setopt(new_obj->handle, CURLOPT_WRITEDATA, new_obj);
    curl_easy_setopt(new_obj->handle, CURLOPT_HEADERFUNCTION, ci_curl_header_cb);
    curl_easy_setopt(new_obj->handle, CURLOPT_HEADERDATA, new_obj);

    /* Re-wire callbacks to point to new_obj */
    if (Z_TYPE(new_obj->debug_cb) != IS_UNDEF) {
        curl_easy_setopt(new_obj->handle, CURLOPT_DEBUGFUNCTION, ci_curl_user_debug_cb);
        curl_easy_setopt(new_obj->handle, CURLOPT_DEBUGDATA, new_obj);
    } else if (new_obj->header_out_enabled) {
        curl_easy_setopt(new_obj->handle, CURLOPT_DEBUGFUNCTION, ci_curl_debug_cb);
        curl_easy_setopt(new_obj->handle, CURLOPT_DEBUGDATA, new_obj);
        curl_easy_setopt(new_obj->handle, CURLOPT_VERBOSE, 1L);
        new_obj->devnull = fopen("/dev/null", "w");
        if (new_obj->devnull) {
            curl_easy_setopt(new_obj->handle, CURLOPT_STDERR, new_obj->devnull);
        }
    }
    if (Z_TYPE(new_obj->progress_cb) != IS_UNDEF) {
        curl_easy_setopt(new_obj->handle, CURLOPT_PROGRESSFUNCTION, ci_curl_progress_cb);
        curl_easy_setopt(new_obj->handle, CURLOPT_PROGRESSDATA, new_obj);
    }
    if (Z_TYPE(new_obj->xferinfo_cb) != IS_UNDEF) {
        curl_easy_setopt(new_obj->handle, CURLOPT_XFERINFOFUNCTION, ci_curl_xferinfo_cb);
        curl_easy_setopt(new_obj->handle, CURLOPT_XFERINFODATA, new_obj);
    }
    if (Z_TYPE(new_obj->read_cb) != IS_UNDEF) {
        curl_easy_setopt(new_obj->handle, CURLOPT_READFUNCTION, ci_curl_read_cb);
        curl_easy_setopt(new_obj->handle, CURLOPT_READDATA, new_obj);
    }
}
/* }}} */

/* {{{ curl_cffi_strerror($errornum) */
PHP_FUNCTION(curl_cffi_strerror) {
    zend_long errornum;

    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(errornum)
    ZEND_PARSE_PARAMETERS_END();

    RETURN_STRING(curl_easy_strerror((CURLcode)errornum));
}
/* }}} */

/* {{{ curl_cffi_impersonate($ch, $target, $default_headers = true) */
PHP_FUNCTION(curl_cffi_impersonate) {
    zval *handle;
    char *target;
    size_t target_len;
    zend_bool default_headers = 1;

    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_OBJECT_OF_CLASS(handle, ci_curl_ce)
        Z_PARAM_STRING(target, target_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(default_headers)
    ZEND_PARSE_PARAMETERS_END();

    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(handle));
    if (!obj->handle || obj->closed) {
        php_error_docref(NULL, E_WARNING, "Curl handle is not initialized or closed");
        RETURN_FALSE;
    }

    CURLcode res = curl_easy_impersonate(obj->handle, target, default_headers ? 1 : 0);
    if (res == CURLE_OK && default_headers) {
        curl_easy_setopt(obj->handle, CURLOPT_ENCODING, "");
    }
    RETURN_BOOL(res == CURLE_OK);
}
/* }}} */

/* Procedural function arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_init, 0, 0, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, url, IS_STRING, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_ci_setopt, 0, 3, _IS_BOOL, 0)
    ZEND_ARG_OBJ_INFO(0, handle, CurlImpersonate\\Curl, 0)
    ZEND_ARG_TYPE_INFO(0, option, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, value, IS_MIXED, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_ci_setopt_array, 0, 2, _IS_BOOL, 0)
    ZEND_ARG_OBJ_INFO(0, handle, CurlImpersonate\\Curl, 0)
    ZEND_ARG_TYPE_INFO(0, options, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_ci_exec, 0, 1, IS_MIXED, 0)
    ZEND_ARG_OBJ_INFO(0, handle, CurlImpersonate\\Curl, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_ci_getinfo, 0, 1, IS_MIXED, 0)
    ZEND_ARG_OBJ_INFO(0, handle, CurlImpersonate\\Curl, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, option, IS_LONG, 1, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_ci_error, 0, 1, IS_STRING, 0)
    ZEND_ARG_OBJ_INFO(0, handle, CurlImpersonate\\Curl, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_ci_errno, 0, 1, IS_LONG, 0)
    ZEND_ARG_OBJ_INFO(0, handle, CurlImpersonate\\Curl, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_ci_close, 0, 1, IS_VOID, 0)
    ZEND_ARG_OBJ_INFO(0, handle, CurlImpersonate\\Curl, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_ci_reset, 0, 1, IS_VOID, 0)
    ZEND_ARG_OBJ_INFO(0, handle, CurlImpersonate\\Curl, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_copy_handle, 0, 0, 1)
    ZEND_ARG_OBJ_INFO(0, handle, CurlImpersonate\\Curl, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_ci_strerror, 0, 1, IS_STRING, 1)
    ZEND_ARG_TYPE_INFO(0, error_code, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_ci_impersonate, 0, 2, _IS_BOOL, 0)
    ZEND_ARG_OBJ_INFO(0, handle, CurlImpersonate\\Curl, 0)
    ZEND_ARG_TYPE_INFO(0, target, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, default_headers, _IS_BOOL, 0, "true")
ZEND_END_ARG_INFO()

/* ========================================================================
 * curl_multi_* API
 * ======================================================================== */

typedef struct {
    CURLM *multi;
    zend_llist easylist; /* track attached easy handles */
    CURLMcode last_errno;
    zend_object std;
} ci_multi_obj;

static zend_class_entry *ci_multi_ce;
static zend_object_handlers ci_multi_handlers;

static inline ci_multi_obj *ci_multi_from_obj(zend_object *obj) {
    return (ci_multi_obj *)((char *)obj - XtOffsetOf(ci_multi_obj, std));
}

static zend_object *ci_multi_create(zend_class_entry *ce) {
    ci_multi_obj *obj = zend_object_alloc(sizeof(ci_multi_obj), ce);
    zend_object_std_init(&obj->std, ce);
    obj->std.handlers = &ci_multi_handlers;
    obj->multi = NULL;
    obj->last_errno = CURLM_OK;
    zend_llist_init(&obj->easylist, sizeof(zval), (llist_dtor_func_t)ZVAL_PTR_DTOR, 0);
    return &obj->std;
}

static void ci_multi_free(zend_object *object) {
    ci_multi_obj *obj = ci_multi_from_obj(object);
    if (obj->multi) {
        curl_multi_cleanup(obj->multi);
        obj->multi = NULL;
    }
    zend_llist_destroy(&obj->easylist);
    zend_object_std_dtor(&obj->std);
}

PHP_FUNCTION(curl_cffi_multi_init) {
    ZEND_PARSE_PARAMETERS_NONE();
    object_init_ex(return_value, ci_multi_ce);
    ci_multi_obj *obj = ci_multi_from_obj(Z_OBJ_P(return_value));
    obj->multi = curl_multi_init();
    if (!obj->multi) {
        zval_ptr_dtor(return_value);
        RETURN_FALSE;
    }
}

PHP_FUNCTION(curl_cffi_multi_add_handle) {
    zval *mh, *ch;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_OBJECT_OF_CLASS(mh, ci_multi_ce)
        Z_PARAM_OBJECT_OF_CLASS(ch, ci_curl_ce)
    ZEND_PARSE_PARAMETERS_END();

    ci_multi_obj *mo = ci_multi_from_obj(Z_OBJ_P(mh));
    ci_curl_obj *co = ci_curl_from_obj(Z_OBJ_P(ch));
    CURLMcode rc = curl_multi_add_handle(mo->multi, co->handle);

    /* Track the easy handle reference */
    Z_ADDREF_P(ch);
    zend_llist_add_element(&mo->easylist, ch);

    RETURN_LONG((long)rc);
}

PHP_FUNCTION(curl_cffi_multi_remove_handle) {
    zval *mh, *ch;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_OBJECT_OF_CLASS(mh, ci_multi_ce)
        Z_PARAM_OBJECT_OF_CLASS(ch, ci_curl_ce)
    ZEND_PARSE_PARAMETERS_END();

    ci_multi_obj *mo = ci_multi_from_obj(Z_OBJ_P(mh));
    ci_curl_obj *co = ci_curl_from_obj(Z_OBJ_P(ch));
    CURLMcode rc = curl_multi_remove_handle(mo->multi, co->handle);
    RETURN_LONG((long)rc);
}

PHP_FUNCTION(curl_cffi_multi_exec) {
    zval *mh, *still_running;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_OBJECT_OF_CLASS(mh, ci_multi_ce)
        Z_PARAM_ZVAL(still_running)
    ZEND_PARSE_PARAMETERS_END();

    ci_multi_obj *mo = ci_multi_from_obj(Z_OBJ_P(mh));
    int running = 0;
    CURLMcode rc = curl_multi_perform(mo->multi, &running);
    mo->last_errno = rc;
    ZEND_TRY_ASSIGN_REF_LONG(still_running, running);
    RETURN_LONG((long)rc);
}

PHP_FUNCTION(curl_cffi_multi_select) {
    zval *mh;
    double timeout = 1.0;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_OBJECT_OF_CLASS(mh, ci_multi_ce)
        Z_PARAM_OPTIONAL
        Z_PARAM_DOUBLE(timeout)
    ZEND_PARSE_PARAMETERS_END();

    ci_multi_obj *mo = ci_multi_from_obj(Z_OBJ_P(mh));

    /* Validate timeout range to prevent overflow (seconds -> ms must fit in int) */
    if (timeout < 0 || timeout > (double)(INT_MAX / 1000)) {
        zend_value_error("curl_cffi_multi_select(): Argument #2 ($timeout) must be between 0 and %d", INT_MAX / 1000);
        RETURN_THROWS();
    }

    int numfds = 0;
    CURLMcode rc = curl_multi_wait(mo->multi, NULL, 0, (int)(timeout * 1000), &numfds);
    if (rc != CURLM_OK) {
        RETURN_LONG(-1);
    }
    RETURN_LONG(numfds);
}

PHP_FUNCTION(curl_cffi_multi_getcontent) {
    zval *ch;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(ch, ci_curl_ce)
    ZEND_PARSE_PARAMETERS_END();

    ci_curl_obj *co = ci_curl_from_obj(Z_OBJ_P(ch));
    if (co->return_transfer) {
        if (co->body_buf.s) {
            smart_str_0(&co->body_buf);
            RETURN_STR_COPY(co->body_buf.s);
        }
        RETURN_EMPTY_STRING();
    }
    RETURN_NULL();
}

PHP_FUNCTION(curl_cffi_multi_info_read) {
    zval *mh, *msgs_in_queue = NULL;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_OBJECT_OF_CLASS(mh, ci_multi_ce)
        Z_PARAM_OPTIONAL
        Z_PARAM_ZVAL(msgs_in_queue)
    ZEND_PARSE_PARAMETERS_END();

    ci_multi_obj *mo = ci_multi_from_obj(Z_OBJ_P(mh));
    int queued = 0;
    CURLMsg *msg = curl_multi_info_read(mo->multi, &queued);

    if (msgs_in_queue) {
        ZEND_TRY_ASSIGN_REF_LONG(msgs_in_queue, queued);
    }

    if (!msg) {
        RETURN_FALSE;
    }

    array_init(return_value);
    add_assoc_long(return_value, "msg", msg->msg);
    add_assoc_long(return_value, "result", msg->data.result);

    /* Find the PHP easy handle object for this CURL* */
    zend_llist_position pos;
    for (zval *el = zend_llist_get_first_ex(&mo->easylist, &pos);
         el != NULL;
         el = zend_llist_get_next_ex(&mo->easylist, &pos)) {
        ci_curl_obj *co = ci_curl_from_obj(Z_OBJ_P(el));
        if (co->handle == msg->easy_handle) {
            co->last_errno = msg->data.result;
            Z_ADDREF_P(el);
            add_assoc_zval(return_value, "handle", el);
            return;
        }
    }
}

PHP_FUNCTION(curl_cffi_multi_close) {
    zval *mh;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(mh, ci_multi_ce)
    ZEND_PARSE_PARAMETERS_END();
    /* Cleanup happens in destructor */
}

PHP_FUNCTION(curl_cffi_multi_errno) {
    zval *mh;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(mh, ci_multi_ce)
    ZEND_PARSE_PARAMETERS_END();
    ci_multi_obj *mo = ci_multi_from_obj(Z_OBJ_P(mh));
    RETURN_LONG((long)mo->last_errno);
}

PHP_FUNCTION(curl_cffi_multi_strerror) {
    zend_long err;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(err)
    ZEND_PARSE_PARAMETERS_END();
    const char *s = curl_multi_strerror((CURLMcode)err);
    if (s) { RETURN_STRING(s); }
    RETURN_NULL();
}

PHP_FUNCTION(curl_cffi_multi_setopt) {
    zval *mh, *value;
    zend_long option;
    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_OBJECT_OF_CLASS(mh, ci_multi_ce)
        Z_PARAM_LONG(option)
        Z_PARAM_ZVAL(value)
    ZEND_PARSE_PARAMETERS_END();
    ci_multi_obj *mo = ci_multi_from_obj(Z_OBJ_P(mh));

    /* CURLMOPT_PUSHFUNCTION (value 20014) */
    if (option == 20014) {
        if (!zend_is_callable(value, 0, NULL)) {
            zend_type_error("curl_cffi_multi_setopt(): Argument #2 ($option) must be a valid callback for option CURLMOPT_PUSHFUNCTION, function \"%s\" not found or invalid function name",
                Z_TYPE_P(value) == IS_STRING ? Z_STRVAL_P(value) : "");
            RETURN_THROWS();
        }
        /* Store but don't wire up (no push callback support yet) */
        RETURN_TRUE;
    }

    CURLMcode rc = curl_multi_setopt(mo->multi, (CURLMoption)option, zval_get_long(value));
    mo->last_errno = rc;
    if (rc == CURLM_UNKNOWN_OPTION) {
        zend_value_error("curl_cffi_multi_setopt(): Argument #2 ($option) is not a valid cURL multi option");
        RETURN_THROWS();
    }
    RETURN_BOOL(rc == CURLM_OK);
}

/* ========================================================================
 * curl_share_* API
 * ======================================================================== */

/* ci_share_obj and ci_share_from_obj are forward-declared above */
static zend_object_handlers ci_share_handlers;

static zend_object *ci_share_create(zend_class_entry *ce) {
    ci_share_obj *obj = zend_object_alloc(sizeof(ci_share_obj), ce);
    zend_object_std_init(&obj->std, ce);
    obj->std.handlers = &ci_share_handlers;
    obj->share = NULL;
    obj->last_errno = CURLSHE_OK;
    return &obj->std;
}

static void ci_share_free(zend_object *object) {
    ci_share_obj *obj = ci_share_from_obj(object);
    if (obj->share) { curl_share_cleanup(obj->share); obj->share = NULL; }
    zend_object_std_dtor(&obj->std);
}

PHP_FUNCTION(curl_cffi_share_init) {
    ZEND_PARSE_PARAMETERS_NONE();
    object_init_ex(return_value, ci_share_ce);
    ci_share_obj *obj = ci_share_from_obj(Z_OBJ_P(return_value));
    obj->share = curl_share_init();
}

PHP_FUNCTION(curl_cffi_share_close) {
    zval *sh;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(sh, ci_share_ce)
    ZEND_PARSE_PARAMETERS_END();
}

PHP_FUNCTION(curl_cffi_share_setopt) {
    zval *sh, *value;
    zend_long option;
    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_OBJECT_OF_CLASS(sh, ci_share_ce)
        Z_PARAM_LONG(option)
        Z_PARAM_ZVAL(value)
    ZEND_PARSE_PARAMETERS_END();
    ci_share_obj *so = ci_share_from_obj(Z_OBJ_P(sh));
    CURLSHcode rc = curl_share_setopt(so->share, (CURLSHoption)option, zval_get_long(value));
    so->last_errno = rc;
    if (rc == CURLSHE_BAD_OPTION) {
        zend_value_error("curl_cffi_share_setopt(): Argument #2 ($option) is not a valid cURL share option");
        RETURN_THROWS();
    }
    RETURN_BOOL(rc == CURLSHE_OK);
}

PHP_FUNCTION(curl_cffi_share_errno) {
    zval *sh;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(sh, ci_share_ce)
    ZEND_PARSE_PARAMETERS_END();
    ci_share_obj *so = ci_share_from_obj(Z_OBJ_P(sh));
    RETURN_LONG((long)so->last_errno);
}

PHP_FUNCTION(curl_cffi_share_strerror) {
    zend_long err;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_LONG(err)
    ZEND_PARSE_PARAMETERS_END();
    const char *s = curl_share_strerror((CURLSHcode)err);
    if (s) { RETURN_STRING(s); }
    RETURN_NULL();
}

/* ========================================================================
 * curl_escape / curl_unescape / curl_pause / curl_upkeep
 * ======================================================================== */

PHP_FUNCTION(curl_cffi_escape) {
    zval *ch;
    char *str;
    size_t str_len;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_OBJECT_OF_CLASS(ch, ci_curl_ce)
        Z_PARAM_STRING(str, str_len)
    ZEND_PARSE_PARAMETERS_END();
    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(ch));
    if (!obj->handle || obj->closed) {
        php_error_docref(NULL, E_WARNING, "Curl handle is not initialized or closed");
        RETURN_FALSE;
    }
    char *escaped = curl_easy_escape(obj->handle, str, (int)str_len);
    if (escaped) {
        RETVAL_STRING(escaped);
        curl_free(escaped);
    } else {
        RETURN_FALSE;
    }
}

PHP_FUNCTION(curl_cffi_unescape) {
    zval *ch;
    char *str;
    size_t str_len;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_OBJECT_OF_CLASS(ch, ci_curl_ce)
        Z_PARAM_STRING(str, str_len)
    ZEND_PARSE_PARAMETERS_END();
    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(ch));
    if (!obj->handle || obj->closed) {
        php_error_docref(NULL, E_WARNING, "Curl handle is not initialized or closed");
        RETURN_FALSE;
    }
    int out_len = 0;
    char *unescaped = curl_easy_unescape(obj->handle, str, (int)str_len, &out_len);
    if (unescaped) {
        RETVAL_STRINGL(unescaped, out_len);
        curl_free(unescaped);
    } else {
        RETURN_FALSE;
    }
}

PHP_FUNCTION(curl_cffi_pause) {
    zval *ch;
    zend_long bitmask;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_OBJECT_OF_CLASS(ch, ci_curl_ce)
        Z_PARAM_LONG(bitmask)
    ZEND_PARSE_PARAMETERS_END();
    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(ch));
    if (!obj->handle || obj->closed) {
        php_error_docref(NULL, E_WARNING, "Curl handle is not initialized or closed");
        RETURN_FALSE;
    }
    RETURN_LONG((long)curl_easy_pause(obj->handle, (int)bitmask));
}

PHP_FUNCTION(curl_cffi_upkeep) {
    zval *ch;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_OBJECT_OF_CLASS(ch, ci_curl_ce)
    ZEND_PARSE_PARAMETERS_END();
    ci_curl_obj *obj = ci_curl_from_obj(Z_OBJ_P(ch));
    if (!obj->handle || obj->closed) {
        php_error_docref(NULL, E_WARNING, "Curl handle is not initialized or closed");
        RETURN_FALSE;
    }
    RETURN_BOOL(curl_easy_upkeep(obj->handle) == CURLE_OK);
}

/* CURLFile methods */
PHP_METHOD(CURLFile, __construct) {
    char *filename, *mime_type = "", *posted_name = "";
    size_t filename_len, mime_len = 0, posted_len = 0;

    ZEND_PARSE_PARAMETERS_START(1, 3)
        Z_PARAM_STRING(filename, filename_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(mime_type, mime_len)
        Z_PARAM_STRING(posted_name, posted_len)
    ZEND_PARSE_PARAMETERS_END();

    zend_update_property_stringl(ci_curlfile_ce, Z_OBJ_P(ZEND_THIS), "name", sizeof("name")-1, filename, filename_len);
    if (mime_len > 0) {
        zend_update_property_stringl(ci_curlfile_ce, Z_OBJ_P(ZEND_THIS), "mime", sizeof("mime")-1, mime_type, mime_len);
    }
    if (posted_len > 0) {
        zend_update_property_stringl(ci_curlfile_ce, Z_OBJ_P(ZEND_THIS), "postname", sizeof("postname")-1, posted_name, posted_len);
    }
}

PHP_METHOD(CURLFile, getFilename) {
    ZEND_PARSE_PARAMETERS_NONE();
    zval rv;
    zval *val = zend_read_property(ci_curlfile_ce, Z_OBJ_P(ZEND_THIS), "name", sizeof("name")-1, 1, &rv);
    RETURN_ZVAL(val, 1, 0);
}

PHP_METHOD(CURLFile, getMimeType) {
    ZEND_PARSE_PARAMETERS_NONE();
    zval rv;
    zval *val = zend_read_property(ci_curlfile_ce, Z_OBJ_P(ZEND_THIS), "mime", sizeof("mime")-1, 1, &rv);
    RETURN_ZVAL(val, 1, 0);
}

PHP_METHOD(CURLFile, getPostFilename) {
    ZEND_PARSE_PARAMETERS_NONE();
    zval rv;
    zval *val = zend_read_property(ci_curlfile_ce, Z_OBJ_P(ZEND_THIS), "postname", sizeof("postname")-1, 1, &rv);
    RETURN_ZVAL(val, 1, 0);
}

PHP_METHOD(CURLFile, setMimeType) {
    char *mime; size_t mime_len;
    ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_STRING(mime, mime_len) ZEND_PARSE_PARAMETERS_END();
    zend_update_property_stringl(ci_curlfile_ce, Z_OBJ_P(ZEND_THIS), "mime", sizeof("mime")-1, mime, mime_len);
}

PHP_METHOD(CURLFile, setPostFilename) {
    char *name; size_t name_len;
    ZEND_PARSE_PARAMETERS_START(1, 1) Z_PARAM_STRING(name, name_len) ZEND_PARSE_PARAMETERS_END();
    zend_update_property_stringl(ci_curlfile_ce, Z_OBJ_P(ZEND_THIS), "postname", sizeof("postname")-1, name, name_len);
}

PHP_METHOD(CURLFile, __serialize) {
    ZEND_PARSE_PARAMETERS_NONE();
    zend_throw_exception(NULL, "Serialization of 'CURLFile' is not allowed", 0);
    RETURN_THROWS();
}

PHP_METHOD(CURLFile, __unserialize) {
    zval *data;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ARRAY(data)
    ZEND_PARSE_PARAMETERS_END();
    zend_throw_exception(NULL, "Unserialization of 'CURLFile' is not allowed", 0);
    RETURN_THROWS();
}

PHP_METHOD(CURLFile, __wakeup) {
    ZEND_PARSE_PARAMETERS_NONE();
    zend_throw_exception(NULL, "Unserialization of 'CURLFile' is not allowed", 0);
    RETURN_THROWS();
}

/* CURLStringFile methods */
PHP_METHOD(CURLStringFile, __construct) {
    char *data, *posted_name, *mime_type = "application/octet-stream";
    size_t data_len, posted_len, mime_len = sizeof("application/octet-stream")-1;

    ZEND_PARSE_PARAMETERS_START(2, 3)
        Z_PARAM_STRING(data, data_len)
        Z_PARAM_STRING(posted_name, posted_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(mime_type, mime_len)
    ZEND_PARSE_PARAMETERS_END();

    zend_update_property_stringl(ci_curlstringfile_ce, Z_OBJ_P(ZEND_THIS), "data", sizeof("data")-1, data, data_len);
    zend_update_property_stringl(ci_curlstringfile_ce, Z_OBJ_P(ZEND_THIS), "mime", sizeof("mime")-1, mime_type, mime_len);
    zend_update_property_stringl(ci_curlstringfile_ce, Z_OBJ_P(ZEND_THIS), "postname", sizeof("postname")-1, posted_name, posted_len);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_curlfile_construct, 0, 0, 1)
    ZEND_ARG_INFO(0, filename)
    ZEND_ARG_INFO(0, mime_type)
    ZEND_ARG_INFO(0, posted_filename)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_curlfile_none, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_curlfile_setstr, 0, 0, 1)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_curlstringfile_construct, 0, 0, 2)
    ZEND_ARG_INFO(0, data)
    ZEND_ARG_INFO(0, posted_filename)
    ZEND_ARG_INFO(0, mime_type)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_curlfile_serialize, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_curlfile_unserialize, 0, 0, 1)
    ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

static const zend_function_entry ci_curlfile_methods[] = {
    PHP_ME(CURLFile, __construct,    arginfo_curlfile_construct, ZEND_ACC_PUBLIC)
    PHP_ME(CURLFile, getFilename,    arginfo_curlfile_none,      ZEND_ACC_PUBLIC)
    PHP_ME(CURLFile, getMimeType,    arginfo_curlfile_none,      ZEND_ACC_PUBLIC)
    PHP_ME(CURLFile, getPostFilename,arginfo_curlfile_none,      ZEND_ACC_PUBLIC)
    PHP_ME(CURLFile, setMimeType,    arginfo_curlfile_setstr,    ZEND_ACC_PUBLIC)
    PHP_ME(CURLFile, setPostFilename,arginfo_curlfile_setstr,    ZEND_ACC_PUBLIC)
    PHP_ME(CURLFile, __serialize,    arginfo_curlfile_serialize, ZEND_ACC_PUBLIC)
    PHP_ME(CURLFile, __unserialize,  arginfo_curlfile_unserialize, ZEND_ACC_PUBLIC)
    PHP_ME(CURLFile, __wakeup,       arginfo_curlfile_none,      ZEND_ACC_PUBLIC)
    PHP_FE_END
};

static const zend_function_entry ci_curlstringfile_methods[] = {
    PHP_ME(CURLStringFile, __construct, arginfo_curlstringfile_construct, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* {{{ curl_cffi_file_create($filename, $mime_type = '', $posted_filename = '') */
PHP_FUNCTION(curl_cffi_file_create) {
    char *filename, *mime_type = "", *posted_name = "";
    size_t filename_len, mime_len = 0, posted_len = 0;

    ZEND_PARSE_PARAMETERS_START(1, 3)
        Z_PARAM_STRING(filename, filename_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING(mime_type, mime_len)
        Z_PARAM_STRING(posted_name, posted_len)
    ZEND_PARSE_PARAMETERS_END();

    object_init_ex(return_value, ci_curlfile_ce);
    zend_update_property_stringl(ci_curlfile_ce, Z_OBJ_P(return_value), "name", sizeof("name")-1, filename, filename_len);
    if (mime_len > 0) {
        zend_update_property_stringl(ci_curlfile_ce, Z_OBJ_P(return_value), "mime", sizeof("mime")-1, mime_type, mime_len);
    }
    if (posted_len > 0) {
        zend_update_property_stringl(ci_curlfile_ce, Z_OBJ_P(return_value), "postname", sizeof("postname")-1, posted_name, posted_len);
    }
}
/* }}} */

/* Arginfo for new functions */
ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_multi_init, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_multi_handle, 0, 0, 2)
    ZEND_ARG_INFO(0, multi_handle)
    ZEND_ARG_INFO(0, handle)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_multi_exec, 0, 0, 2)
    ZEND_ARG_INFO(0, multi_handle)
    ZEND_ARG_INFO(1, still_running)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_multi_select, 0, 0, 1)
    ZEND_ARG_INFO(0, multi_handle)
    ZEND_ARG_INFO(0, timeout)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_multi_one, 0, 0, 1)
    ZEND_ARG_INFO(0, handle)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_multi_info_read, 0, 0, 1)
    ZEND_ARG_INFO(0, multi_handle)
    ZEND_ARG_INFO(1, msgs_in_queue)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_multi_setopt, 0, 0, 3)
    ZEND_ARG_INFO(0, multi_handle)
    ZEND_ARG_INFO(0, option)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_share_init, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_share_one, 0, 0, 1)
    ZEND_ARG_INFO(0, share_handle)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_share_setopt, 0, 0, 3)
    ZEND_ARG_INFO(0, share_handle)
    ZEND_ARG_INFO(0, option)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_escape, 0, 0, 2)
    ZEND_ARG_INFO(0, handle)
    ZEND_ARG_INFO(0, string)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_pause, 0, 0, 2)
    ZEND_ARG_INFO(0, handle)
    ZEND_ARG_INFO(0, flags)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_upkeep, 0, 0, 1)
    ZEND_ARG_INFO(0, handle)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_long, 0, 0, 1)
    ZEND_ARG_INFO(0, error_code)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_ci_file_create, 0, 0, 1)
    ZEND_ARG_INFO(0, filename)
    ZEND_ARG_INFO(0, mime_type)
    ZEND_ARG_INFO(0, posted_filename)
ZEND_END_ARG_INFO()

/* Module-level function table (must be after all PHP_FUNCTION definitions) */
static const zend_function_entry ci_functions[] = {
    /* Easy handle */
    PHP_FE(curl_cffi_init,           arginfo_ci_init)
    PHP_FE(curl_cffi_setopt,         arginfo_ci_setopt)
    PHP_FE(curl_cffi_setopt_array,   arginfo_ci_setopt_array)
    PHP_FE(curl_cffi_exec,           arginfo_ci_exec)
    PHP_FE(curl_cffi_getinfo,        arginfo_ci_getinfo)
    PHP_FE(curl_cffi_error,          arginfo_ci_error)
    PHP_FE(curl_cffi_errno,          arginfo_ci_errno)
    PHP_FE(curl_cffi_close,          arginfo_ci_close)
    PHP_FE(curl_cffi_reset,          arginfo_ci_reset)
    PHP_FE(curl_cffi_copy_handle,    arginfo_ci_copy_handle)
    PHP_FE(curl_cffi_strerror,       arginfo_ci_strerror)
    PHP_FE(curl_cffi_impersonate,    arginfo_ci_impersonate)
    PHP_FE(curl_cffi_version,        arginfo_ci_version)
    PHP_FE(curl_cffi_escape,         arginfo_ci_escape)
    PHP_FE(curl_cffi_unescape,       arginfo_ci_escape)
    PHP_FE(curl_cffi_pause,          arginfo_ci_pause)
    PHP_FE(curl_cffi_upkeep,         arginfo_ci_upkeep)
    PHP_FE(curl_cffi_file_create,     arginfo_ci_file_create)
    /* Multi handle */
    PHP_FE(curl_cffi_multi_init,          arginfo_ci_multi_init)
    PHP_FE(curl_cffi_multi_add_handle,    arginfo_ci_multi_handle)
    PHP_FE(curl_cffi_multi_remove_handle, arginfo_ci_multi_handle)
    PHP_FE(curl_cffi_multi_exec,          arginfo_ci_multi_exec)
    PHP_FE(curl_cffi_multi_select,        arginfo_ci_multi_select)
    PHP_FE(curl_cffi_multi_getcontent,    arginfo_ci_multi_one)
    PHP_FE(curl_cffi_multi_info_read,     arginfo_ci_multi_info_read)
    PHP_FE(curl_cffi_multi_close,         arginfo_ci_multi_one)
    PHP_FE(curl_cffi_multi_errno,         arginfo_ci_multi_one)
    PHP_FE(curl_cffi_multi_strerror,      arginfo_ci_long)
    PHP_FE(curl_cffi_multi_setopt,        arginfo_ci_multi_setopt)
    /* Share handle */
    PHP_FE(curl_cffi_share_init,      arginfo_ci_share_init)
    PHP_FE(curl_cffi_share_close,     arginfo_ci_share_one)
    PHP_FE(curl_cffi_share_setopt,    arginfo_ci_share_setopt)
    PHP_FE(curl_cffi_share_errno,     arginfo_ci_share_one)
    PHP_FE(curl_cffi_share_strerror,  arginfo_ci_long)
    PHP_FE_END
};

/* ========================================================================
 * MINIT - Register classes and constants
 * ======================================================================== */

static void ci_register_curlopt_constants(zend_class_entry *ce) {
#define REG_OPT(name) zend_declare_class_constant_long(ce, #name, sizeof(#name)-1, CURLOPT_##name)
    /* String options */
    REG_OPT(URL);
    REG_OPT(USERAGENT);
    REG_OPT(REFERER);
    REG_OPT(COOKIE);
    REG_OPT(CUSTOMREQUEST);
    REG_OPT(POSTFIELDS);
    REG_OPT(USERNAME);
    REG_OPT(PASSWORD);
    REG_OPT(PROXY);
    REG_OPT(CAINFO);
    REG_OPT(CAPATH);
    REG_OPT(USERPWD);
    REG_OPT(INTERFACE);
    REG_OPT(SSLCERT);
    REG_OPT(SSLKEY);

    /* Long options */
    REG_OPT(POST);
    REG_OPT(HTTPGET);
    REG_OPT(NOBODY);
    REG_OPT(FOLLOWLOCATION);
    REG_OPT(MAXREDIRS);
    REG_OPT(TIMEOUT);
    REG_OPT(TIMEOUT_MS);
    REG_OPT(CONNECTTIMEOUT);
    REG_OPT(CONNECTTIMEOUT_MS);
    REG_OPT(SSL_VERIFYPEER);
    REG_OPT(SSL_VERIFYHOST);
    REG_OPT(HTTPPROXYTUNNEL);
    REG_OPT(POSTFIELDSIZE);
    REG_OPT(VERBOSE);
    REG_OPT(HEADER);
    REG_OPT(NOPROGRESS);
    REG_OPT(HTTP_VERSION);
    REG_OPT(PORT);
    REG_OPT(BUFFERSIZE);
#ifdef CURLOPT_TCP_KEEPALIVE
    REG_OPT(TCP_KEEPALIVE);
#endif
#ifdef CURLOPT_TCP_NODELAY
    REG_OPT(TCP_NODELAY);
#endif

    /* slist options */
    REG_OPT(HTTPHEADER);
    REG_OPT(PROXYHEADER);
    REG_OPT(RESOLVE);

    /* Callback options */
    REG_OPT(WRITEFUNCTION);
    REG_OPT(HEADERFUNCTION);

    /* PHP-specific options */
    zend_declare_class_constant_long(ce, "RETURNTRANSFER", sizeof("RETURNTRANSFER")-1, 19913);

    /* Curl-impersonate specific options */
    zend_declare_class_constant_long(ce, "IMPERSONATE", sizeof("IMPERSONATE")-1, 999);
    zend_declare_class_constant_long(ce, "SSL_SIG_HASH_ALGS", sizeof("SSL_SIG_HASH_ALGS")-1, 1001);
    zend_declare_class_constant_long(ce, "SSL_ENABLE_ALPS", sizeof("SSL_ENABLE_ALPS")-1, 1002);
    zend_declare_class_constant_long(ce, "SSL_CERT_COMPRESSION", sizeof("SSL_CERT_COMPRESSION")-1, 1003);
    zend_declare_class_constant_long(ce, "SSL_ENABLE_TICKET", sizeof("SSL_ENABLE_TICKET")-1, 1004);
    zend_declare_class_constant_long(ce, "HTTP2_PSEUDO_HEADERS_ORDER", sizeof("HTTP2_PSEUDO_HEADERS_ORDER")-1, 1005);
    zend_declare_class_constant_long(ce, "HTTP2_SETTINGS", sizeof("HTTP2_SETTINGS")-1, 1006);
    zend_declare_class_constant_long(ce, "SSL_PERMUTE_EXTENSIONS", sizeof("SSL_PERMUTE_EXTENSIONS")-1, 1007);
    zend_declare_class_constant_long(ce, "HTTP2_WINDOW_UPDATE", sizeof("HTTP2_WINDOW_UPDATE")-1, 1008);
    zend_declare_class_constant_long(ce, "TLS_GREASE", sizeof("TLS_GREASE")-1, 1011);
#undef REG_OPT
}

static void ci_register_curlinfo_constants(zend_class_entry *ce) {
#define REG_INFO(name) zend_declare_class_constant_long(ce, #name, sizeof(#name)-1, CURLINFO_##name)
    REG_INFO(EFFECTIVE_URL);
    REG_INFO(RESPONSE_CODE);
    REG_INFO(TOTAL_TIME);
    REG_INFO(NAMELOOKUP_TIME);
    REG_INFO(CONNECT_TIME);
    REG_INFO(PRETRANSFER_TIME);
    REG_INFO(STARTTRANSFER_TIME);
    REG_INFO(REDIRECT_TIME);
    REG_INFO(REDIRECT_COUNT);
    REG_INFO(SIZE_DOWNLOAD);
    REG_INFO(SIZE_UPLOAD);
    REG_INFO(SPEED_DOWNLOAD);
    REG_INFO(SPEED_UPLOAD);
    REG_INFO(HEADER_SIZE);
    REG_INFO(REQUEST_SIZE);
    REG_INFO(CONTENT_TYPE);
    REG_INFO(PRIMARY_IP);
    REG_INFO(PRIMARY_PORT);
    REG_INFO(LOCAL_IP);
    REG_INFO(LOCAL_PORT);
    REG_INFO(HTTP_CONNECTCODE);
    REG_INFO(REDIRECT_URL);
    REG_INFO(CONTENT_LENGTH_DOWNLOAD);
    REG_INFO(CONTENT_LENGTH_UPLOAD);
#ifdef CURLINFO_SCHEME
    REG_INFO(SCHEME);
#endif
#undef REG_INFO
}

static zend_result ci_curl_cast(zend_object *obj, zval *result, int type) {
    if (type == IS_LONG) {
        ZVAL_LONG(result, obj->handle);
        return SUCCESS;
    }
    return zend_std_cast_object_tostring(obj, result, type);
}

static zend_result ci_multi_cast(zend_object *obj, zval *result, int type) {
    if (type == IS_LONG) {
        ZVAL_LONG(result, obj->handle);
        return SUCCESS;
    }
    return zend_std_cast_object_tostring(obj, result, type);
}

/* Curl constructor is allowed - PHP_METHOD(Curl, __construct) handles initialization */

static zend_function *ci_multi_get_constructor(zend_object *object) {
    zend_throw_error(NULL, "Cannot directly construct CurlImpersonate\\CurlMultiHandle, use curl_cffi_multi_init() instead");
    return NULL;
}

static zend_function *ci_share_get_constructor(zend_object *object) {
    zend_throw_error(NULL, "Cannot directly construct CurlImpersonate\\CurlShareHandle, use curl_cffi_share_init() instead");
    return NULL;
}

static zend_object *ci_curl_clone(zend_object *old_object) {
    ci_curl_obj *old_obj = ci_curl_from_obj(old_object);
    zend_object *new_object = ci_curl_create(old_object->ce);
    ci_curl_obj *new_obj = ci_curl_from_obj(new_object);

    zend_objects_clone_members(new_object, old_object);

    new_obj->handle = curl_easy_duphandle(old_obj->handle);
    if (!new_obj->handle) {
        return new_object;
    }

    new_obj->return_transfer = old_obj->return_transfer;
    new_obj->header_out_enabled = old_obj->header_out_enabled;
    if (Z_TYPE(old_obj->private_data) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->private_data, &old_obj->private_data);
    }

    /* Copy callbacks */
    if (Z_TYPE(old_obj->write_cb) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->write_cb, &old_obj->write_cb);
    }
    if (Z_TYPE(old_obj->header_cb) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->header_cb, &old_obj->header_cb);
    }
    if (Z_TYPE(old_obj->progress_cb) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->progress_cb, &old_obj->progress_cb);
    }
    if (Z_TYPE(old_obj->xferinfo_cb) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->xferinfo_cb, &old_obj->xferinfo_cb);
    }
    if (Z_TYPE(old_obj->debug_cb) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->debug_cb, &old_obj->debug_cb);
    }
    if (Z_TYPE(old_obj->read_cb) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->read_cb, &old_obj->read_cb);
    }
    if (Z_TYPE(old_obj->fnmatch_cb) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->fnmatch_cb, &old_obj->fnmatch_cb);
    }
    if (Z_TYPE(old_obj->postfields) != IS_UNDEF) {
        ZVAL_COPY(&new_obj->postfields, &old_obj->postfields);
    }
    if (old_obj->header_out) {
        new_obj->header_out = zend_string_copy(old_obj->header_out);
    }

    /* Copy slist options */
    if (old_obj->req_headers) {
        struct curl_slist *src = old_obj->req_headers;
        struct curl_slist *dst = NULL;
        while (src) { dst = curl_slist_append(dst, src->data); src = src->next; }
        new_obj->req_headers = dst;
        curl_easy_setopt(new_obj->handle, CURLOPT_HTTPHEADER, dst);
    }

    {
        curl_easy_setopt(new_obj->handle, CURLOPT_ERRORBUFFER, new_obj->errbuf);
        curl_easy_setopt(new_obj->handle, CURLOPT_WRITEFUNCTION, ci_curl_write_cb);
        curl_easy_setopt(new_obj->handle, CURLOPT_WRITEDATA, new_obj);
        curl_easy_setopt(new_obj->handle, CURLOPT_HEADERFUNCTION, ci_curl_header_cb);
        curl_easy_setopt(new_obj->handle, CURLOPT_HEADERDATA, new_obj);

        /* Re-wire callbacks to point to new_obj */
        if (Z_TYPE(new_obj->debug_cb) != IS_UNDEF) {
            curl_easy_setopt(new_obj->handle, CURLOPT_DEBUGFUNCTION, ci_curl_user_debug_cb);
            curl_easy_setopt(new_obj->handle, CURLOPT_DEBUGDATA, new_obj);
        } else if (new_obj->header_out_enabled) {
            curl_easy_setopt(new_obj->handle, CURLOPT_DEBUGFUNCTION, ci_curl_debug_cb);
            curl_easy_setopt(new_obj->handle, CURLOPT_DEBUGDATA, new_obj);
            curl_easy_setopt(new_obj->handle, CURLOPT_VERBOSE, 1L);
            new_obj->devnull = fopen("/dev/null", "w");
            if (new_obj->devnull) {
                curl_easy_setopt(new_obj->handle, CURLOPT_STDERR, new_obj->devnull);
            }
        }
        if (Z_TYPE(new_obj->progress_cb) != IS_UNDEF) {
            curl_easy_setopt(new_obj->handle, CURLOPT_PROGRESSFUNCTION, ci_curl_progress_cb);
            curl_easy_setopt(new_obj->handle, CURLOPT_PROGRESSDATA, new_obj);
        }
        if (Z_TYPE(new_obj->xferinfo_cb) != IS_UNDEF) {
            curl_easy_setopt(new_obj->handle, CURLOPT_XFERINFOFUNCTION, ci_curl_xferinfo_cb);
            curl_easy_setopt(new_obj->handle, CURLOPT_XFERINFODATA, new_obj);
        }
        if (Z_TYPE(new_obj->read_cb) != IS_UNDEF) {
            curl_easy_setopt(new_obj->handle, CURLOPT_READFUNCTION, ci_curl_read_cb);
            curl_easy_setopt(new_obj->handle, CURLOPT_READDATA, new_obj);
        }
    }

    return new_object;
}

PHP_MINIT_FUNCTION(curl_impersonate) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    zend_class_entry ce;

    /* --- CurlException --- */
    INIT_NS_CLASS_ENTRY(ce, "CurlImpersonate", "CurlException", NULL);
    ci_exception_ce = zend_register_internal_class_ex(&ce, spl_ce_RuntimeException);
    /* Add 'response' property for attaching Response to redirect errors */
    zend_declare_property_null(ci_exception_ce, "response", sizeof("response") - 1, ZEND_ACC_PUBLIC);

    /* --- CurlOpt constants class --- */
    INIT_NS_CLASS_ENTRY(ce, "CurlImpersonate", "CurlOpt", NULL);
    ci_curlopt_ce = zend_register_internal_class(&ce);
    ci_curlopt_ce->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES;
    ci_register_curlopt_constants(ci_curlopt_ce);

    /* --- CurlInfo constants class --- */
    INIT_NS_CLASS_ENTRY(ce, "CurlImpersonate", "CurlInfo", NULL);
    ci_curlinfo_ce = zend_register_internal_class(&ce);
    ci_curlinfo_ce->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES;
    ci_register_curlinfo_constants(ci_curlinfo_ce);

    /* --- Curl handle class --- */
    INIT_NS_CLASS_ENTRY(ce, "CurlImpersonate", "Curl", ci_curl_methods);
    ci_curl_ce = zend_register_internal_class(&ce);
    ci_curl_ce->create_object = ci_curl_create;
    ci_curl_ce->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES;

    memcpy(&ci_curl_handlers, &std_object_handlers, sizeof(zend_object_handlers));
    ci_curl_handlers.offset = XtOffsetOf(ci_curl_obj, std);
    ci_curl_handlers.free_obj = ci_curl_free;
    ci_curl_handlers.cast_object = ci_curl_cast;
    ci_curl_handlers.clone_obj = ci_curl_clone;

    /* --- Response class --- */
    INIT_NS_CLASS_ENTRY(ce, "CurlImpersonate", "Response", ci_response_methods);
    ci_response_ce = zend_register_internal_class(&ce);
    ci_response_ce->ce_flags |= ZEND_ACC_FINAL;

    zend_declare_property_long(ci_response_ce, "statusCode", sizeof("statusCode") - 1, 0, ZEND_ACC_PUBLIC);
    zend_declare_property_string(ci_response_ce, "url", sizeof("url") - 1, "", ZEND_ACC_PUBLIC);
    zend_declare_property_string(ci_response_ce, "content", sizeof("content") - 1, "", ZEND_ACC_PUBLIC);
    zend_declare_property_null(ci_response_ce, "headers", sizeof("headers") - 1, ZEND_ACC_PUBLIC);
    zend_declare_property_null(ci_response_ce, "cookies", sizeof("cookies") - 1, ZEND_ACC_PUBLIC);
    zend_declare_property_double(ci_response_ce, "elapsed", sizeof("elapsed") - 1, 0.0, ZEND_ACC_PUBLIC);
    zend_declare_property_string(ci_response_ce, "reason", sizeof("reason") - 1, "", ZEND_ACC_PUBLIC);
    zend_declare_property_long(ci_response_ce, "redirectCount", sizeof("redirectCount") - 1, 0, ZEND_ACC_PUBLIC);

    /* --- Session class --- */
    INIT_NS_CLASS_ENTRY(ce, "CurlImpersonate", "Session", ci_session_methods);
    ci_session_ce = zend_register_internal_class(&ce);
    ci_session_ce->create_object = ci_session_create;
    ci_session_ce->ce_flags |= ZEND_ACC_FINAL;

    memcpy(&ci_session_handlers, &std_object_handlers, sizeof(zend_object_handlers));
    ci_session_handlers.offset = XtOffsetOf(ci_session_obj, std);
    ci_session_handlers.free_obj = ci_session_free;
    ci_session_handlers.clone_obj = NULL;

    /* Declare session public properties for introspection */
    zend_declare_property_null(ci_session_ce, "cookies", sizeof("cookies") - 1, ZEND_ACC_PUBLIC);

    /* --- CurlMultiHandle class --- */
    INIT_NS_CLASS_ENTRY(ce, "CurlImpersonate", "CurlMultiHandle", NULL);
    ci_multi_ce = zend_register_internal_class(&ce);
    ci_multi_ce->create_object = ci_multi_create;
    ci_multi_ce->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES;
    memcpy(&ci_multi_handlers, &std_object_handlers, sizeof(zend_object_handlers));
    ci_multi_handlers.offset = XtOffsetOf(ci_multi_obj, std);
    ci_multi_handlers.free_obj = ci_multi_free;
    ci_multi_handlers.get_constructor = ci_multi_get_constructor;
    ci_multi_handlers.cast_object = ci_multi_cast;
    ci_multi_handlers.clone_obj = NULL;

    /* --- CurlShareHandle class --- */
    INIT_NS_CLASS_ENTRY(ce, "CurlImpersonate", "CurlShareHandle", NULL);
    ci_share_ce = zend_register_internal_class(&ce);
    ci_share_ce->create_object = ci_share_create;
    ci_share_ce->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NO_DYNAMIC_PROPERTIES;
    memcpy(&ci_share_handlers, &std_object_handlers, sizeof(zend_object_handlers));
    ci_share_handlers.offset = XtOffsetOf(ci_share_obj, std);
    ci_share_handlers.free_obj = ci_share_free;
    ci_share_handlers.get_constructor = ci_share_get_constructor;
    ci_share_handlers.clone_obj = NULL;

    /* --- Register standard CURLOPT/CURLINFO/CURLE constants --- */
    /* Conditionally: if ext/curl isn't loaded, provide the same constant names */
#define RC(name, val) do { \
    if (!zend_get_constant_str(#name, sizeof(#name)-1)) { \
        REGISTER_LONG_CONSTANT(#name, val, CONST_CS | CONST_PERSISTENT); \
    } \
} while(0)

    RC(CURLOPT_RETURNTRANSFER, 19913);
    RC(CURLOPT_URL, CURLOPT_URL);
    RC(CURLOPT_USERAGENT, CURLOPT_USERAGENT);
    RC(CURLOPT_REFERER, CURLOPT_REFERER);
    RC(CURLOPT_COOKIE, CURLOPT_COOKIE);
    RC(CURLOPT_COOKIEFILE, CURLOPT_COOKIEFILE);
    RC(CURLOPT_COOKIEJAR, CURLOPT_COOKIEJAR);
    RC(CURLOPT_COOKIELIST, CURLOPT_COOKIELIST);
    RC(CURLOPT_CUSTOMREQUEST, CURLOPT_CUSTOMREQUEST);
    RC(CURLOPT_POSTFIELDS, CURLOPT_POSTFIELDS);
    RC(CURLOPT_USERNAME, CURLOPT_USERNAME);
    RC(CURLOPT_PASSWORD, CURLOPT_PASSWORD);
    RC(CURLOPT_USERPWD, CURLOPT_USERPWD);
    RC(CURLOPT_PROXY, CURLOPT_PROXY);
    RC(CURLOPT_PROXYUSERPWD, CURLOPT_PROXYUSERPWD);
    RC(CURLOPT_CAINFO, CURLOPT_CAINFO);
    RC(CURLOPT_CAPATH, CURLOPT_CAPATH);
    RC(CURLOPT_SSLCERT, CURLOPT_SSLCERT);
    RC(CURLOPT_SSLKEY, CURLOPT_SSLKEY);
    RC(CURLOPT_INTERFACE, CURLOPT_INTERFACE);
    RC(CURLOPT_ENCODING, CURLOPT_ENCODING);
    RC(CURLOPT_ACCEPT_ENCODING, CURLOPT_ACCEPT_ENCODING);
    RC(CURLOPT_RANGE, CURLOPT_RANGE);
    RC(CURLOPT_POST, CURLOPT_POST);
    RC(CURLOPT_HTTPGET, CURLOPT_HTTPGET);
    RC(CURLOPT_NOBODY, CURLOPT_NOBODY);
    RC(CURLOPT_FOLLOWLOCATION, CURLOPT_FOLLOWLOCATION);
    RC(CURLOPT_MAXREDIRS, CURLOPT_MAXREDIRS);
    RC(CURLOPT_TIMEOUT, CURLOPT_TIMEOUT);
    RC(CURLOPT_TIMEOUT_MS, CURLOPT_TIMEOUT_MS);
    RC(CURLOPT_CONNECTTIMEOUT, CURLOPT_CONNECTTIMEOUT);
    RC(CURLOPT_CONNECTTIMEOUT_MS, CURLOPT_CONNECTTIMEOUT_MS);
    RC(CURLOPT_SSL_VERIFYPEER, CURLOPT_SSL_VERIFYPEER);
    RC(CURLOPT_SSL_VERIFYHOST, CURLOPT_SSL_VERIFYHOST);
    RC(CURLOPT_HTTPPROXYTUNNEL, CURLOPT_HTTPPROXYTUNNEL);
    RC(CURLOPT_POSTFIELDSIZE, CURLOPT_POSTFIELDSIZE);
    RC(CURLOPT_VERBOSE, CURLOPT_VERBOSE);
    RC(CURLOPT_HEADER, CURLOPT_HEADER);
    RC(CURLOPT_NOPROGRESS, CURLOPT_NOPROGRESS);
    RC(CURLOPT_PORT, CURLOPT_PORT);
    RC(CURLOPT_AUTOREFERER, CURLOPT_AUTOREFERER);
    RC(CURLOPT_BUFFERSIZE, CURLOPT_BUFFERSIZE);
    RC(CURLOPT_FAILONERROR, CURLOPT_FAILONERROR);
    RC(CURLOPT_FILETIME, CURLOPT_FILETIME);
    RC(CURLOPT_FRESH_CONNECT, CURLOPT_FRESH_CONNECT);
    RC(CURLOPT_FORBID_REUSE, CURLOPT_FORBID_REUSE);
    RC(CURLOPT_LOW_SPEED_LIMIT, CURLOPT_LOW_SPEED_LIMIT);
    RC(CURLOPT_LOW_SPEED_TIME, CURLOPT_LOW_SPEED_TIME);
    RC(CURLOPT_MAXCONNECTS, CURLOPT_MAXCONNECTS);
    RC(CURLOPT_HTTPAUTH, CURLOPT_HTTPAUTH);
    RC(CURLOPT_PROXYAUTH, CURLOPT_PROXYAUTH);
    RC(CURLOPT_HTTP_VERSION, CURLOPT_HTTP_VERSION);
    RC(CURLOPT_UPLOAD, CURLOPT_UPLOAD);
    RC(CURLOPT_INFILESIZE, CURLOPT_INFILESIZE);
    RC(CURLOPT_TCP_NODELAY, CURLOPT_TCP_NODELAY);
    RC(CURLOPT_HTTPHEADER, CURLOPT_HTTPHEADER);
    RC(CURLOPT_PROXYHEADER, CURLOPT_PROXYHEADER);
    RC(CURLOPT_RESOLVE, CURLOPT_RESOLVE);
    RC(CURLOPT_WRITEFUNCTION, CURLOPT_WRITEFUNCTION);
    RC(CURLOPT_HEADERFUNCTION, CURLOPT_HEADERFUNCTION);
    RC(CURLOPT_READFUNCTION, CURLOPT_READFUNCTION);
    RC(CURLOPT_FILE, CURLOPT_FILE);
    RC(CURLOPT_INFILE, CURLOPT_INFILE);
    RC(CURLOPT_WRITEHEADER, CURLOPT_WRITEHEADER);
    RC(CURLOPT_STDERR, CURLOPT_STDERR);
    RC(CURLOPT_QUOTE, CURLOPT_QUOTE);
    RC(CURLOPT_POSTQUOTE, CURLOPT_POSTQUOTE);
#ifdef CURLOPT_TCP_KEEPALIVE
    RC(CURLOPT_TCP_KEEPALIVE, CURLOPT_TCP_KEEPALIVE);
#endif
#ifdef CURLOPT_SSLVERSION
    RC(CURLOPT_SSLVERSION, CURLOPT_SSLVERSION);
#endif
#ifdef CURLOPT_UNIX_SOCKET_PATH
    RC(CURLOPT_UNIX_SOCKET_PATH, CURLOPT_UNIX_SOCKET_PATH);
#endif
#ifdef CURLOPT_CONNECT_TO
    RC(CURLOPT_CONNECT_TO, CURLOPT_CONNECT_TO);
#endif

    RC(CURLAUTH_BASIC, CURLAUTH_BASIC);
    RC(CURLAUTH_DIGEST, CURLAUTH_DIGEST);
    RC(CURLAUTH_BEARER, CURLAUTH_BEARER);
    RC(CURLAUTH_ANY, CURLAUTH_ANY);
    RC(CURLAUTH_ANYSAFE, CURLAUTH_ANYSAFE);

    RC(CURL_HTTP_VERSION_NONE, CURL_HTTP_VERSION_NONE);
    RC(CURL_HTTP_VERSION_1_0, CURL_HTTP_VERSION_1_0);
    RC(CURL_HTTP_VERSION_1_1, CURL_HTTP_VERSION_1_1);
    RC(CURL_HTTP_VERSION_2_0, CURL_HTTP_VERSION_2_0);
#ifdef CURL_HTTP_VERSION_2TLS
    RC(CURL_HTTP_VERSION_2TLS, CURL_HTTP_VERSION_2TLS);
#endif
#ifdef CURL_HTTP_VERSION_3
    RC(CURL_HTTP_VERSION_3, CURL_HTTP_VERSION_3);
#endif

    RC(CURLINFO_EFFECTIVE_URL, CURLINFO_EFFECTIVE_URL);
    RC(CURLINFO_HTTP_CODE, CURLINFO_RESPONSE_CODE);
    RC(CURLINFO_RESPONSE_CODE, CURLINFO_RESPONSE_CODE);
    RC(CURLINFO_TOTAL_TIME, CURLINFO_TOTAL_TIME);
    RC(CURLINFO_NAMELOOKUP_TIME, CURLINFO_NAMELOOKUP_TIME);
    RC(CURLINFO_CONNECT_TIME, CURLINFO_CONNECT_TIME);
    RC(CURLINFO_PRETRANSFER_TIME, CURLINFO_PRETRANSFER_TIME);
    RC(CURLINFO_STARTTRANSFER_TIME, CURLINFO_STARTTRANSFER_TIME);
    RC(CURLINFO_REDIRECT_TIME, CURLINFO_REDIRECT_TIME);
    RC(CURLINFO_REDIRECT_COUNT, CURLINFO_REDIRECT_COUNT);
    RC(CURLINFO_CONTENT_TYPE, CURLINFO_CONTENT_TYPE);
    RC(CURLINFO_HEADER_SIZE, CURLINFO_HEADER_SIZE);
    RC(CURLINFO_REQUEST_SIZE, CURLINFO_REQUEST_SIZE);
    RC(CURLINFO_REDIRECT_URL, CURLINFO_REDIRECT_URL);
    RC(CURLINFO_PRIMARY_IP, CURLINFO_PRIMARY_IP);
    RC(CURLINFO_PRIMARY_PORT, CURLINFO_PRIMARY_PORT);
    RC(CURLINFO_LOCAL_IP, CURLINFO_LOCAL_IP);
    RC(CURLINFO_LOCAL_PORT, CURLINFO_LOCAL_PORT);
    RC(CURLINFO_HTTP_CONNECTCODE, CURLINFO_HTTP_CONNECTCODE);
    RC(CURLINFO_CONTENT_LENGTH_DOWNLOAD, CURLINFO_CONTENT_LENGTH_DOWNLOAD);
#ifdef CURLINFO_SCHEME
    RC(CURLINFO_SCHEME, CURLINFO_SCHEME);
#endif

    RC(CURLE_OK, CURLE_OK);
    RC(CURLE_UNSUPPORTED_PROTOCOL, CURLE_UNSUPPORTED_PROTOCOL);
    RC(CURLE_COULDNT_RESOLVE_HOST, CURLE_COULDNT_RESOLVE_HOST);
    RC(CURLE_COULDNT_CONNECT, CURLE_COULDNT_CONNECT);
    RC(CURLE_OPERATION_TIMEDOUT, CURLE_OPERATION_TIMEDOUT);
    RC(CURLE_SSL_CONNECT_ERROR, CURLE_SSL_CONNECT_ERROR);
    RC(CURLE_TOO_MANY_REDIRECTS, CURLE_TOO_MANY_REDIRECTS);
    RC(CURLE_GOT_NOTHING, CURLE_GOT_NOTHING);
    RC(CURLE_PARTIAL_FILE, CURLE_PARTIAL_FILE);
    RC(CURLE_ABORTED_BY_CALLBACK, CURLE_ABORTED_BY_CALLBACK);

    RC(CURL_IPRESOLVE_WHATEVER, CURL_IPRESOLVE_WHATEVER);
    RC(CURL_IPRESOLVE_V4, CURL_IPRESOLVE_V4);
    RC(CURL_IPRESOLVE_V6, CURL_IPRESOLVE_V6);

    /* Multi constants */
    RC(CURLM_OK, CURLM_OK);
    RC(CURLM_CALL_MULTI_PERFORM, CURLM_CALL_MULTI_PERFORM);
    RC(CURLMSG_DONE, CURLMSG_DONE);
#ifdef CURLMOPT_PIPELINING
    RC(CURLMOPT_PIPELINING, CURLMOPT_PIPELINING);
#endif
#ifdef CURLMOPT_MAXCONNECTS
    RC(CURLMOPT_MAXCONNECTS, CURLMOPT_MAXCONNECTS);
#endif
#ifdef CURLPIPE_NOTHING
    RC(CURLPIPE_NOTHING, CURLPIPE_NOTHING);
    RC(CURLPIPE_MULTIPLEX, CURLPIPE_MULTIPLEX);
#endif

    /* Share constants */
    RC(CURL_LOCK_DATA_COOKIE, CURL_LOCK_DATA_COOKIE);
    RC(CURL_LOCK_DATA_DNS, CURL_LOCK_DATA_DNS);
    RC(CURL_LOCK_DATA_SSL_SESSION, CURL_LOCK_DATA_SSL_SESSION);
    RC(CURLSHOPT_SHARE, CURLSHOPT_SHARE);
    RC(CURLSHOPT_UNSHARE, CURLSHOPT_UNSHARE);

    /* Pause constants */
    RC(CURLPAUSE_ALL, CURLPAUSE_ALL);
    RC(CURLPAUSE_CONT, CURLPAUSE_CONT);
    RC(CURLPAUSE_RECV, CURLPAUSE_RECV);
    RC(CURLPAUSE_RECV_CONT, CURLPAUSE_RECV_CONT);
    RC(CURLPAUSE_SEND, CURLPAUSE_SEND);
    RC(CURLPAUSE_SEND_CONT, CURLPAUSE_SEND_CONT);

    /* CURLOPT_PRIVATE */
    RC(CURLOPT_PRIVATE, CURLOPT_PRIVATE);
    /* CURLINFO_PRIVATE */
    RC(CURLINFO_PRIVATE, CURLINFO_PRIVATE);

    /* Additional CURLOPT constants used by tests */
    RC(CURLOPT_IPRESOLVE, CURLOPT_IPRESOLVE);
    RC(CURLOPT_SHARE, CURLOPT_SHARE);

    RC(CURLOPT_PROGRESSFUNCTION, CURLOPT_PROGRESSFUNCTION);
    RC(CURLOPT_XFERINFOFUNCTION, CURLOPT_XFERINFOFUNCTION);
    RC(CURLOPT_DEBUGFUNCTION, CURLOPT_DEBUGFUNCTION);
    RC(CURLOPT_READFUNCTION, CURLOPT_READFUNCTION);
#ifdef CURLOPT_FNMATCH_FUNCTION
    RC(CURLOPT_FNMATCH_FUNCTION, CURLOPT_FNMATCH_FUNCTION);
#endif
#ifdef CURLOPT_PREREQFUNCTION
    RC(CURLOPT_PREREQFUNCTION, CURLOPT_PREREQFUNCTION);
#endif
#ifdef CURLOPT_SSH_HOSTKEYFUNCTION
    RC(CURLOPT_SSH_HOSTKEYFUNCTION, CURLOPT_SSH_HOSTKEYFUNCTION);
#endif
    RC(CURLINFO_HEADER_OUT, CURLINFO_HEADER_OUT);
    RC(CURLOPT_HEADEROPT, CURLOPT_HEADEROPT);
#ifdef CURLHEADER_UNIFIED
    RC(CURLHEADER_UNIFIED, CURLHEADER_UNIFIED);
    RC(CURLHEADER_SEPARATE, CURLHEADER_SEPARATE);
#endif
    RC(CURLOPT_PROTOCOLS, CURLOPT_PROTOCOLS);
    RC(CURLOPT_REDIR_PROTOCOLS, CURLOPT_REDIR_PROTOCOLS);
    RC(CURLOPT_CERTINFO, CURLOPT_CERTINFO);
    RC(CURLINFO_CERTINFO, CURLINFO_CERTINFO);
    RC(CURLOPT_PINNEDPUBLICKEY, CURLOPT_PINNEDPUBLICKEY);
    RC(CURLOPT_MAXFILESIZE, CURLOPT_MAXFILESIZE);
    RC(CURLOPT_DNS_CACHE_TIMEOUT, CURLOPT_DNS_CACHE_TIMEOUT);

    RC(CURLM_BAD_HANDLE, CURLM_BAD_HANDLE);
    RC(CURLM_BAD_EASY_HANDLE, CURLM_BAD_EASY_HANDLE);
    RC(CURLM_OUT_OF_MEMORY, CURLM_OUT_OF_MEMORY);
    RC(CURLM_INTERNAL_ERROR, CURLM_INTERNAL_ERROR);
    RC(CURLSHE_OK, CURLSHE_OK);
    RC(CURLSHE_BAD_OPTION, CURLSHE_BAD_OPTION);
    RC(CURLSHE_IN_USE, CURLSHE_IN_USE);
    RC(CURLSHE_INVALID, CURLSHE_INVALID);
    RC(CURLSHE_NOMEM, CURLSHE_NOMEM);
#ifdef CURL_VERSION_ASYNCHDNS
    RC(CURL_VERSION_IPV6, CURL_VERSION_IPV6);
    RC(CURL_VERSION_KERBEROS4, CURL_VERSION_KERBEROS4);
    RC(CURL_VERSION_SSL, CURL_VERSION_SSL);
    RC(CURL_VERSION_LIBZ, CURL_VERSION_LIBZ);
    RC(CURL_VERSION_ASYNCHDNS, CURL_VERSION_ASYNCHDNS);
    RC(CURL_VERSION_IDN, CURL_VERSION_IDN);
#endif
#ifdef CURL_VERSION_HTTP2
    RC(CURL_VERSION_HTTP2, CURL_VERSION_HTTP2);
#endif
#ifdef CURL_VERSION_HTTP3
    RC(CURL_VERSION_HTTP3, CURL_VERSION_HTTP3);
#endif
#ifdef CURL_VERSION_BROTLI
    RC(CURL_VERSION_BROTLI, CURL_VERSION_BROTLI);
#endif
#ifdef CURL_VERSION_ZSTD
    RC(CURL_VERSION_ZSTD, CURL_VERSION_ZSTD);
#endif

#ifdef CURL_PUSH_OK
    RC(CURL_PUSH_OK, CURL_PUSH_OK);
    RC(CURL_PUSH_DENY, CURL_PUSH_DENY);
#endif
    RC(CURLINFO_COOKIELIST, CURLINFO_COOKIELIST);
#ifdef CURLOPT_SAFE_UPLOAD
    RC(CURLOPT_SAFE_UPLOAD, CURLOPT_SAFE_UPLOAD);
#endif
#ifdef CURLINFO_POSTTRANSFER_TIME_T
    RC(CURLINFO_POSTTRANSFER_TIME_T, CURLINFO_POSTTRANSFER_TIME_T);
#endif
    RC(CURLOPT_NOPROGRESS, CURLOPT_NOPROGRESS);
    RC(CURLOPT_READDATA, CURLOPT_READDATA);
    RC(CURLOPT_WRITEDATA, CURLOPT_WRITEDATA);
#ifdef CURLOPT_SUPPRESS_CONNECT_HEADERS
    RC(CURLOPT_SUPPRESS_CONNECT_HEADERS, CURLOPT_SUPPRESS_CONNECT_HEADERS);
#endif
#ifdef CURLOPT_MAXAGE_CONN
    RC(CURLOPT_MAXAGE_CONN, CURLOPT_MAXAGE_CONN);
#endif
    RC(CURL_READFUNC_PAUSE, CURL_READFUNC_PAUSE);

#undef RC

    /* --- CURLFile class --- */
    INIT_CLASS_ENTRY(ce, "CURLFile", ci_curlfile_methods);
    ci_curlfile_ce = zend_register_internal_class(&ce);
    zend_declare_property_string(ci_curlfile_ce, "name", sizeof("name")-1, "", ZEND_ACC_PUBLIC);
    zend_declare_property_string(ci_curlfile_ce, "mime", sizeof("mime")-1, "", ZEND_ACC_PUBLIC);
    zend_declare_property_string(ci_curlfile_ce, "postname", sizeof("postname")-1, "", ZEND_ACC_PUBLIC);

    /* --- CURLStringFile class --- */
    INIT_CLASS_ENTRY(ce, "CURLStringFile", ci_curlstringfile_methods);
    ci_curlstringfile_ce = zend_register_internal_class(&ce);
    zend_declare_property_string(ci_curlstringfile_ce, "data", sizeof("data")-1, "", ZEND_ACC_PUBLIC);
    zend_declare_property_string(ci_curlstringfile_ce, "mime", sizeof("mime")-1, "", ZEND_ACC_PUBLIC);
    zend_declare_property_string(ci_curlstringfile_ce, "postname", sizeof("postname")-1, "", ZEND_ACC_PUBLIC);

    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(curl_impersonate) {
    curl_global_cleanup();
    return SUCCESS;
}

PHP_MINFO_FUNCTION(curl_impersonate) {
    php_info_print_table_start();
    php_info_print_table_header(2, "curl_impersonate support", "enabled");
    php_info_print_table_row(2, "Version", PHP_CURL_IMPERSONATE_VERSION);
    php_info_print_table_row(2, "libcurl version", curl_version());
    php_info_print_table_end();
}

/* ========================================================================
 * Module entry
 * ======================================================================== */

zend_module_entry curl_impersonate_module_entry = {
    STANDARD_MODULE_HEADER,
    "curl_impersonate",
    ci_functions,
    PHP_MINIT(curl_impersonate),
    PHP_MSHUTDOWN(curl_impersonate),
    NULL, /* RINIT */
    NULL, /* RSHUTDOWN */
    PHP_MINFO(curl_impersonate),
    PHP_CURL_IMPERSONATE_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_CURL_IMPERSONATE
ZEND_GET_MODULE(curl_impersonate)
#endif
