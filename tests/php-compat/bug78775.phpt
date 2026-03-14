--TEST--
Bug #78775: TLS issues from HTTP request affecting other encrypted connections
--EXTENSIONS--
curl_impersonate
openssl
--SKIPIF--
<?php
if (getenv('SKIP_ONLINE_TESTS')) die('skip Online test');
?>
--FILE--
<?php

$sock = fsockopen("tls://google.com", 443);

var_dump($sock);

$handle = curl_cffi_init('https://self-signed.badssl.com/');
curl_cffi_setopt_array(
    $handle,
    [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_SSL_VERIFYPEER => true,
    ]
);

var_dump(curl_cffi_exec($handle));
curl_cffi_close($handle);

fwrite($sock, "GET / HTTP/1.0\n\n");
var_dump(fread($sock, 8));

?>
--EXPECTF--
resource(%d) of type (stream)
bool(false)
string(8) "HTTP/1.0"
