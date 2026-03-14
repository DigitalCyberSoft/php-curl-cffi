--TEST--
CurlImpersonate\Curl - custom headers
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

use CurlImpersonate\Curl;
use CurlImpersonate\CurlOpt;

$c = new Curl();
$c->setOpt(CurlOpt::URL, $host . '?test=headers_json');
$c->setOpt(CurlOpt::HTTPHEADER, ['Foo: bar']);
$c->perform();

$headers = json_decode($c->getBody(), true);
echo "foo: " . $headers['foo'] . "\n";

// Setting again should replace, not append
$c->setOpt(CurlOpt::HTTPHEADER, ['Foo: baz']);
$c->perform();

$headers = json_decode($c->getBody(), true);
echo "foo: " . $headers['foo'] . "\n";

$c->close();
?>
--EXPECT--
foo: bar
foo: baz
