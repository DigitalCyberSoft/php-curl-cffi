--TEST--
Bug #46711 (lost memory when foreach is used for values passed to curl_cffi_setopt())
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$ch = curl_cffi_init();

$opt = array(
    CURLOPT_AUTOREFERER  => TRUE,
    CURLOPT_BINARYTRANSFER => TRUE
);

curl_cffi_setopt( $ch, CURLOPT_AUTOREFERER  , TRUE );

foreach( $opt as $option => $value ) {
    curl_cffi_setopt( $ch, $option, $value );
}

var_dump($opt); // with this bug, $opt[58] becomes NULL

?>
--EXPECTF--
Deprecated: Constant CURLOPT_BINARYTRANSFER is deprecated in %s on line %d
array(2) {
  [58]=>
  bool(true)
  [19914]=>
  bool(true)
}
