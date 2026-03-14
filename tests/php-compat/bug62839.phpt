--TEST--
Bug #62839 (curl_cffi_copy_handle segfault with CURLOPT_FILE)
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$curl = curl_cffi_init();

$fd = tmpfile();
curl_cffi_setopt($curl, CURLOPT_FILE, $fd);

curl_cffi_copy_handle($curl);

echo 'DONE!';
?>
--EXPECT--
DONE!
