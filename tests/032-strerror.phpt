--TEST--
curl_cffi_strerror returns error descriptions
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
// CURLE_OK = 0
echo curl_cffi_strerror(0) . "\n";
// CURLE_UNSUPPORTED_PROTOCOL = 1
$s = curl_cffi_strerror(1);
echo (stripos($s, 'protocol') !== false ? "Has protocol: yes" : "Has protocol: no") . "\n";
// CURLE_COULDNT_RESOLVE_HOST = 6
$s = curl_cffi_strerror(6);
echo (stripos($s, 'resolve') !== false || stripos($s, 'host') !== false ? "Has resolve: yes" : "Has resolve: no") . "\n";
// CURLE_OPERATION_TIMEDOUT = 28
$s = curl_cffi_strerror(28);
echo (stripos($s, 'timeout') !== false || stripos($s, 'timed') !== false ? "Has timeout: yes" : "Has timeout: no") . "\n";
?>
--EXPECTF--
No error
Has protocol: yes
Has resolve: yes
Has timeout: yes
