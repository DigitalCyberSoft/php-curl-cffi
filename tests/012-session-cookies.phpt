--TEST--
CurlImpersonate\Session - cookie handling
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

use CurlImpersonate\Session;

// Preset cookies
$s = new Session(['cookies' => ['foo' => 'bar']]);
$r = $s->get($host . '?test=echo_cookies', ['cookies' => ['hello' => 'world']]);
$cookies = $r->json();
echo "foo: " . $cookies['foo'] . "\n";
echo "hello: " . $cookies['hello'] . "\n";

// Cookie override
$r = $s->get($host . '?test=echo_cookies', ['cookies' => ['foo' => 'notbar']]);
$cookies = $r->json();
echo "override: " . $cookies['foo'] . "\n";

// Cookie persistence from Set-Cookie
$s2 = new Session();
$r = $s2->get($host . '?test=set_cookies');
echo "set response: " . (strlen($r->content) > 0 ? 'ok' : 'empty') . "\n";

// Session should now have the cookie
$r = $s2->get($host . '?test=echo_cookies');
$cookies = $r->json();
echo "persisted: " . ($cookies['foo'] ?? 'missing') . "\n";

$s->close();
$s2->close();
?>
--EXPECT--
foo: bar
hello: world
override: notbar
set response: ok
persisted: bar
