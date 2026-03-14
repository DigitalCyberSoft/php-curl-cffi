--TEST--
Bug #66109 (Option CURLOPT_CUSTOMREQUEST can't be reset to default.)
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();
$ch = curl_cffi_init();
curl_cffi_setopt($ch, CURLOPT_URL, "{$host}/get.inc?test=method");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);

curl_cffi_setopt($ch, CURLOPT_CUSTOMREQUEST, 'DELETE');
var_dump(curl_cffi_exec($ch));

curl_cffi_setopt($ch, CURLOPT_CUSTOMREQUEST, NULL);
var_dump(curl_cffi_exec($ch));

curl_cffi_close($ch);

?>
--EXPECT--
string(6) "DELETE"
string(3) "GET"
