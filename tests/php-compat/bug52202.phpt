--TEST--
Bug #52202 (CURLOPT_PRIVATE gets clobbered)
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$curl = curl_cffi_init("http://www.google.com");
curl_cffi_setopt($curl, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($curl, CURLOPT_PRIVATE, "123");
curl_cffi_setopt($curl, CURLOPT_CONNECTTIMEOUT, 1);
curl_cffi_setopt($curl, CURLOPT_TIMEOUT, 1);
curl_cffi_exec($curl);

var_dump(curl_cffi_getinfo($curl, CURLINFO_PRIVATE));
?>
--EXPECT--
string(3) "123"
