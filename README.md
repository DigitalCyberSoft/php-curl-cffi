# php-curl-impersonate

A PHP extension wrapping [libcurl-impersonate](https://github.com/lwthiker/curl-impersonate) to enable browser TLS and HTTP/2 fingerprint impersonation from PHP.

When making HTTP requests, many anti-bot systems inspect the TLS Client Hello and HTTP/2 settings to identify the client. Standard PHP curl requests are easily identified as non-browser traffic. This extension solves that by impersonating real browser fingerprints (Chrome, Firefox, Safari, etc.).

## Requirements

- PHP 8.0 or later
- libcurl development headers (`libcurl-devel` / `libcurl4-openssl-dev`)
- `libcurl-impersonate.a` static library (see [Building libcurl-impersonate](#building-libcurl-impersonate))
- Standard build tools: `gcc`, `make`, `autoconf`, `phpize`

## Installation

### 1. Build libcurl-impersonate

Follow the [curl-impersonate build instructions](https://github.com/lwthiker/curl-impersonate) to produce `libcurl-impersonate.a`. Place it in `/usr/local/lib/` or note the path for step 3.

### 2. Build the extension

```bash
cd php-curl-impersonate
phpize
./configure --with-curl-impersonate=/path/to/libcurl-impersonate.a
make
sudo make install
```

If `libcurl-impersonate.a` is at the default location (`/usr/local/lib/libcurl-impersonate.a`), you can omit the path:

```bash
./configure --with-curl-impersonate
```

### 3. Enable the extension

Add to your `php.ini`:

```ini
extension=curl_impersonate.so
```

### 4. Verify

```bash
php -m | grep curl_impersonate
php -r "var_dump(curl_cffi_version());"
```

## API Overview

The extension provides two API levels:

| API | Style | Best for |
|-----|-------|----------|
| **Session API** (OOP) | High-level, requests-like | Most use cases |
| **Procedural API** | Low-level, mirrors PHP's `curl_*` | Drop-in migration, fine-grained control |

Both APIs support browser impersonation via `curl_easy_impersonate()` from libcurl-impersonate.

---

## Session API (Recommended)

The `CurlImpersonate\Session` class provides a high-level HTTP client similar to Python's `requests` library.

### Basic Usage

```php
use CurlImpersonate\Session;

$session = new Session([
    'impersonate' => 'chrome120',
]);

$response = $session->get('https://httpbin.org/get');

echo $response->statusCode;  // 200
echo $response->text();       // Response body as string
print_r($response->json());   // Decoded JSON
print_r($response->headers);  // ['Content-Type' => ['application/json'], ...]
print_r($response->cookies);  // ['session_id' => 'abc123', ...]
echo $response->elapsed;      // Request time in seconds
echo $response->reason;       // "OK"
```

### Session Constructor Options

```php
$session = new Session([
    'impersonate'      => 'chrome120',      // Browser to impersonate
    'base_url'         => 'https://api.example.com',
    'headers'          => ['Authorization' => 'Bearer token123'],
    'cookies'          => ['session' => 'abc'],
    'params'           => ['api_key' => 'xyz'],  // Default query params
    'timeout'          => 30,               // Seconds
    'max_redirects'    => 10,
    'allow_redirects'  => true,
    'verify'           => true,             // SSL verification
    'raise_for_status' => false,            // Auto-throw on 4xx/5xx
    'proxy'            => 'http://proxy:8080',
]);
```

### HTTP Methods

```php
$response = $session->get($url, $options);
$response = $session->post($url, $options);
$response = $session->put($url, $options);
$response = $session->patch($url, $options);
$response = $session->delete_($url, $options);
$response = $session->head($url, $options);
$response = $session->options($url, $options);
```

### Per-Request Options

```php
// POST JSON
$response = $session->post('https://api.example.com/data', [
    'json' => ['key' => 'value'],
]);

// POST form data
$response = $session->post('https://example.com/login', [
    'data' => ['username' => 'user', 'password' => 'pass'],
]);

// POST raw string body
$response = $session->post('https://example.com/api', [
    'data' => '<xml>payload</xml>',
    'headers' => ['Content-Type' => 'application/xml'],
]);

// Query parameters
$response = $session->get('https://api.example.com/search', [
    'params' => ['q' => 'php curl', 'page' => '1'],
]);

// Authentication
$response = $session->get('https://api.example.com/protected', [
    'auth' => ['username', 'password'],
]);

// Override session defaults per-request
$response = $session->get('https://example.com', [
    'timeout'          => 5,
    'allow_redirects'  => false,
    'verify'           => false,
    'impersonate'      => 'firefox117',
    'cookies'          => ['extra_cookie' => 'val'],
    'headers'          => ['X-Custom' => 'value'],
    'proxy'            => 'socks5://localhost:9050',
    'referer'          => 'https://google.com',
    'raise_for_status' => true,
]);
```

### Cookie Persistence

Cookies persist across requests within a session:

```php
$session = new Session(['impersonate' => 'chrome120']);

// First request sets cookies via Set-Cookie headers
$session->get('https://example.com/login');

// Subsequent requests automatically include those cookies
$session->get('https://example.com/dashboard');

// Access current cookies
print_r($session->cookies);
```

### Response Object

```php
$response = $session->get('https://httpbin.org/get');

$response->statusCode;    // int: HTTP status code
$response->url;           // string: Final URL (after redirects)
$response->content;       // string: Raw response body
$response->headers;       // array: ['Name' => ['value1', 'value2']]
$response->cookies;       // array: ['name' => 'value']
$response->elapsed;       // float: Total time in seconds
$response->reason;        // string: HTTP reason phrase ("OK", "Not Found")
$response->redirectCount; // int: Number of redirects followed

$response->text();             // string: Response body
$response->json();             // mixed: JSON-decoded body (assoc array)
$response->json(false);        // mixed: JSON-decoded body (stdClass objects)
$response->getHeader('Name');  // string|null: First value of header
$response->raiseForStatus();   // throws CurlException if status >= 400
```

### Error Handling

```php
use CurlImpersonate\CurlException;

try {
    $response = $session->get('https://example.com/api');
    $response->raiseForStatus();
} catch (CurlException $e) {
    echo $e->getMessage();  // "HTTP Error: 404" or "curl: (28) Operation timed out"
    echo $e->getCode();     // HTTP status code or curl error code

    // For redirect errors (CURLE_TOO_MANY_REDIRECTS), the partial response is attached
    if ($e->response) {
        echo $e->response->statusCode;
        echo $e->response->url;
    }
}
```

### Base URL Support

```php
$session = new Session([
    'impersonate' => 'chrome120',
    'base_url'    => 'https://api.example.com/v2',
]);

// Absolute path: https://api.example.com/users
$session->get('/users');

// Relative path: https://api.example.com/v2/users
$session->get('users');

// Full URL overrides base: https://other.com/endpoint
$session->get('https://other.com/endpoint');
```

---

## Procedural API

The procedural API mirrors PHP's built-in `curl_*` functions using the `curl_cffi_*` prefix. This is useful for drop-in migration from existing curl code.

### Basic Usage

```php
$ch = curl_cffi_init('https://httpbin.org/get');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_impersonate($ch, 'chrome120');

$response = curl_cffi_exec($ch);
$info = curl_cffi_getinfo($ch);

echo $response;
echo $info['http_code'];

curl_cffi_close($ch);
```

### Setting Options

```php
$ch = curl_cffi_init();

// Individual options
curl_cffi_setopt($ch, CURLOPT_URL, 'https://example.com');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_FOLLOWLOCATION, true);
curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 30);

// Bulk options
curl_cffi_setopt_array($ch, [
    CURLOPT_URL            => 'https://example.com',
    CURLOPT_RETURNTRANSFER => true,
    CURLOPT_HTTPHEADER     => ['Accept: application/json'],
    CURLOPT_SSL_VERIFYPEER => true,
]);
```

### POST Requests

```php
$ch = curl_cffi_init('https://httpbin.org/post');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);

// String body
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, 'key=value&key2=value2');

// Multipart form with file upload
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, [
    'field1' => 'value1',
    'file'   => new CURLFile('/path/to/file.pdf', 'application/pdf', 'upload.pdf'),
    'data'   => new CURLStringFile('raw content', 'text/plain', 'data.txt'),
]);

$result = curl_cffi_exec($ch);
```

### Callbacks

```php
$ch = curl_cffi_init('https://example.com/large-file');

// Write callback
curl_cffi_setopt($ch, CURLOPT_WRITEFUNCTION, function($ch, $data) {
    file_put_contents('/tmp/download', $data, FILE_APPEND);
    return strlen($data);
});

// Header callback
curl_cffi_setopt($ch, CURLOPT_HEADERFUNCTION, function($ch, $header) {
    echo "Header: $header";
    return strlen($header);
});

// Progress callback
curl_cffi_setopt($ch, CURLOPT_NOPROGRESS, false);
curl_cffi_setopt($ch, CURLOPT_PROGRESSFUNCTION, function($ch, $dlTotal, $dlNow, $ulTotal, $ulNow) {
    if ($dlTotal > 0) {
        printf("\rDownload: %.1f%%", ($dlNow / $dlTotal) * 100);
    }
    return 0; // Return non-zero to abort
});

// Read callback (for uploads)
curl_cffi_setopt($ch, CURLOPT_READFUNCTION, function($ch, $stream, $length) {
    return fread($stream, $length);
});

curl_cffi_exec($ch);
```

### Error Handling

```php
$ch = curl_cffi_init('https://nonexistent.example.com');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);

$result = curl_cffi_exec($ch);

if ($result === false) {
    echo curl_cffi_error($ch);   // Human-readable error
    echo curl_cffi_errno($ch);   // Error code (CURLE_*)
    echo curl_cffi_strerror(curl_cffi_errno($ch));  // Error code to string
}

curl_cffi_close($ch);
```

### Multi Handle (Parallel Requests)

```php
$mh = curl_cffi_multi_init();

$handles = [];
$urls = [
    'https://httpbin.org/get?n=1',
    'https://httpbin.org/get?n=2',
    'https://httpbin.org/get?n=3',
];

foreach ($urls as $url) {
    $ch = curl_cffi_init($url);
    curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_cffi_impersonate($ch, 'chrome120');
    curl_cffi_multi_add_handle($mh, $ch);
    $handles[] = $ch;
}

// Execute all requests in parallel
do {
    $status = curl_cffi_multi_exec($mh, $active);
    if ($active) {
        curl_cffi_multi_select($mh);
    }
} while ($active && $status === CURLM_OK);

// Collect results
while ($info = curl_cffi_multi_info_read($mh)) {
    if ($info['msg'] === CURLMSG_DONE) {
        $content = curl_cffi_multi_getcontent($info['handle']);
        echo $content . "\n";
    }
}

// Cleanup
foreach ($handles as $ch) {
    curl_cffi_multi_remove_handle($mh, $ch);
    curl_cffi_close($ch);
}
curl_cffi_multi_close($mh);
```

### Share Handle (Shared Cookies/DNS)

```php
$sh = curl_cffi_share_init();
curl_cffi_share_setopt($sh, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
curl_cffi_share_setopt($sh, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);

$ch1 = curl_cffi_init('https://example.com');
curl_cffi_setopt($ch1, CURLOPT_SHARE, $sh);
curl_cffi_setopt($ch1, CURLOPT_RETURNTRANSFER, true);
curl_cffi_exec($ch1);

$ch2 = curl_cffi_init('https://example.com/page2');
curl_cffi_setopt($ch2, CURLOPT_SHARE, $sh);  // Shares cookies and DNS cache
curl_cffi_setopt($ch2, CURLOPT_RETURNTRANSFER, true);
curl_cffi_exec($ch2);

curl_cffi_close($ch1);
curl_cffi_close($ch2);
curl_cffi_share_close($sh);
```

### Misc Functions

```php
// URL encoding/decoding
$ch = curl_cffi_init();
$encoded = curl_cffi_escape($ch, 'hello world & more');   // "hello%20world%20%26%20more"
$decoded = curl_cffi_unescape($ch, 'hello%20world');       // "hello world"

// Copy a handle
$ch2 = curl_cffi_copy_handle($ch);

// Reset handle to defaults
curl_cffi_reset($ch);

// Connection upkeep (for long-lived connections)
curl_cffi_upkeep($ch);

// Pause/resume transfers
curl_cffi_pause($ch, CURLPAUSE_ALL);
curl_cffi_pause($ch, CURLPAUSE_CONT);

// Get version info
$version = curl_cffi_version();
echo $version['version'];       // "7.84.0-DEV"
echo $version['ssl_version'];   // "BoringSSL"
print_r($version['protocols']); // ['http', 'https', ...]
```

---

## OOP Low-Level API

The `CurlImpersonate\Curl` class provides OOP access to the low-level curl handle.

```php
use CurlImpersonate\Curl;
use CurlImpersonate\CurlOpt;
use CurlImpersonate\CurlInfo;

$curl = new Curl();
$curl->setOpt(CurlOpt::URL, 'https://httpbin.org/get');
$curl->impersonate('chrome120');
$curl->perform();

echo $curl->getBody();
echo $curl->getResponseHeaders();
echo $curl->getInfo(CurlInfo::RESPONSE_CODE);

// Duplicate handle
$curl2 = $curl->dupHandle();

// Static method
echo Curl::version();

$curl->close();
```

---

## Impersonation Targets

The following browser targets are available (depends on your libcurl-impersonate build):

| Target | Browser |
|--------|---------|
| `chrome99` | Chrome 99 |
| `chrome100` | Chrome 100 |
| `chrome101` | Chrome 101 |
| `chrome104` | Chrome 104 |
| `chrome107` | Chrome 107 |
| `chrome110` | Chrome 110 |
| `chrome116` | Chrome 116 |
| `chrome119` | Chrome 119 |
| `chrome120` | Chrome 120 |
| `chrome123` | Chrome 123 |
| `chrome124` | Chrome 124 |
| `chrome126` | Chrome 126 |
| `chrome127` | Chrome 127 |
| `chrome131` | Chrome 131 |
| `edge99` | Edge 99 |
| `edge101` | Edge 101 |
| `firefox91esr` | Firefox 91 ESR |
| `firefox95` | Firefox 95 |
| `firefox98` | Firefox 98 |
| `firefox100` | Firefox 100 |
| `firefox102` | Firefox 102 |
| `firefox109` | Firefox 109 |
| `firefox117` | Firefox 117 |
| `safari15_3` | Safari 15.3 |
| `safari15_5` | Safari 15.5 |
| `safari17_0` | Safari 17.0 |
| `safari17_2_ios` | Safari 17.2 (iOS) |
| `safari18_0` | Safari 18.0 |
| `safari18_0_ios` | Safari 18.0 (iOS) |

The exact list depends on your libcurl-impersonate version. Pass an invalid target to get an error with the full list.

---

## Constants

### CurlOpt (Option Constants)

Available as `CurlImpersonate\CurlOpt::*` and as global `CURLOPT_*` constants (when ext/curl is not loaded).

**Curl-impersonate specific options:**

| Constant | Value | Description |
|----------|-------|-------------|
| `IMPERSONATE` | 999 | Set browser impersonation target |
| `SSL_SIG_HASH_ALGS` | 1001 | TLS signature hash algorithms |
| `SSL_ENABLE_ALPS` | 1002 | Enable ALPS TLS extension |
| `SSL_CERT_COMPRESSION` | 1003 | Certificate compression algorithms |
| `SSL_ENABLE_TICKET` | 1004 | Enable TLS session tickets |
| `HTTP2_PSEUDO_HEADERS_ORDER` | 1005 | HTTP/2 pseudo-header ordering |
| `HTTP2_SETTINGS` | 1006 | HTTP/2 SETTINGS frame values |
| `SSL_PERMUTE_EXTENSIONS` | 1007 | Permute TLS extensions order |
| `HTTP2_WINDOW_UPDATE` | 1008 | HTTP/2 window update value |
| `TLS_GREASE` | 1011 | Enable TLS GREASE |

### CurlInfo (Info Constants)

Available as `CurlImpersonate\CurlInfo::*` and as global `CURLINFO_*` constants.

---

## Compatibility with ext/curl

This extension can coexist with PHP's built-in `ext/curl`:

- All procedural functions use the `curl_cffi_*` prefix to avoid naming conflicts
- Global constants are only registered if ext/curl hasn't already registered them
- Classes live in the `CurlImpersonate\` namespace
- `CURLFile` and `CURLStringFile` classes are registered in the global namespace (will not conflict if ext/curl provides them first)

---

## Running Tests

```bash
# Start the test responder server
php -S 127.0.0.1:8399 -t tests/responder &

# Run the test suite
php run-tests.php -d extension=modules/curl_impersonate.so tests/
```

---

## License

This extension wraps libcurl-impersonate. See the respective project for licensing terms.
