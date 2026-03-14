--TEST--
curl_cffi_error() function - basic test for curl_cffi_error using a fake url
--CREDITS--
Mattijs Hoitink mattijshoitink@gmail.com
#Testfest Utrecht 2009
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
if(getenv("SKIP_ONLINE_TESTS")) die("skip online test");
if(getenv("SKIP_SLOW_TESTS")) die("skip slow test");
$url = "fakeURL";
$ip = gethostbyname($url);
if ($ip != $url) die("skip 'fakeURL' resolves to $ip\n");

?>
--FILE--
<?php
/*
 * Description:   Returns a clear text error message for the last cURL operation.
 * Source:        ext/curl/interface.c
 * Documentation: http://wiki.php.net/qa/temp/ext/curl
 */

// Fake URL to trigger an error
$url = "fakeURL";

echo "== Testing curl_cffi_error with a fake URL ==\n";

// cURL handler
$ch = curl_cffi_init($url);
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, 1);

curl_cffi_exec($ch);
var_dump(curl_cffi_error($ch));
curl_cffi_close($ch);

?>
--EXPECTF--
== Testing curl_cffi_error with a fake URL ==
string(%d) "%sfakeURL%S"
