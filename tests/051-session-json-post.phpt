--TEST--
Session POST with JSON body
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
use CurlImpersonate\Session;
include 'server.inc';
$host = curl_cli_server_start();

$session = new Session();
$response = $session->post($host . '?test=json_echo', [
    'json' => ['key' => 'value', 'number' => 42],
]);
$data = $response->json();
echo "method: " . $data['method'] . "\n";
echo "content_type contains json: " . (strpos($data['content_type'], 'json') !== false ? "yes" : "no") . "\n";
$body = json_decode($data['body'], true);
echo "key: " . $body['key'] . "\n";
echo "number: " . $body['number'] . "\n";
?>
--EXPECT--
method: POST
content_type contains json: yes
key: value
number: 42
