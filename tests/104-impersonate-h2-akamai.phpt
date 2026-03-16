--TEST--
Impersonation: Akamai HTTP/2 fingerprint hash
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
$skip = 0;
$port = 18700;

foreach ($targets as $target => $sig_name) {
    $sig = load_signature($target);
    if (!$sig) {
        echo "SKIP $target: signature not found\n";
        $skip++;
        continue;
    }

    $expected_hash = $sig['third_party']['akamai_hash'] ?? null;
    $expected_text = $sig['third_party']['akamai_text'] ?? null;
    if (!$expected_hash) {
        echo "SKIP $target: no akamai_hash in signature\n";
        $skip++;
        continue;
    }

    $port++;
    [$proc, $stderr_file] = start_nghttpd($port);

    $result = impersonate_request("https://127.0.0.1:$port/", $target);
    $log = read_nghttpd_log($stderr_file);
    $parsed = parse_nghttpd_log($log);
    stop_server($proc);

    $actual_text = compute_akamai_text($parsed);
    $actual_hash = md5($actual_text);

    if ($actual_hash === $expected_hash) {
        echo "PASS $target (akamai: $actual_hash)\n";
        $pass++;
    } else {
        echo "FAIL $target: Akamai hash mismatch\n";
        echo "  Expected text: $expected_text\n";
        echo "  Actual text:   $actual_text\n";
        echo "  Expected hash: $expected_hash\n";
        echo "  Actual hash:   $actual_hash\n";
        $fail++;
    }
}

echo "\nResults: $pass passed, $fail failed, $skip skipped\n";
if ($fail === 0) echo "ALL TESTS PASSED\n";
?>
--EXPECTF--
%aPASS %s
%a
ALL TESTS PASSED
