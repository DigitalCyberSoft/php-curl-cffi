--TEST--
Test curl_cffi_getinfo() function with CURLINFO_HTTP_VERSION parameter
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

include 'server.inc';

$ch = curl_cffi_init();
var_dump(0 === curl_cffi_getinfo($ch, CURLINFO_HTTP_VERSION));

$host = curl_cli_server_start();

$url = "{$host}/get.inc?test=";
curl_cffi_setopt($ch, CURLOPT_URL, $url);
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
curl_cffi_exec($ch);
var_dump(CURL_HTTP_VERSION_1_1 === curl_cffi_getinfo($ch, CURLINFO_HTTP_VERSION));
curl_cffi_close($ch);
?>
--EXPECT--
bool(true)
bool(true)
