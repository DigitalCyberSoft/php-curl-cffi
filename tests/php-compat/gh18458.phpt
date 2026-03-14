--TEST--
GH-18458 (authorization header is set despite CURLOPT_USERPWD set to null)
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
include 'skipif-nocaddy.inc';
?>
--FILE--
<?php

$ch = curl_cffi_init("https://localhost/userpwd");
curl_cffi_setopt($ch, CURLOPT_USERPWD, null);
curl_cffi_setopt($ch, CURLOPT_VERBOSE, true);
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_STDERR, fopen("php://stdout", "w"));
$response = curl_cffi_exec($ch);
var_dump(str_contains($response, "authorization"));

$ch = curl_cffi_init("https://localhost/username");
curl_cffi_setopt($ch, CURLOPT_USERNAME, null);
curl_cffi_setopt($ch, CURLOPT_PASSWORD, null);
curl_cffi_setopt($ch, CURLOPT_VERBOSE, true);
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_STDERR, fopen("php://stdout", "w"));
$response = curl_cffi_exec($ch);
var_dump(str_contains($response, "authorization"));
?>
--EXPECTF--
%A
bool(false)
%A
bool(false)
