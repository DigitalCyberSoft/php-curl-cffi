--TEST--
CURLOPT_PRIVATE
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$curl = curl_cffi_init("foobar");
$obj = new stdClass;
curl_cffi_setopt($curl, CURLOPT_PRIVATE, $obj);
var_dump($obj === curl_cffi_getinfo($curl, CURLINFO_PRIVATE));

$curl2 = curl_cffi_copy_handle($curl);
var_dump($obj === curl_cffi_getinfo($curl2, CURLINFO_PRIVATE));
$obj2 = new stdClass;
curl_cffi_setopt($curl2, CURLOPT_PRIVATE, $obj2);
var_dump($obj === curl_cffi_getinfo($curl, CURLINFO_PRIVATE));
var_dump($obj2 === curl_cffi_getinfo($curl2, CURLINFO_PRIVATE));
?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
