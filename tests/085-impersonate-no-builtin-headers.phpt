--TEST--
Impersonate with default_headers=false sends only user-specified headers
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

// With default_headers=false, only our custom headers should appear
$ch = curl_cffi_init($host . '?test=headers_json');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_impersonate($ch, 'chrome120', false);
curl_cffi_setopt($ch, CURLOPT_HTTPHEADER, [
    'X-Custom-One: hello',
    'X-Custom-Two: world',
]);
$result = curl_cffi_exec($ch);
$headers = json_decode($result, true);
curl_cffi_close($ch);

echo "X-Custom-One: " . ($headers['x-custom-one'] ?? 'missing') . "\n";
echo "X-Custom-Two: " . ($headers['x-custom-two'] ?? 'missing') . "\n";
// Browser-injected headers should NOT be present without default headers
echo "Has sec-ch-ua: " . (isset($headers['sec-ch-ua']) ? "yes" : "no") . "\n";
echo "Has accept-encoding: " . (isset($headers['accept-encoding']) ? "yes" : "no") . "\n";
?>
--EXPECT--
X-Custom-One: hello
X-Custom-Two: world
Has sec-ch-ua: no
Has accept-encoding: no
