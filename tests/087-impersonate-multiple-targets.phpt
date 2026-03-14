--TEST--
All impersonate targets produce HTTPS connections successfully
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
// Test that all supported browser targets can complete a real HTTPS handshake
$targets = [
    'chrome99', 'chrome104', 'chrome110', 'chrome120', 'chrome124', 'chrome131',
    'edge99', 'edge101',
    'safari15_3', 'safari15_5', 'safari17_0', 'safari18_0',
];

$pass = 0;
$fail = 0;
foreach ($targets as $target) {
    $ch = curl_cffi_init("https://tls.browserleaks.com/json");
    curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 15);
    curl_cffi_impersonate($ch, $target);
    $result = curl_cffi_exec($ch);
    $errno = curl_cffi_errno($ch);

    if ($errno === 0) {
        $data = json_decode($result, true);
        if (isset($data['ja3_hash'])) {
            $pass++;
        } else {
            echo "FAIL: $target - no JA3 in response\n";
            $fail++;
        }
    } else {
        echo "FAIL: $target - error $errno\n";
        $fail++;
    }
    curl_cffi_close($ch);
}

echo "Targets tested: " . ($pass + $fail) . "\n";
echo "All passed: " . ($fail === 0 ? "yes" : "no") . "\n";
?>
--EXPECT--
Targets tested: 12
All passed: yes
