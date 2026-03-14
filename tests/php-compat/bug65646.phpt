--TEST--
Bug #65646 (re-enable CURLOPT_FOLLOWLOCATION with open_basedir or safe_mode): open_basedir disabled
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
if (ini_get('open_basedir')) exit("skip open_basedir is set");
?>
--FILE--
<?php
$ch = curl_cffi_init();
var_dump(curl_cffi_setopt($ch, CURLOPT_FOLLOWLOCATION, true));
curl_cffi_close($ch);
?>
--EXPECT--
bool(true)
