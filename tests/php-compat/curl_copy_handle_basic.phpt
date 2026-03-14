--TEST--
Test curl_cffi_copy_handle() function with basic functionality
--CREDITS--
Francesco Fullone ff@ideato.it
#PHPTestFest Cesena Italia on 2009-06-20
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
echo "*** Testing curl_cffi_copy_handle(): basic ***\n";

// create a new cURL resource
$ch = curl_cffi_init();

// set URL and other appropriate options
curl_cffi_setopt($ch, CURLOPT_URL, 'http://www.example.com/');
curl_cffi_setopt($ch, CURLOPT_HEADER, 0);

// copy the handle
$ch2 = curl_cffi_copy_handle($ch);

var_dump(curl_cffi_getinfo($ch) === curl_cffi_getinfo($ch2));
?>
--EXPECT--
*** Testing curl_cffi_copy_handle(): basic ***
bool(true)
