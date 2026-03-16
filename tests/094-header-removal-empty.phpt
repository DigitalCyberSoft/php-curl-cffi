--TEST--
Header removal: suppress default headers by setting empty value
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
require_once __DIR__ . '/server.inc';
$host = curl_cli_server_start();

// Test 1: Remove Accept header by setting it to empty
$ch = curl_cffi_init("$host/?test=headers_json");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_HTTPHEADER, [
    'Accept:',  // Empty value = suppress header
    'X-Custom: present',
]);
$result = json_decode(curl_cffi_exec($ch), true);
echo "accept suppressed: " . (!isset($result['accept']) ? 'PASS' : 'FAIL: ' . $result['accept']) . "\n";
echo "custom present: " . ($result['x-custom'] === 'present' ? 'PASS' : 'FAIL') . "\n";
curl_cffi_close($ch);

// Test 2: Remove Host header (curl should still send it)
$ch = curl_cffi_init("$host/?test=headers_json");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_HTTPHEADER, [
    'X-Test: value',
]);
$result = json_decode(curl_cffi_exec($ch), true);
echo "host present: " . (isset($result['host']) ? 'PASS' : 'FAIL') . "\n";
echo "x-test: " . $result['x-test'] . "\n";
curl_cffi_close($ch);

// Test 3: Multiple custom headers
$ch = curl_cffi_init("$host/?test=headers_json");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_HTTPHEADER, [
    'X-First: one',
    'X-Second: two',
    'X-Third: three',
]);
$result = json_decode(curl_cffi_exec($ch), true);
echo "first: " . $result['x-first'] . "\n";
echo "second: " . $result['x-second'] . "\n";
echo "third: " . $result['x-third'] . "\n";
curl_cffi_close($ch);
?>
--EXPECT--
accept suppressed: PASS
custom present: PASS
host present: PASS
x-test: value
first: one
second: two
third: three
