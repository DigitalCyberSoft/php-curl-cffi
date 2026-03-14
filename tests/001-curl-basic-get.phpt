--TEST--
CurlImpersonate\Curl - basic GET request
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
$c->setOpt(CurlOpt::URL, $host . '?test=get');
$c->perform();

$code = $c->getInfo(CurlInfo::RESPONSE_CODE);
echo "status: $code\n";

$body = $c->getBody();
echo "has body: " . (strlen($body) > 0 ? 'yes' : 'no') . "\n";
echo "contains hello: " . (str_contains($body, 'Hello World!') ? 'yes' : 'no') . "\n";

$c->close();
echo "done\n";
?>
--EXPECT--
status: 200
has body: yes
contains hello: yes
done
