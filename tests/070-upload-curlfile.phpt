--TEST--
File upload with CURLFile
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

// Create a temp file to upload
$tmpfile = tempnam(sys_get_temp_dir(), 'curl_test_');
file_put_contents($tmpfile, 'test file content here');

$ch = curl_cffi_init($host . '?test=file');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, [
    'file' => new CURLFile($tmpfile, 'text/plain', 'upload.txt'),
]);
$result = curl_cffi_exec($ch);

// Result format: name|type|size
$parts = explode('|', $result);
echo "Filename: " . $parts[0] . "\n";
echo "Type: " . $parts[1] . "\n";
echo "Size: " . $parts[2] . "\n";

curl_cffi_close($ch);
unlink($tmpfile);
?>
--EXPECT--
Filename: upload.txt
Type: text/plain
Size: 22
