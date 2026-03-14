--TEST--
Test curl_cffi_getinfo() function with CURLINFO_* from curl >= 7.72.0
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php $curl_cffi_version = curl_cffi_version();
if ($curl_cffi_version['version_number'] < 0x074800) {
        exit("skip: test works only with curl >= 7.72.0");
}
?>
--FILE--
<?php

include 'server.inc';

$ch = curl_cffi_init();
$host = curl_cli_server_start();

$url = "{$host}/get.inc?test=";
curl_cffi_setopt($ch, CURLOPT_URL, $url);
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, "data");
curl_cffi_exec($ch);
var_dump(curl_cffi_getinfo($ch, CURLINFO_EFFECTIVE_METHOD));
curl_cffi_close($ch);
?>
--EXPECT--
string(4) "POST"
