--TEST--
CurlImpersonate\Session - headers and response properties
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

use CurlImpersonate\Session;

$s = new Session();

// Custom headers
$r = $s->get($host . '?test=headers_json', ['headers' => ['Foo' => 'bar']]);
$headers = $r->json();
echo "foo: " . $headers['foo'] . "\n";

// Response headers
$r = $s->get($host . '?test=set_headers');
$xtest = $r->headers['X-Test'] ?? [];
echo "x-test count: " . count($xtest) . "\n";
echo "x-test[0]: " . $xtest[0] . "\n";
echo "x-test[1]: " . $xtest[1] . "\n";

// Reason phrase
$r = $s->get($host . '?test=redirect_301', ['allow_redirects' => false]);
echo "reason: " . $r->reason . "\n";

// OK reason
$r = $s->get($host . '?test=get');
echo "ok reason: " . $r->reason . "\n";

// Elapsed time
echo "elapsed > 0: " . ($r->elapsed > 0 ? 'yes' : 'no') . "\n";

$s->close();
?>
--EXPECT--
foo: bar
x-test count: 2
x-test[0]: test
x-test[1]: test2
reason: Moved Permanently
ok reason: OK
elapsed > 0: yes
