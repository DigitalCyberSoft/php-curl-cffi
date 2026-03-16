--TEST--
Impersonation: TLS JA3 hash matches expected fingerprint
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
$skip = 0;

foreach ($targets as $target => $sig_name) {
    $sig = load_signature($target);
    if (!$sig) {
        echo "SKIP $target: signature not found\n";
        $skip++;
        continue;
    }

    $expected_hash = $sig['third_party']['ja3_hash'] ?? null;
    if (!$expected_hash) {
        echo "SKIP $target: no ja3_hash in signature\n";
        $skip++;
        continue;
    }

    $result = impersonate_request("https://127.0.0.1:$port/", $target);
    if ($result === false) {
        echo "FAIL $target: request failed\n";
        $fail++;
        continue;
    }

    $data = json_decode($result, true);
    $actual_hash = $data['ja3_hash'] ?? '';

    // JA3 hashes may differ due to GREASE randomization (different random GREASE values each time)
    // For targets with tls_permute_extensions, JA3 will also differ due to extension reordering
    // So we only compare JA3 for targets without permutation AND with stable GREASE
    $permute = $sig['signature']['options']['tls_permute_extensions'] ?? false;

    // JA3 comparison strategy:
    // - GREASE values are random, so strip them from JA3 text
    // - Extension 0 (SNI) is absent when connecting to IP addresses
    // - For permuted targets, extension order changes each request
    // Compare by stripping GREASE values and SNI from both texts

    $actual_text = $data['ja3_text'] ?? '';
    $expected_text = $sig['third_party']['ja3_text'] ?? '';

    // Strip GREASE values from JA3 text fields (ciphers, extensions, groups)
    // GREASE appears as decimal numbers in hyphen-separated lists
    $grease_decimals = array_map(fn($g) => (string)$g, GREASE_VALUES);

    $strip_grease = function(string $ja3) use ($grease_decimals): string {
        $parts = explode(',', $ja3);
        // Fields 1-4 are hyphen-separated lists (ciphers, extensions, groups, points)
        for ($i = 1; $i <= 4 && $i < count($parts); $i++) {
            $vals = explode('-', $parts[$i]);
            $vals = array_filter($vals, fn($v) => !in_array($v, $grease_decimals));
            $parts[$i] = implode('-', array_values($vals));
        }
        return implode(',', $parts);
    };

    // Strip extensions that legitimately differ in local testing:
    // 0=SNI (not sent for IP), 21=padding (size-dependent), 41=pre_shared_key (session-dependent)
    $strip_local_exts = function(string $ja3) use ($grease_decimals): string {
        $skip_exts = ['0', '21', '41'];
        $parts = explode(',', $ja3);
        if (count($parts) >= 3) {
            $exts = explode('-', $parts[2]);
            $exts = array_filter($exts, fn($v) => !in_array($v, $skip_exts));
            $parts[2] = implode('-', array_values($exts));
        }
        return implode(',', $parts);
    };

    $norm_actual = $strip_local_exts($strip_grease($actual_text));
    $norm_expected = $strip_local_exts($strip_grease($expected_text));

    if ($permute) {
        // With permutation, extension order changes - compare as sorted sets
        $pa = explode(',', $norm_actual);
        $pe = explode(',', $norm_expected);
        $match = true;
        for ($i = 0; $i < min(count($pa), count($pe)); $i++) {
            if ($i == 2) {
                // Extensions field: compare as sets
                $a_exts = explode('-', $pa[$i]);
                $e_exts = explode('-', $pe[$i]);
                sort($a_exts);
                sort($e_exts);
                if ($a_exts != $e_exts) { $match = false; break; }
            } else {
                if ($pa[$i] !== $pe[$i]) { $match = false; break; }
            }
        }
        if ($match) {
            echo "PASS $target (permuted, structure matches)\n";
            $pass++;
        } else {
            echo "FAIL $target: JA3 structure mismatch (permuted)\n";
            echo "  Expected: $norm_expected\n";
            echo "  Actual:   $norm_actual\n";
            $fail++;
        }
    } else {
        if ($norm_actual === $norm_expected) {
            echo "PASS $target (ja3 structure matches)\n";
            $pass++;
        } else {
            echo "FAIL $target: JA3 mismatch\n";
            echo "  Expected (norm): $norm_expected\n";
            echo "  Actual (norm):   $norm_actual\n";
            $fail++;
        }
    }
}

stop_server($proc);
echo "\nResults: $pass passed, $fail failed, $skip skipped\n";
if ($fail === 0) echo "ALL TESTS PASSED\n";
?>
--EXPECTF--
%aPASS %s
%a
ALL TESTS PASSED
