--TEST--
curl_cffi_multi_strerror basic test
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

var_dump(strtolower(curl_cffi_multi_strerror(CURLM_OK)));
var_dump(strtolower(curl_cffi_multi_strerror(CURLM_BAD_HANDLE)));

?>
--EXPECT--
string(8) "no error"
string(20) "invalid multi handle"
