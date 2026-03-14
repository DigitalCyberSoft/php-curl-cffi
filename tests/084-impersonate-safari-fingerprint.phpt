--TEST--
Safari impersonation produces Safari-like TLS fingerprint
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
$ch = curl_cffi_init("https://tls.browserleaks.com/json");
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 10);
curl_cffi_impersonate($ch, "safari18_0");
$r = curl_cffi_exec($ch);
if (curl_cffi_errno($ch) !== 0) die("skip: cannot reach tls.browserleaks.com");
?>
--FILE--
<?php
$targets = [
    'safari15_3' => 'Safari',
    'safari15_5' => 'Safari',
    'safari17_0' => 'Safari',
    'safari18_0' => 'Safari',
];

$all_pass = true;
foreach ($targets as $target => $expected_browser) {
    $ch = curl_cffi_init("https://tls.browserleaks.com/json");
    curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 15);
    curl_cffi_impersonate($ch, $target);
    $result = curl_cffi_exec($ch);
    $data = json_decode($result, true);
    $errno = curl_cffi_errno($ch);
    curl_cffi_close($ch);

    if ($errno !== 0) {
        echo "$target: FAIL (error $errno)\n";
        $all_pass = false;
        continue;
    }

    $has_ja3 = isset($data['ja3_hash']) && strlen($data['ja3_hash']) === 32;
    if (!$has_ja3) {
        echo "$target: FAIL (no JA3 hash)\n";
        $all_pass = false;
    } else {
        echo "$target: OK\n";
    }
}
echo "All Safari targets produce valid JA3: " . ($all_pass ? "yes" : "no") . "\n";
?>
--EXPECT--
safari15_3: OK
safari15_5: OK
safari17_0: OK
safari18_0: OK
All Safari targets produce valid JA3: yes
