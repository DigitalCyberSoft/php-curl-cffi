--TEST--
Test curl_cffi_error() & curl_cffi_errno() function without url
--CREDITS--
TestFest 2009 - AFUP - Perrick Penet <perrick@noparking.net>
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

//In January 2008 , level 7.18.0 of the curl lib, many of the messages changed.
//The final crlf was removed. This test is coded to work with or without the crlf.

$ch = curl_cffi_init();

curl_cffi_exec($ch);
var_dump(curl_cffi_error($ch));
var_dump(curl_cffi_errno($ch));
curl_cffi_close($ch);


?>
--EXPECTF--
string(%d) "No URL set%A"
int(3)
