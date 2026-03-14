--TEST--
Test curl_cffi_setopt() with CURLOPT_ACCEPT_ENCODING
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

include 'server.inc';
$host = curl_cli_server_start();

$ch = curl_cffi_init();

$url = "{$host}/get.inc?test=";
curl_cffi_setopt($ch, CURLOPT_URL, $url);
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
curl_cffi_setopt($ch, CURLOPT_ACCEPT_ENCODING, "gzip");
curl_cffi_setopt($ch, CURLINFO_HEADER_OUT, 1);

// First execution, with gzip accept
curl_cffi_exec($ch);
echo curl_cffi_getinfo($ch, CURLINFO_HEADER_OUT);

// Second execution, with the encoding accept disabled
curl_cffi_setopt($ch, CURLOPT_ACCEPT_ENCODING, NULL);
curl_cffi_exec($ch);
echo curl_cffi_getinfo($ch, CURLINFO_HEADER_OUT);

curl_cffi_close($ch);
?>
--EXPECTF--
GET /get.inc?test= HTTP/1.1
Host: %s
%AAccept-Encoding: gzip

GET /get.inc?test= HTTP/1.1
Host: %s
