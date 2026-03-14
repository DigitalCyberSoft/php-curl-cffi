--TEST--
curl_cffi header function callback
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

$headers = [];
$ch = curl_cffi_init($host . '?test=contenttype');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_HEADERFUNCTION, function($ch, $header) use (&$headers) {
    $len = strlen($header);
    $header = trim($header);
    if (strlen($header) > 0 && strpos($header, ':') !== false) {
        list($name, $value) = explode(':', $header, 2);
        $headers[strtolower(trim($name))] = trim($value);
    }
    return $len;
});
curl_cffi_exec($ch);

echo "Has content-type: " . (isset($headers['content-type']) ? "yes" : "no") . "\n";
echo "Content-type: " . $headers['content-type'] . "\n";

curl_cffi_close($ch);
?>
--EXPECT--
Has content-type: yes
Content-type: text/plain;charset=utf-8
