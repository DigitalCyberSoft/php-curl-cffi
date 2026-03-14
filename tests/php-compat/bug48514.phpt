--TEST--
Bug #48514 (cURL extension uses same resource name for simple and multi APIs)
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

$ch1 = curl_cffi_init();
var_dump($ch1);
var_dump($ch1::class);

$ch2 = curl_cffi_multi_init();
var_dump($ch2);
var_dump($ch2::class);

?>
--EXPECT--
object(CurlImpersonate\Curl)#1 (0) {
}
string(20) "CurlImpersonate\Curl"
object(CurlImpersonate\CurlMultiHandle)#2 (0) {
}
string(31) "CurlImpersonate\CurlMultiHandle"
