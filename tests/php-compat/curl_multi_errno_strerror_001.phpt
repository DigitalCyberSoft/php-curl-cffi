--TEST--
curl_cffi_multi_errno and curl_cffi_multi_strerror basic test
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

$mh = curl_cffi_multi_init();
$errno = curl_cffi_multi_errno($mh);
echo $errno . PHP_EOL;
echo curl_cffi_multi_strerror($errno) . PHP_EOL;

try {
    curl_cffi_multi_setopt($mh, -1, -1);
} catch (ValueError $exception) {
    echo $exception->getMessage() . "\n";
}

$errno = curl_cffi_multi_errno($mh);
echo $errno . PHP_EOL;
echo curl_cffi_multi_strerror($errno) . PHP_EOL;
?>
--EXPECT--
0
No error
curl_cffi_multi_setopt(): Argument #2 ($option) is not a valid cURL multi option
6
Unknown option
