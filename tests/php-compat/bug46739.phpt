--TEST--
Bug #46739 (array returned by curl_cffi_getinfo should contain content_type key)
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();
$ch = curl_cffi_init("{$host}/get.inc");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);

curl_cffi_exec($ch);
$info = curl_cffi_getinfo($ch);

echo (array_key_exists('content_type', $info)) ? "set" : "not set";
?>
--EXPECT--
set
