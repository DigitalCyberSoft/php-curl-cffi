--TEST--
Bug #68089 (NULL byte injection - cURL lib)
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$url = "file:///etc/passwd\0http://google.com";
$ch = curl_cffi_init();

try {
    curl_cffi_setopt($ch, CURLOPT_URL, $url);
} catch (ValueError $exception) {
    echo $exception->getMessage() . "\n";
}

?>
Done
--EXPECT--
curl_cffi_setopt(): Argument #3 ($value) must not contain any null bytes
Done
