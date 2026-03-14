--TEST--
Response properties: elapsed, reason, url, redirectCount
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
use CurlImpersonate\Session;
include 'server.inc';
$host = curl_cli_server_start();

$session = new Session();
$response = $session->get($host . '?test=get');

// elapsed should be a positive float
echo "elapsed > 0: " . ($response->elapsed > 0 ? "yes" : "no") . "\n";
echo "elapsed < 10: " . ($response->elapsed < 10 ? "yes" : "no") . "\n";

// reason for 200 should be "OK"
echo "reason: " . $response->reason . "\n";

// url should contain the request URL
echo "url contains host: " . (strpos($response->url, 'localhost') !== false ? "yes" : "no") . "\n";

// redirectCount should be 0 for direct request
echo "redirectCount: " . $response->redirectCount . "\n";

// statusCode
echo "statusCode: " . $response->statusCode . "\n";
?>
--EXPECT--
elapsed > 0: yes
elapsed < 10: yes
reason: OK
url contains host: yes
redirectCount: 0
statusCode: 200
