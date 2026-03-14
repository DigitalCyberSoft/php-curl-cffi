--TEST--
Bug #72202 (curl_cffi_close doesn't close cURL handle)
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$a = fopen(__FILE__, "r");
$b = $a;
var_dump($a, $b);
fclose($a);
var_dump($a, $b);
unset($a, $b);

$a = curl_cffi_init();
$b = $a;
var_dump($a, $b);
curl_cffi_close($a);
var_dump($a, $b);
unset($a, $b);
?>
--EXPECTF--
resource(%d) of type (stream)
resource(%d) of type (stream)
resource(%d) of type (Unknown)
resource(%d) of type (Unknown)
object(CurlImpersonate\Curl)#1 (0) {
}
object(CurlImpersonate\Curl)#1 (0) {
}
object(CurlImpersonate\Curl)#1 (0) {
}
object(CurlImpersonate\Curl)#1 (0) {
}
