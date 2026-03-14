# Migration Guide

Migrating to php-curl-impersonate from PHP's built-in `ext/curl` or from Python's `curl_cffi`/`requests`.

---

## From PHP ext/curl (Procedural)

The procedural API is designed as a near-drop-in replacement. Replace the `curl_` prefix with `curl_cffi_` and add impersonation.

### Function Mapping

| ext/curl | php-curl-impersonate | Notes |
|----------|---------------------|-------|
| `curl_init()` | `curl_cffi_init()` | Returns `CurlImpersonate\Curl` object, not resource |
| `curl_setopt()` | `curl_cffi_setopt()` | Same signature |
| `curl_setopt_array()` | `curl_cffi_setopt_array()` | Same signature |
| `curl_exec()` | `curl_cffi_exec()` | Same behavior |
| `curl_getinfo()` | `curl_cffi_getinfo()` | Same signature, same output format |
| `curl_error()` | `curl_cffi_error()` | Same behavior |
| `curl_errno()` | `curl_cffi_errno()` | Same behavior |
| `curl_close()` | `curl_cffi_close()` | Same behavior |
| `curl_reset()` | `curl_cffi_reset()` | Same behavior |
| `curl_copy_handle()` | `curl_cffi_copy_handle()` | Same behavior |
| `curl_strerror()` | `curl_cffi_strerror()` | Same behavior |
| `curl_escape()` | `curl_cffi_escape()` | Same behavior |
| `curl_unescape()` | `curl_cffi_unescape()` | Same behavior |
| `curl_pause()` | `curl_cffi_pause()` | Same behavior |
| `curl_upkeep()` | `curl_cffi_upkeep()` | Same behavior |
| `curl_version()` | `curl_cffi_version()` | Returns full version array |
| `curl_multi_init()` | `curl_cffi_multi_init()` | Returns `CurlImpersonate\CurlMultiHandle` |
| `curl_multi_add_handle()` | `curl_cffi_multi_add_handle()` | Same signature |
| `curl_multi_remove_handle()` | `curl_cffi_multi_remove_handle()` | Same signature |
| `curl_multi_exec()` | `curl_cffi_multi_exec()` | Same signature |
| `curl_multi_select()` | `curl_cffi_multi_select()` | Same signature |
| `curl_multi_getcontent()` | `curl_cffi_multi_getcontent()` | Same behavior |
| `curl_multi_info_read()` | `curl_cffi_multi_info_read()` | Same behavior |
| `curl_multi_close()` | `curl_cffi_multi_close()` | Same behavior |
| `curl_multi_errno()` | `curl_cffi_multi_errno()` | Same behavior |
| `curl_multi_strerror()` | `curl_cffi_multi_strerror()` | Same behavior |
| `curl_multi_setopt()` | `curl_cffi_multi_setopt()` | Same behavior |
| `curl_share_init()` | `curl_cffi_share_init()` | Returns `CurlImpersonate\CurlShareHandle` |
| `curl_share_close()` | `curl_cffi_share_close()` | Same behavior |
| `curl_share_setopt()` | `curl_cffi_share_setopt()` | Same behavior |
| `curl_share_errno()` | `curl_cffi_share_errno()` | Same behavior |
| `curl_share_strerror()` | `curl_cffi_share_strerror()` | Same behavior |
| *N/A* | `curl_cffi_impersonate()` | **New.** Sets browser impersonation |
| *N/A* | `curl_cffi_file_create()` | Factory for CURLFile objects |

### Quick Migration Example

**Before (ext/curl):**

```php
$ch = curl_init('https://api.example.com/data');
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_setopt($ch, CURLOPT_HTTPHEADER, ['Accept: application/json']);
curl_setopt($ch, CURLOPT_TIMEOUT, 30);

$response = curl_exec($ch);
$httpCode = curl_getinfo($ch, CURLINFO_RESPONSE_CODE);

if ($response === false) {
    echo curl_error($ch);
}

curl_close($ch);
```

**After (php-curl-impersonate):**

```php
$ch = curl_cffi_init('https://api.example.com/data');
curl_cffi_impersonate($ch, 'chrome120');  // Add impersonation
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_HTTPHEADER, ['Accept: application/json']);
curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 30);

$response = curl_cffi_exec($ch);
$httpCode = curl_cffi_getinfo($ch, CURLINFO_RESPONSE_CODE);

if ($response === false) {
    echo curl_cffi_error($ch);
}

curl_cffi_close($ch);
```

### Automated Migration (sed)

For simple codebases:

```bash
# Replace function names
sed -i 's/\bcurl_init\b/curl_cffi_init/g' *.php
sed -i 's/\bcurl_setopt\b/curl_cffi_setopt/g' *.php
sed -i 's/\bcurl_setopt_array\b/curl_cffi_setopt_array/g' *.php
sed -i 's/\bcurl_exec\b/curl_cffi_exec/g' *.php
sed -i 's/\bcurl_getinfo\b/curl_cffi_getinfo/g' *.php
sed -i 's/\bcurl_error\b/curl_cffi_error/g' *.php
sed -i 's/\bcurl_errno\b/curl_cffi_errno/g' *.php
sed -i 's/\bcurl_close\b/curl_cffi_close/g' *.php
sed -i 's/\bcurl_reset\b/curl_cffi_reset/g' *.php
sed -i 's/\bcurl_copy_handle\b/curl_cffi_copy_handle/g' *.php
sed -i 's/\bcurl_strerror\b/curl_cffi_strerror/g' *.php
sed -i 's/\bcurl_escape\b/curl_cffi_escape/g' *.php
sed -i 's/\bcurl_unescape\b/curl_cffi_unescape/g' *.php
sed -i 's/\bcurl_multi_init\b/curl_cffi_multi_init/g' *.php
sed -i 's/\bcurl_multi_add_handle\b/curl_cffi_multi_add_handle/g' *.php
sed -i 's/\bcurl_multi_remove_handle\b/curl_cffi_multi_remove_handle/g' *.php
sed -i 's/\bcurl_multi_exec\b/curl_cffi_multi_exec/g' *.php
sed -i 's/\bcurl_multi_select\b/curl_cffi_multi_select/g' *.php
sed -i 's/\bcurl_multi_getcontent\b/curl_cffi_multi_getcontent/g' *.php
sed -i 's/\bcurl_multi_info_read\b/curl_cffi_multi_info_read/g' *.php
sed -i 's/\bcurl_multi_close\b/curl_cffi_multi_close/g' *.php
sed -i 's/\bcurl_share_init\b/curl_cffi_share_init/g' *.php
sed -i 's/\bcurl_share_close\b/curl_cffi_share_close/g' *.php
sed -i 's/\bcurl_share_setopt\b/curl_cffi_share_setopt/g' *.php
```

**Warning:** Review the results manually. This will not catch dynamic function calls (`call_user_func('curl_init', ...)`) or function references in strings.

### Key Differences from ext/curl

1. **Handle types are objects, not resources.** `curl_cffi_init()` returns a `CurlImpersonate\Curl` object. This means `is_resource()` checks will fail. Use `instanceof CurlImpersonate\Curl` instead.

2. **Constants are shared.** When ext/curl is not loaded, `CURLOPT_*`, `CURLINFO_*`, and `CURLE_*` constants are registered by this extension. When ext/curl IS loaded, the existing constants are reused. No code changes needed.

3. **CURLFile/CURLStringFile coexistence.** If ext/curl is loaded, its `CURLFile` class takes precedence. The extension's version is only registered when ext/curl's isn't present.

4. **No CURLOPT_SAFE_UPLOAD restriction.** The `@filename` syntax for file uploads is never supported. Always use `CURLFile`.

---

## From Guzzle / PSR-18 HTTP Clients

If you're using Guzzle or another PSR-18 client and want browser impersonation, use the Session API.

**Before (Guzzle):**

```php
$client = new \GuzzleHttp\Client([
    'base_uri' => 'https://api.example.com',
    'timeout'  => 30,
    'headers'  => ['Accept' => 'application/json'],
]);

$response = $client->get('/users', [
    'query' => ['page' => 1],
]);

$data = json_decode($response->getBody(), true);
$status = $response->getStatusCode();
```

**After (Session API):**

```php
use CurlImpersonate\Session;

$session = new Session([
    'impersonate' => 'chrome120',
    'base_url'    => 'https://api.example.com',
    'timeout'     => 30,
    'headers'     => ['Accept' => 'application/json'],
]);

$response = $session->get('/users', [
    'params' => ['page' => '1'],
]);

$data = $response->json();
$status = $response->statusCode;
```

### Guzzle to Session Mapping

| Guzzle | Session | Notes |
|--------|---------|-------|
| `new Client(['base_uri' => ...])` | `new Session(['base_url' => ...])` | `base_uri` vs `base_url` |
| `$client->get($url, ['query' => ...])` | `$session->get($url, ['params' => ...])` | `query` vs `params` |
| `$client->post($url, ['json' => ...])` | `$session->post($url, ['json' => ...])` | Same |
| `$client->post($url, ['form_params' => ...])` | `$session->post($url, ['data' => ...])` | `form_params` vs `data` |
| `$client->post($url, ['body' => ...])` | `$session->post($url, ['data' => ...])` | `body` vs `data` (string) |
| `$response->getBody()` | `$response->text()` | |
| `$response->getStatusCode()` | `$response->statusCode` | Method vs property |
| `$response->getHeader('Name')` | `$response->getHeader('Name')` | Same |
| `['verify' => false]` | `['verify' => false]` | Same |
| `['proxy' => '...']` | `['proxy' => '...']` | Same |
| `['auth' => ['user', 'pass']]` | `['auth' => ['user', 'pass']]` | Same |
| `['allow_redirects' => false]` | `['allow_redirects' => false]` | Same |
| `['timeout' => 10]` | `['timeout' => 10]` | Same |
| `['cookies' => $jar]` | `['cookies' => ['name' => 'val']]` | Array instead of CookieJar |

---

## From Python curl_cffi

If you're porting Python code that uses `curl_cffi`, the Session API is designed to be familiar.

**Python (curl_cffi):**

```python
from curl_cffi.requests import Session

with Session(impersonate="chrome120") as session:
    response = session.get("https://example.com", params={"q": "test"})
    print(response.status_code)
    print(response.json())
    print(response.headers)
    print(response.cookies)
```

**PHP (php-curl-impersonate):**

```php
use CurlImpersonate\Session;

$session = new Session(['impersonate' => 'chrome120']);
$response = $session->get('https://example.com', ['params' => ['q' => 'test']]);
echo $response->statusCode;
print_r($response->json());
print_r($response->headers);
print_r($response->cookies);
$session->close();
```

### Python to PHP Differences

| Python curl_cffi | PHP php-curl-impersonate | Notes |
|-----------------|------------------------|-------|
| `Session(impersonate="chrome120")` | `new Session(['impersonate' => 'chrome120'])` | Array instead of kwargs |
| `response.status_code` | `$response->statusCode` | snake_case vs camelCase |
| `response.text` | `$response->text()` | Property vs method |
| `response.json()` | `$response->json()` | Same |
| `response.headers["Name"]` | `$response->headers["Name"]` | Same (but values are arrays) |
| `response.cookies["name"]` | `$response->cookies["name"]` | Same |
| `response.elapsed` | `$response->elapsed` | Python: timedelta, PHP: float seconds |
| `session.delete(url)` | `$session->delete_(url)` | Trailing underscore (PHP reserved word) |
| Context manager (`with`) | Manual `$session->close()` | No auto-close in PHP |
| `response.raise_for_status()` | `$response->raiseForStatus()` | camelCase in PHP |

### Header Format Difference

Python curl_cffi returns headers as single values. PHP returns arrays (to support duplicate headers):

```python
# Python
response.headers["Content-Type"]  # "application/json"
```

```php
// PHP
$response->headers["Content-Type"]  // ["application/json"]
$response->getHeader("Content-Type") // "application/json" (first value)
```

---

## From Python requests

**Python (requests):**

```python
import requests

session = requests.Session()
session.headers.update({"Authorization": "Bearer token"})

response = session.post(
    "https://api.example.com/data",
    json={"key": "value"},
    timeout=30,
)

response.raise_for_status()
data = response.json()
```

**PHP (php-curl-impersonate):**

```php
use CurlImpersonate\Session;

$session = new Session([
    'impersonate' => 'chrome120',
    'headers' => ['Authorization' => 'Bearer token'],
]);

$response = $session->post('https://api.example.com/data', [
    'json'    => ['key' => 'value'],
    'timeout' => 30,
]);

$response->raiseForStatus();
$data = $response->json();
```

---

## Coexistence with ext/curl

Both extensions can be loaded simultaneously. The `curl_cffi_*` prefix prevents any function name conflicts. Use this extension for requests that need browser impersonation and ext/curl for everything else:

```php
// Standard curl for internal APIs (no impersonation needed)
$ch = curl_init('http://internal-api.local/health');
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
$result = curl_exec($ch);
curl_close($ch);

// curl-impersonate for external sites with bot detection
$ch = curl_cffi_init('https://protected-site.com');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_impersonate($ch, 'chrome120');
$result = curl_cffi_exec($ch);
curl_cffi_close($ch);
```

---

## Wrapper Function Pattern

If you want to make migration reversible, create a wrapper:

```php
function http_get(string $url, array $options = []): array {
    $session = new \CurlImpersonate\Session([
        'impersonate' => $options['impersonate'] ?? 'chrome120',
        'timeout'     => $options['timeout'] ?? 30,
        'verify'      => $options['verify'] ?? true,
    ]);

    try {
        $response = $session->get($url, $options);
        return [
            'status'  => $response->statusCode,
            'body'    => $response->text(),
            'headers' => $response->headers,
        ];
    } catch (\CurlImpersonate\CurlException $e) {
        return [
            'status'  => 0,
            'body'    => '',
            'error'   => $e->getMessage(),
            'headers' => [],
        ];
    } finally {
        $session->close();
    }
}
```
