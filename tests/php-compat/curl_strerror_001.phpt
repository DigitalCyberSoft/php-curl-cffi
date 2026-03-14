--TEST--
curl_cffi_strerror basic test
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

var_dump(strtolower(curl_cffi_strerror(CURLE_OK)));
var_dump(strtolower(curl_cffi_strerror(CURLE_UNSUPPORTED_PROTOCOL)));
var_dump(strtolower(curl_cffi_strerror(-1)));

?>
--EXPECT--
string(8) "no error"
string(20) "unsupported protocol"
string(13) "unknown error"
