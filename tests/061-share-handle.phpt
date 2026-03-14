--TEST--
curl_cffi share handle
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

$sh = curl_cffi_share_init();
curl_cffi_share_setopt($sh, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);

$ch1 = curl_cffi_init($host . '?test=get');
curl_cffi_setopt($ch1, CURLOPT_SHARE, $sh);
curl_cffi_setopt($ch1, CURLOPT_RETURNTRANSFER, true);
$result1 = curl_cffi_exec($ch1);
echo "Request 1: " . (strlen($result1) > 0 ? "OK" : "FAIL") . "\n";

$ch2 = curl_cffi_init($host . '?test=method');
curl_cffi_setopt($ch2, CURLOPT_SHARE, $sh);
curl_cffi_setopt($ch2, CURLOPT_RETURNTRANSFER, true);
$result2 = curl_cffi_exec($ch2);
echo "Request 2: " . trim($result2) . "\n";

curl_cffi_close($ch1);
curl_cffi_close($ch2);
curl_cffi_share_close($sh);
echo "Cleanup: OK\n";
?>
--EXPECT--
Request 1: OK
Request 2: GET
Cleanup: OK
