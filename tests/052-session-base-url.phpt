--TEST--
Session base_url support
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
use CurlImpersonate\Session;
include 'server.inc';
$host = curl_cli_server_start();

// Session with base_url
$session = new Session(['base_url' => $host]);

// Relative path (should append to base)
$response = $session->get('/?test=get');
echo "Relative: " . trim($response->text()) . "\n";

// Full URL overrides base
$response = $session->get($host . '?test=method');
echo "Full URL: " . trim($response->text()) . "\n";
?>
--EXPECT--
Relative: Hello World!
Hello World!
Full URL: GET
