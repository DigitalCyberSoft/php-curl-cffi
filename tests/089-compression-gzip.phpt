--TEST--
Gzip compression: response is transparently decompressed
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
require_once __DIR__ . '/server.inc';
$host = curl_cli_server_start();

// Test 1: Procedural API with gzip
$ch = curl_cffi_init("$host/?test=gzip");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_ENCODING, 'gzip');
$result = curl_cffi_exec($ch);
$errno = curl_cffi_errno($ch);
echo "errno: $errno\n";
echo "gzip decoded: " . ($result === 'This is gzip compressed content for testing' ? 'PASS' : 'FAIL: ' . var_export($result, true)) . "\n";
curl_cffi_close($ch);

// Test 2: With CURLOPT_ENCODING="" (accept all)
$ch = curl_cffi_init("$host/?test=gzip");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_ENCODING, '');
$result = curl_cffi_exec($ch);
echo "auto-decode: " . ($result === 'This is gzip compressed content for testing' ? 'PASS' : 'FAIL') . "\n";
curl_cffi_close($ch);

// Test 3: Session API with impersonation (browsers accept gzip by default)
$session = new CurlImpersonate\Session(['impersonate' => 'chrome120']);
$resp = $session->get("$host/?test=gzip");
echo "session gzip: " . ($resp->text() === 'This is gzip compressed content for testing' ? 'PASS' : 'FAIL') . "\n";
?>
--EXPECT--
errno: 0
gzip decoded: PASS
auto-decode: PASS
session gzip: PASS
