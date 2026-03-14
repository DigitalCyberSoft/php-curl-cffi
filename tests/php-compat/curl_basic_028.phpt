--TEST--
Test curl_cffi_getinfo() function with CURLOPT_* from curl >= 7.85.0
--INI--
open_basedir=.
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php $curl_cffi_version = curl_cffi_version();
if ($curl_cffi_version['version_number'] < 0x075500) {
    exit("skip: test works only with curl >= 7.85.0");
}
?>
--FILE--
<?php
include 'server.inc';

$ch = curl_cffi_init();

$host = curl_cli_server_start();

$url = "{$host}/get.inc?test=";
curl_cffi_setopt($ch, CURLOPT_URL, $url);
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
var_dump(curl_cffi_setopt($ch, CURLOPT_PROTOCOLS_STR, "FilE,DICT"));
var_dump(curl_cffi_setopt($ch, CURLOPT_PROTOCOLS_STR, "DICT"));
var_dump(curl_cffi_setopt($ch, CURLOPT_REDIR_PROTOCOLS_STR, "HTTP"));
curl_cffi_exec($ch);
curl_cffi_close($ch);
?>
--EXPECTF--
Warning: curl_cffi_setopt(): The FILE protocol cannot be activated when an open_basedir is set in %s on line %d
bool(false)
bool(true)
bool(true)
