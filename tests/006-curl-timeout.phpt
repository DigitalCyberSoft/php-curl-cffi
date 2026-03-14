--TEST--
CurlImpersonate\Curl - timeout
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

use CurlImpersonate\Curl;
use CurlImpersonate\CurlOpt;
use CurlImpersonate\CurlException;

$c = new Curl();
$c->setOpt(CurlOpt::URL, $host . '?test=delay&ms=5000');
$c->setOpt(CurlOpt::TIMEOUT_MS, 100);

try {
    $c->perform();
    echo "ERROR: should have timed out\n";
} catch (CurlException $e) {
    echo "caught timeout\n";
    echo "code: " . $e->getCode() . "\n"; // 28 = CURLE_OPERATION_TIMEDOUT
}

$c->close();
?>
--EXPECT--
caught timeout
code: 28
