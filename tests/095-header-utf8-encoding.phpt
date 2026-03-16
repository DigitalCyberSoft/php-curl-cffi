--TEST--
Header encoding: UTF-8 and special characters in header values
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
require_once __DIR__ . '/server.inc';
$host = curl_cli_server_start();

// Test 1: UTF-8 characters in header values
$ch = curl_cffi_init("$host/?test=headers_json");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_HTTPHEADER, [
    'X-Name: José García',
    'X-City: München',
    'X-Symbol: ★ Star',
]);
$result = json_decode(curl_cffi_exec($ch), true);
echo "utf8 name: " . ($result['x-name'] === 'José García' ? 'PASS' : 'FAIL: ' . var_export($result['x-name'], true)) . "\n";
echo "utf8 city: " . ($result['x-city'] === 'München' ? 'PASS' : 'FAIL') . "\n";
echo "utf8 symbol: " . ($result['x-symbol'] === '★ Star' ? 'PASS' : 'FAIL') . "\n";
curl_cffi_close($ch);

// Test 2: Header values with colons (should not break parsing)
$ch = curl_cffi_init("$host/?test=headers_json");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_HTTPHEADER, [
    'X-Time: 12:30:45',
    'X-Url: https://example.com:8080/path',
]);
$result = json_decode(curl_cffi_exec($ch), true);
echo "colon time: " . ($result['x-time'] === '12:30:45' ? 'PASS' : 'FAIL: ' . var_export($result['x-time'], true)) . "\n";
echo "colon url: " . ($result['x-url'] === 'https://example.com:8080/path' ? 'PASS' : 'FAIL') . "\n";
curl_cffi_close($ch);

// Test 3: Session API with UTF-8 headers
$session = new CurlImpersonate\Session([
    'headers' => ['X-Default-Lang' => '日本語'],
]);
$resp = $session->get("$host/?test=headers_json");
$result = $resp->json();
echo "session utf8: " . ($result['x-default-lang'] === '日本語' ? 'PASS' : 'FAIL') . "\n";

// Test 4: Per-request header override with UTF-8
$resp = $session->get("$host/?test=headers_json", [
    'headers' => ['X-Request-Lang' => '中文'],
]);
$result = $resp->json();
echo "request utf8: " . ($result['x-request-lang'] === '中文' ? 'PASS' : 'FAIL') . "\n";
?>
--EXPECT--
utf8 name: PASS
utf8 city: PASS
utf8 symbol: PASS
colon time: PASS
colon url: PASS
session utf8: PASS
request utf8: PASS
