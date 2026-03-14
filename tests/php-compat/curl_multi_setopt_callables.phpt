--TEST--
Test curl_cffi_multi_setopt() with options that take callabes
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

$mh = curl_cffi_multi_init();
try {
    var_dump(curl_cffi_multi_setopt($mh, CURLMOPT_PUSHFUNCTION, 'undefined'));
} catch (Throwable $e) {
    echo $e::class, ': ', $e->getMessage(), PHP_EOL;
}

?>
--EXPECT--
TypeError: curl_cffi_multi_setopt(): Argument #2 ($option) must be a valid callback for option CURLMOPT_PUSHFUNCTION, function "undefined" not found or invalid function name
