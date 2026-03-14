--TEST--
Impersonate HTTP version defaults to HTTP/2 and can be overridden
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
// CURLINFO_HTTP_VERSION returns CURL_HTTP_VERSION_* constants:
// 2 = HTTP/1.1, 3 = HTTP/2, 4 = HTTP/2TLS

// Default: impersonation should use HTTP/2
$ch = curl_cffi_init("https://tls.browserleaks.com/json");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 15);
curl_cffi_impersonate($ch, 'chrome120');
$result = curl_cffi_exec($ch);
$info = curl_cffi_getinfo($ch, CURLINFO_HTTP_VERSION);
echo "Default: " . ($info >= 3 ? "HTTP/2+" : "HTTP/1.x") . "\n";
curl_cffi_close($ch);

// Force HTTP/1.1 after impersonate
$ch = curl_cffi_init("https://tls.browserleaks.com/json");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 15);
curl_cffi_impersonate($ch, 'chrome120');
curl_cffi_setopt($ch, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
$result = curl_cffi_exec($ch);
$info = curl_cffi_getinfo($ch, CURLINFO_HTTP_VERSION);
echo "Forced HTTP/1.1: " . ($info <= 2 ? "HTTP/1.x" : "HTTP/2+") . "\n";
curl_cffi_close($ch);
?>
--EXPECT--
Default: HTTP/2+
Forced HTTP/1.1: HTTP/1.x
