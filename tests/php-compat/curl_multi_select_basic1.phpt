--TEST--
Test curl_cffi_multi_select()
--CREDITS--
Ivo Jansch <ivo@ibuildings.com>
#testfest Utrecht 2009
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

//create the multiple cURL handle
$mh = curl_cffi_multi_init();
echo curl_cffi_multi_select($mh)."\n";

curl_cffi_multi_close($mh);
?>
--EXPECTF--
%r(0|-1)%r
