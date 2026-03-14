--TEST--
GH-15547 - curl_cffi_multi_select overflow on timeout argument
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

$mh = curl_cffi_multi_init();

try {
	curl_cffi_multi_select($mh, -2500000);
} catch (\ValueError $e) {
	echo $e->getMessage() . PHP_EOL;
}
curl_cffi_multi_close($mh);
$mh = curl_cffi_multi_init();
try {
	curl_cffi_multi_select($mh, 2500000);
} catch (\ValueError $e) {
	echo $e->getMessage() . PHP_EOL;
}
curl_cffi_multi_close($mh);
$mh = curl_cffi_multi_init();
var_dump(curl_cffi_multi_select($mh, 1000000));
?>
--EXPECTF--
curl_cffi_multi_select(): Argument #2 ($timeout) must be between %s
curl_cffi_multi_select(): Argument #2 ($timeout) must be between %s
int(0)
