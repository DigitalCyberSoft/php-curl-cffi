--TEST--
File upload with CURLStringFile
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

$ch = curl_cffi_init($host . '?test=string_file');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, [
    'file' => new CURLStringFile('raw content data', 'data.txt', 'text/plain'),
]);
$result = curl_cffi_exec($ch);

// Result format: name|type|md5
$parts = explode('|', $result);
echo "Filename: " . $parts[0] . "\n";
echo "Type: " . $parts[1] . "\n";
echo "Has MD5: " . (strlen($parts[2]) === 32 ? "yes" : "no") . "\n";
echo "MD5 matches: " . ($parts[2] === md5('raw content data') ? "yes" : "no") . "\n";

curl_cffi_close($ch);
?>
--EXPECT--
Filename: data.txt
Type: text/plain
Has MD5: yes
MD5 matches: yes
