--TEST--
curl_cffi_version returns version info
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
$v = curl_cffi_version();
echo "Has version: " . (isset($v['version']) ? "yes" : "no") . "\n";
echo "Has ssl_version: " . (isset($v['ssl_version']) ? "yes" : "no") . "\n";
echo "Has protocols: " . (is_array($v['protocols']) ? "yes" : "no") . "\n";
echo "SSL is BoringSSL: " . (strpos($v['ssl_version'], 'BoringSSL') !== false ? "yes" : "no") . "\n";
echo "Has https: " . (in_array('https', $v['protocols']) ? "yes" : "no") . "\n";
?>
--EXPECT--
Has version: yes
Has ssl_version: yes
Has protocols: yes
SSL is BoringSSL: yes
Has https: yes
