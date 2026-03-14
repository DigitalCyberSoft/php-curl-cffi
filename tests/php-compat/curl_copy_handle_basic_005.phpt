--TEST--
Test curl_cffi_copy_handle() after exec() with POST
--CREDITS--
Rick Buitenman <rick@meritos.nl>
#testfest Utrecht 2009
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

  include 'server.inc';
  $host = curl_cli_server_start();

  echo '*** Test curl_cffi_copy_handle() after exec() with POST ***' . "\n";

  $url = "{$host}/get.inc?test=getpost";
  $ch = curl_cffi_init();

  ob_start(); // start output buffering
  curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
  curl_cffi_setopt($ch, CURLOPT_POST, 1);
  curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, "Hello=World&Foo=Bar&Person=John%20Doe");
  curl_cffi_setopt($ch, CURLOPT_URL, $url); //set the url we want to use


  $curl_content = curl_cffi_exec($ch);
  $copy = curl_cffi_copy_handle($ch);
  curl_cffi_close($ch);

  $curl_content_copy = curl_cffi_exec($copy);
  curl_cffi_close($copy);

  var_dump( $curl_content_copy );
?>
--EXPECT--
*** Test curl_cffi_copy_handle() after exec() with POST ***
string(163) "array(1) {
  ["test"]=>
  string(7) "getpost"
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
