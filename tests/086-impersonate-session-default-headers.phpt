--TEST--
Session impersonate injects browser-appropriate headers
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
use CurlImpersonate\Session;
include 'server.inc';
$host = curl_cli_server_start();

// Chrome session should have Chrome-like headers
$session = new Session(['impersonate' => 'chrome120']);
$response = $session->get($host . '?test=headers_json');
$headers = json_decode($response->text(), true);

echo "Chrome UA contains Chrome: " . (strpos($headers['user-agent'] ?? '', 'Chrome') !== false ? "yes" : "no") . "\n";

// Safari session should have Safari-like headers
$session = new Session(['impersonate' => 'safari18_0']);
$response = $session->get($host . '?test=useragent');
$ua = $response->text();
echo "Safari UA contains Safari: " . (strpos($ua, 'Safari') !== false ? "yes" : "no") . "\n";
echo "Safari UA not Chrome: " . (strpos($ua, 'Chrome') === false ? "yes" : "no") . "\n";
?>
--EXPECT--
Chrome UA contains Chrome: yes
Safari UA contains Safari: yes
Safari UA not Chrome: yes
