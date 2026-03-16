--TEST--
File upload: CURLStringFile with correct parameter order and mixed uploads
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
require_once __DIR__ . '/server.inc';
$host = curl_cli_server_start();

// Test 1: CURLStringFile with correct constructor: (data, postname, mime)
$ch = curl_cffi_init("$host/?test=multi_upload");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, [
    'config' => new CURLStringFile('port=8080', 'config.ini', 'text/plain'),
    'note' => 'String file test',
]);
$result = curl_cffi_exec($ch);
$r = json_decode($result, true);
echo "file name: " . $r['files'][0]['name'] . "\n";
echo "file content: " . $r['files'][0]['content'] . "\n";
echo "note: " . $r['fields']['note'] . "\n";
curl_cffi_close($ch);

// Test 2: Mix CURLFile + CURLStringFile + text in one request
$tmp = tempnam(sys_get_temp_dir(), 'mix_upload_');
file_put_contents($tmp, 'real file data');

$ch = curl_cffi_init("$host/?test=multi_upload");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_POSTFIELDS, [
    'real_file' => new CURLFile($tmp, 'application/octet-stream', 'binary.dat'),
    'virtual_file' => new CURLStringFile('{"ok":true}', 'response.json', 'application/json'),
    'comment' => 'mixed upload test',
]);
$result = curl_cffi_exec($ch);
$r = json_decode($result, true);

echo "file count: " . count($r['files']) . "\n";
$real = $virtual = null;
foreach ($r['files'] as $f) {
    if ($f['field'] === 'real_file') $real = $f;
    if ($f['field'] === 'virtual_file') $virtual = $f;
}
echo "real name: " . $real['name'] . "\n";
echo "real content: " . $real['content'] . "\n";
echo "virtual name: " . $virtual['name'] . "\n";
echo "virtual content: " . $virtual['content'] . "\n";
echo "comment: " . $r['fields']['comment'] . "\n";

curl_cffi_close($ch);
unlink($tmp);
?>
--EXPECT--
file name: config.ini
file content: port=8080
note: String file test
file count: 2
real name: binary.dat
real content: real file data
virtual name: response.json
virtual content: {"ok":true}
comment: mixed upload test
