--TEST--
Test curl_cffi_multi_init()
--CREDITS--
Mark van der Velden
#testfest Utrecht 2009
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

// start testing
echo "*** Testing curl_cffi_multi_init(void); ***\n";

//create the multiple cURL handle
$mh = curl_cffi_multi_init();
var_dump($mh);

curl_cffi_multi_close($mh);
var_dump($mh);
?>
--EXPECT--
*** Testing curl_cffi_multi_init(void); ***
object(CurlImpersonate\CurlMultiHandle)#1 (0) {
}
object(CurlImpersonate\CurlMultiHandle)#1 (0) {
}
