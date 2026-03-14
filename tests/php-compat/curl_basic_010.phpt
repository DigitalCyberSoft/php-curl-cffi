--TEST--
Test curl_cffi_error() & curl_cffi_errno() function with problematic proxy
--CREDITS--
TestFest 2009 - AFUP - Perrick Penet <perrick@noparking.net>
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
    $addr = "www.".uniqid().".".uniqid();
    if (gethostbyname($addr) != $addr) {
        print "skip catch all dns";
    }
?>
--FILE--
<?php

$url = "http://www.example.org";
$ch = curl_cffi_init();
curl_cffi_setopt($ch, CURLOPT_PROXY, uniqid().":".uniqid());
curl_cffi_setopt($ch, CURLOPT_URL, $url);

curl_cffi_exec($ch);
var_dump(curl_cffi_error($ch));
var_dump(curl_cffi_errno($ch));
curl_cffi_close($ch);


?>
--EXPECTF--
string(%d) "%r(Couldn't resolve proxy|Could not resolve proxy:|Could not resolve host:|Could not resolve:|Unsupported proxy syntax in)%r %s"
int(5)
