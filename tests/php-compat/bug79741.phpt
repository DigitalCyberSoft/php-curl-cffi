--TEST--
Bug #79741: curl_cffi_setopt CURLOPT_POSTFIELDS asserts on object with declared properties
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

class Test {
    public $prop = "value";
}

$ch = curl_cffi_init();
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, new Test);

?>
===DONE===
--EXPECTF--
Fatal error: Uncaught Error: Object of class Test could not be converted to string in %s:%d
Stack trace:
#0 %s(%d): curl_cffi_setopt(Object(CurlImpersonate\Curl), %d, Object(Test))
#1 {main}
  thrown in %s on line %d
