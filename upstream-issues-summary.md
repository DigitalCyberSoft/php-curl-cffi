# Upstream Issues: curl-impersonate and Python curl_cffi

## curl-impersonate (C library) - lexiforest/curl-impersonate

### 1. ~~CURLOPT_HTTP_VERSION is ignored~~ NOT A BUG

- **Status:** Resolved - was a misinterpretation of `CURLINFO_HTTP_VERSION` return values
- **What happened:** `CURLINFO_HTTP_VERSION` returns `CURL_HTTP_VERSION_*` constants (2=HTTP/1.1, 3=HTTP/2), NOT major.minor concatenation (11, 20). We and the upstream Python issue reporters both misread `2` as "HTTP/2" when it actually means "HTTP/1.1".
- `CURLOPT_HTTP_VERSION = CURL_HTTP_VERSION_1_1` **does work** correctly, both before and after `curl_easy_impersonate()`. The impersonate code explicitly respects user-set HTTP version (curl.patch line 1008: `if (data->set.httpwant != CURL_HTTP_VERSION_NONE)`).
- **Upstream issues that are likely the same misunderstanding:**
  - [curl_cffi#720](https://github.com/lexiforest/curl_cffi/issues/720) / [#721](https://github.com/lexiforest/curl_cffi/issues/721) - "Unable to use HTTP/1.1" - Python `r.http_version` returns `2` which IS HTTP/1.1

### 2. Impersonation sets Accept-Encoding but not CURLOPT_ENCODING

- **Severity:** Medium
- **Description:** `curl_easy_impersonate()` injects browser-like `Accept-Encoding: gzip, deflate, br` headers, but does NOT set `CURLOPT_ENCODING` to enable automatic decompression. Responses come back as raw compressed bytes.
- **Impact:** Every caller must remember to also set `CURLOPT_ENCODING ""` or they get garbled response bodies.
- **Expected behavior:** `curl_easy_impersonate()` should either set `CURLOPT_ENCODING` automatically, or not inject Accept-Encoding headers at all.
- **Our fix:** Added `curl_easy_setopt(ch, CURLOPT_ENCODING, "")` after every `curl_easy_impersonate()` call, but only when `default_headers=true` (since `default_headers=false` shouldn't inject any browser headers including Accept-Encoding).
- **Matching issues:**
  - [curl-impersonate#226](https://github.com/lexiforest/curl-impersonate/issues/226) - "`Accept-Encoding: br` is not working for some site" (OPEN) - related symptom
  - [curl-impersonate#203](https://github.com/lexiforest/curl-impersonate/issues/203) - "Inconsistency in Header Injection and Decoding" (CLOSED) - maintainer says "impersonation should only tamper with the request, not the response" - intentional design, but a footgun

---

## Python curl_cffi - lexiforest/curl_cffi

### 1. ~~HTTP version override doesn't work~~ LIKELY NOT A BUG

- See curl-impersonate #1 above. The Python issues ([#720](https://github.com/lexiforest/curl_cffi/issues/720), [#721](https://github.com/lexiforest/curl_cffi/issues/721)) are likely caused by misreading `r.http_version` return value `2` as HTTP/2 when it means HTTP/1.1.

### 2. default_headers=False doesn't fully suppress injected headers

- **Severity:** Low
- **Matching issue:** [curl_cffi#595](https://github.com/lexiforest/curl_cffi/issues/595) - "default_headers=False parameter is ignored for Accept header" (CLOSED, fixed in 0.12.1b1)
- libcurl always adds an Accept header regardless of the flag. Fixed in curl_cffi but worth noting.
