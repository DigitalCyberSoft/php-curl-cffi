--TEST--
Bug #45161 (Reusing a curl handle leaks memory)
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

// Fill memory for test
$ch = curl_cffi_init();
$fp = fopen(PHP_OS_FAMILY === 'Windows' ? 'nul' : '/dev/null', 'w');

/*
$i = $start = $end = 100000.00;
for ($i = 0; $i < 100; $i++) {
    curl_cffi_setopt($ch, CURLOPT_URL, 'http://127.0.0.1:9/');
    curl_cffi_setopt($ch, CURLOPT_FILE, $fp);
    curl_cffi_exec($ch);
}
*/

// Start actual test
$start = memory_get_usage() + 1024;
for($i = 0; $i < 1024; $i++) {
    curl_cffi_setopt($ch, CURLOPT_URL, "{$host}/get.inc");
    curl_cffi_setopt($ch, CURLOPT_FILE, $fp);
    curl_cffi_exec($ch);
}
if ($start < memory_get_usage()) {
    echo 'FAIL';
} else {
    echo 'PASS';
}
echo "\n";
fclose($fp);
unset($fp);
?>
--EXPECT--
PASS
