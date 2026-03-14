--TEST--
Bug #27023 (CURLOPT_POSTFIELDS does not parse content types for files)
--INI--
error_reporting = E_ALL & ~E_DEPRECATED
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

include 'server.inc';
$host = curl_cli_server_start();
$ch = curl_cffi_init();
curl_cffi_setopt($ch, CURLOPT_SAFE_UPLOAD, 1);
curl_cffi_setopt($ch, CURLOPT_URL, "{$host}/get.inc?test=file");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);

$file = curl_cffi_file_create(__DIR__ . '/curl_testdata1.txt');
$params = array('file' => $file);
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, $params);
var_dump(curl_cffi_exec($ch));

$file = curl_cffi_file_create(__DIR__ . '/curl_testdata1.txt', "text/plain");
$params = array('file' => $file);
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, $params);
var_dump(curl_cffi_exec($ch));

$file = curl_cffi_file_create(__DIR__ . '/curl_testdata1.txt', null, "foo.txt");
$params = array('file' => $file);
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, $params);
var_dump(curl_cffi_exec($ch));

$file = curl_cffi_file_create(__DIR__ . '/curl_testdata1.txt', "text/plain", "foo.txt");
$params = array('file' => $file);
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, $params);
var_dump(curl_cffi_exec($ch));


curl_cffi_close($ch);
?>
--EXPECTF--
string(%d) "curl_testdata1.txt|application/octet-stream|6"
string(%d) "curl_testdata1.txt|text/plain|6"
string(%d) "foo.txt|application/octet-stream|6"
string(%d) "foo.txt|text/plain|6"
