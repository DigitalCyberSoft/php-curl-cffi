--TEST--
Test that cloning of Curl objects is supported
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

include 'server.inc';
$host = curl_cli_server_start();

$ch1 = curl_cffi_init();
curl_cffi_setopt($ch1, CURLOPT_URL, $host);
curl_cffi_setopt($ch1, CURLOPT_RETURNTRANSFER, 1);
curl_cffi_exec($ch1);

$ch2 = clone $ch1;
curl_cffi_setopt($ch2, CURLOPT_RETURNTRANSFER, 0);

var_dump(curl_cffi_getinfo($ch1, CURLINFO_EFFECTIVE_URL) === curl_cffi_getinfo($ch2, CURLINFO_EFFECTIVE_URL));
curl_cffi_exec($ch2);

?>
--EXPECT--
bool(true)
Hello World!
Hello World!
