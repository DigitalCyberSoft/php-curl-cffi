--TEST--
URL encoding edge cases: special chars, non-ASCII, percent-encoded paths
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
require_once __DIR__ . '/server.inc';
$host = curl_cli_server_start();

$ch = curl_cffi_init();
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);

// Test 1: Query params with special characters
curl_cffi_setopt($ch, CURLOPT_URL, "$host/?test=params_json&q=" . urlencode('hello world&foo=bar'));
$r = json_decode(curl_cffi_exec($ch), true);
echo "Special chars: " . ($r['q'] === 'hello world&foo=bar' ? 'PASS' : 'FAIL: ' . var_export($r['q'], true)) . "\n";

// Test 2: URL with percent-encoded path
curl_cffi_setopt($ch, CURLOPT_URL, "$host/path%20with%20spaces?test=echo_all");
$r = json_decode(curl_cffi_exec($ch), true);
echo "Percent path: " . (str_contains($r['uri'], 'path%20with%20spaces') ? 'PASS' : 'FAIL: ' . $r['uri']) . "\n";

// Test 3: Query params with unicode (URL-encoded)
$encoded = urlencode('café résumé');
curl_cffi_setopt($ch, CURLOPT_URL, "$host/?test=params_json&text=$encoded");
$r = json_decode(curl_cffi_exec($ch), true);
echo "Unicode: " . ($r['text'] === 'café résumé' ? 'PASS' : 'FAIL: ' . var_export($r['text'], true)) . "\n";

// Test 4: Empty query parameter value
curl_cffi_setopt($ch, CURLOPT_URL, "$host/?test=params_json&empty=&novalue");
$r = json_decode(curl_cffi_exec($ch), true);
echo "Empty param: " . (array_key_exists('empty', $r) && $r['empty'] === '' ? 'PASS' : 'FAIL') . "\n";

// Test 5: Session with params containing special chars
$session = new CurlImpersonate\Session();
$resp = $session->get("$host/?test=params_json", [
    'params' => ['search' => 'a+b=c&d', 'arr[]' => 'val'],
]);
$r = $resp->json();
echo "Session params: " . ($r['search'] === 'a+b=c&d' ? 'PASS' : 'FAIL: ' . var_export($r['search'], true)) . "\n";

// Test 6: POST with URL-encoded form data containing special chars
curl_cffi_setopt($ch, CURLOPT_URL, "$host/?test=json_echo");
curl_cffi_setopt($ch, CURLOPT_POST, true);
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, 'key=' . urlencode('value with spaces & symbols <>"'));
$r = json_decode(curl_cffi_exec($ch), true);
$body = $r['body'];
parse_str($body, $parsed);
echo "POST encoded: " . ($parsed['key'] === 'value with spaces & symbols <>"' ? 'PASS' : 'FAIL') . "\n";

curl_cffi_close($ch);
?>
--EXPECT--
Special chars: PASS
Percent path: PASS
Unicode: PASS
Empty param: PASS
Session params: PASS
POST encoded: PASS
