--TEST--
Test curl_cffi_error() & curl_cffi_errno() with problematic protocol
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

$url = 'a' . substr(uniqid(),0,6)."://www.example.com";
$ch = curl_cffi_init();
curl_cffi_setopt($ch, CURLOPT_URL, $url);

curl_cffi_exec($ch);
var_dump(curl_cffi_error($ch));
var_dump(curl_cffi_errno($ch));
curl_cffi_close($ch);

?>
--EXPECTF--
string(%d) "%Srotocol%s"
int(1)
