--TEST--
Procedural API - curl_cffi_* functions (drop-in replacement for curl_*)
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

// Init with URL
$ch = curl_cffi_init($host . '?test=get');
echo "type: " . get_class($ch) . "\n";

// RETURNTRANSFER
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
$body = curl_cffi_exec($ch);
echo "has body: " . (str_contains($body, 'Hello World!') ? 'yes' : 'no') . "\n";

// getinfo with no option (returns array)
$info = curl_cffi_getinfo($ch);
echo "http_code: " . $info['http_code'] . "\n";
echo "has url: " . (isset($info['url']) ? 'yes' : 'no') . "\n";

// getinfo with option
$code = curl_cffi_getinfo($ch, CURLINFO_RESPONSE_CODE);
echo "code: $code\n";

// error/errno
echo "errno: " . curl_cffi_errno($ch) . "\n";
echo "error empty: " . (curl_cffi_error($ch) === '' ? 'yes' : 'no') . "\n";

// POST with setopt_array
curl_cffi_setopt_array($ch, [
    CURLOPT_URL => $host . '?test=echo_body',
    CURLOPT_POST => true,
    CURLOPT_POSTFIELDS => 'test=data',
]);
$body = curl_cffi_exec($ch);
echo "post: $body\n";

// strerror
echo "timeout str: " . curl_cffi_strerror(28) . "\n";

// Impersonate
$ok = curl_cffi_impersonate($ch, 'chrome136');
echo "impersonate: " . ($ok ? 'ok' : 'fail') . "\n";

// copy_handle
$ch2 = curl_cffi_copy_handle($ch);
echo "copy type: " . get_class($ch2) . "\n";

// version
$ver = curl_cffi_version();
echo "has impersonate: " . (str_contains($ver['version'], 'IMPERSONATE') ? 'yes' : 'no') . "\n";

// reset
curl_cffi_reset($ch);
echo "reset errno: " . curl_cffi_errno($ch) . "\n";

curl_cffi_close($ch);
curl_cffi_close($ch2);
echo "done\n";
?>
--EXPECT--
type: CurlImpersonate\Curl
has body: yes
http_code: 200
has url: yes
code: 200
errno: 0
error empty: yes
post: test=data
timeout str: Timeout was reached
impersonate: ok
copy type: CurlImpersonate\Curl
has impersonate: yes
reset errno: 0
done
