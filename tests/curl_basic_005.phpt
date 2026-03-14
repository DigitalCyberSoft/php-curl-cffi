--TEST--
Test curl_cffi with user agent
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
  include 'server.inc';
  $host = curl_cli_server_start();

  echo '*** Testing curl with user agent ***' . "\n";

  $url = "{$host}/get.inc?test=useragent";
  $ch = curl_cffi_init();

  curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
  curl_cffi_setopt($ch, CURLOPT_USERAGENT, 'cURL phpt');
  curl_cffi_setopt($ch, CURLOPT_URL, $url);

  $curl_content = curl_cffi_exec($ch);
  curl_cffi_close($ch);

  var_dump( $curl_content );
?>
--EXPECT--
*** Testing curl with user agent ***
string(9) "cURL phpt"
