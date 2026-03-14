--TEST--
Test trampoline for curl option CURLOPT_WRITEFUNCTION
--EXTENSIONS--
curl_impersonate
--FILE--
<?php

class TrampolineTest {
    public function __call(string $name, array $arguments) {
        echo 'Trampoline for ', $name, PHP_EOL;
	    return 0;
    }
}
$o = new TrampolineTest();
$callback = [$o, 'trampoline'];

$log_file = tempnam(sys_get_temp_dir(), 'php-curl-CURLOPT_WRITEFUNCTION-trampoline');

$fp = fopen($log_file, 'w+');
fwrite($fp, "test");
fclose($fp);

$ch = curl_cffi_init();
curl_cffi_setopt($ch, CURLOPT_WRITEFUNCTION, $callback);
curl_cffi_setopt($ch, CURLOPT_URL, 'file://' . $log_file);
curl_cffi_exec($ch);
curl_cffi_close($ch);

?>
--CLEAN--
<?php
$log_file = tempnam(sys_get_temp_dir(), 'php-curl-CURLOPT_WRITEFUNCTION-trampoline');
@unlink($log_file);
?>
--EXPECT--
Trampoline for trampoline
