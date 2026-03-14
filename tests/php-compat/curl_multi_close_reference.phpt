--TEST--
curl_cffi_multi_close closed by cleanup functions
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$mh = curl_cffi_multi_init();
$array = array($mh);
$array[] = &$array;

curl_cffi_multi_add_handle($mh, curl_cffi_init());
curl_cffi_multi_add_handle($mh, curl_cffi_init());
curl_cffi_multi_add_handle($mh, curl_cffi_init());
curl_cffi_multi_add_handle($mh, curl_cffi_init());
echo "okey";
?>
--EXPECT--
okey
