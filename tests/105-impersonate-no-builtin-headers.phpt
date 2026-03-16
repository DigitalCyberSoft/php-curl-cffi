--TEST--
Impersonation: Header suppression with default_headers=false
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
if (!file_exists(__DIR__ . '/ssl/server.key')) die('skip SSL certs not generated');
$out = shell_exec('which nghttpd 2>/dev/null');
if (empty(trim($out ?? ''))) die('skip nghttpd not installed');
?>
--FILE--
<?php
require_once __DIR__ . '/impersonate-server.inc';

$port = 19800;
[$proc, $stderr_file] = start_nghttpd($port);

// Custom headers to send (same as upstream test_no_builtin_headers)
$custom_headers = [
    'X-Hello: World',
    'Accept: application/json',
    'X-Goodbye: World',
    'Accept-Encoding: deflate, gzip, br',
    'X-Foo: Bar',
    'User-Agent: curl-impersonate',
];

// Make request with default_headers=false
$ch = curl_cffi_init("https://127.0.0.1:$port/");
curl_cffi_impersonate($ch, 'chrome120', false);
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_SSL_VERIFYPEER, false);
curl_cffi_setopt($ch, CURLOPT_SSL_VERIFYHOST, 0);
curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 10);
curl_cffi_setopt($ch, CURLOPT_HTTPHEADER, $custom_headers);
curl_cffi_exec($ch);
curl_cffi_close($ch);

$log = read_nghttpd_log($stderr_file);
$parsed = parse_nghttpd_log($log);
stop_server($proc);

// Verify pseudo-headers are present
echo "Pseudo-headers: " . json_encode($parsed['pseudo_headers']) . "\n";

// Verify only custom headers appear (no browser-injected sec-ch-ua, sec-fetch-*, etc.)
$browser_headers = ['sec-ch-ua', 'sec-ch-ua-mobile', 'sec-ch-ua-platform',
                     'sec-fetch-site', 'sec-fetch-mode', 'sec-fetch-user',
                     'sec-fetch-dest', 'upgrade-insecure-requests'];

$found_browser = [];
$actual_names = [];
foreach ($parsed['headers'] as $h) {
    $name = strtolower(trim(explode(':', $h, 2)[0]));
    $actual_names[] = $name;
    if (in_array($name, $browser_headers)) {
        $found_browser[] = $name;
    }
}

if (!empty($found_browser)) {
    echo "FAIL: Browser-injected headers found: " . implode(', ', $found_browser) . "\n";
} else {
    echo "PASS: No browser-injected headers\n";
}

// Verify custom headers appear in order
$expected_names = ['x-hello', 'accept', 'x-goodbye', 'accept-encoding', 'x-foo', 'user-agent'];
if ($actual_names === $expected_names) {
    echo "PASS: Custom headers in correct order\n";
} else {
    echo "FAIL: Header order mismatch\n";
    echo "  Expected: " . json_encode($expected_names) . "\n";
    echo "  Actual:   " . json_encode($actual_names) . "\n";
}

// Verify header values
foreach ($parsed['headers'] as $h) {
    echo "  Header: $h\n";
}
?>
--EXPECTF--
Pseudo-headers: [":method",":authority",":scheme",":path"]
PASS: No browser-injected headers
PASS: Custom headers in correct order
  Header: x-hello: World
  Header: accept: application/json
  Header: x-goodbye: World
  Header: accept-encoding: deflate, gzip, br
  Header: x-foo: Bar
  Header: user-agent: curl-impersonate
