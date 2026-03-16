--TEST--
Session raise_for_status option and various HTTP status codes
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
require_once __DIR__ . '/server.inc';
$host = curl_cli_server_start();

// Test 1: raise_for_status=true throws on 4xx
$session = new CurlImpersonate\Session(['raise_for_status' => true]);
$caught = false;
try {
    $resp = $session->get("$host/?test=status&code=403");
} catch (CurlImpersonate\CurlException $e) {
    $caught = true;
    echo "403 caught: yes\n";
    echo "has response: " . (isset($e->response) && $e->response->statusCode === 403 ? 'yes' : 'no') . "\n";
}
if (!$caught) echo "403 caught: no\n";

// Test 2: raise_for_status=true throws on 5xx
$caught = false;
try {
    $resp = $session->get("$host/?test=status&code=500");
} catch (CurlImpersonate\CurlException $e) {
    $caught = true;
    echo "500 caught: yes\n";
}
if (!$caught) echo "500 caught: no\n";

// Test 3: raise_for_status=true does NOT throw on 2xx
$resp = $session->get("$host/?test=status&code=200");
echo "200 ok: " . $resp->statusCode . "\n";

// Test 4: raise_for_status=true does NOT throw on 3xx that completed (redirect followed)
$resp = $session->get("$host/?test=redirect_chain&step=0");
echo "redirect ok: " . (trim($resp->text()) === 'final destination' ? 'yes' : 'no') . "\n";

// Test 5: Per-request override
$session2 = new CurlImpersonate\Session(['raise_for_status' => false]);
$resp = $session2->get("$host/?test=status&code=404");
echo "no raise 404: " . $resp->statusCode . "\n";

// Override to raise on this request
$caught = false;
try {
    $resp = $session2->get("$host/?test=status&code=404", [
        'raise_for_status' => true,
    ]);
} catch (CurlImpersonate\CurlException $e) {
    $caught = true;
}
echo "override raise: " . ($caught ? 'yes' : 'no') . "\n";
?>
--EXPECT--
403 caught: yes
has response: yes
500 caught: yes
200 ok: 200
redirect ok: yes
no raise 404: 404
override raise: yes
