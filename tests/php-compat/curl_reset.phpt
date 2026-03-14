--TEST--
Test curl_cffi_reset
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

$test_file = tempnam(sys_get_temp_dir(), 'php-curl-test');
$log_file = tempnam(sys_get_temp_dir(), 'php-curl-test');

$fp = fopen($log_file, 'w+');
fwrite($fp, "test");
fclose($fp);

$testfile_fp = fopen($test_file, 'w+');

$ch = curl_cffi_init();
curl_cffi_setopt($ch, CURLOPT_FILE, $testfile_fp);
curl_cffi_setopt($ch, CURLOPT_URL, 'file://' . $log_file);
curl_cffi_exec($ch);

curl_cffi_reset($ch);
curl_cffi_setopt($ch, CURLOPT_URL, 'file://' . $log_file);
curl_cffi_exec($ch);

curl_cffi_close($ch);

fclose($testfile_fp);

echo file_get_contents($test_file);

// cleanup
unlink($test_file);
unlink($log_file);
?>
--EXPECT--
testtest
