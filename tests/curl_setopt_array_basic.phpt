--TEST--
curl_cffi_setopt_array() basic test
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

$url = "{$host}/get.inc?test=get";

echo '== Starting test curl_cffi_setopt_array($ch, $options); ==' . "\n";

$ch = curl_cffi_init();

$options = array (
    CURLOPT_URL => $url,
    CURLOPT_RETURNTRANSFER => 1
);

ob_start();

curl_cffi_setopt_array($ch, $options);
$returnContent = curl_cffi_exec($ch);
curl_cffi_close($ch);

var_dump($returnContent);
?>
--EXPECT--
== Starting test curl_cffi_setopt_array($ch, $options); ==
string(25) "Hello World!
Hello World!"
