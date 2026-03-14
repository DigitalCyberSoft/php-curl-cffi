--TEST--
Session with authentication
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
use CurlImpersonate\Session;
include 'server.inc';
$host = curl_cli_server_start();

$session = new Session();
$response = $session->get($host . '?test=auth', [
    'auth' => ['myuser', 'mypass'],
]);
echo "Status: " . $response->statusCode . "\n";
echo "Body: " . $response->text() . "\n";
?>
--EXPECT--
Status: 200
Body: user=myuser&pass=mypass
