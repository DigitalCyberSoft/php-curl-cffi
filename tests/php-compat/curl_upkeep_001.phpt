--TEST--
curl_cffi_upkeep() function
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
if (getenv("SKIP_ONLINE_TESTS")) die("skip online test");
if (curl_cffi_version()['version_number'] < 0x073e00) die('skip requires curl >= 7.62.0');
?>
--FILE--
<?php

$url = "https://example.com";

$ch = curl_cffi_init();
curl_cffi_setopt($ch,CURLOPT_URL,$url);
curl_cffi_setopt($ch,CURLOPT_RETURNTRANSFER,true);
curl_cffi_setopt($ch,CURLOPT_HTTP_VERSION,CURL_HTTP_VERSION_2_0);
curl_cffi_setopt($ch,CURLOPT_SSL_VERIFYPEER,false);
curl_cffi_setopt($ch, CURLOPT_UPKEEP_INTERVAL_MS, 200);
if (curl_cffi_exec($ch)) {
    usleep(300);
    var_dump(curl_cffi_upkeep($ch));
}
curl_cffi_close($ch);
?>
--EXPECT--
bool(true)
