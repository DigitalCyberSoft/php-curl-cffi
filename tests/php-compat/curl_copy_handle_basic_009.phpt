--TEST--
Test curl_cffi_copy_handle() with CURLINFO_HEADER_OUT
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
  include 'server.inc';
  $host = curl_cli_server_start();

  $url = "{$host}/get.inc";
  $ch = curl_cffi_init($url);

  curl_cffi_setopt($ch, CURLINFO_HEADER_OUT, 1);
  curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
  $ch2 = curl_cffi_copy_handle($ch);
  echo curl_cffi_exec($ch), PHP_EOL;
  var_dump(strstr(curl_cffi_getinfo($ch, CURLINFO_HEADER_OUT), "\r", true));
  unset($ch);
  echo curl_cffi_exec($ch2), PHP_EOL;
  var_dump(strstr(curl_cffi_getinfo($ch2, CURLINFO_HEADER_OUT), "\r", true));

?>
--EXPECT--
Hello World!
Hello World!
string(21) "GET /get.inc HTTP/1.1"
Hello World!
Hello World!
string(21) "GET /get.inc HTTP/1.1"
