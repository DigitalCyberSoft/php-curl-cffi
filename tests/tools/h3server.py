#!/usr/bin/env python3
"""
H2+H3 capture server with wire-order headers and TLS ClientHello fingerprint.

Captures the complete browser fingerprint: TLS ClientHello (cipher suites,
extensions, groups, sig algs, key shares) and HTTP headers in exact wire
order for both HTTP/2 (via h2 library) and HTTP/3 (via aioquic).

Renders protocol + full debug data on the page for visual verification.
Logs each request as one JSON object per line to stdout.

Port layout:
  PORT     TLS/TCP  HTTP/2 (h2 library, wire-order headers, ssl msg_callback TLS)
  PORT+2   QUIC/UDP HTTP/3 (aioquic, wire-order headers, patched ClientHello TLS)

Requirements:
  pip install h2 aioquic

Usage:
  python3 h3server.py --cert fullchain.pem --key privkey.pem [--port 4511]

  Then visit https://hostname:PORT/ for H2 (red page).
  H2 response includes Alt-Svc header advertising H3 on PORT+2.
  Subsequent visits (new tab) upgrade to H3 (green page).
  Force H3: chrome --origin-to-force-quic-on=hostname:PORT+2

Capture output (stdout, one JSON per line):
  {
    "protocol": "HTTP/2" | "HTTP/3",
    "pseudo_headers": [":method", ":authority", ...],
    "headers": ["sec-ch-ua: ...", "user-agent: ...", ...],
    "user_agent": "Mozilla/5.0 ...",
    "tls": {
      "cipher_suites": ["GREASE", 4865, ...],
      "extensions": ["GREASE", 51, 13, ...],
      "supported_groups": ["GREASE", 4588, 29, ...],
      "signature_algorithms": [1027, 2052, ...],
      "supported_versions": ["GREASE", 772, 771],
      "key_shares": [{"group": "GREASE", "length": 1}, ...],
      "alpn": ["h2", "http/1.1"],
      ...
    }
  }
"""

import argparse
import asyncio
import json
import ssl
import struct
import sys
import time
from typing import Optional

# ================================================================
# MUST patch aioquic TLS BEFORE other aioquic imports.
# This captures the QUIC ClientHello for HTTP/3 connections.
# ================================================================
from aioquic.tls import Context as _TlsCtx, pull_client_hello as _pull_ch
from aioquic.buffer import Buffer as _AioBuf

GREASE = {0x0A0A, 0x1A1A, 0x2A2A, 0x3A3A, 0x4A4A, 0x5A5A, 0x6A6A, 0x7A7A,
          0x8A8A, 0x9A9A, 0xAAAA, 0xBABA, 0xCACA, 0xDADA, 0xEAEA, 0xFAFA}

TLS_EXT_NAMES = {
    0: "server_name", 5: "status_request", 10: "supported_groups",
    11: "ec_point_formats", 13: "signature_algorithms",
    16: "application_layer_protocol_negotiation",
    18: "signed_certificate_timestamp", 21: "padding",
    22: "encrypt_then_mac", 23: "extended_master_secret",
    27: "compress_certificate", 28: "record_size_limit",
    34: "delegated_credentials", 35: "session_ticket",
    41: "pre_shared_key", 43: "supported_versions",
    45: "psk_key_exchange_modes", 49: "post_handshake_auth",
    50: "signature_algorithms_cert", 51: "key_share",
    57: "quic_transport_parameters",
    17513: "application_settings", 17613: "application_settings_new",
    65037: "encrypted_client_hello", 65281: "renegotiation_info",
}


def _gv(v):
    """Replace GREASE values with 'GREASE' string."""
    return "GREASE" if v in GREASE else v


def _ext_name(eid):
    """Extension ID to name, or ID if unknown."""
    if eid in GREASE:
        return "GREASE"
    return TLS_EXT_NAMES.get(eid, eid)


# Captured QUIC ClientHello (H3 side)
_h3_tls = {}
_orig_server_hello = _TlsCtx._server_handle_hello


def _patched_server_hello(self, input_buf, initial_buf, handshake_buf, onertt_buf):
    try:
        pos = input_buf.tell()
        raw = input_buf.data_slice(pos, input_buf.capacity)
        hello = _pull_ch(_AioBuf(data=bytes(raw)))
        _h3_tls["latest"] = _aioquic_hello_to_dict(hello)
    except Exception as e:
        print(f"H3 TLS capture error: {e}", file=sys.stderr, flush=True)
    return _orig_server_hello(self, input_buf, initial_buf, handshake_buf, onertt_buf)


def _aioquic_hello_to_dict(hello):
    """Convert aioquic ClientHello dataclass to capture dict."""
    # Build ordered extension list from the known fields + other_extensions
    # aioquic parses some extensions into dedicated fields and puts the rest
    # in other_extensions. We reconstruct the full list from other_extensions
    # (which preserves wire order for unparsed extensions) plus the parsed ones.
    other_ext_ids = []
    for e in hello.other_extensions:
        eid = e[0] if isinstance(e, tuple) else e.extension_type
        other_ext_ids.append({"type": _ext_name(eid), "id": eid})

    return {
        "cipher_suites": [_gv(c) for c in hello.cipher_suites],
        "supported_groups": [_gv(g) for g in (hello.supported_groups or [])],
        "signature_algorithms": hello.signature_algorithms or [],
        "supported_versions": [_gv(v) for v in (hello.supported_versions or [])],
        "alpn": hello.alpn_protocols or [],
        "server_name": hello.server_name or "",
        "session_id_length": len(hello.legacy_session_id),
        "comp_methods": hello.legacy_compression_methods,
        "key_shares": [{"group": _gv(ks[0]), "length": len(ks[1])}
                       for ks in (hello.key_share or [])],
        "psk_key_exchange_modes": hello.psk_key_exchange_modes or [],
        "other_extensions": other_ext_ids,
    }


_TlsCtx._server_handle_hello = _patched_server_hello
# ================================================================
# End early patch
# ================================================================

from aioquic.asyncio import serve as quic_serve
from aioquic.asyncio.protocol import QuicConnectionProtocol
from aioquic.h3.connection import H3Connection
from aioquic.h3.events import HeadersReceived
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import QuicEvent, ProtocolNegotiated

import h2.connection
import h2.config
import h2.events


# ================================================================
# Raw TLS ClientHello parser (for H2/TCP via ssl._msg_callback)
# ================================================================

def parse_client_hello_raw(data):
    """Parse raw TLS ClientHello handshake message into a fingerprint dict.

    The ssl module's _msg_callback provides the handshake message body
    (starting with handshake type byte), NOT the record layer header.
    """
    if len(data) < 38 or data[0] != 1:  # type 1 = ClientHello
        return None

    pos = 4  # skip type(1) + length(3)
    hs_version = struct.unpack("!H", data[pos:pos + 2])[0]
    pos += 2 + 32  # version(2) + random(32)

    sid_len = data[pos]; pos += 1 + sid_len

    cs_len = struct.unpack("!H", data[pos:pos + 2])[0]; pos += 2
    ciphers = [_gv(struct.unpack("!H", data[pos + i:pos + i + 2])[0])
               for i in range(0, cs_len, 2)]
    pos += cs_len

    comp_len = data[pos]; pos += 1
    comp = list(data[pos:pos + comp_len]); pos += comp_len

    if pos + 2 > len(data):
        return {"cipher_suites": ciphers, "extensions": [], "comp_methods": comp}

    ext_len = struct.unpack("!H", data[pos:pos + 2])[0]; pos += 2
    ext_end = pos + ext_len

    extensions = []
    groups = []
    sig_algs = []
    versions = []
    alpn_list = []
    key_shares = []
    points = []

    while pos + 4 <= ext_end and pos + 4 <= len(data):
        etype = struct.unpack("!H", data[pos:pos + 2])[0]
        elen = struct.unpack("!H", data[pos + 2:pos + 4])[0]
        edata = data[pos + 4:pos + 4 + elen]
        extensions.append(_gv(etype))

        if etype == 0x000A and len(edata) >= 2:  # supported_groups
            gl = struct.unpack("!H", edata[:2])[0]
            for i in range(0, min(gl, len(edata) - 2), 2):
                groups.append(_gv(struct.unpack("!H", edata[2 + i:4 + i])[0]))

        elif etype == 0x000B and len(edata) >= 1:  # ec_point_formats
            fl = edata[0]
            points = list(edata[1:1 + fl])

        elif etype == 0x000D and len(edata) >= 2:  # signature_algorithms
            sl = struct.unpack("!H", edata[:2])[0]
            for i in range(0, min(sl, len(edata) - 2), 2):
                sig_algs.append(struct.unpack("!H", edata[2 + i:4 + i])[0])

        elif etype == 0x0010 and len(edata) >= 2:  # ALPN
            al = struct.unpack("!H", edata[:2])[0]
            d = edata[2:2 + al]
            while d:
                pl = d[0]; d = d[1:]
                alpn_list.append(d[:pl].decode(errors="replace")); d = d[pl:]

        elif etype == 0x002B and len(edata) >= 1:  # supported_versions
            vl = edata[0]
            for i in range(0, min(vl, len(edata) - 1), 2):
                versions.append(_gv(struct.unpack("!H", edata[1 + i:3 + i])[0]))

        elif etype == 0x0033 and len(edata) >= 2:  # key_share
            kl = struct.unpack("!H", edata[:2])[0]
            d = edata[2:2 + kl]
            while len(d) >= 4:
                kg = struct.unpack("!H", d[:2])[0]
                klen = struct.unpack("!H", d[2:4])[0]
                key_shares.append({"group": _gv(kg), "length": klen})
                d = d[4 + klen:]

        pos += 4 + elen

    return {
        "cipher_suites": ciphers,
        "extensions": extensions,
        "supported_groups": groups,
        "signature_algorithms": sig_algs,
        "supported_versions": versions,
        "alpn": alpn_list,
        "key_shares": key_shares,
        "ec_point_formats": points,
        "comp_methods": comp,
        "session_id_length": sid_len,
        "handshake_version": hs_version,
    }


# Captured H2 TLS ClientHello
_h2_tls = {}


def _h2_tls_msg_callback(conn, direction, version, content_type, msg_type, data):
    """ssl._msg_callback: capture ClientHello from incoming TLS handshake."""
    # content_type 22 = Handshake, msg_type 1 = ClientHello
    if direction == "read" and content_type == 22 and msg_type == 1:
        parsed = parse_client_hello_raw(data)
        if parsed:
            _h2_tls["latest"] = parsed


# ================================================================
# Page renderer
# ================================================================

def _render_page(protocol, pseudo_headers, headers, json_data):
    color = "#0a0" if "3" in protocol else "#c00"
    lines = [f"<!DOCTYPE html><html><head><style>",
             f"body {{ font-family: monospace; background: #1a1a2e; color: #eee; padding: 20px; }}",
             f"h1 {{ color: {color}; font-size: 48px; }}",
             f"pre {{ background: #16213e; padding: 15px; border-radius: 8px; white-space: pre-wrap; }}",
             f".label {{ color: #888; }}",
             f"</style></head><body>",
             f"<h1>{protocol}</h1><pre>",
             f'<span class="label">Pseudo-headers (wire order):</span>']
    for p in pseudo_headers:
        lines.append(f"  {p}")
    lines.append(f'\n<span class="label">Headers (wire order):</span>')
    for h in headers:
        lines.append(f"  {h}")
    lines.append(f'\n<span class="label">JSON:</span>')
    lines.append(json.dumps(json_data, indent=2))
    lines.append("</pre></body></html>")
    return "\n".join(lines)


def _log_request(protocol, pseudo, headers, ua):
    return {
        "protocol": protocol,
        "pseudo_headers": pseudo,
        "headers": headers,
        "user_agent": ua,
        "timestamp": time.strftime("%H:%M:%S"),
    }


# ================================================================
# HTTP/2 handler (h2 library - preserves wire order)
# ================================================================

class H2Protocol(asyncio.Protocol):
    def __init__(self, h3_port):
        self.transport = None
        self.conn = None
        self.h3_port = h3_port

    def connection_made(self, transport):
        self.transport = transport
        config = h2.config.H2Configuration(client_side=False)
        self.conn = h2.connection.H2Connection(config=config)
        self.conn.initiate_connection()
        self.transport.write(self.conn.data_to_send())

    def data_received(self, data):
        try:
            events = self.conn.receive_data(data)
        except Exception:
            return
        for event in events:
            if isinstance(event, h2.events.RequestReceived):
                self._handle(event)
        out = self.conn.data_to_send()
        if out:
            self.transport.write(out)

    def _handle(self, event):
        sid = event.stream_id
        pseudo, headers, ua = [], [], ""
        # event.headers is an ordered list of (name, value) tuples - wire order
        for name, value in event.headers:
            n = name.decode() if isinstance(name, bytes) else name
            v = value.decode() if isinstance(value, bytes) else value
            if n.startswith(":"):
                pseudo.append(n)
            else:
                headers.append(f"{n}: {v}")
                if n == "user-agent":
                    ua = v

        result = _log_request("HTTP/2", pseudo, headers, ua)
        tls_info = _h2_tls.pop("latest", None)
        if tls_info:
            result["tls"] = tls_info
        print(json.dumps(result), flush=True)

        page = _render_page("HTTP/2", pseudo, headers, result).encode()
        self.conn.send_headers(sid, [
            (":status", "200"),
            ("content-type", "text/html; charset=utf-8"),
            ("content-length", str(len(page))),
            ("alt-svc", f'h3=":{self.h3_port}"; ma=86400'),
        ])
        self.conn.send_data(sid, page, end_stream=True)
        self.transport.write(self.conn.data_to_send())

    def connection_lost(self, exc):
        pass


# ================================================================
# HTTP/3 handler (aioquic - preserves wire order)
# ================================================================

class H3ServerProtocol(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._h3: Optional[H3Connection] = None

    def quic_event_received(self, event: QuicEvent) -> None:
        if isinstance(event, ProtocolNegotiated):
            self._h3 = H3Connection(self._quic)
        if self._h3 is None:
            return
        for h3_event in self._h3.handle_event(event):
            if isinstance(h3_event, HeadersReceived):
                self._handle(h3_event)

    def _handle(self, event: HeadersReceived):
        sid = event.stream_id
        pseudo, headers, ua = [], [], ""
        # event.headers is an ordered list of (name, value) tuples - wire order
        for name, value in event.headers:
            n = name.decode() if isinstance(name, bytes) else name
            v = value.decode() if isinstance(value, bytes) else value
            if n.startswith(":"):
                pseudo.append(n)
            else:
                headers.append(f"{n}: {v}")
                if n == "user-agent":
                    ua = v

        result = _log_request("HTTP/3", pseudo, headers, ua)
        tls_info = _h3_tls.pop("latest", None)
        if tls_info:
            result["tls"] = tls_info
        print(json.dumps(result), flush=True)

        page = _render_page("HTTP/3", pseudo, headers, result).encode()
        self._h3.send_headers(sid, [
            (b":status", b"200"),
            (b"content-type", b"text/html; charset=utf-8"),
            (b"content-length", str(len(page)).encode()),
        ])
        self._h3.send_data(sid, page, end_stream=True)
        self.transmit()


# ================================================================
# Main
# ================================================================

async def main():
    parser = argparse.ArgumentParser(
        description="H2+H3 capture server for browser fingerprinting",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--cert", required=True, help="TLS certificate (fullchain.pem)")
    parser.add_argument("--key", required=True, help="TLS private key (privkey.pem)")
    parser.add_argument("--port", type=int, default=4511,
                        help="H2 port (H3 = port+2, default: 4511)")
    args = parser.parse_args()

    h2_port = args.port
    h3_port = args.port + 2
    loop = asyncio.get_event_loop()

    # H2 on TLS/TCP with msg_callback for ClientHello capture
    ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ssl_ctx.load_cert_chain(args.cert, args.key)
    ssl_ctx.set_alpn_protocols(["h2", "http/1.1"])
    ssl_ctx._msg_callback = _h2_tls_msg_callback

    await loop.create_server(
        lambda: H2Protocol(h3_port), "0.0.0.0", h2_port, ssl=ssl_ctx,
    )
    print(f"H2 on :{h2_port}", file=sys.stderr, flush=True)

    # H3 on QUIC/UDP (patched TLS captures ClientHello)
    qc = QuicConfiguration(is_client=False, alpn_protocols=["h3"])
    qc.load_cert_chain(args.cert, args.key)
    await quic_serve("::", h3_port, configuration=qc, create_protocol=H3ServerProtocol)
    print(f"H3 on :{h3_port}", file=sys.stderr, flush=True)

    print(f"READY h2=:{h2_port} h3=:{h3_port}", file=sys.stderr, flush=True)
    await asyncio.Future()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
