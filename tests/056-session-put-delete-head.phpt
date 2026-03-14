--TEST--
Session PUT, DELETE, HEAD, OPTIONS methods
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
use CurlImpersonate\Session;
include 'server.inc';
$host = curl_cli_server_start();

$session = new Session();

// PUT
$response = $session->put($host . '?test=method');
echo "PUT: " . trim($response->text()) . "\n";

// DELETE
$response = $session->delete_($host . '?test=method');
echo "DELETE: " . trim($response->text()) . "\n";

// PATCH
$response = $session->patch($host . '?test=method');
echo "PATCH: " . trim($response->text()) . "\n";

// HEAD
$response = $session->head($host . '?test=get');
echo "HEAD status: " . $response->statusCode . "\n";

// OPTIONS
$response = $session->options($host . '?test=method');
echo "OPTIONS: " . trim($response->text()) . "\n";
?>
--EXPECT--
PUT: PUT
DELETE: DELETE
PATCH: PATCH
HEAD status: 200
OPTIONS: OPTIONS
