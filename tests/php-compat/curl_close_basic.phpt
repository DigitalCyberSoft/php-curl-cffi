--TEST--
curl_cffi_close
--CREDITS--
Stefan Koopmanschap <stefan@php.net>
#testfest Utrecht 2009
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$ch = curl_cffi_init();
curl_cffi_close($ch);
var_dump($ch);
?>
--EXPECT--
object(CurlImpersonate\Curl)#1 (0) {
}
