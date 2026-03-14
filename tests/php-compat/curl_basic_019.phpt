--TEST--
Test curl_cffi_getinfo() function with CURLINFO_EFFECTIVE_URL parameter
--CREDITS--
Jean-Marc Fontaine <jmf@durcommefaire.net>
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
  include 'server.inc';
  $host = curl_cli_server_start();

  $url = "http://{$host}/get.inc?test=";
  $ch  = curl_cffi_init();

  curl_cffi_setopt($ch, CURLOPT_URL, $url);
  curl_cffi_exec($ch);
  $info = curl_cffi_getinfo($ch, CURLINFO_EFFECTIVE_URL);
  var_dump($url == $info);
  curl_cffi_close($ch);
?>
--EXPECT--
Hello World!
Hello World!bool(true)
