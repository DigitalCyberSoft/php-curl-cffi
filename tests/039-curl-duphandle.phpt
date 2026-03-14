--TEST--
curl_cffi_copy_handle duplicates handle
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

$ch = curl_cffi_init($host . '?test=get');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);

// Duplicate the handle
$ch2 = curl_cffi_copy_handle($ch);

// Execute both
$result1 = curl_cffi_exec($ch);
$result2 = curl_cffi_exec($ch2);

echo "Original: " . trim($result1) . "\n";
echo "Copy: " . trim($result2) . "\n";
echo "Match: " . ($result1 === $result2 ? "yes" : "no") . "\n";

curl_cffi_close($ch);
curl_cffi_close($ch2);
?>
--EXPECT--
Original: Hello World!
Hello World!
Copy: Hello World!
Hello World!
Match: yes
