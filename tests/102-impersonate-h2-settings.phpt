--TEST--
Impersonation: HTTP/2 SETTINGS and WINDOW_UPDATE match expected signature
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
$port = 18500;

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

    // Extract expected SETTINGS and WINDOW_UPDATE from frames
    $expected_settings = [];
    $expected_wu = null;
    foreach ($h2['frames'] as $frame) {
        if ($frame['frame_type'] === 'SETTINGS' && !empty($frame['settings'])) {
            $expected_settings = $frame['settings'];
        }
        if ($frame['frame_type'] === 'WINDOW_UPDATE') {
            $expected_wu = $frame['window_size_increment'];
        }
    }

    // Start nghttpd for this target
    $port++;
    [$proc, $stderr_file] = start_nghttpd($port);

    $result = impersonate_request("https://127.0.0.1:$port/", $target);
    $log = read_nghttpd_log($stderr_file);
    $parsed = parse_nghttpd_log($log);
    stop_server($proc);

    $ok = true;

    // Compare SETTINGS (normalize GREASE)
    // Older signatures (chrome99-116, edge99-101) only have HEADERS - skip if no expected SETTINGS
    if (!empty($expected_settings)) {
        $actual_settings = normalize_h2_settings($parsed['settings']);
        $exp_settings = normalize_h2_settings($expected_settings);

        if ($actual_settings != $exp_settings) {
            echo "FAIL $target: SETTINGS mismatch\n";
            echo "  Expected: " . json_encode($exp_settings) . "\n";
            echo "  Actual:   " . json_encode($actual_settings) . "\n";
            $ok = false;
        }
    }

    // Compare WINDOW_UPDATE (only if signature has it)
    if ($expected_wu !== null && $parsed['window_update'] !== $expected_wu) {
        echo "FAIL $target: WINDOW_UPDATE mismatch\n";
        echo "  Expected: $expected_wu\n";
        echo "  Actual:   " . ($parsed['window_update'] ?? 'null') . "\n";
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
