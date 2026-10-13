#!/usr/bin/env bash
set -euo pipefail

PORT=18080
BIN_PATH="${1:-./build/release/socketline_server}"

if [[ ! -f "$BIN_PATH" ]]; then
    echo "Error: Server binary not found at $BIN_PATH"
    exit 1
fi

echo "=== Running Integration Test for Socketline Server ==="
export DARAJA_SECRET="daraja_secret_passkey"

# 1. Start Server
"$BIN_PATH" "$PORT" &
SERVER_PID=$!

cleanup() {
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "Cleaning up server PID $SERVER_PID..."
        kill -SIGTERM "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# Wait for server to listen
for i in {1..20}; do
    if nc -z 127.0.0.1 "$PORT" 2>/dev/null || curl -s "http://127.0.0.1:${PORT}/" >/dev/null 2>&1; then
        echo "Server is ready on port $PORT"
        break
    fi
    sleep 0.1
done

# 2. Test Static File Serving (sendfile zero-copy)
echo "Testing GET /index.html..."
HTTP_CODE=$(curl -s -o /tmp/resp_index.html -w "%{http_code}" "http://127.0.0.1:${PORT}/index.html")
if [[ "$HTTP_CODE" -ne 200 ]]; then
    echo "FAIL: Expected HTTP 200, got $HTTP_CODE"
    exit 1
fi
echo "PASS: Static file served successfully."

# 3. Test Daraja Callback - Invalid HMAC (Expected: 403)
echo "Testing POST /callback with invalid HMAC signature..."
HTTP_CODE_INVALID=$(curl -s -o /dev/null -w "%{http_code}" \
    -X POST "http://127.0.0.1:${PORT}/callback" \
    -H "Content-Type: application/json" \
    -H "X-Hub-Signature: sha256=invalidhex00000000000000000000000000000000000000000000000000000000" \
    -d '{"BusinessShortCode":"174379","amount":100}')
if [[ "$HTTP_CODE_INVALID" -ne 403 ]]; then
    echo "FAIL: Expected 403 Forbidden for bad signature, got $HTTP_CODE_INVALID"
    exit 1
fi
echo "PASS: Rejected invalid HMAC with 403 Forbidden."

# 4. Test Daraja Callback - Valid HMAC (Expected: 200)
PAYLOAD='{"BusinessShortCode":"174379","amount":100}'
# Compute HMAC-SHA256 with openssl
VALID_HMAC=$(printf "%s" "$PAYLOAD" | openssl dgst -sha256 -hmac "$DARAJA_SECRET" | awk '{print $NF}')

echo "Testing POST /callback with valid HMAC signature..."
HTTP_CODE_VALID=$(curl -s -o /dev/null -w "%{http_code}" \
    -X POST "http://127.0.0.1:${PORT}/callback" \
    -H "Content-Type: application/json" \
    -H "X-Hub-Signature: sha256=${VALID_HMAC}" \
    -d "$PAYLOAD")
if [[ "$HTTP_CODE_VALID" -ne 200 ]]; then
    echo "FAIL: Expected 200 OK for valid signature, got $HTTP_CODE_VALID"
    exit 1
fi
echo "PASS: Accepted valid HMAC and processed callback."

# 5. Test Graceful Shutdown (SIGTERM)
echo "Testing SIGTERM graceful drain..."
kill -SIGTERM "$SERVER_PID"
wait "$SERVER_PID"
EXIT_CODE=$?

if [[ "$EXIT_CODE" -ne 0 ]]; then
    echo "FAIL: Server exited with non-zero code $EXIT_CODE"
    exit 1
fi
echo "PASS: Server drained and stopped gracefully with exit code 0."

echo "=== All integration tests PASSED successfully ==="
