--TEST--
curl_cffi_reset resets handle to defaults
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

$ch = curl_cffi_init($host . '?test=get');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_USERAGENT, 'CustomAgent/1.0');

// Execute first request
$result = curl_cffi_exec($ch);
echo "First request: " . (strlen($result) > 0 ? "OK" : "FAIL") . "\n";

// Reset handle - clears all options
curl_cffi_reset($ch);

// After reset, must set URL again
curl_cffi_setopt($ch, CURLOPT_URL, $host . '?test=useragent');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
$result = curl_cffi_exec($ch);

// After reset, custom useragent should be gone
echo "After reset UA is not Custom: " . (strpos($result, 'CustomAgent') === false ? "yes" : "no") . "\n";

curl_cffi_close($ch);
?>
--EXPECT--
First request: OK
After reset UA is not Custom: yes
