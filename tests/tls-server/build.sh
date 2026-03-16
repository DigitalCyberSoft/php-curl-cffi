#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)/bin"

mkdir -p "$OUTPUT_DIR"

cd "$SCRIPT_DIR"
go build -o "$OUTPUT_DIR/tls-server" .

echo "Built: $OUTPUT_DIR/tls-server"
