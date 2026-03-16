--TEST--
Response object: all properties and methods
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
require_once __DIR__ . '/server.inc';
$host = curl_cli_server_start();

$session = new CurlImpersonate\Session();

// Test 1: Basic response properties
$resp = $session->get("$host/?test=get");
echo "statusCode: " . $resp->statusCode . "\n";
echo "has content: " . (strlen($resp->content) > 0 ? 'yes' : 'no') . "\n";
echo "text equals content: " . ($resp->text() === $resp->content ? 'yes' : 'no') . "\n";
echo "url set: " . (str_contains($resp->url, 'test=get') ? 'yes' : 'no') . "\n";
echo "elapsed > 0: " . ($resp->elapsed > 0 ? 'yes' : 'no') . "\n";
echo "reason: " . $resp->reason . "\n";

// Test 2: JSON response
$resp = $session->get("$host/?test=json_echo");
$json = $resp->json();
echo "json method: " . $json['method'] . "\n";

// Test 3: Headers are arrays
$resp = $session->get("$host/?test=set_headers");
$ct = $resp->getHeader('Content-Type');
echo "getHeader: " . (str_contains($ct, 'text/html') ? 'yes' : 'no') . "\n";
echo "headers is array: " . (is_array($resp->headers) ? 'yes' : 'no') . "\n";

// Test 4: Cookies from response
$resp = $session->get("$host/?test=set_cookies");
echo "cookie foo: " . ($resp->cookies['foo'] ?? 'missing') . "\n";

// Test 5: Redirect count
$resp = $session->get("$host/?test=redirect_chain&step=0");
echo "final text: " . trim($resp->text()) . "\n";
echo "redirects >= 1: " . ($resp->redirectCount >= 1 ? 'yes' : 'no') . "\n";

// Test 6: Status code checking
$resp = $session->get("$host/?test=status&code=404");
echo "404 status: " . $resp->statusCode . "\n";

$caught = false;
try {
    $resp->raiseForStatus();
} catch (CurlImpersonate\CurlException $e) {
    $caught = true;
    echo "raised: " . str_contains($e->getMessage(), '404') . "\n";
}
echo "exception caught: " . ($caught ? 'yes' : 'no') . "\n";

// Test 7: 200 does not raise
$resp = $session->get("$host/?test=get");
$resp->raiseForStatus(); // should not throw
echo "200 ok: yes\n";
?>
--EXPECTF--
statusCode: 200
has content: yes
text equals content: yes
url set: yes
elapsed > 0: yes
reason: OK
json method: GET
getHeader: yes
headers is array: yes
cookie foo: bar
final text: final destination
redirects >= 1: yes
404 status: 404
raised: 1
exception caught: yes
200 ok: yes
