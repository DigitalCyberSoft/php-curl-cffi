--TEST--
curl_cffi_multi_close return false when supplied resource not valid cURL multi handle
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$cmh = curl_cffi_multi_init();
var_dump($cmh);
$multi_close_result = curl_cffi_multi_close($cmh);
var_dump($multi_close_result);
var_dump($cmh);
curl_cffi_multi_close($cmh);
?>
--EXPECT--
object(CurlImpersonate\CurlMultiHandle)#1 (0) {
}
NULL
object(CurlImpersonate\CurlMultiHandle)#1 (0) {
}
