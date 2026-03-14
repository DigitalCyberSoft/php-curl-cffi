--TEST--
Test curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1)
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
  include 'server.inc';
  $host = curl_cli_server_start();

  echo '*** Testing curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1); ***' . "\n";

  $url = "{$host}/get.inc?test=get";
  $ch = curl_cffi_init();

  ob_start();
  curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
  curl_cffi_setopt($ch, CURLOPT_URL, $url);

  $curl_content = curl_cffi_exec($ch);
  curl_cffi_close($ch);

  var_dump( $curl_content );
?>
--EXPECT--
*** Testing curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1); ***
string(25) "Hello World!
Hello World!"
