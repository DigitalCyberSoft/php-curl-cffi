--TEST--
Impersonate user-agent can be overridden
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

// Override UA via CURLOPT_USERAGENT after impersonate
$ch = curl_cffi_init($host . '?test=useragent');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_impersonate($ch, 'chrome120');
curl_cffi_setopt($ch, CURLOPT_USERAGENT, 'CustomAgent/1.0');
$ua = trim(curl_cffi_exec($ch));
echo "USERAGENT override: $ua\n";
curl_cffi_close($ch);

// Override UA via HTTPHEADER after impersonate
$ch = curl_cffi_init($host . '?test=useragent');
curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
curl_cffi_impersonate($ch, 'chrome120');
curl_cffi_setopt($ch, CURLOPT_HTTPHEADER, ['User-Agent: HeaderAgent/2.0']);
$ua = trim(curl_cffi_exec($ch));
echo "HTTPHEADER override: $ua\n";
curl_cffi_close($ch);
?>
--EXPECT--
USERAGENT override: CustomAgent/1.0
HTTPHEADER override: HeaderAgent/2.0
