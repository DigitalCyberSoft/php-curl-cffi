--TEST--
curl_cffi_impersonate accepts valid browser targets
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$targets = [
    'chrome99', 'chrome100', 'chrome101', 'chrome104', 'chrome107',
    'chrome110', 'chrome116', 'chrome119', 'chrome120', 'chrome123',
    'chrome124', 'chrome131',
    'edge99', 'edge101',
    'safari15_3', 'safari15_5', 'safari17_0', 'safari18_0',
];

$pass = 0;
$fail = 0;
foreach ($targets as $target) {
    $ch = curl_cffi_init();
    $result = curl_cffi_impersonate($ch, $target);
    if ($result === true) {
        $pass++;
    } else {
        echo "FAIL: $target\n";
        $fail++;
    }
    curl_cffi_close($ch);
}
echo "Targets tested: " . ($pass + $fail) . "\n";
echo "Passed: $pass\n";
echo "Failed: $fail\n";
?>
--EXPECT--
Targets tested: 18
Passed: 18
Failed: 0
