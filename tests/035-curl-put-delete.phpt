--TEST--
curl_cffi PUT and DELETE methods
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

// PUT
$ch = curl_cffi_init($host . '?test=method');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_CUSTOMREQUEST, 'PUT');
$result = curl_cffi_exec($ch);
echo "PUT: $result\n";
curl_cffi_close($ch);

// DELETE
$ch = curl_cffi_init($host . '?test=method');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_CUSTOMREQUEST, 'DELETE');
$result = curl_cffi_exec($ch);
echo "DELETE: $result\n";
curl_cffi_close($ch);

// HEAD (should return empty body)
$ch = curl_cffi_init($host . '?test=get');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_NOBODY, true);
$result = curl_cffi_exec($ch);
echo "HEAD body empty: " . (empty($result) ? "yes" : "no") . "\n";
$code = curl_cffi_getinfo($ch, CURLINFO_RESPONSE_CODE);
echo "HEAD status: $code\n";
curl_cffi_close($ch);
?>
--EXPECT--
PUT: PUT
DELETE: DELETE
HEAD body empty: yes
HEAD status: 200
