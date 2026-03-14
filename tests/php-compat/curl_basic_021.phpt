--TEST--
Test curl_cffi_getinfo() function with CURLINFO_CONTENT_TYPE parameter
--CREDITS--
Jean-Marc Fontaine <jmf@durcommefaire.net>
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
  include 'server.inc';
  $host = curl_cli_server_start();
  $url  = "{$host}/get.inc?test=contenttype";

  $ch = curl_cffi_init();
  curl_cffi_setopt($ch, CURLOPT_URL, $url);
  curl_cffi_exec($ch);
  var_dump(curl_cffi_getinfo($ch, CURLINFO_CONTENT_TYPE));
  curl_cffi_close($ch);
?>
--EXPECT--
string(24) "text/plain;charset=utf-8"
