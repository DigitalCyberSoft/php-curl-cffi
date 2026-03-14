--TEST--
CurlImpersonate\Session - POST with various data types
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

use CurlImpersonate\Session;

$s = new Session();

// POST with dict data (form encoded)
$r = $s->post($host . '?test=echo_body', ['data' => ['foo' => 'bar']]);
echo "form: " . $r->content . "\n";

// POST with string data
$r = $s->post($host . '?test=echo_body', ['data' => '{"foo": "bar"}']);
echo "string: " . $r->content . "\n";

// POST with JSON
$r = $s->post($host . '?test=echo_body', ['json' => ['foo' => 'bar']]);
echo "json: " . $r->content . "\n";

// POST with empty JSON
$r = $s->post($host . '?test=echo_body', ['json' => new stdClass()]);
echo "empty: " . $r->content . "\n";

$s->close();
?>
--EXPECT--
form: foo=bar
string: {"foo": "bar"}
json: {"foo":"bar"}
empty: {}
