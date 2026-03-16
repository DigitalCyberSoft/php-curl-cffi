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

// Uint8Slice is a []uint8 that marshals to a JSON array of numbers
// instead of Go's default base64-encoded string for []byte.
type Uint8Slice []uint8

func (u Uint8Slice) MarshalJSON() ([]byte, error) {
	nums := make([]string, len(u))
	for i, v := range u {
		nums[i] = strconv.Itoa(int(v))
	}
	return []byte("[" + strings.Join(nums, ",") + "]"), nil
}

// TLSInspectionResult holds the parsed ClientHello data returned as JSON.
type TLSInspectionResult struct {
	CipherSuites      []uint16   `json:"cipher_suites"`
	Extensions        []uint16   `json:"extensions"`
	SupportedGroups   []uint16   `json:"supported_groups"`
	SupportedPoints   Uint8Slice `json:"supported_points"`
	SignatureSchemes   []uint16   `json:"signature_schemes"`
	SupportedVersions []uint16   `json:"supported_versions"`
	ALPN              []string   `json:"alpn"`
	ServerName        string     `json:"server_name"`
	JA3Hash           string     `json:"ja3_hash"`
	JA3Text           string     `json:"ja3_text"`
}

// recordConn wraps a net.Conn and records the first N bytes read from it.
// This lets us capture the raw ClientHello before TLS processes it.
type recordConn struct {
	net.Conn
	mu       sync.Mutex
	buf      []byte
	maxBytes int
	done     bool
}

func newRecordConn(c net.Conn, maxBytes int) *recordConn {
	return &recordConn{
		Conn:     c,
		buf:      make([]byte, 0, maxBytes),
		maxBytes: maxBytes,
	}
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

// connStore maps connections to their recordConn wrappers so the HTTP handler
// can retrieve the raw bytes after the TLS handshake completes.
type connStore struct {
	mu    sync.Mutex
	conns map[net.Conn]*recordConn
}

func newConnStore() *connStore {
	return &connStore{conns: make(map[net.Conn]*recordConn)}
}

func (cs *connStore) put(raw net.Conn, rc *recordConn) {
	cs.mu.Lock()
	cs.conns[raw] = rc
	cs.mu.Unlock()
}

func (cs *connStore) get(raw net.Conn) *recordConn {
	cs.mu.Lock()
	defer cs.mu.Unlock()
	return cs.conns[raw]
}

func (cs *connStore) remove(raw net.Conn) {
	cs.mu.Lock()
	delete(cs.conns, raw)
	cs.mu.Unlock()
}

// helloStore maps connections to the ClientHelloInfo captured in GetConfigForClient.
type helloStore struct {
	mu     sync.Mutex
	hellos map[net.Conn]*tls.ClientHelloInfo
}

func newHelloStore() *helloStore {
	return &helloStore{hellos: make(map[net.Conn]*tls.ClientHelloInfo)}
}

func (hs *helloStore) put(raw net.Conn, info *tls.ClientHelloInfo) {
	hs.mu.Lock()
	hs.hellos[raw] = info
	hs.mu.Unlock()
}

func (hs *helloStore) get(raw net.Conn) *tls.ClientHelloInfo {
	hs.mu.Lock()
	defer hs.mu.Unlock()
	return hs.hellos[raw]
}

func (hs *helloStore) remove(raw net.Conn) {
	hs.mu.Lock()
	delete(hs.hellos, raw)
	hs.mu.Unlock()
}

func generateSelfSignedCert() (tls.Certificate, error) {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return tls.Certificate{}, fmt.Errorf("generate key: %w", err)
	}

	serialNumber, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		return tls.Certificate{}, fmt.Errorf("generate serial: %w", err)
	}

	tmpl := &x509.Certificate{
		SerialNumber: serialNumber,
		Subject:      pkix.Name{CommonName: "localhost"},
		NotBefore:    time.Now().Add(-1 * time.Hour),
		NotAfter:     time.Now().Add(24 * time.Hour),
		KeyUsage:     x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		DNSNames:     []string{"localhost"},
	}

	certDER, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, &key.PublicKey, key)
	if err != nil {
		return tls.Certificate{}, fmt.Errorf("create certificate: %w", err)
	}

	return tls.Certificate{
		Certificate: [][]byte{certDER},
		PrivateKey:  key,
	}, nil
}

// parseClientHelloExtensions parses the raw TLS record bytes to extract
// extension IDs from the ClientHello message.
func parseClientHelloExtensions(raw []byte) ([]uint16, error) {
	// TLS record header: content_type(1) + version(2) + length(2)
	if len(raw) < 5 {
		return nil, fmt.Errorf("too short for TLS record header")
	}
	if raw[0] != 22 { // handshake
		return nil, fmt.Errorf("not a handshake record: %d", raw[0])
	}
	recordLen := int(binary.BigEndian.Uint16(raw[3:5]))
	payload := raw[5:]
	if len(payload) < recordLen {
		// We may have captured enough for parsing even if truncated
		// Just use what we have
	}

	// Handshake header: type(1) + length(3)
	if len(payload) < 4 {
		return nil, fmt.Errorf("too short for handshake header")
	}
	if payload[0] != 1 { // ClientHello
		return nil, fmt.Errorf("not a ClientHello: %d", payload[0])
	}
	// handshakeLen := int(payload[1])<<16 | int(payload[2])<<8 | int(payload[3])
	pos := 4

	// ClientHello body:
	// client_version(2) + random(32) = 34
	if len(payload) < pos+34 {
		return nil, fmt.Errorf("too short for version+random")
	}
	pos += 34

	// session_id: length(1) + data
	if len(payload) < pos+1 {
		return nil, fmt.Errorf("too short for session_id length")
	}
	sessionIDLen := int(payload[pos])
	pos++
	pos += sessionIDLen

	// cipher_suites: length(2) + data
	if len(payload) < pos+2 {
		return nil, fmt.Errorf("too short for cipher_suites length")
	}
	cipherSuitesLen := int(binary.BigEndian.Uint16(payload[pos : pos+2]))
	pos += 2
	pos += cipherSuitesLen

	// compression_methods: length(1) + data
	if len(payload) < pos+1 {
		return nil, fmt.Errorf("too short for compression length")
	}
	compressionLen := int(payload[pos])
	pos++
	pos += compressionLen

	// extensions: length(2) + data
	if len(payload) < pos+2 {
		return nil, fmt.Errorf("too short for extensions length")
	}
	extensionsLen := int(binary.BigEndian.Uint16(payload[pos : pos+2]))
	pos += 2

	extEnd := pos + extensionsLen
	if extEnd > len(payload) {
		extEnd = len(payload)
	}

	var extIDs []uint16
	for pos+4 <= extEnd {
		extType := binary.BigEndian.Uint16(payload[pos : pos+2])
		extLen := int(binary.BigEndian.Uint16(payload[pos+2 : pos+4]))
		extIDs = append(extIDs, extType)
		pos += 4 + extLen
	}

	return extIDs, nil
}

// parseClientHelloFull parses the raw TLS ClientHello for JA3 computation.
// Returns cipherSuites, extensions, supportedGroups, ecPointFormats, supportedVersions
// all from the raw bytes for consistency.
func parseClientHelloFull(raw []byte) (
	cipherSuites []uint16,
	extensions []uint16,
	supportedGroups []uint16,
	ecPointFormats []uint8,
	supportedVersions []uint16,
	err error,
) {
	// TLS record header
	if len(raw) < 5 || raw[0] != 22 {
		err = fmt.Errorf("not a TLS handshake record")
		return
	}
	payload := raw[5:]

	// Handshake header
	if len(payload) < 4 || payload[0] != 1 {
		err = fmt.Errorf("not a ClientHello")
		return
	}
	pos := 4

	// client_version(2) + random(32)
	if len(payload) < pos+34 {
		err = fmt.Errorf("too short")
		return
	}
	pos += 34

	// session_id
	if len(payload) < pos+1 {
		err = fmt.Errorf("too short for session_id")
		return
	}
	sessionIDLen := int(payload[pos])
	pos++
	pos += sessionIDLen

	// cipher_suites
	if len(payload) < pos+2 {
		err = fmt.Errorf("too short for cipher_suites")
		return
	}
	csLen := int(binary.BigEndian.Uint16(payload[pos : pos+2]))
	pos += 2
	if len(payload) < pos+csLen {
		err = fmt.Errorf("too short for cipher_suites data")
		return
	}
	for i := 0; i < csLen; i += 2 {
		cipherSuites = append(cipherSuites, binary.BigEndian.Uint16(payload[pos+i:pos+i+2]))
	}
	pos += csLen

	// compression_methods
	if len(payload) < pos+1 {
		err = fmt.Errorf("too short for compression")
		return
	}
	compLen := int(payload[pos])
	pos++
	pos += compLen

	// extensions
	if len(payload) < pos+2 {
		err = fmt.Errorf("too short for extensions")
		return
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

		extensions = append(extensions, extType)

		switch extType {
		case 0x000a: // supported_groups (elliptic_curves)
			if len(extData) >= 2 {
				listLen := int(binary.BigEndian.Uint16(extData[0:2]))
				d := extData[2:]
				if len(d) > listLen {
					d = d[:listLen]
				}
				for i := 0; i+1 < len(d); i += 2 {
					supportedGroups = append(supportedGroups, binary.BigEndian.Uint16(d[i:i+2]))
				}
			}
		case 0x000b: // ec_point_formats
			if len(extData) >= 1 {
				fmtLen := int(extData[0])
				d := extData[1:]
				if len(d) > fmtLen {
					d = d[:fmtLen]
				}
				for _, b := range d {
					ecPointFormats = append(ecPointFormats, b)
				}
			}
		case 0x002b: // supported_versions
			if len(extData) >= 1 {
				svLen := int(extData[0])
				d := extData[1:]
				if len(d) > svLen {
					d = d[:svLen]
				}
				for i := 0; i+1 < len(d); i += 2 {
					supportedVersions = append(supportedVersions, binary.BigEndian.Uint16(d[i:i+2]))
				}
			}
		}

		pos += 4 + extLen
	}

	return
}

// computeJA3 computes the JA3 fingerprint from raw ClientHello bytes.
// JA3 format: TLSVersion,CipherSuites,Extensions,EllipticCurves,EllipticCurvePointFormats
func computeJA3(raw []byte) (hash string, text string, err error) {
	// Get TLS version from ClientHello
	if len(raw) < 5+4+2 {
		err = fmt.Errorf("too short for JA3")
		return
	}
	payload := raw[5:]   // skip record header
	tlsVersion := binary.BigEndian.Uint16(payload[4:6]) // client_version in ClientHello

	cs, exts, groups, points, _, parseErr := parseClientHelloFull(raw)
	if parseErr != nil {
		err = parseErr
		return
	}

	// Build JA3 string
	// Field 1: TLS version
	ja3Parts := make([]string, 5)
	ja3Parts[0] = strconv.Itoa(int(tlsVersion))

	// Field 2: cipher suites (joined by -)
	csStrs := make([]string, len(cs))
	for i, v := range cs {
		csStrs[i] = strconv.Itoa(int(v))
	}
	ja3Parts[1] = strings.Join(csStrs, "-")

	// Field 3: extensions (joined by -)
	extStrs := make([]string, len(exts))
	for i, v := range exts {
		extStrs[i] = strconv.Itoa(int(v))
	}
	ja3Parts[2] = strings.Join(extStrs, "-")

	// Field 4: elliptic curves / supported groups (joined by -)
	grpStrs := make([]string, len(groups))
	for i, v := range groups {
		grpStrs[i] = strconv.Itoa(int(v))
	}
	ja3Parts[3] = strings.Join(grpStrs, "-")

	// Field 5: EC point formats (joined by -)
	ptStrs := make([]string, len(points))
	for i, v := range points {
		ptStrs[i] = strconv.Itoa(int(v))
	}
	ja3Parts[4] = strings.Join(ptStrs, "-")

	text = strings.Join(ja3Parts, ",")
	sum := md5.Sum([]byte(text))
	hash = fmt.Sprintf("%x", sum)
	return
}

// extractSignatureSchemes parses the signature_algorithms extension (0x000d)
// from raw ClientHello bytes.
func extractSignatureSchemes(raw []byte) []uint16 {
	if len(raw) < 5 || raw[0] != 22 {
		return nil
	}
	payload := raw[5:]
	if len(payload) < 4 || payload[0] != 1 {
		return nil
	}
	pos := 4

	// client_version(2) + random(32)
	if len(payload) < pos+34 {
		return nil
	}
	pos += 34

	// session_id
	if len(payload) < pos+1 {
		return nil
	}
	pos++
	pos += int(payload[pos-1])

	// cipher_suites
	if len(payload) < pos+2 {
		return nil
	}
	csLen := int(binary.BigEndian.Uint16(payload[pos : pos+2]))
	pos += 2 + csLen

	// compression
	if len(payload) < pos+1 {
		return nil
	}
	pos++
	pos += int(payload[pos-1])

	// extensions
	if len(payload) < pos+2 {
		return nil
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

		if extType == 0x000d { // signature_algorithms
			if len(extData) >= 2 {
				listLen := int(binary.BigEndian.Uint16(extData[0:2]))
				d := extData[2:]
				if len(d) > listLen {
					d = d[:listLen]
				}
				var schemes []uint16
				for i := 0; i+1 < len(d); i += 2 {
					schemes = append(schemes, binary.BigEndian.Uint16(d[i:i+2]))
				}
				return schemes
			}
		}

		pos += 4 + extLen
	}

	return nil
}

func main() {
	cert, err := generateSelfSignedCert()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Failed to generate cert: %v\n", err)
		os.Exit(1)
	}

	cs := newConnStore()
	hs := newHelloStore()

	tlsConfig := &tls.Config{
		Certificates: []tls.Certificate{cert},
		GetConfigForClient: func(hello *tls.ClientHelloInfo) (*tls.Config, error) {
			// Store the ClientHelloInfo keyed by the underlying conn.
			// hello.Conn is the net.Conn passed to tls.Server, which is our recordConn.
			// We need to find the underlying conn to use as key.
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

	// Custom listener that wraps connections with recordConn
	for {
		rawConn, err := ln.Accept()
		if err != nil {
			fmt.Fprintf(os.Stderr, "Accept error: %v\n", err)
			continue
		}

		go handleConnection(rawConn, tlsConfig, cs, hs)
	}
}

func handleConnection(rawConn net.Conn, tlsConfig *tls.Config, cs *connStore, hs *helloStore) {
	defer rawConn.Close()

	// Wrap connection to record raw bytes (16KB should be plenty for ClientHello)
	rc := newRecordConn(rawConn, 16384)
	cs.put(rawConn, rc)
	defer cs.remove(rawConn)

	tlsConn := tls.Server(rc, tlsConfig)
	defer tlsConn.Close()

	// Set a deadline for the handshake
	tlsConn.SetDeadline(time.Now().Add(10 * time.Second))

	if err := tlsConn.Handshake(); err != nil {
		fmt.Fprintf(os.Stderr, "Handshake error: %v\n", err)
		return
	}

	// Reset deadline for HTTP handling
	tlsConn.SetDeadline(time.Now().Add(10 * time.Second))

	// Read HTTP request
	buf := make([]byte, 4096)
	n, err := tlsConn.Read(buf)
	if err != nil && err != io.EOF {
		fmt.Fprintf(os.Stderr, "Read error: %v\n", err)
		return
	}
	_ = n // We don't need to parse the HTTP request in detail

	// Build the inspection result from raw bytes + ClientHelloInfo
	recorded := rc.getRecordedBytes()
	hello := hs.get(rawConn)
	defer hs.remove(rawConn)

	result := TLSInspectionResult{}

	// Get data from ClientHelloInfo (Go-parsed)
	if hello != nil {
		result.ServerName = hello.ServerName
		result.ALPN = hello.SupportedProtos
		if result.ALPN == nil {
			result.ALPN = []string{}
		}

		// SupportedPoints from ClientHelloInfo
		result.SupportedPoints = Uint8Slice(hello.SupportedPoints)
		if result.SupportedPoints == nil {
			result.SupportedPoints = Uint8Slice{}
		}
	}

	// Get cipher suites, extensions, groups, point formats, supported versions from raw bytes
	if len(recorded) > 0 {
		rawCS, rawExts, rawGroups, _, rawSV, parseErr := parseClientHelloFull(recorded)
		if parseErr == nil {
			result.CipherSuites = rawCS
			result.Extensions = rawExts
			result.SupportedGroups = rawGroups
			result.SupportedVersions = rawSV
		} else {
			fmt.Fprintf(os.Stderr, "Parse error: %v\n", parseErr)
			// Fallback to ClientHelloInfo for cipher suites
			if hello != nil {
				result.CipherSuites = hello.CipherSuites
			}
		}

		if result.CipherSuites == nil {
			result.CipherSuites = []uint16{}
		}
		if result.Extensions == nil {
			result.Extensions = []uint16{}
		}
		if result.SupportedGroups == nil {
			result.SupportedGroups = []uint16{}
		}
		if result.SupportedVersions == nil {
			result.SupportedVersions = []uint16{}
		}

		// Extract signature schemes from raw bytes
		result.SignatureSchemes = extractSignatureSchemes(recorded)
		if result.SignatureSchemes == nil {
			result.SignatureSchemes = []uint16{}
		}

		// Compute JA3
		ja3Hash, ja3Text, ja3Err := computeJA3(recorded)
		if ja3Err == nil {
			result.JA3Hash = ja3Hash
			result.JA3Text = ja3Text
		} else {
			fmt.Fprintf(os.Stderr, "JA3 error: %v\n", ja3Err)
		}
	}

	// Build JSON response
	jsonData, err := json.Marshal(result)
	if err != nil {
		fmt.Fprintf(os.Stderr, "JSON marshal error: %v\n", err)
		return
	}

	// Write HTTP response
	response := fmt.Sprintf("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s", len(jsonData), jsonData)
	tlsConn.Write([]byte(response))
}

// Ensure we implement net.Listener for potential future use
type tlsInspectListener struct {
	net.Listener
	connStore *connStore
}

func (l *tlsInspectListener) Accept() (net.Conn, error) {
	conn, err := l.Listener.Accept()
	if err != nil {
		return nil, err
	}
	rc := newRecordConn(conn, 16384)
	l.connStore.put(conn, rc)
	return rc, nil
}

// Verify interfaces are satisfied
var _ net.Conn = (*recordConn)(nil)
var _ http.Handler = http.HandlerFunc(nil)
