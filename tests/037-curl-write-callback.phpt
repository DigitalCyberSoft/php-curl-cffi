--TEST--
curl_cffi write function callback
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

$data = '';
$ch = curl_cffi_init($host . '?test=get');
curl_cffi_setopt($ch, CURLOPT_WRITEFUNCTION, function($ch, $chunk) use (&$data) {
    $data .= $chunk;
    return strlen($chunk);
});
curl_cffi_exec($ch);
curl_cffi_close($ch);

echo "Callback received: " . (strlen($data) > 0 ? "yes" : "no") . "\n";
echo "Content: " . trim($data) . "\n";
?>
--EXPECT--
Callback received: yes
Content: Hello World!
Hello World!
