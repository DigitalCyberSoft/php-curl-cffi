--TEST--
curl_cffi_impersonate rejects invalid target
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$ch = curl_cffi_init();
curl_cffi_setopt($ch, CURLOPT_URL, 'http://localhost');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);

$result = curl_cffi_impersonate($ch, 'invalid_browser_target_xyz');
if ($result === false) {
    echo "Invalid target rejected: yes\n";
} else {
    // Try to exec and see if it fails
    echo "Invalid target rejected: returned non-false\n";
}
curl_cffi_close($ch);
?>
--EXPECT--
Invalid target rejected: yes
