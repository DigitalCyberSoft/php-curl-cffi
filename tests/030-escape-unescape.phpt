--TEST--
curl_cffi_escape and curl_cffi_unescape
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$ch = curl_cffi_init();
$encoded = curl_cffi_escape($ch, 'hello world & more');
echo "Encoded: " . $encoded . "\n";
$decoded = curl_cffi_unescape($ch, $encoded);
echo "Decoded: " . $decoded . "\n";
echo ($decoded === 'hello world & more') ? "Match: OK\n" : "Match: FAIL\n";
curl_cffi_close($ch);
?>
--EXPECT--
Encoded: hello%20world%20%26%20more
Decoded: hello world & more
Match: OK
