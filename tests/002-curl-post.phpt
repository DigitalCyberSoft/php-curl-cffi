--TEST--
CurlImpersonate\Curl - POST request
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

use CurlImpersonate\Curl;
use CurlImpersonate\CurlOpt;
use CurlImpersonate\CurlInfo;

$c = new Curl();
$c->setOpt(CurlOpt::URL, $host . '?test=echo_body');
$c->setOpt(CurlOpt::POST, 1);
$c->setOpt(CurlOpt::POSTFIELDS, 'foo=bar');
$c->perform();

echo "body: " . $c->getBody() . "\n";
echo "status: " . $c->getInfo(CurlInfo::RESPONSE_CODE) . "\n";

$c->close();
?>
--EXPECT--
body: foo=bar
status: 200
