--TEST--
CurlImpersonate\Session - basic GET
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

use CurlImpersonate\Session;

$s = new Session();
$r = $s->get($host . '?test=get');

echo "status: " . $r->statusCode . "\n";
echo "type: " . gettype($r->headers) . "\n";

echo "has body: " . (strlen($r->content) > 0 ? 'yes' : 'no') . "\n";
echo "text: " . (strlen($r->text()) > 0 ? 'yes' : 'no') . "\n";

$s->close();
?>
--EXPECT--
status: 200
type: array
has body: yes
text: yes
