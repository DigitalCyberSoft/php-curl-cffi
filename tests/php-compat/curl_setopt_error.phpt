--TEST--
curl_cffi_setopt() basic parameter test
--CREDITS--
Paul Sohier
#phptestfest utrecht
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
echo "*** curl_cffi_setopt() call with incorrect parameters\n";
$ch = curl_cffi_init();

try {
    curl_cffi_setopt($ch, '', false);
} catch (TypeError $e) {
    echo $e->getMessage(), "\n";
}

try {
    curl_cffi_setopt($ch, -10, 0);
} catch (ValueError $e) {
    echo $e->getMessage(), "\n";
}

try {
    curl_cffi_setopt($ch, 1000, 0);
} catch (ValueError $e) {
    echo $e->getMessage(), "\n";
}

?>
--EXPECT--
*** curl_cffi_setopt() call with incorrect parameters
curl_cffi_setopt(): Argument #2 ($option) must be of type int, string given
curl_cffi_setopt(): Argument #2 ($option) is not a valid cURL option
curl_cffi_setopt(): Argument #2 ($option) is not a valid cURL option
