--TEST--
Test curl_cffi_copy_handle() with CURLOPT_XFERINFOFUNCTION
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

$url = "{$host}/get.inc";
$ch = curl_cffi_init($url);

function foo() {
    static $done = false; if (!$done) { echo "Download progress!\n"; $done = true; }
}

curl_cffi_setopt($ch, CURLOPT_NOPROGRESS, 0);
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
curl_cffi_setopt($ch, CURLOPT_XFERINFOFUNCTION, 'foo');
$ch2 = curl_cffi_copy_handle($ch);
echo curl_cffi_exec($ch), PHP_EOL;
unset($ch);
echo curl_cffi_exec($ch2);

?>
--EXPECT--
Download progress!
Hello World!
Hello World!
Hello World!
Hello World!
