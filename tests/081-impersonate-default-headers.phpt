--TEST--
Impersonate default_headers controls browser header injection
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

// With default_headers=true (default): should inject Chrome-like UA
$ch = curl_cffi_init($host . '?test=useragent');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_impersonate($ch, 'chrome120');
$ua_with = curl_cffi_exec($ch);
curl_cffi_close($ch);

echo "With default headers:\n";
echo "  Has UA: " . (strlen(trim($ua_with)) > 0 ? "yes" : "no") . "\n";
echo "  Contains Mozilla: " . (strpos($ua_with, 'Mozilla') !== false ? "yes" : "no") . "\n";
echo "  Contains Chrome: " . (strpos($ua_with, 'Chrome') !== false ? "yes" : "no") . "\n";

// With default_headers=false: should NOT inject browser UA
$ch = curl_cffi_init($host . '?test=useragent');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_impersonate($ch, 'chrome120', false);
$ua_without = curl_cffi_exec($ch);
curl_cffi_close($ch);

echo "Without default headers:\n";
echo "  Contains Chrome: " . (strpos($ua_without, 'Chrome') !== false ? "yes" : "no") . "\n";
?>
--EXPECT--
With default headers:
  Has UA: yes
  Contains Mozilla: yes
  Contains Chrome: yes
Without default headers:
  Contains Chrome: no
