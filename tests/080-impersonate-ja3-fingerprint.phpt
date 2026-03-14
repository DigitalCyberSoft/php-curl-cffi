--TEST--
Impersonate browser has valid JA3 fingerprint
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
// Test that impersonation produces a real JA3 fingerprint
$targets = ['chrome120', 'safari15_5'];

foreach ($targets as $target) {
    $ch = curl_cffi_init("https://tls.browserleaks.com/json");
    curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 15);
    curl_cffi_setopt($ch, CURLOPT_ENCODING, "");
    curl_cffi_impersonate($ch, $target);
    $result = curl_cffi_exec($ch);
    $data = json_decode($result, true);

    echo "$target:\n";
    echo "  has ja3_hash: " . (isset($data['ja3_hash']) && strlen($data['ja3_hash']) === 32 ? "yes" : "no") . "\n";
    echo "  has ja3_text: " . (isset($data['ja3_text']) && strlen($data['ja3_text']) > 10 ? "yes" : "no") . "\n";
    curl_cffi_close($ch);
}

// Test that two different browsers produce different JA3 hashes
$hashes = [];
foreach (['chrome120', 'safari15_5'] as $target) {
    $ch = curl_cffi_init("https://tls.browserleaks.com/json");
    curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 15);
    curl_cffi_setopt($ch, CURLOPT_ENCODING, "");
    curl_cffi_impersonate($ch, $target);
    $result = curl_cffi_exec($ch);
    $data = json_decode($result, true);
    $hashes[$target] = $data['ja3_hash'] ?? '';
    curl_cffi_close($ch);
}
echo "Different JA3 for chrome vs safari: " . ($hashes['chrome120'] !== $hashes['safari15_5'] ? "yes" : "no") . "\n";
?>
--EXPECT--
chrome120:
  has ja3_hash: yes
  has ja3_text: yes
safari15_5:
  has ja3_hash: yes
  has ja3_text: yes
Different JA3 for chrome vs safari: yes
