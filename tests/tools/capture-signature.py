#!/usr/bin/env python3
"""
Capture TLS and HTTP/2 signatures from real browsers.

Launches a Go TLS inspection server and nghttpd, drives the browser to connect
to both, then parses the captured data into upstream-compatible YAML signature
files matching the format used by curl-impersonate.

Requirements:
  - Go TLS server built: tests/bin/tls-server
  - nghttpd installed (nghttp2 package)
  - PyYAML: pip install pyyaml
  - For Firefox: certutil (nss-tools package)
  - CA cert pair at /tmp/ca.crt + /tmp/ca.key (generated if missing)
  - Server cert at /tmp/srv.crt + /tmp/srv.key (generated if missing)

Requirements:
  - xvfb-run (xorg-x11-server-Xvfb package)
  - Go TLS server built: tests/bin/tls-server
  - nghttpd (nghttp2 package)
  - For Firefox: certutil (nss-tools package)

Usage:
    # Capture all installed browsers
    python3 tests/tools/capture-signature.py

    # Capture specific browser
    python3 tests/tools/capture-signature.py --browser chrome

    # Custom output directory
    python3 tests/tools/capture-signature.py --output-dir /tmp/signatures
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

try:
    import yaml
    HAS_YAML = True
except ImportError:
    HAS_YAML = False
    print("WARNING: PyYAML not installed. Output will be JSON instead of YAML.", file=sys.stderr)
    print("  Install with: pip install pyyaml", file=sys.stderr)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TEST_DIR = os.path.dirname(SCRIPT_DIR)
PROJECT_DIR = os.path.dirname(TEST_DIR)
TLS_SERVER = os.path.join(TEST_DIR, "bin", "tls-server")
SSL_KEY = "/tmp/srv.key"
SSL_CRT = "/tmp/srv.crt"
CA_KEY = "/tmp/ca.key"
CA_CRT = "/tmp/ca.crt"

VALID_H2_SETTINGS = {1, 2, 3, 4, 5, 6, 8, 9}

BROWSERS = {
    "chrome": {
        "bin": "google-chrome",
        "name": "chrome",
        "permute": True,
        "chrome_based": True,
    },
    "edge": {
        "bin": "microsoft-edge",
        "alt_bins": ["microsoft-edge-stable"],
        "name": "edge",
        "permute": True,
        "chrome_based": True,
    },
    "firefox": {
        "bin": "firefox",
        "name": "firefox",
        "permute": False,
        "chrome_based": False,
    },
}


def find_binary(info):
    """Find the browser binary."""
    bins = [info["bin"]] + info.get("alt_bins", [])
    for b in bins:
        path = shutil.which(b)
        if path:
            return path
    return None


def get_version(binary):
    """Get browser version string."""
    out = subprocess.check_output([binary, "--version"], text=True, timeout=10)
    m = re.search(r"([\d.]+)", out)
    return m.group(1) if m else "unknown"


def ensure_certs():
    """Generate CA and server certs if they don't exist."""
    if not os.path.exists(CA_CRT) or not os.path.exists(CA_KEY):
        print("Generating CA certificate...")
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "ec",
            "-pkeyopt", "ec_paramgen_curve:prime256v1",
            "-keyout", CA_KEY, "-out", CA_CRT,
            "-days", "3650", "-nodes", "-subj", "/CN=Test CA",
        ], check=True, capture_output=True)

    if not os.path.exists(SSL_CRT) or not os.path.exists(SSL_KEY):
        print("Generating server certificate...")
        csr = tempfile.NamedTemporaryFile(suffix=".csr", delete=False)
        csr.close()
        subprocess.run([
            "openssl", "req", "-newkey", "ec",
            "-pkeyopt", "ec_paramgen_curve:prime256v1",
            "-keyout", SSL_KEY, "-out", csr.name, "-nodes",
            "-subj", "/CN=localhost",
            "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
        ], check=True, capture_output=True)
        subprocess.run([
            "openssl", "x509", "-req", "-in", csr.name,
            "-CA", CA_CRT, "-CAkey", CA_KEY, "-CAcreateserial",
            "-out", SSL_CRT, "-days", "3650",
            "-extfile", "/dev/stdin",
        ], input=b"subjectAltName=DNS:localhost,IP:127.0.0.1",
           check=True, capture_output=True)
        os.unlink(csr.name)


def start_tls_server(use_ca_cert=False):
    """Start the Go TLS inspection server. Returns (proc, port)."""
    cmd = [TLS_SERVER]
    if use_ca_cert:
        cmd += [SSL_CRT, SSL_KEY]

    stderr_file = tempfile.NamedTemporaryFile(prefix="tls_", suffix=".log", delete=False, mode="w")
    stdout_file = tempfile.NamedTemporaryFile(prefix="tls_stdout_", suffix=".log", delete=False, mode="w")

    proc = subprocess.Popen(cmd, stdout=stdout_file, stderr=stderr_file)
    time.sleep(1)

    stdout_file.close()
    with open(stdout_file.name) as f:
        line = f.read()
    m = re.search(r"READY:(\d+)", line)
    if not m:
        proc.kill()
        raise RuntimeError(f"TLS server failed to start. stdout: {line}")

    return proc, int(m.group(1)), stderr_file.name, stdout_file.name


def start_nghttpd(port):
    """Start nghttpd HTTP/2 server. Returns (proc, log_path)."""
    log_file = tempfile.NamedTemporaryFile(prefix="h2_", suffix=".log", delete=False, mode="w")
    proc = subprocess.Popen(
        f"exec nghttpd -v {port} {SSL_KEY} {SSL_CRT}",
        shell=True, stdout=log_file, stderr=subprocess.STDOUT,
    )
    time.sleep(1)
    return proc, log_file.name


def run_browser(binary, url, info, profile_dir=None, timeout=8):
    """Launch browser in Xvfb virtual display, wait, then kill it.

    Always runs headed (not --headless) for accurate fingerprints,
    but inside Xvfb so no real display is needed and no interference
    with the user's running browsers.
    """
    browser_cmd = [binary]

    if info["chrome_based"]:
        browser_cmd += [
            "--no-sandbox", "--ignore-certificate-errors",
            "--disable-extensions", "--disable-background-networking",
            "--disable-sync", "--no-first-run", "--no-default-browser-check",
        ]
        if profile_dir:
            browser_cmd += [f"--user-data-dir={profile_dir}"]
    else:
        # Firefox
        if profile_dir:
            browser_cmd += ["--profile", profile_dir, "--no-remote"]

    browser_cmd.append(url)

    # Wrap in xvfb-run for a clean virtual display per invocation
    cmd = [
        "xvfb-run", "--auto-servernum",
        "--server-args=-screen 0 1920x1080x24",
    ] + browser_cmd

    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(timeout)

    # Kill by profile dir to avoid hitting user's running browser
    if profile_dir:
        subprocess.run(["pkill", "-f", profile_dir], capture_output=True)
        time.sleep(1)
    else:
        proc.terminate()

    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


def parse_h2_log(path):
    """Parse nghttpd verbose log into structured data."""
    with open(path) as f:
        content = f.read()

    result = {"settings": [], "window_update": None, "pseudo_headers": [], "headers": []}
    lines = content.split("\n")
    i = 0
    first_headers_stream = None
    header_lines = {}

    while i < len(lines):
        line = lines[i]

        m = re.search(r"recv \(stream_id=(\d+)\) (.+)", line)
        if m:
            sid = int(m.group(1))
            header_lines.setdefault(sid, []).append(m.group(2))

        m = re.search(r"\[id=\d+\].*recv ([A-Z_]+) frame.*stream_id=(\d+)", line)
        if m:
            ft, sid = m.group(1), int(m.group(2))

            if ft == "SETTINGS" and sid == 0 and not result["settings"]:
                i += 1
                niv = 0
                while i < len(lines):
                    nm = re.search(r"\(niv=(\d+)\)", lines[i])
                    if nm:
                        niv = int(nm.group(1))
                        break
                    i += 1
                for _ in range(niv):
                    i += 1
                    if i < len(lines):
                        sm = re.search(r"\[[A-Z0-9_]+\(0x([0-9a-fA-F]+)\):(\d+)\]", lines[i])
                        if sm:
                            result["settings"].append({
                                "key": int(sm.group(1), 16),
                                "value": int(sm.group(2)),
                            })

            elif ft == "WINDOW_UPDATE" and sid == 0 and result["window_update"] is None:
                i += 1
                if i < len(lines):
                    wm = re.search(r"\(window_size_increment=(\d+)\)", lines[i])
                    if wm:
                        result["window_update"] = int(wm.group(1))

            elif ft == "HEADERS" and first_headers_stream is None and sid > 0:
                first_headers_stream = sid
                for hl in header_lines.get(sid, []):
                    if hl.startswith(":"):
                        pm = re.match(r"(:\w+):", hl)
                        if pm:
                            result["pseudo_headers"].append(pm.group(1))
                    else:
                        result["headers"].append(hl)
                return result
        i += 1

    return result


def build_signature(name, version, tls_data, h2_data, permute):
    """Build a full upstream-format signature dict."""
    sig = {
        "browser": {"name": name, "version": version, "os": "linux"},
        "signature": {
            "options": {},
            "http2": {"frames": []},
            "tls_client_hello": {
                "ciphersuites": tls_data["cipher_suites"],
                "comp_methods": tls_data["comp_methods"],
                "extensions": tls_data["extension_details"],
                "handshake_version": tls_data["handshake_version"],
                "record_version": tls_data["record_version"],
                "session_id_length": tls_data["session_id_length"],
            },
        },
        "third_party": {
            "ja3_hash": tls_data["ja3_hash"],
            "ja3_text": tls_data["ja3_text"],
        },
    }

    if permute:
        sig["signature"]["options"]["tls_permute_extensions"] = True

    # H2 frames
    if h2_data["settings"]:
        sig["signature"]["http2"]["frames"].append({
            "frame_type": "SETTINGS", "stream_id": 0,
            "settings": h2_data["settings"],
        })
    if h2_data["window_update"] is not None:
        sig["signature"]["http2"]["frames"].append({
            "frame_type": "WINDOW_UPDATE", "stream_id": 0,
            "window_size_increment": h2_data["window_update"],
        })
    if h2_data["pseudo_headers"]:
        sig["signature"]["http2"]["frames"].append({
            "frame_type": "HEADERS", "stream_id": 1,
            "pseudo_headers": h2_data["pseudo_headers"],
            "headers": h2_data["headers"],
        })

    # User-Agent from H2 headers
    ua = ""
    for h in h2_data.get("headers", []):
        if h.lower().startswith("user-agent:"):
            ua = h.split(":", 1)[1].strip()
            break
    sig["third_party"]["user_agent"] = ua

    # Akamai fingerprint
    s_str = ";".join(
        f"{s['key']}:{s['value']}" for s in h2_data["settings"]
        if s["key"] in VALID_H2_SETTINGS
    )
    wu = h2_data.get("window_update") or 0
    pi = ",".join(p[1] for p in h2_data.get("pseudo_headers", []))
    akamai_text = f"{s_str}|{wu}|0|{pi}"
    sig["third_party"]["akamai_text"] = akamai_text
    sig["third_party"]["akamai_hash"] = hashlib.md5(akamai_text.encode()).hexdigest()

    return sig


def capture_browser(browser_key, info, output_dir=None, h2_port_base=19990):
    """Capture TLS and HTTP/2 signatures for one browser."""
    binary = find_binary(info)
    if not binary:
        print(f"  SKIP: {info['bin']} not found")
        return None

    version = get_version(binary)
    print(f"\n{'=' * 60}")
    print(f"  {info['name']} {version} (Xvfb)")
    print(f"{'=' * 60}")

    profile_dir = tempfile.mkdtemp(prefix=f"sig_{browser_key}_")

    # Firefox needs CA cert in NSS database
    if not info["chrome_based"]:
        if shutil.which("certutil"):
            subprocess.run(
                ["certutil", "-N", "-d", f"sql:{profile_dir}", "--empty-password"],
                capture_output=True,
            )
            subprocess.run(
                ["certutil", "-A", "-d", f"sql:{profile_dir}",
                 "-t", "CT,C,C", "-n", "TestCA", "-i", CA_CRT],
                capture_output=True,
            )
        else:
            print("  WARNING: certutil not found, Firefox may reject certs")

    # --- TLS capture ---
    print("  Capturing TLS ClientHello...")
    use_ca = not info["chrome_based"]  # Firefox needs CA cert, Chrome ignores cert errors
    tls_proc, tls_port, tls_err, tls_out = start_tls_server(use_ca_cert=use_ca)

    host = "localhost" if not info["chrome_based"] else "127.0.0.1"
    run_browser(binary, f"https://{host}:{tls_port}/", info,
                profile_dir=profile_dir, timeout=8)

    tls_proc.terminate()
    tls_proc.wait()

    # Parse TLS data from CAPTURED log lines
    tls_data = None
    with open(tls_err) as f:
        for line in f:
            if line.startswith("CAPTURED:"):
                tls_data = json.loads(line[9:])
                break
    os.unlink(tls_err)
    os.unlink(tls_out)

    if not tls_data:
        print("  ERROR: No TLS data captured")
        shutil.rmtree(profile_dir, ignore_errors=True)
        return None

    print(f"  TLS: {len(tls_data['cipher_suites'])} ciphers, {len(tls_data['extension_details'])} extensions")

    # --- H2 capture ---
    print("  Capturing HTTP/2 frames...")
    h2_port = h2_port_base + hash(browser_key) % 100
    h2_proc, h2_log = start_nghttpd(h2_port)

    run_browser(binary, f"https://{host}:{h2_port}/", info,
                profile_dir=profile_dir, timeout=8)

    time.sleep(1)
    h2_proc.terminate()
    h2_proc.wait()

    h2_data = parse_h2_log(h2_log)
    os.unlink(h2_log)
    shutil.rmtree(profile_dir, ignore_errors=True)

    print(f"  H2:  {len(h2_data['settings'])} settings, {len(h2_data.get('headers', []))} headers")
    print(f"  H2:  pseudo={h2_data.get('pseudo_headers')}")

    # Build signature
    sig = build_signature(info["name"], version, tls_data, h2_data, info["permute"])

    print(f"  UA:  {sig['third_party']['user_agent'][:80]}")
    print(f"  JA3: {sig['third_party']['ja3_hash']}")
    print(f"  Akamai: {sig['third_party']['akamai_hash']}")

    # Save
    if output_dir is None:
        output_dir = os.path.join(
            PROJECT_DIR, "curl-impersonate", "tests", "signatures"
        )
    os.makedirs(output_dir, exist_ok=True)

    filename = f"{info['name']}_{version}_linux"
    if HAS_YAML:
        outpath = os.path.join(output_dir, f"{filename}.yaml")
        with open(outpath, "w") as f:
            yaml.dump(sig, f, default_flow_style=False, sort_keys=False, allow_unicode=True)
    else:
        outpath = os.path.join(output_dir, f"{filename}.json")
        with open(outpath, "w") as f:
            json.dump(sig, f, indent=2)

    print(f"  Saved: {outpath}")
    return sig


def main():
    parser = argparse.ArgumentParser(
        description="Capture browser TLS/HTTP2 signatures for curl-impersonate"
    )
    parser.add_argument(
        "--browser", "-b", choices=list(BROWSERS.keys()),
        help="Capture specific browser (default: all installed)",
    )
    parser.add_argument(
        "--output-dir", "-o",
        help="Output directory for YAML files (default: curl-impersonate/tests/signatures/)",
    )
    args = parser.parse_args()

    # Preflight checks
    if not os.path.exists(TLS_SERVER):
        print(f"ERROR: TLS server not built at {TLS_SERVER}")
        print(f"  Run: cd tests/tls-server && go build -o ../bin/tls-server .")
        sys.exit(1)

    if not shutil.which("nghttpd"):
        print("ERROR: nghttpd not found. Install nghttp2 package.")
        sys.exit(1)

    if not shutil.which("xvfb-run"):
        print("ERROR: xvfb-run not found. Install xorg-x11-server-Xvfb.")
        sys.exit(1)

    ensure_certs()

    browsers_to_capture = (
        {args.browser: BROWSERS[args.browser]} if args.browser
        else BROWSERS
    )

    results = {}
    for key, info in browsers_to_capture.items():
        sig = capture_browser(
            key, info,
            output_dir=args.output_dir,
        )
        if sig:
            results[key] = sig

    print(f"\n{'=' * 60}")
    print(f"Captured {len(results)} browser(s)")
    print(f"{'=' * 60}")
    for key, sig in results.items():
        ver = sig["browser"]["version"]
        ua = sig["third_party"]["user_agent"][:60]
        print(f"  {key} {ver}: {ua}")


if __name__ == "__main__":
    main()
