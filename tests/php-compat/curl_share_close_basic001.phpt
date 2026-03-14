--TEST--
curl_cffi_share_close basic test
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

$sh = curl_cffi_share_init();
//Show that there's a curl_share object
var_dump($sh);

curl_cffi_share_close($sh);
var_dump($sh);

?>
--EXPECT--
object(CurlImpersonate\CurlShareHandle)#1 (0) {
}
object(CurlImpersonate\CurlShareHandle)#1 (0) {
}
