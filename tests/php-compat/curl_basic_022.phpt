--TEST--
Test curl_cffi_getinfo() function with CURLINFO_COOKIELIST parameter
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

$ch = curl_cffi_init();
curl_cffi_setopt($ch, CURLOPT_COOKIELIST, 'Set-Cookie: C1=v1; expires=Thu, 31-Dec-2037 23:59:59 GMT; path=/; domain=.php.net');
curl_cffi_setopt($ch, CURLOPT_COOKIELIST, 'Set-Cookie: C2=v2; expires=Thu, 31-Dec-2037 23:59:59 GMT; path=/; domain=.php.net');
var_dump(curl_cffi_getinfo($ch, CURLINFO_COOKIELIST));

?>
--EXPECTF--
array(2) {
  [0]=>
  string(38) ".php.net	TRUE	/	FALSE	%d	C1	v1"
  [1]=>
  string(38) ".php.net	TRUE	/	FALSE	%d	C2	v2"
}
