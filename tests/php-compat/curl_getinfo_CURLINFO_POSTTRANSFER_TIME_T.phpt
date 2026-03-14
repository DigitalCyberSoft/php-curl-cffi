--TEST--
Curlinfo CURLINFO_POSTTRANSFER_TIME_T
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
$curl_cffi_version = curl_cffi_version();
if ($curl_cffi_version['version_number'] < 0x080a00) die("skip: test works only with curl >= 8.10.0");
// Check if our extension actually supports posttransfer_time_us
$ch = curl_cffi_init();
$info = curl_cffi_getinfo($ch);
if (!isset($info['posttransfer_time_us'])) die("skip: posttransfer_time_us not available in this build");
?>
--FILE--
<?php
include 'server.inc';

$host = curl_cli_server_start();
$port = (int) (explode(':', $host))[1];

$ch = curl_cffi_init();
curl_cffi_setopt($ch, CURLOPT_URL, "{$host}/get.inc?test=file");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);

$info = curl_cffi_getinfo($ch);
var_dump(isset($info['posttransfer_time_us']));
var_dump($info['posttransfer_time_us'] === 0); // this is always 0 before executing the transfer

$result = curl_cffi_exec($ch);

$info = curl_cffi_getinfo($ch);
var_dump(isset($info['posttransfer_time_us']));
var_dump(is_int($info['posttransfer_time_us']));
var_dump(curl_cffi_getinfo($ch, CURLINFO_POSTTRANSFER_TIME_T) === $info['posttransfer_time_us']);
var_dump(curl_cffi_getinfo($ch, CURLINFO_POSTTRANSFER_TIME_T) > 0);

?>
--EXPECT--
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)

