--TEST--
CurlImpersonate\Curl - cookies
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

use CurlImpersonate\Curl;
use CurlImpersonate\CurlOpt;

$c = new Curl();
$c->setOpt(CurlOpt::URL, $host . '?test=cookie');
$c->setOpt(CurlOpt::COOKIE, 'foo=bar');
$c->perform();

echo "foo: " . $c->getBody() . "\n";

$c->close();
?>
--EXPECT--
foo: bar
