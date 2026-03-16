--TEST--
Cookie persistence: multiple cookies set and sent across requests
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
require_once __DIR__ . '/server.inc';
$host = curl_cli_server_start();

$session = new CurlImpersonate\Session();

// Test 1: Set multiple cookies via server response
$resp = $session->get("$host/?test=set_domain_cookies");
echo "set: " . $resp->text() . "\n";

// Test 2: Verify cookies are sent back on next request
$resp = $session->get("$host/?test=echo_cookies");
$cookies = $resp->json();
echo "shared: " . ($cookies['shared'] ?? 'missing') . "\n";
echo "session: " . ($cookies['session'] ?? 'missing') . "\n";
echo "pref: " . ($cookies['pref'] ?? 'missing') . "\n";

// Test 3: Set additional cookies via session config
$session2 = new CurlImpersonate\Session([
    'cookies' => ['token' => 'xyz789', 'lang' => 'en'],
]);
$resp = $session2->get("$host/?test=echo_cookies");
$cookies = $resp->json();
echo "token: " . ($cookies['token'] ?? 'missing') . "\n";
echo "lang: " . ($cookies['lang'] ?? 'missing') . "\n";

// Test 4: Per-request cookies merge with session cookies
$resp = $session2->get("$host/?test=echo_cookies", [
    'cookies' => ['extra' => 'per-request'],
]);
$cookies = $resp->json();
echo "token still: " . ($cookies['token'] ?? 'missing') . "\n";
echo "extra: " . ($cookies['extra'] ?? 'missing') . "\n";

// Test 5: Cookie override (per-request overrides session)
$resp = $session2->get("$host/?test=echo_cookies", [
    'cookies' => ['token' => 'overridden'],
]);
$cookies = $resp->json();
echo "overridden: " . $cookies['token'] . "\n";
?>
--EXPECT--
set: domain cookies set
shared: value1
session: abc123
pref: dark
token: xyz789
lang: en
token still: xyz789
extra: per-request
overridden: overridden
