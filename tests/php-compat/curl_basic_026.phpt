--TEST--
Test curl_cffi_getinfo() function with CURLOPT_* from curl >= 7.81.0
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php $curl_cffi_version = curl_cffi_version();
if ($curl_cffi_version['version_number'] < 0x075100) {
    exit("skip: test works only with curl >= 7.81.0");
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
var_dump(curl_cffi_setopt($ch, CURLOPT_MIME_OPTIONS, CURLMIMEOPT_FORMESCAPE));
curl_cffi_exec($ch);
curl_cffi_close($ch);
?>
--EXPECT--
bool(true)
