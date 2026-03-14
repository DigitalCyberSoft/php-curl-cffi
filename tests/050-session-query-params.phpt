--TEST--
Session with query parameters
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
use CurlImpersonate\Session;
include 'server.inc';
$host = curl_cli_server_start();

$session = new Session();

// Per-request params
$response = $session->get($host . '?test=params_json', [
    'params' => ['q' => 'php curl', 'page' => '2'],
]);
$data = $response->json();
echo "test: " . $data['test'] . "\n";
echo "q: " . $data['q'] . "\n";
echo "page: " . $data['page'] . "\n";
echo "status: " . $response->statusCode . "\n";
?>
--EXPECT--
test: params_json
q: php curl
page: 2
status: 200
