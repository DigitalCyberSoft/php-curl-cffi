--TEST--
Bug #80121: Null pointer deref if CurlImpersonate\Curl directly instantiated
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

try {
    new CurlImpersonate\Curl;
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}
try {
    new CurlImpersonate\CurlMultiHandle;
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}
try {
    new CurlImpersonate\CurlShareHandle;
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}

?>
--EXPECT--
Cannot directly construct CurlImpersonate\Curl, use curl_cffi_init() instead
Cannot directly construct CurlImpersonate\CurlMultiHandle, use curl_cffi_multi_init() instead
Cannot directly construct CurlImpersonate\CurlShareHandle, use curl_cffi_share_init() instead
