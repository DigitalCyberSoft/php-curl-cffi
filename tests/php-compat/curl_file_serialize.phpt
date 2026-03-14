--TEST--
CURL file uploading
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$file = new CURLFile(__DIR__ . '/curl_testdata1.txt');
var_dump(serialize($file));
?>
--EXPECTF--
Fatal error: Uncaught Exception: Serialization of 'CURLFile' is not allowed in %s:%d
Stack trace:
#0 %A
%A
  thrown in %s on line %d
