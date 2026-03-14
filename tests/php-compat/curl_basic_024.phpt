--TEST--
Test curl_cffi_getinfo() function with CURLINFO_* from curl >= 7.52.0
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

include 'server.inc';

$ch = curl_cffi_init();
$host = curl_cli_server_start();

$url = "{$host}/get.inc?test=";
curl_cffi_setopt($ch, CURLOPT_URL, $url);
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
curl_cffi_exec($ch);
var_dump(CURLPROTO_HTTP === curl_cffi_getinfo($ch, CURLINFO_PROTOCOL));
var_dump(0 === curl_cffi_getinfo($ch, CURLINFO_PROXY_SSL_VERIFYRESULT));
var_dump(curl_cffi_getinfo($ch, CURLINFO_SCHEME));
curl_cffi_close($ch);
?>
--EXPECTF--
bool(true)
bool(true)
string(4) "%r(HTTP|http)%r"
