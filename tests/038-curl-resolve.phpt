--TEST--
curl_cffi CURLOPT_RESOLVE
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

// Extract port from host URL
preg_match('/localhost:(\d+)/', $host, $matches);
$port = $matches[1];

// Detect if server is on IPv4 or IPv6
$ipv4 = @fsockopen("tcp://127.0.0.1", (int)$port, $e, $es, 1);
if ($ipv4) {
    fclose($ipv4);
    $resolve_addr = "127.0.0.1";
} else {
    $resolve_addr = "::1";
}

// Use RESOLVE to map "testhost" to the server address
$ch = curl_cffi_init("http://testhost:$port/?test=get");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_RESOLVE, ["testhost:$port:$resolve_addr"]);
$result = curl_cffi_exec($ch);
echo trim($result) . "\n";
echo "Error: " . curl_cffi_errno($ch) . "\n";
curl_cffi_close($ch);
?>
--EXPECT--
Hello World!
Hello World!
Error: 0
