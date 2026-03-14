--TEST--
Test curl_cffi setting referer
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
  include 'server.inc';
  $host = curl_cli_server_start();

  echo '*** Testing curl setting referer ***' . "\n";

  $url = "{$host}/get.inc?test=referer";
  $ch = curl_cffi_init();

  curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
  curl_cffi_setopt($ch, CURLOPT_REFERER, 'http://www.refer.er');
  curl_cffi_setopt($ch, CURLOPT_URL, $url);

  $curl_content = curl_cffi_exec($ch);
  curl_cffi_close($ch);

  var_dump( $curl_content );
?>
--EXPECT--
*** Testing curl setting referer ***
string(19) "http://www.refer.er"
