<?php declare(strict_types=1);

/**
 * Shared test infrastructure for impersonation fingerprint tests.
 * Provides TLS server management, nghttpd management, signature loading,
 * nghttpd log parsing, and fingerprint comparison utilities.
 */

// GREASE values per RFC 8701
define('GREASE_VALUES', [
    0x0A0A, 0x1A1A, 0x2A2A, 0x3A3A, 0x4A4A, 0x5A5A, 0x6A6A, 0x7A7A,
    0x8A8A, 0x9A9A, 0xAAAA, 0xBABA, 0xCACA, 0xDADA, 0xEAEA, 0xFAFA,
]);

// Valid HTTP/2 SETTINGS keys (RFC 7540 + extensions)
// 1-6: RFC 7540, 8: RFC 8441 (CONNECT protocol), 9: SETTINGS_ENABLE_METADATA
define('VALID_H2_SETTINGS', [1, 2, 3, 4, 5, 6, 8, 9]);

/**
 * Start the Go TLS inspection server.
 * Returns [proc_handle, port, pipes].
 */
function start_tls_server(): array {
    $binary = __DIR__ . '/bin/tls-server';
    if (!file_exists($binary)) {
        echo "SKIP TLS server binary not found at $binary\n";
        exit(0);
    }

    $proc = proc_open(
        [$binary],
        [1 => ['pipe', 'w'], 2 => ['pipe', 'w']],
        $pipes,
        __DIR__
    );

    if (!$proc) {
        echo "Failed to start TLS server\n";
        exit(1);
    }

    // Read READY:<port> from stdout
    $line = fgets($pipes[1], 256);
    if (!preg_match('/READY:(\d+)/', $line, $m)) {
        echo "TLS server did not output READY message: $line\n";
        proc_terminate($proc);
        exit(1);
    }

    $port = (int)$m[1];

    register_shutdown_function(function($proc) {
        proc_terminate($proc);
        for ($i = 0; $i < 20; $i++) {
            $s = proc_get_status($proc);
            if (!$s['running']) break;
            usleep(50000);
        }
    }, $proc);

    return [$proc, $port, $pipes];
}

/**
 * Start nghttpd HTTP/2 server on the given port.
 * Returns [proc_handle, stderr_file_path].
 */
function start_nghttpd(int $port): array {
    $key = __DIR__ . '/ssl/server.key';
    $crt = __DIR__ . '/ssl/server.crt';

    if (!file_exists($key) || !file_exists($crt)) {
        echo "SKIP SSL certs not found\n";
        exit(0);
    }

    $stdout_file = tempnam(sys_get_temp_dir(), 'nghttpd_');

    // nghttpd -v writes verbose frame info to STDOUT (not stderr)
    $proc = proc_open(
        "exec nghttpd -v $port $key $crt >" . escapeshellarg($stdout_file) . " 2>&1",
        [0 => ['pipe', 'r']],
        $pipes,
        __DIR__
    );

    if (!$proc) {
        echo "Failed to start nghttpd\n";
        exit(1);
    }

    // Wait for nghttpd to be ready
    usleep(500000);
    $status = proc_get_status($proc);
    if (!$status['running']) {
        echo "nghttpd not running. Log: " . file_get_contents($stdout_file) . "\n";
        exit(1);
    }

    register_shutdown_function(function($proc, $file) {
        proc_terminate($proc);
        for ($i = 0; $i < 20; $i++) {
            $s = proc_get_status($proc);
            if (!$s['running']) break;
            usleep(50000);
        }
        @unlink($file);
    }, $proc, $stdout_file);

    return [$proc, $stdout_file];
}

/**
 * Stop a server process.
 */
function stop_server($proc): void {
    proc_terminate($proc);
    for ($i = 0; $i < 20; $i++) {
        $s = proc_get_status($proc);
        if (!$s['running']) break;
        usleep(50000);
    }
}

/**
 * Read nghttpd log after request completes.
 */
function read_nghttpd_log(string $log_file): string {
    usleep(300000); // Give nghttpd time to flush
    return file_get_contents($log_file);
}

/**
 * Parse nghttpd verbose log output.
 * Returns: ['settings' => [...], 'window_update' => int, 'headers' => [...],
 *           'pseudo_headers' => [...], 'priority' => [...]]
 *
 * Ports upstream th1.http2.parser.parse_nghttpd_log()
 */
function parse_nghttpd_log(string $output): array {
    $result = [
        'settings' => [],
        'window_update' => null,
        'headers' => [],
        'pseudo_headers' => [],
        'priority' => null,
    ];

    $lines = explode("\n", $output);
    $i = 0;
    $count = count($lines);
    $header_lines = []; // Collect header lines that appear before HEADERS frame

    while ($i < $count) {
        $line = $lines[$i];

        // Collect recv header lines (they appear before HEADERS frame announcement)
        if (preg_match('/recv \(stream_id=(\d+)\) (.+)/', $line, $m)) {
            $header_lines[] = $m[2];
        }

        // Look for frame announcements
        if (preg_match('/\[id=\d+\].*recv ([A-Z_]+) frame.*stream_id=(\d+)/', $line, $m)) {
            $frame_type = $m[1];
            $stream_id = (int)$m[2];

            switch ($frame_type) {
                case 'SETTINGS':
                    // Parse (niv=N)
                    $i++;
                    $niv = 0;
                    while ($i < $count) {
                        if (preg_match('/\(niv=(\d+)\)/', $lines[$i], $nm)) {
                            $niv = (int)$nm[1];
                            break;
                        }
                        $i++;
                    }
                    // Parse N settings
                    for ($j = 0; $j < $niv && $i + 1 < $count; $j++) {
                        $i++;
                        if (preg_match('/\[[A-Z0-9_]+\(0x([0-9a-fA-F]+)\):(\d+)\]/', $lines[$i], $sm)) {
                            $key = intval($sm[1], 16);
                            $value = (int)$sm[2];
                            $result['settings'][] = ['key' => $key, 'value' => $value];
                        }
                    }
                    break;

                case 'WINDOW_UPDATE':
                    $i++;
                    if ($i < $count && preg_match('/\(window_size_increment=(\d+)\)/', $lines[$i], $wm)) {
                        $result['window_update'] = (int)$wm[1];
                    }
                    break;

                case 'HEADERS':
                    // Parse pseudo-headers and headers from collected lines
                    foreach ($header_lines as $hl) {
                        if (str_starts_with($hl, ':')) {
                            // Pseudo-header: extract just the name
                            if (preg_match('/^(:\w+):/', $hl, $pm)) {
                                $result['pseudo_headers'][] = $pm[1];
                            }
                        } else {
                            $result['headers'][] = $hl;
                        }
                    }

                    // Check for PRIORITY info on this frame
                    if (preg_match('/dep_stream_id=(\d+).*weight=(\d+)/', $line, $pm) ||
                        ($i + 1 < $count && preg_match('/dep_stream_id=(\d+).*weight=(\d+)/', $lines[$i + 1], $pm))) {
                        $result['priority'] = [
                            'dep_stream_id' => (int)$pm[1],
                            'weight' => (int)$pm[2],
                        ];
                    }

                    // Stop parsing after first HEADERS frame
                    return $result;
            }
        }

        $i++;
    }

    return $result;
}

/**
 * Load target mapping.
 */
function get_target_map(): array {
    $file = __DIR__ . '/signatures/targets.json';
    if (!file_exists($file)) {
        echo "SKIP targets.json not found\n";
        exit(0);
    }
    return json_decode(file_get_contents($file), true);
}

/**
 * Load signature for a target.
 */
function load_signature(string $target): ?array {
    $map = get_target_map();
    if (!isset($map[$target])) {
        return null;
    }
    $file = __DIR__ . '/signatures/' . $map[$target] . '.json';
    if (!file_exists($file)) {
        return null;
    }
    return json_decode(file_get_contents($file), true);
}

/**
 * Replace GREASE values with "GREASE" string.
 */
function normalize_grease(array $values): array {
    return array_map(function($v) {
        if (is_int($v) && in_array($v, GREASE_VALUES)) {
            return 'GREASE';
        }
        return $v;
    }, $values);
}

/**
 * Normalize H2 SETTINGS - replace non-standard keys with GREASE.
 */
function normalize_h2_settings(array $settings): array {
    return array_map(function($s) {
        if (!in_array($s['key'], VALID_H2_SETTINGS)) {
            return ['key' => 'GREASE', 'value' => 'GREASE'];
        }
        return $s;
    }, $settings);
}

/**
 * Compute Akamai H2 fingerprint text.
 * Format: "key:val;key:val|window_update|priority_info|pseudo_initials"
 */
function compute_akamai_text(array $parsed): string {
    // SETTINGS pairs
    $settings_parts = [];
    foreach ($parsed['settings'] as $s) {
        if ($s['key'] === 'GREASE' || (is_int($s['key']) && !in_array($s['key'], VALID_H2_SETTINGS))) {
            continue; // Skip GREASE settings for Akamai
        }
        $settings_parts[] = $s['key'] . ':' . $s['value'];
    }
    $settings_str = implode(';', $settings_parts);

    // WINDOW_UPDATE
    $wu = $parsed['window_update'] ?? 0;

    // Priority - stream dependency
    $priority = 0; // Default

    // Pseudo-header initials
    $initials = [];
    foreach ($parsed['pseudo_headers'] as $ph) {
        // :method -> m, :authority -> a, :scheme -> s, :path -> p
        $initials[] = substr($ph, 1, 1);
    }
    $pseudo_str = implode(',', $initials);

    return "$settings_str|$wu|$priority|$pseudo_str";
}

/**
 * Compute Akamai H2 fingerprint hash.
 */
function compute_akamai_hash(array $parsed): string {
    return md5(compute_akamai_text($parsed));
}

// Extensions that legitimately differ in local testing:
// - 0 (server_name/SNI): not sent when connecting to IP addresses
// - 41 (pre_shared_key): only sent on session resumption
// - 21 (padding): varies based on ClientHello message size
define('SKIP_EXTENSIONS_LOCAL', [0, 41, 21]);

/**
 * Compare extension IDs from TLS server against expected signature.
 * If tls_permute_extensions is true, compare as sorted sets.
 * Returns [bool $match, string $reason].
 */
function compare_extensions(array $actual, array $expected, bool $permute): array {
    $actual_norm = normalize_grease($actual);
    $expected_norm = $expected; // Already contains "GREASE" strings from YAML

    // Filter out extensions that legitimately differ in local testing
    $filter = fn($v) => $v !== 'GREASE' && !in_array($v, SKIP_EXTENSIONS_LOCAL);

    if (!$permute) {
        // Strict order comparison (after removing skipped extensions)
        $actual_filtered = array_values(array_filter($actual_norm, $filter));
        $expected_filtered = array_values(array_filter($expected_norm, $filter));
        if ($actual_filtered != $expected_filtered) {
            return [false, sprintf(
                "Extension order mismatch.\nExpected: %s\nActual:   %s",
                json_encode($expected_filtered),
                json_encode($actual_filtered)
            )];
        }
        return [true, ''];
    }

    // Permuted: compare as sets (excluding GREASE and skipped extensions)
    $actual_set = array_values(array_filter($actual_norm, $filter));
    $expected_set = array_values(array_filter($expected_norm, $filter));

    sort($actual_set);
    sort($expected_set);

    if ($actual_set != $expected_set) {
        return [false, sprintf(
            "Extension set mismatch (permuted mode).\nExpected: %s\nActual:   %s",
            json_encode($expected_set),
            json_encode($actual_set)
        )];
    }

    return [true, ''];
}

/**
 * Extract extension type IDs from a signature's tls_client_hello.extensions array.
 * Maps extension type names to numeric IDs.
 */
function get_expected_extension_ids(array $extensions): array {
    static $name_to_id = [
        'server_name' => 0,
        'status_request' => 5,
        'supported_groups' => 10,
        'ec_point_formats' => 11,
        'signature_algorithms' => 13,
        'application_layer_protocol_negotiation' => 16,
        'signed_certificate_timestamp' => 18,
        'padding' => 21,
        'encrypt_then_mac' => 22,
        'extended_master_secret' => 23,
        'compress_certificate' => 27,
        'session_ticket' => 35,
        'supported_versions' => 43,
        'psk_key_exchange_modes' => 45,
        'post_handshake_auth' => 49,
        'signature_algorithms_cert' => 50,
        'keyshare' => 51,
        'key_share' => 51,
        'renegotiation_info' => 65281,
        'application_settings' => 17513,
        'encrypted_client_hello' => 65037,
        'extensionencryptedclienthello' => 65037,
        // Less common
        'delegated_credentials' => 34,
        'record_size_limit' => 28,
        'pre_shared_key' => 41,
    ];

    $ids = [];
    foreach ($extensions as $ext) {
        $type = $ext['type'] ?? '';
        if ($type === 'GREASE') {
            $ids[] = 'GREASE';
        } elseif (is_numeric($type)) {
            $ids[] = (int)$type;
        } elseif (isset($name_to_id[strtolower($type)])) {
            $ids[] = $name_to_id[strtolower($type)];
        } else {
            // Try to extract from type field as-is
            $ids[] = $type;
        }
    }
    return $ids;
}

/**
 * Make an impersonated request to a URL.
 * Returns the response body string or false.
 */
function impersonate_request(string $url, string $target, bool $default_headers = true, array $extra_headers = []): string|false {
    $ch = curl_cffi_init($url);
    curl_cffi_impersonate($ch, $target, $default_headers);
    curl_cffi_setopt($ch, CURLOPT_RETURNTRANSFER, true);
    curl_cffi_setopt($ch, CURLOPT_SSL_VERIFYPEER, false);
    curl_cffi_setopt($ch, CURLOPT_SSL_VERIFYHOST, 0);
    curl_cffi_setopt($ch, CURLOPT_TIMEOUT, 10);
    if (!empty($extra_headers)) {
        curl_cffi_setopt($ch, CURLOPT_HTTPHEADER, $extra_headers);
    }
    $result = curl_cffi_exec($ch);
    $errno = curl_cffi_errno($ch);
    if ($errno !== 0) {
        echo "  curl error $errno: " . curl_cffi_error($ch) . "\n";
    }
    curl_cffi_close($ch);
    return $result;
}
