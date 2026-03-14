--TEST--
curl_cffi_multi_setopt basic test
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

$mh = curl_cffi_multi_init();
var_dump(curl_cffi_multi_setopt($mh, CURLMOPT_PIPELINING, 0));

try {
    curl_cffi_multi_setopt($mh, -1, 0);
} catch (ValueError $exception) {
    echo $exception->getMessage() . "\n";
}

?>
--EXPECT--
bool(true)
curl_cffi_multi_setopt(): Argument #2 ($option) is not a valid cURL multi option
