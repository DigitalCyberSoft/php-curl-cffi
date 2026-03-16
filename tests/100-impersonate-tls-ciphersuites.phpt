--TEST--
Impersonation: TLS ClientHello cipher suites, extensions, groups, sig algs
--EXTENSIONS--
curl_impersonate
--SKIPIF--
<?php
if (!file_exists(__DIR__ . '/bin/tls-server')) die('skip TLS server binary not built');
if (!file_exists(__DIR__ . '/signatures/targets.json')) die('skip signature files not converted');
?>
--FILE--
<?php
require_once __DIR__ . '/impersonate-server.inc';

[$proc, $port, $pipes] = start_tls_server();
$targets = get_target_map();
$pass = 0;
$fail = 0;

foreach ($targets as $target => $sig_name) {
    $sig = load_signature($target);
    if (!$sig) {
        echo "SKIP $target: signature not found\n";
        continue;
    }

    $tls = $sig['signature']['tls_client_hello'];
    $permute = $sig['signature']['options']['tls_permute_extensions'] ?? false;

    $result = impersonate_request("https://127.0.0.1:$port/", $target);
    if ($result === false) {
        echo "FAIL $target: request failed\n";
        $fail++;
        continue;
    }

    $data = json_decode($result, true);
    if (!$data) {
        echo "FAIL $target: invalid JSON response\n";
        $fail++;
        continue;
    }

    $ok = true;

    // Compare cipher suites (GREASE-normalized)
    $actual_ciphers = normalize_grease($data['cipher_suites'] ?? []);
    $expected_ciphers = $tls['ciphersuites'];
    // Normalize expected: string "GREASE" stays, ints stay
    $expected_ciphers = array_map(fn($v) => is_string($v) && $v === 'GREASE' ? 'GREASE' : (int)$v, $expected_ciphers);
    if ($actual_ciphers != $expected_ciphers) {
        echo "FAIL $target: cipher suites mismatch\n";
        echo "  Expected: " . json_encode($expected_ciphers) . "\n";
        echo "  Actual:   " . json_encode($actual_ciphers) . "\n";
        $ok = false;
    }

    // Compare extensions
    $actual_ext_ids = $data['extensions'] ?? [];
    $expected_ext_ids = get_expected_extension_ids($tls['extensions']);
    [$ext_match, $ext_reason] = compare_extensions($actual_ext_ids, $expected_ext_ids, $permute);
    if (!$ext_match) {
        echo "FAIL $target: $ext_reason\n";
        $ok = false;
    }

    // Compare supported groups
    $actual_groups = normalize_grease($data['supported_groups'] ?? []);
    $expected_groups = [];
    foreach ($tls['extensions'] as $ext) {
        if (($ext['type'] ?? '') === 'supported_groups') {
            $expected_groups = array_map(fn($v) => $v === 'GREASE' ? 'GREASE' : (int)$v,
                $ext['supported_groups'] ?? $ext['data'] ?? []);
            break;
        }
    }
    if (!empty($expected_groups) && $actual_groups != $expected_groups) {
        echo "FAIL $target: supported groups mismatch\n";
        echo "  Expected: " . json_encode($expected_groups) . "\n";
        echo "  Actual:   " . json_encode($actual_groups) . "\n";
        $ok = false;
    }

    // Compare signature algorithms
    $actual_sigalgs = $data['signature_schemes'] ?? [];
    $expected_sigalgs = [];
    foreach ($tls['extensions'] as $ext) {
        if (($ext['type'] ?? '') === 'signature_algorithms') {
            $expected_sigalgs = array_map('intval', $ext['signature_algorithms'] ?? $ext['data'] ?? []);
            break;
        }
    }
    if (!empty($expected_sigalgs) && $actual_sigalgs != $expected_sigalgs) {
        echo "FAIL $target: signature algorithms mismatch\n";
        echo "  Expected: " . json_encode($expected_sigalgs) . "\n";
        echo "  Actual:   " . json_encode($actual_sigalgs) . "\n";
        $ok = false;
    }

    // Compare ALPN
    $actual_alpn = $data['alpn'] ?? [];
    $expected_alpn = [];
    foreach ($tls['extensions'] as $ext) {
        if (($ext['type'] ?? '') === 'application_layer_protocol_negotiation') {
            $expected_alpn = $ext['protocols'] ?? $ext['data'] ?? [];
            break;
        }
    }
    if (!empty($expected_alpn) && $actual_alpn != $expected_alpn) {
        echo "FAIL $target: ALPN mismatch\n";
        echo "  Expected: " . json_encode($expected_alpn) . "\n";
        echo "  Actual:   " . json_encode($actual_alpn) . "\n";
        $ok = false;
    }

    if ($ok) {
        echo "PASS $target\n";
        $pass++;
    } else {
        $fail++;
    }
}

stop_server($proc);
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
PASS chrome136
PASS chrome142
PASS edge99
PASS edge101
PASS safari15_3
PASS safari15_5
PASS safari17_0
PASS safari17_2_ios
PASS safari18_0
PASS safari18_0_ios
PASS safari18_4
PASS firefox133
PASS firefox135
PASS firefox144

Results: 26 passed, 0 failed out of 26 targets
ALL TESTS PASSED
