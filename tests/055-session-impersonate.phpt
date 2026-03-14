--TEST--
Session with browser impersonation
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
use CurlImpersonate\Session;
include 'server.inc';
$host = curl_cli_server_start();

// Session with impersonate
$session = new Session(['impersonate' => 'chrome120']);
$response = $session->get($host . '?test=useragent');
$ua = $response->text();
echo "Has user agent: " . (strlen($ua) > 0 ? "yes" : "no") . "\n";
// Chrome impersonation should set a Chrome-like UA
echo "Contains Chrome: " . (strpos($ua, 'Chrome') !== false ? "yes" : "no") . "\n";
echo "Contains Mozilla: " . (strpos($ua, 'Mozilla') !== false ? "yes" : "no") . "\n";
echo "Status: " . $response->statusCode . "\n";
?>
--EXPECT--
Has user agent: yes
Contains Chrome: yes
Contains Mozilla: yes
Status: 200
