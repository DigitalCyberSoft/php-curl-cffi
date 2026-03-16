--TEST--
File upload: multiple files with different field names and mixed form data
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
require_once __DIR__ . '/server.inc';
$host = curl_cli_server_start();

// Create temp files
$tmp1 = tempnam(sys_get_temp_dir(), 'upload1_');
$tmp2 = tempnam(sys_get_temp_dir(), 'upload2_');
file_put_contents($tmp1, 'content of file one');
file_put_contents($tmp2, 'content of file two');

// Test 1: Multiple files with different field names + text fields
$ch = curl_cffi_init("$host/?test=multi_upload");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_POST, true);
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, [
    'doc' => new CURLFile($tmp1, 'text/plain', 'document.txt'),
    'photo' => new CURLFile($tmp2, 'image/jpeg', 'photo.jpg'),
    'title' => 'My Upload',
    'tags' => 'test,upload',
]);
$result = curl_cffi_exec($ch);
$r = json_decode($result, true);

// Check files
$files = $r['files'];
echo "File count: " . count($files) . "\n";

$doc = null;
$photo = null;
foreach ($files as $f) {
    if ($f['field'] === 'doc') $doc = $f;
    if ($f['field'] === 'photo') $photo = $f;
}

echo "doc name: " . $doc['name'] . "\n";
echo "doc type: " . $doc['type'] . "\n";
echo "doc content: " . $doc['content'] . "\n";
echo "photo name: " . $photo['name'] . "\n";
echo "photo type: " . $photo['type'] . "\n";
echo "photo content: " . $photo['content'] . "\n";

// Check text fields
echo "title: " . $r['fields']['title'] . "\n";
echo "tags: " . $r['fields']['tags'] . "\n";
curl_cffi_close($ch);

// Test 2: CURLStringFile uploads
$ch = curl_cffi_init("$host/?test=multi_upload");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_POST, true);
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, [
    'data' => new CURLStringFile('{"key":"value"}', 'data.json', 'application/json'),
    'csv' => new CURLStringFile("a,b,c\n1,2,3", 'export.csv', 'text/csv'),
    'description' => 'String file test',
]);
$result = curl_cffi_exec($ch);
$r = json_decode($result, true);

echo "string files: " . count($r['files']) . "\n";
$data_file = null;
$csv_file = null;
foreach ($r['files'] as $f) {
    if ($f['field'] === 'data') $data_file = $f;
    if ($f['field'] === 'csv') $csv_file = $f;
}
echo "json name: " . $data_file['name'] . "\n";
echo "json content: " . $data_file['content'] . "\n";
echo "csv name: " . $csv_file['name'] . "\n";
echo "description: " . $r['fields']['description'] . "\n";

curl_cffi_close($ch);
unlink($tmp1);
unlink($tmp2);
?>
--EXPECT--
File count: 2
doc name: document.txt
doc type: text/plain
doc content: content of file one
photo name: photo.jpg
photo type: image/jpeg
photo content: content of file two
title: My Upload
tags: test,upload
string files: 2
json name: data.json
json content: {"key":"value"}
csv name: export.csv
description: String file test
