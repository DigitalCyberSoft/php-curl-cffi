--TEST--
GH-16802 (open_basedir bypass using curl extension)
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
$curl_cffi_version = curl_cffi_version();
if ($curl_cffi_version['version_number'] < 0x075500) {
    die("skip: blob options not supported for curl < 7.85.0");
}
?>
--INI--
open_basedir=/nowhere
--FILE--
<?php
$ch = curl_cffi_init("file:///etc/passwd");
curl_cffi_setopt($ch, CURLOPT_PROTOCOLS_STR, "all");
curl_cffi_setopt($ch, CURLOPT_PROTOCOLS_STR, "ftp,all");
curl_cffi_setopt($ch, CURLOPT_PROTOCOLS_STR, "all,ftp");
curl_cffi_setopt($ch, CURLOPT_PROTOCOLS_STR, "all,file,ftp");
var_dump(curl_cffi_exec($ch));
?>
--EXPECTF--
Warning: curl_cffi_setopt(): The FILE protocol cannot be activated when an open_basedir is set in %s on line %d

Warning: curl_cffi_setopt(): The FILE protocol cannot be activated when an open_basedir is set in %s on line %d

Warning: curl_cffi_setopt(): The FILE protocol cannot be activated when an open_basedir is set in %s on line %d

Warning: curl_cffi_setopt(): The FILE protocol cannot be activated when an open_basedir is set in %s on line %d
bool(false)
