--TEST--
Impersonate defaults to HTTP/2+ for HTTPS connections
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
$ch = curl_cffi_init("https://tls.browserleaks.com/json");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 10);
curl_cffi_impersonate($ch, "chrome120");
$r = curl_cffi_exec($ch);
if (curl_cffi_errno($ch) !== 0) die("skip: cannot reach tls.browserleaks.com");
?>
--FILE--
<?php
// Chrome impersonation should negotiate HTTP/2+ via ALPN
$ch = curl_cffi_init("https://tls.browserleaks.com/json");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 15);
curl_cffi_impersonate($ch, 'chrome120');
$result = curl_cffi_exec($ch);
$info = curl_cffi_getinfo($ch, CURLINFO_HTTP_VERSION);
echo "Chrome HTTP version: " . ($info >= 2 ? "HTTP/2+" : "HTTP/1.x") . "\n";
curl_cffi_close($ch);

// Safari should also use HTTP/2+
$ch = curl_cffi_init("https://tls.browserleaks.com/json");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 15);
curl_cffi_impersonate($ch, 'safari18_0');
$result = curl_cffi_exec($ch);
$info = curl_cffi_getinfo($ch, CURLINFO_HTTP_VERSION);
echo "Safari HTTP version: " . ($info >= 2 ? "HTTP/2+" : "HTTP/1.x") . "\n";
curl_cffi_close($ch);
?>
--EXPECT--
Chrome HTTP version: HTTP/2+
Safari HTTP version: HTTP/2+
