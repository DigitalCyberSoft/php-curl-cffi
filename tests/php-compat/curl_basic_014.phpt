--TEST--
Test curl_cffi_init() function with basic functionality
--CREDITS--
Jean-Marc Fontaine <jmf@durcommefaire.net>
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
  $ch = curl_cffi_init();
  var_dump($ch);
?>
--EXPECT--
object(CurlImpersonate\Curl)#1 (0) {
}
