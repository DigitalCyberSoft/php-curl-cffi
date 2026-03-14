--TEST--
clone() allows to post CURLFile multiple times
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

$ch1 = curl_cffi_init();
curl_cffi_setopt($ch1, CURLOPT_SAFE_UPLOAD, 1);
curl_cffi_setopt($ch1, CURLOPT_URL, "{$host}/get.php?test=file");
curl_cffi_setopt($ch1, CURLOPT_RETURNTRANSFER, 1);

$filename = __DIR__ . '/curl_copy_handle_variation3_clone.txt';
file_put_contents($filename, "Test.");
$file = curl_cffi_file_create($filename);
$params = array('file' => $file);
var_dump(curl_cffi_setopt($ch1, CURLOPT_POSTFIELDS, $params));

$ch2 = clone($ch1);

var_dump(curl_cffi_exec($ch1));

var_dump(curl_cffi_exec($ch2));
?>
--EXPECTF--
bool(true)
string(%d) "curl_copy_handle_variation3_clone.txt|application/octet-stream|5"
string(%d) "curl_copy_handle_variation3_clone.txt|application/octet-stream|5"
--CLEAN--
<?php
@unlink(__DIR__ . '/curl_copy_handle_variation3_clone.txt');
?>
