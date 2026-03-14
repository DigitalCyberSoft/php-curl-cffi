--TEST--
CurlImpersonate\Session - redirect handling
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

use CurlImpersonate\Session;
use CurlImpersonate\CurlException;

$s = new Session();

// Follow redirect (default)
$r = $s->get($host . '?test=redirect_301');
echo "follow status: " . $r->statusCode . "\n";
echo "redirect_count: " . $r->redirectCount . "\n";

// Don't follow redirect
$r = $s->get($host . '?test=redirect_301', ['allow_redirects' => false]);
echo "no-follow status: " . $r->statusCode . "\n";
echo "no-follow count: " . $r->redirectCount . "\n";

// Too many redirects
try {
    $r = $s->get($host . '?test=redirect_loop', ['max_redirects' => 2]);
    echo "ERROR: should have thrown\n";
} catch (CurlException $e) {
    echo "caught redirect error\n";
}

$s->close();
?>
--EXPECT--
follow status: 200
redirect_count: 1
no-follow status: 301
no-follow count: 0
caught redirect error
