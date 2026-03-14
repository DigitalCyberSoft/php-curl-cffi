--TEST--
Test curl_cffi POST with parameters
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
  include 'server.inc';
  $host = curl_cli_server_start();

  echo '*** Testing curl sending through GET and POST ***' . "\n";

  $url = "{$host}/get.inc?test=getpost&get_param=Hello%20World";
  $ch = curl_cffi_init();

  ob_start();
  curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
  curl_cffi_setopt($ch, CURLOPT_POST, 1);
  curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, "Hello=World&Foo=Bar&Person=John%20Doe");
  curl_cffi_setopt($ch, CURLOPT_URL, $url);

  $curl_content = curl_cffi_exec($ch);
  curl_cffi_close($ch);

  var_dump( $curl_content );
?>
--EXPECT--
*** Testing curl sending through GET and POST ***
string(208) "array(2) {
  ["test"]=>
  string(7) "getpost"
  ["get_param"]=>
  string(11) "Hello World"
}
array(3) {
  ["Hello"]=>
  string(5) "World"
  ["Foo"]=>
  string(3) "Bar"
  ["Person"]=>
  string(8) "John Doe"
}
"
