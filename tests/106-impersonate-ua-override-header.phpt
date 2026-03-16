--TEST--
Impersonation: User-Agent override via CURLOPT_HTTPHEADER
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

$custom_ua = 'My-Custom-User-Agent/1.0';

// Test 1: default_headers=true with UA override via HTTPHEADER
echo "=== Test 1: default_headers=true + HTTPHEADER UA ===\n";
$port = 18810;
[$proc, $stderr_file] = start_nghttpd($port);

$ch = curl_cffi_init("https://127.0.0.1:$port/");
curl_cffi_impersonate($ch, 'chrome120', true);
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_SSL_VERIFYPEER, false);
curl_cffi_setopt($ch, CURLOPT_SSL_VERIFYHOST, 0);
curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 10);
curl_cffi_setopt($ch, CURLOPT_HTTPHEADER, ["User-Agent: $custom_ua"]);
curl_cffi_exec($ch);
curl_cffi_close($ch);

$log = read_nghttpd_log($stderr_file);
$parsed = parse_nghttpd_log($log);
stop_server($proc);

$found_ua = null;
foreach ($parsed['headers'] as $h) {
    if (stripos($h, 'user-agent:') === 0) {
        $found_ua = trim(substr($h, strlen('user-agent:')));
        break;
    }
}

if ($found_ua === $custom_ua) {
    echo "PASS: UA override works with default_headers=true\n";
} else {
    echo "FAIL: Expected '$custom_ua', got '$found_ua'\n";
}

// Test 2: default_headers=false with UA override via HTTPHEADER
echo "=== Test 2: default_headers=false + HTTPHEADER UA ===\n";
$port = 18811;
[$proc, $stderr_file] = start_nghttpd($port);

$ch = curl_cffi_init("https://127.0.0.1:$port/");
curl_cffi_impersonate($ch, 'chrome120', false);
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_SSL_VERIFYPEER, false);
curl_cffi_setopt($ch, CURLOPT_SSL_VERIFYHOST, 0);
curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 10);
curl_cffi_setopt($ch, CURLOPT_HTTPHEADER, ["User-Agent: $custom_ua"]);
curl_cffi_exec($ch);
curl_cffi_close($ch);

$log = read_nghttpd_log($stderr_file);
$parsed = parse_nghttpd_log($log);
stop_server($proc);

$found_ua = null;
foreach ($parsed['headers'] as $h) {
    if (stripos($h, 'user-agent:') === 0) {
        $found_ua = trim(substr($h, strlen('user-agent:')));
        break;
    }
}

if ($found_ua === $custom_ua) {
    echo "PASS: UA override works with default_headers=false\n";
} else {
    echo "FAIL: Expected '$custom_ua', got '$found_ua'\n";
}
?>
--EXPECT--
=== Test 1: default_headers=true + HTTPHEADER UA ===
PASS: UA override works with default_headers=true
=== Test 2: default_headers=false + HTTPHEADER UA ===
PASS: UA override works with default_headers=false
