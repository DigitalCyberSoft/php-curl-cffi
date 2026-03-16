package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/md5"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"math/big"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
)

// Uint8Slice marshals as JSON number array instead of base64.
type Uint8Slice []uint8

func (u Uint8Slice) MarshalJSON() ([]byte, error) {
	nums := make([]string, len(u))
	for i, v := range u {
		nums[i] = strconv.Itoa(int(v))
	}
	return []byte("[" + strings.Join(nums, ",") + "]"), nil
}

// ExtensionDetail holds full parsed extension data matching upstream YAML structure.
type ExtensionDetail struct {
	Type              interface{}   `json:"type"` // string name or "GREASE"
	Length            int           `json:"length"`
	Data              string        `json:"data,omitempty"`              // base64 for unknown/GREASE
	SupportedGroups   []interface{} `json:"supported_groups,omitempty"`  // ext 10
	ECPointFormats    []int         `json:"ec_point_formats,omitempty"`  // ext 11
	SigHashAlgs       []int         `json:"sig_hash_algs,omitempty"`     // ext 13
	ALPNList          []string      `json:"alpn_list,omitempty"`         // ext 16
	SupportedVersions []interface{} `json:"supported_versions,omitempty"`// ext 43
	PSKKEMode         int           `json:"psk_ke_mode,omitempty"`       // ext 45
	KeyShares         []KeyShare    `json:"key_shares,omitempty"`        // ext 51
	Algorithms        []int         `json:"algorithms,omitempty"`        // ext 27 (compress_certificate)
	ALPSAlpnList      []string      `json:"alps_alpn_list,omitempty"`    // ext 17513/17613
	StatusReqType     int           `json:"status_request_type,omitempty"` // ext 5
}

type KeyShare struct {
	Group  interface{} `json:"group"` // int or "GREASE"
	Length int         `json:"length"`
}

// TLSInspectionResult holds the full parsed ClientHello.
type TLSInspectionResult struct {
	CipherSuites      []interface{}     `json:"cipher_suites"`
	Extensions        []uint16          `json:"extensions"`
	ExtensionDetails  []ExtensionDetail `json:"extension_details"`
	SupportedGroups   []interface{}     `json:"supported_groups"`
	SupportedPoints   Uint8Slice        `json:"supported_points"`
	SignatureSchemes   []uint16          `json:"signature_schemes"`
	SupportedVersions []interface{}     `json:"supported_versions"`
	ALPN              []string          `json:"alpn"`
	ServerName        string            `json:"server_name"`
	JA3Hash           string            `json:"ja3_hash"`
	JA3Text           string            `json:"ja3_text"`
	CompMethods       []int             `json:"comp_methods"`
	RecordVersion     string            `json:"record_version"`
	HandshakeVersion  string            `json:"handshake_version"`
	SessionIDLength   int               `json:"session_id_length"`
}

// GREASE values
var greaseSet = map[uint16]bool{
	0x0A0A: true, 0x1A1A: true, 0x2A2A: true, 0x3A3A: true,
	0x4A4A: true, 0x5A5A: true, 0x6A6A: true, 0x7A7A: true,
	0x8A8A: true, 0x9A9A: true, 0xAAAA: true, 0xBABA: true,
	0xCACA: true, 0xDADA: true, 0xEAEA: true, 0xFAFA: true,
}

func isGREASE(v uint16) bool { return greaseSet[v] }

func greaseOrInt(v uint16) interface{} {
	if isGREASE(v) {
		return "GREASE"
	}
	return int(v)
}

func tlsVersionName(v uint16) interface{} {
	switch v {
	case 0x0301:
		return "TLS_VERSION_1_0"
	case 0x0302:
		return "TLS_VERSION_1_1"
	case 0x0303:
		return "TLS_VERSION_1_2"
	case 0x0304:
		return "TLS_VERSION_1_3"
	default:
		if isGREASE(v) {
			return "GREASE"
		}
		return int(v)
	}
}

// Extension type names matching upstream YAML
var extNames = map[uint16]string{
	0: "server_name", 5: "status_request", 10: "supported_groups",
	11: "ec_point_formats", 13: "signature_algorithms",
	16: "application_layer_protocol_negotiation",
	17: "extended_master_secret", 18: "signed_certificate_timestamp",
	21: "padding", 22: "encrypt_then_mac", 23: "extended_master_secret",
	27: "compress_certificate", 28: "record_size_limit",
	34: "delegated_credentials", 35: "session_ticket",
	41: "pre_shared_key", 43: "supported_versions",
	45: "psk_key_exchange_modes", 49: "post_handshake_auth",
	50: "signature_algorithms_cert", 51: "keyshare",
	17513: "application_settings", 17613: "application_settings_new",
	65037: "encrypted_client_hello", 65281: "renegotiation_info",
}

// recordConn wraps a net.Conn and records the first N bytes read.
type recordConn struct {
	net.Conn
	mu       sync.Mutex
	buf      []byte
	maxBytes int
	done     bool
}

func newRecordConn(c net.Conn, maxBytes int) *recordConn {
	return &recordConn{Conn: c, buf: make([]byte, 0, maxBytes), maxBytes: maxBytes}
}

func (r *recordConn) Read(p []byte) (int, error) {
	n, err := r.Conn.Read(p)
	r.mu.Lock()
	if !r.done && n > 0 {
		remaining := r.maxBytes - len(r.buf)
		if remaining > 0 {
			toAppend := n
			if toAppend > remaining {
				toAppend = remaining
			}
			r.buf = append(r.buf, p[:toAppend]...)
			if len(r.buf) >= r.maxBytes {
				r.done = true
			}
		}
	}
	r.mu.Unlock()
	return n, err
}

func (r *recordConn) getRecordedBytes() []byte {
	r.mu.Lock()
	defer r.mu.Unlock()
	out := make([]byte, len(r.buf))
	copy(out, r.buf)
	return out
}

type connStore struct {
	mu    sync.Mutex
	conns map[net.Conn]*recordConn
}

func newConnStore() *connStore {
	return &connStore{conns: make(map[net.Conn]*recordConn)}
}

func (cs *connStore) put(raw net.Conn, rc *recordConn) {
	cs.mu.Lock(); cs.conns[raw] = rc; cs.mu.Unlock()
}
func (cs *connStore) get(raw net.Conn) *recordConn {
	cs.mu.Lock(); defer cs.mu.Unlock(); return cs.conns[raw]
}
func (cs *connStore) remove(raw net.Conn) {
	cs.mu.Lock(); delete(cs.conns, raw); cs.mu.Unlock()
}

type helloStore struct {
	mu     sync.Mutex
	hellos map[net.Conn]*tls.ClientHelloInfo
}

func newHelloStore() *helloStore {
	return &helloStore{hellos: make(map[net.Conn]*tls.ClientHelloInfo)}
}

func (hs *helloStore) put(raw net.Conn, info *tls.ClientHelloInfo) {
	hs.mu.Lock(); hs.hellos[raw] = info; hs.mu.Unlock()
}
func (hs *helloStore) get(raw net.Conn) *tls.ClientHelloInfo {
	hs.mu.Lock(); defer hs.mu.Unlock(); return hs.hellos[raw]
}
func (hs *helloStore) remove(raw net.Conn) {
	hs.mu.Lock(); delete(hs.hellos, raw); hs.mu.Unlock()
}

func generateSelfSignedCert() (tls.Certificate, error) {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return tls.Certificate{}, err
	}
	serial, _ := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	tmpl := &x509.Certificate{
		SerialNumber: serial,
		Subject:      pkix.Name{CommonName: "localhost"},
		NotBefore:    time.Now().Add(-1 * time.Hour),
		NotAfter:     time.Now().Add(24 * time.Hour),
		KeyUsage:     x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		DNSNames:     []string{"localhost"},
	}
	certDER, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, &key.PublicKey, key)
	if err != nil {
		return tls.Certificate{}, err
	}
	return tls.Certificate{Certificate: [][]byte{certDER}, PrivateKey: key}, nil
}

// parseClientHelloFull does a deep parse of raw TLS ClientHello bytes.
func parseClientHelloFull(raw []byte) (*TLSInspectionResult, error) {
	if len(raw) < 5 || raw[0] != 22 {
		return nil, fmt.Errorf("not a TLS handshake record")
	}

	result := &TLSInspectionResult{}

	// Record version
	recVer := binary.BigEndian.Uint16(raw[1:3])
	result.RecordVersion = tlsVersionName(recVer).(string)

	payload := raw[5:]
	if len(payload) < 4 || payload[0] != 1 {
		return nil, fmt.Errorf("not a ClientHello")
	}
	pos := 4

	// Handshake version
	if len(payload) < pos+2 {
		return nil, fmt.Errorf("too short for version")
	}
	hsVer := binary.BigEndian.Uint16(payload[pos : pos+2])
	result.HandshakeVersion = tlsVersionName(hsVer).(string)
	pos += 2

	// Random (32 bytes)
	pos += 32

	// Session ID
	if len(payload) < pos+1 {
		return nil, fmt.Errorf("too short for session_id")
	}
	sidLen := int(payload[pos])
	result.SessionIDLength = sidLen
	pos++
	pos += sidLen

	// Cipher suites
	if len(payload) < pos+2 {
		return nil, fmt.Errorf("too short for cipher_suites")
	}
	csLen := int(binary.BigEndian.Uint16(payload[pos : pos+2]))
	pos += 2
	for i := 0; i < csLen && pos+1 < len(payload); i += 2 {
		cs := binary.BigEndian.Uint16(payload[pos+i : pos+i+2])
		result.CipherSuites = append(result.CipherSuites, greaseOrInt(cs))
	}
	pos += csLen

	// Compression methods
	if len(payload) < pos+1 {
		return nil, fmt.Errorf("too short for compression")
	}
	compLen := int(payload[pos])
	pos++
	for i := 0; i < compLen && pos+i < len(payload); i++ {
		result.CompMethods = append(result.CompMethods, int(payload[pos+i]))
	}
	pos += compLen

	// Extensions
	if len(payload) < pos+2 {
		return nil, fmt.Errorf("too short for extensions")
	}
	extTotalLen := int(binary.BigEndian.Uint16(payload[pos : pos+2]))
	pos += 2

	extEnd := pos + extTotalLen
	if extEnd > len(payload) {
		extEnd = len(payload)
	}

	for pos+4 <= extEnd {
		extType := binary.BigEndian.Uint16(payload[pos : pos+2])
		extLen := int(binary.BigEndian.Uint16(payload[pos+2 : pos+4]))
		extData := payload[pos+4:]
		if len(extData) > extLen {
			extData = extData[:extLen]
		}

		result.Extensions = append(result.Extensions, extType)

		detail := ExtensionDetail{Length: extLen}

		// Set type name
		if isGREASE(extType) {
			detail.Type = "GREASE"
			if len(extData) > 0 {
				detail.Data = fmt.Sprintf("%x", extData)
			}
		} else if name, ok := extNames[extType]; ok {
			detail.Type = name
		} else {
			detail.Type = int(extType)
		}

		// Parse extension internals
		switch extType {
		case 0: // server_name
			// Just note it exists, length varies

		case 5: // status_request
			if len(extData) >= 1 {
				detail.StatusReqType = int(extData[0])
			}

		case 10: // supported_groups
			if len(extData) >= 2 {
				listLen := int(binary.BigEndian.Uint16(extData[0:2]))
				d := extData[2:]
				if len(d) > listLen {
					d = d[:listLen]
				}
				for i := 0; i+1 < len(d); i += 2 {
					g := binary.BigEndian.Uint16(d[i : i+2])
					detail.SupportedGroups = append(detail.SupportedGroups, greaseOrInt(g))
					result.SupportedGroups = append(result.SupportedGroups, greaseOrInt(g))
				}
			}

		case 11: // ec_point_formats
			if len(extData) >= 1 {
				fmtLen := int(extData[0])
				d := extData[1:]
				if len(d) > fmtLen {
					d = d[:fmtLen]
				}
				for _, b := range d {
					detail.ECPointFormats = append(detail.ECPointFormats, int(b))
					result.SupportedPoints = append(result.SupportedPoints, b)
				}
			}

		case 13, 50: // signature_algorithms, signature_algorithms_cert
			if len(extData) >= 2 {
				listLen := int(binary.BigEndian.Uint16(extData[0:2]))
				d := extData[2:]
				if len(d) > listLen {
					d = d[:listLen]
				}
				for i := 0; i+1 < len(d); i += 2 {
					s := binary.BigEndian.Uint16(d[i : i+2])
					detail.SigHashAlgs = append(detail.SigHashAlgs, int(s))
					if extType == 13 {
						result.SignatureSchemes = append(result.SignatureSchemes, s)
					}
				}
			}

		case 16: // ALPN
			if len(extData) >= 2 {
				listLen := int(binary.BigEndian.Uint16(extData[0:2]))
				d := extData[2:]
				if len(d) > listLen {
					d = d[:listLen]
				}
				for len(d) > 0 {
					pLen := int(d[0])
					d = d[1:]
					if pLen > len(d) {
						break
					}
					proto := string(d[:pLen])
					detail.ALPNList = append(detail.ALPNList, proto)
					result.ALPN = append(result.ALPN, proto)
					d = d[pLen:]
				}
			}

		case 27: // compress_certificate
			if len(extData) >= 1 {
				algLen := int(extData[0])
				d := extData[1:]
				if len(d) > algLen {
					d = d[:algLen]
				}
				for i := 0; i+1 < len(d); i += 2 {
					detail.Algorithms = append(detail.Algorithms, int(binary.BigEndian.Uint16(d[i:i+2])))
				}
			}

		case 43: // supported_versions
			if len(extData) >= 1 {
				svLen := int(extData[0])
				d := extData[1:]
				if len(d) > svLen {
					d = d[:svLen]
				}
				for i := 0; i+1 < len(d); i += 2 {
					v := binary.BigEndian.Uint16(d[i : i+2])
					detail.SupportedVersions = append(detail.SupportedVersions, tlsVersionName(v))
					result.SupportedVersions = append(result.SupportedVersions, tlsVersionName(v))
				}
			}

		case 45: // psk_key_exchange_modes
			if len(extData) >= 2 {
				detail.PSKKEMode = int(extData[1])
			}

		case 51: // key_share
			if len(extData) >= 2 {
				ksLen := int(binary.BigEndian.Uint16(extData[0:2]))
				d := extData[2:]
				if len(d) > ksLen {
					d = d[:ksLen]
				}
				for len(d) >= 4 {
					group := binary.BigEndian.Uint16(d[0:2])
					kLen := int(binary.BigEndian.Uint16(d[2:4]))
					ks := KeyShare{Group: greaseOrInt(group), Length: kLen}
					detail.KeyShares = append(detail.KeyShares, ks)
					d = d[4+kLen:]
				}
			}

		case 17513, 17613: // application_settings / application_settings_new
			if len(extData) >= 2 {
				alpnLen := int(binary.BigEndian.Uint16(extData[0:2]))
				d := extData[2:]
				if len(d) > alpnLen {
					d = d[:alpnLen]
				}
				for len(d) > 0 {
					pLen := int(d[0])
					d = d[1:]
					if pLen > len(d) {
						break
					}
					detail.ALPSAlpnList = append(detail.ALPSAlpnList, string(d[:pLen]))
					d = d[pLen:]
				}
			}

		case 34: // delegated_credentials
			if len(extData) >= 2 {
				listLen := int(binary.BigEndian.Uint16(extData[0:2]))
				d := extData[2:]
				if len(d) > listLen {
					d = d[:listLen]
				}
				for i := 0; i+1 < len(d); i += 2 {
					detail.SigHashAlgs = append(detail.SigHashAlgs, int(binary.BigEndian.Uint16(d[i:i+2])))
				}
			}
		}

		result.ExtensionDetails = append(result.ExtensionDetails, detail)
		pos += 4 + extLen
	}

	// Defaults
	if result.CipherSuites == nil { result.CipherSuites = []interface{}{} }
	if result.Extensions == nil { result.Extensions = []uint16{} }
	if result.ExtensionDetails == nil { result.ExtensionDetails = []ExtensionDetail{} }
	if result.SupportedGroups == nil { result.SupportedGroups = []interface{}{} }
	if result.SupportedPoints == nil { result.SupportedPoints = Uint8Slice{} }
	if result.SignatureSchemes == nil { result.SignatureSchemes = []uint16{} }
	if result.SupportedVersions == nil { result.SupportedVersions = []interface{}{} }
	if result.ALPN == nil { result.ALPN = []string{} }
	if result.CompMethods == nil { result.CompMethods = []int{} }

	return result, nil
}

// computeJA3 computes JA3 fingerprint directly from raw ClientHello bytes.
// JA3 = MD5(TLSVersion,CipherSuites,Extensions,EllipticCurves,ECPointFormats)
// Uses raw uint16 values (including GREASE) - no normalization.
func computeJA3(raw []byte) (hash string, text string, err error) {
	if len(raw) < 5 || raw[0] != 22 {
		err = fmt.Errorf("not a TLS handshake")
		return
	}
	payload := raw[5:]
	if len(payload) < 38 {
		err = fmt.Errorf("too short")
		return
	}

	// Field 1: TLS version from ClientHello body
	tlsVersion := binary.BigEndian.Uint16(payload[4:6])
	pos := 4 + 2 + 32 // handshake_header(4) + version(2) + random(32)

	// Session ID
	if pos >= len(payload) { err = fmt.Errorf("short"); return }
	sidLen := int(payload[pos]); pos += 1 + sidLen

	// Field 2: Cipher suites
	if pos+2 > len(payload) { err = fmt.Errorf("short"); return }
	csLen := int(binary.BigEndian.Uint16(payload[pos : pos+2])); pos += 2
	var ciphers []string
	for i := 0; i < csLen && pos+i+1 < len(payload); i += 2 {
		ciphers = append(ciphers, strconv.Itoa(int(binary.BigEndian.Uint16(payload[pos+i:pos+i+2]))))
	}
	pos += csLen

	// Compression methods
	if pos >= len(payload) { err = fmt.Errorf("short"); return }
	compLen := int(payload[pos]); pos += 1 + compLen

	// Extensions
	if pos+2 > len(payload) { err = fmt.Errorf("short"); return }
	extTotalLen := int(binary.BigEndian.Uint16(payload[pos : pos+2])); pos += 2
	extEnd := pos + extTotalLen
	if extEnd > len(payload) { extEnd = len(payload) }

	var extensions []string
	var groups []string
	var points []string

	for pos+4 <= extEnd {
		extType := binary.BigEndian.Uint16(payload[pos : pos+2])
		extLen := int(binary.BigEndian.Uint16(payload[pos+2 : pos+4]))
		extData := payload[pos+4:]
		if len(extData) > extLen { extData = extData[:extLen] }

		extensions = append(extensions, strconv.Itoa(int(extType)))

		switch extType {
		case 0x000a: // supported_groups
			if len(extData) >= 2 {
				listLen := int(binary.BigEndian.Uint16(extData[0:2]))
				d := extData[2:]
				if len(d) > listLen { d = d[:listLen] }
				for i := 0; i+1 < len(d); i += 2 {
					groups = append(groups, strconv.Itoa(int(binary.BigEndian.Uint16(d[i:i+2]))))
				}
			}
		case 0x000b: // ec_point_formats
			if len(extData) >= 1 {
				fmtLen := int(extData[0])
				d := extData[1:]
				if len(d) > fmtLen { d = d[:fmtLen] }
				for _, b := range d {
					points = append(points, strconv.Itoa(int(b)))
				}
			}
		}

		pos += 4 + extLen
	}

	ja3Parts := []string{
		strconv.Itoa(int(tlsVersion)),
		strings.Join(ciphers, "-"),
		strings.Join(extensions, "-"),
		strings.Join(groups, "-"),
		strings.Join(points, "-"),
	}

	text = strings.Join(ja3Parts, ",")
	sum := md5.Sum([]byte(text))
	hash = fmt.Sprintf("%x", sum)
	return
}

func main() {
	var cert tls.Certificate
	var err error

	if len(os.Args) >= 3 {
		cert, err = tls.LoadX509KeyPair(os.Args[1], os.Args[2])
		if err != nil {
			fmt.Fprintf(os.Stderr, "Failed to load cert/key: %v\n", err)
			os.Exit(1)
		}
	} else {
		cert, err = generateSelfSignedCert()
		if err != nil {
			fmt.Fprintf(os.Stderr, "Failed to generate cert: %v\n", err)
			os.Exit(1)
		}
	}

	cs := newConnStore()
	hs := newHelloStore()

	tlsConfig := &tls.Config{
		Certificates: []tls.Certificate{cert},
		GetConfigForClient: func(hello *tls.ClientHelloInfo) (*tls.Config, error) {
			if rc, ok := hello.Conn.(*recordConn); ok {
				hs.put(rc.Conn, hello)
			}
			return nil, nil
		},
	}

	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		fmt.Fprintf(os.Stderr, "Failed to listen: %v\n", err)
		os.Exit(1)
	}

	port := ln.Addr().(*net.TCPAddr).Port
	fmt.Printf("READY:%d\n", port)
	os.Stdout.Sync()

	for {
		rawConn, err := ln.Accept()
		if err != nil {
			continue
		}
		go handleConnection(rawConn, tlsConfig, cs, hs)
	}
}

func buildResult(recorded []byte, hello *tls.ClientHelloInfo) *TLSInspectionResult {
	result, err := parseClientHelloFull(recorded)
	if err != nil {
		return nil
	}

	if hello != nil {
		result.ServerName = hello.ServerName
		if hello.SupportedProtos != nil {
			result.ALPN = hello.SupportedProtos
		}
	}

	ja3Hash, ja3Text, ja3Err := computeJA3(recorded)
	if ja3Err == nil {
		result.JA3Hash = ja3Hash
		result.JA3Text = ja3Text
	}

	return result
}

func handleConnection(rawConn net.Conn, tlsConfig *tls.Config, cs *connStore, hs *helloStore) {
	defer rawConn.Close()

	rc := newRecordConn(rawConn, 16384)
	cs.put(rawConn, rc)
	defer cs.remove(rawConn)

	tlsConn := tls.Server(rc, tlsConfig)
	defer tlsConn.Close()
	tlsConn.SetDeadline(time.Now().Add(10 * time.Second))

	if err := tlsConn.Handshake(); err != nil {
		recorded := rc.getRecordedBytes()
		if len(recorded) > 0 {
			hello := hs.get(rawConn)
			if result := buildResult(recorded, hello); result != nil {
				if jsonData, jsonErr := json.Marshal(result); jsonErr == nil {
					fmt.Fprintf(os.Stderr, "CAPTURED:%s\n", jsonData)
				}
			}
		}
		fmt.Fprintf(os.Stderr, "Handshake error: %v\n", err)
		return
	}

	tlsConn.SetDeadline(time.Now().Add(10 * time.Second))

	buf := make([]byte, 4096)
	n, err := tlsConn.Read(buf)
	if err != nil && err != io.EOF {
		return
	}
	_ = n

	recorded := rc.getRecordedBytes()
	hello := hs.get(rawConn)
	defer hs.remove(rawConn)

	result := buildResult(recorded, hello)
	if result == nil {
		return
	}

	jsonData, err := json.Marshal(result)
	if err != nil {
		return
	}

	fmt.Fprintf(os.Stderr, "CAPTURED:%s\n", jsonData)

	response := fmt.Sprintf("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s", len(jsonData), jsonData)
	tlsConn.Write([]byte(response))
}

var _ net.Conn = (*recordConn)(nil)
var _ http.Handler = http.HandlerFunc(nil)
