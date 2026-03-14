--TEST--
CurlImpersonate\Session - closed session throws error
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
use CurlImpersonate\Session;
use CurlImpersonate\CurlException;

$s = new Session();
$s->close();

$methods = ['get', 'post', 'put', 'delete_', 'head', 'options', 'patch'];
$caught = 0;

foreach ($methods as $method) {
    try {
        $s->$method('http://127.0.0.1:8399/');
        echo "ERROR: $method should have thrown\n";
    } catch (CurlException $e) {
        $caught++;
    }
}

echo "caught: $caught\n";
?>
--EXPECT--
caught: 7
