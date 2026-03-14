--TEST--
curl_cffi basic authentication
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

// Test auth with CURLOPT_USERPWD
$ch = curl_cffi_init($host . '?test=auth');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_USERPWD, 'testuser:testpass');
$result = curl_cffi_exec($ch);
echo "Auth: $result\n";
curl_cffi_close($ch);

// Test without auth - should get 401
$ch = curl_cffi_init($host . '?test=auth');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
$result = curl_cffi_exec($ch);
$code = curl_cffi_getinfo($ch, CURLINFO_RESPONSE_CODE);
echo "No auth status: $code\n";
curl_cffi_close($ch);
?>
--EXPECT--
Auth: user=testuser&pass=testpass
No auth status: 401
