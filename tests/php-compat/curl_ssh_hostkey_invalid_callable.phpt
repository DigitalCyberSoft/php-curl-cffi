--TEST--
Test curl_cffi_setopt(_array)() with CURLOPT_SSH_HOSTKEYFUNCTION option
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
$curl_cffi_version = curl_cffi_version();
if ($curl_cffi_version['version_number'] < 0x075400) {
    die("skip: blob options not supported for curl < 7.84.0");
}
?>
--FILE--
<?php

function testOption(CurlImpersonate\Curl $handle, int $option) {
    try {
        var_dump(curl_cffi_setopt($handle, $option, 'undefined'));
    } catch (Throwable $e) {
        echo $e::class, ': ', $e->getMessage(), PHP_EOL;
    }

    try {
        var_dump(curl_cffi_setopt_array($handle, [$option => 'undefined']));
    } catch (Throwable $e) {
        echo $e::class, ': ', $e->getMessage(), PHP_EOL;
    }
}

$url = "https://example.com";
$ch = curl_cffi_init($url);
testOption($ch, CURLOPT_SSH_HOSTKEYFUNCTION);

?>
--EXPECT--
TypeError: curl_cffi_setopt(): Argument #3 ($value) must be a valid callback for option CURLOPT_SSH_HOSTKEYFUNCTION, function "undefined" not found or invalid function name
TypeError: curl_cffi_setopt_array(): Argument #2 ($options) must be a valid callback for option CURLOPT_SSH_HOSTKEYFUNCTION, function "undefined" not found or invalid function name
