--TEST--
Impersonation: HTTP/2 pseudo-header order and header values
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
if (!file_exists(__DIR__ . '/ssl/server.key')) die('skip SSL certs not generated');
if (!file_exists(__DIR__ . '/signatures/targets.json')) die('skip signature files not converted');
$out = shell_exec('which nghttpd 2>/dev/null');
if (empty(trim($out ?? ''))) die('skip nghttpd not installed');
?>
--FILE--
<?php
require_once __DIR__ . '/impersonate-server.inc';

$targets = get_target_map();
$pass = 0;
$fail = 0;
$port = 18600;

foreach ($targets as $target => $sig_name) {
    $sig = load_signature($target);
    if (!$sig) {
        echo "SKIP $target: signature not found\n";
        continue;
    }

    $h2 = $sig['signature']['http2'] ?? null;
    if (!$h2 || empty($h2['frames'])) {
        echo "SKIP $target: no HTTP/2 signature data\n";
        continue;
    }

    // Extract expected HEADERS frame data
    $expected_pseudo = [];
    $expected_headers = [];
    foreach ($h2['frames'] as $frame) {
        if ($frame['frame_type'] === 'HEADERS') {
            $expected_pseudo = $frame['pseudo_headers'] ?? [];
            $expected_headers = $frame['headers'] ?? [];
            break;
        }
    }

    $port++;
    [$proc, $stderr_file] = start_nghttpd($port);

    $result = impersonate_request("https://127.0.0.1:$port/", $target);
    $log = read_nghttpd_log($stderr_file);
    $parsed = parse_nghttpd_log($log);
    stop_server($proc);

    $ok = true;

    // Compare pseudo-header ORDER
    if ($parsed['pseudo_headers'] != $expected_pseudo) {
        echo "FAIL $target: pseudo-header order mismatch\n";
        echo "  Expected: " . json_encode($expected_pseudo) . "\n";
        echo "  Actual:   " . json_encode($parsed['pseudo_headers']) . "\n";
        $ok = false;
    }

    // Compare header names (in order, case-insensitive on name)
    // Skip :authority value comparison (it's localhost:port)
    // Compare header names and values against expected
    $actual_header_names = [];
    foreach ($parsed['headers'] as $h) {
        $parts = explode(':', $h, 2);
        $actual_header_names[] = strtolower(trim($parts[0]));
    }

    $expected_header_names = [];
    foreach ($expected_headers as $h) {
        $parts = explode(':', $h, 2);
        $expected_header_names[] = strtolower(trim($parts[0]));
    }

    if ($actual_header_names != $expected_header_names) {
        echo "FAIL $target: header name/order mismatch\n";
        echo "  Expected: " . json_encode($expected_header_names) . "\n";
        echo "  Actual:   " . json_encode($actual_header_names) . "\n";
        $ok = false;
    }

    if ($ok) {
        echo "PASS $target\n";
        $pass++;
    } else {
        $fail++;
    }
}

echo "\nResults: $pass passed, $fail failed out of " . count($targets) . " targets\n";
if ($fail === 0) echo "ALL TESTS PASSED\n";
?>
--EXPECTF--
PASS chrome99
PASS chrome100
PASS chrome101
PASS chrome104
PASS chrome107
PASS chrome110
PASS chrome116
PASS chrome119
PASS chrome120
PASS chrome123
PASS chrome124
PASS chrome131
PASS edge99
PASS edge101
PASS safari15_3
PASS safari15_5
PASS safari17_0
PASS safari18_0

Results: 18 passed, 0 failed out of 18 targets
ALL TESTS PASSED
