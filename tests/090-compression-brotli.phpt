--TEST--
Brotli compression: response is transparently decompressed
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
if (!function_exists('brotli_compress')) die('skip brotli PHP extension not available');
?>
--FILE--
<?php
require_once __DIR__ . '/server.inc';
$host = curl_cli_server_start();

// Test 1: Procedural API with brotli
$ch = curl_cffi_init("$host/?test=brotli");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_ENCODING, 'br');
$result = curl_cffi_exec($ch);
$errno = curl_cffi_errno($ch);
echo "errno: $errno\n";
echo "brotli decoded: " . ($result === 'This is brotli compressed content for testing' ? 'PASS' : 'FAIL: ' . var_export($result, true)) . "\n";
curl_cffi_close($ch);

// Test 2: With CURLOPT_ENCODING="" (accept all including br)
$ch = curl_cffi_init("$host/?test=brotli");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_ENCODING, '');
$result = curl_cffi_exec($ch);
echo "auto-decode: " . ($result === 'This is brotli compressed content for testing' ? 'PASS' : 'FAIL') . "\n";
curl_cffi_close($ch);

// Test 3: Session with impersonation (chrome accepts br)
$session = new CurlImpersonate\Session(['impersonate' => 'chrome120']);
$resp = $session->get("$host/?test=brotli");
echo "session brotli: " . ($resp->text() === 'This is brotli compressed content for testing' ? 'PASS' : 'FAIL') . "\n";
?>
--EXPECT--
errno: 0
brotli decoded: PASS
auto-decode: PASS
session brotli: PASS
