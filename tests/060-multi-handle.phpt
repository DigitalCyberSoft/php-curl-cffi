--TEST--
curl_cffi multi handle parallel requests
--EXTENSIONS--
curl_impersonate
--FILE--
<?php
include 'server.inc';
$host = curl_cli_server_start();

$mh = curl_cffi_multi_init();

$handles = [];
$urls = [
    $host . '?test=method',
    $host . '?test=get',
    $host . '?test=method',
];

foreach ($urls as $url) {
    $ch = curl_cffi_init($url);
    curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_cffi_multi_add_handle($mh, $ch);
    $handles[] = $ch;
}

// Execute all
do {
    $status = curl_cffi_multi_exec($mh, $active);
    if ($active) {
        curl_cffi_multi_select($mh);
    }
} while ($active && $status === CURLM_OK);

echo "Status: " . ($status === CURLM_OK ? "OK" : "FAIL") . "\n";

// Collect results
$results = [];
foreach ($handles as $ch) {
    $results[] = trim(curl_cffi_multi_getcontent($ch));
}

echo "Result 1: " . $results[0] . "\n";
echo "Result 2 has Hello: " . (strpos($results[1], 'Hello') !== false ? "yes" : "no") . "\n";
echo "Result 3: " . $results[2] . "\n";
echo "Count: " . count($results) . "\n";

// Cleanup
foreach ($handles as $ch) {
    curl_cffi_multi_remove_handle($mh, $ch);
    curl_cffi_close($ch);
}
curl_cffi_multi_close($mh);
?>
--EXPECT--
Status: OK
Result 1: GET
Result 2 has Hello: yes
Result 3: GET
Count: 3
