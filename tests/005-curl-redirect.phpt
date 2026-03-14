--TEST--
CurlImpersonate\Curl - follow/disallow redirects
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

use CurlImpersonate\Curl;
use CurlImpersonate\CurlOpt;
use CurlImpersonate\CurlInfo;

// Not following redirect
$c = new Curl();
$c->setOpt(CurlOpt::URL, $host . '?test=redirect_301');
$c->perform();
echo "no follow: " . $c->getInfo(CurlInfo::RESPONSE_CODE) . "\n";

// Following redirect
$c->setOpt(CurlOpt::FOLLOWLOCATION, 1);
$c->perform();
echo "follow: " . $c->getInfo(CurlInfo::RESPONSE_CODE) . "\n";

$c->close();
?>
--EXPECT--
no follow: 301
follow: 200
