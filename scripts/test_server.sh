#!/usr/bin/env bash
#
# Sends curl requests against the PromptEngine server to exercise the endpoints.
# Usage:
#   ./scripts/test_server.sh                 # uses default host/prompt
#   ./scripts/test_server.sh "your prompt"   # custom prompt
#   HOST=localhost PORT=18080 ./scripts/test_server.sh

set -u

HOST="${HOST:-localhost}"
PORT="${PORT:-18080}"
BASE="http://${HOST}:${PORT}"
PROMPT="${1:-Say hello in one short sentence}"

divider() { printf '\n========== %s ==========\n' "$1"; }

divider "POST /generate (valid prompt)"
curl -s -w "\nHTTP %{http_code} (%{time_total}s)\n" \
  -X POST "${BASE}/generate" \
  -H "Content-Type: application/json" \
  -d "{\"prompt\": \"${PROMPT}\"}"

divider "POST /generate (invalid JSON -> expect 400)"
curl -s -w "\nHTTP %{http_code} (%{time_total}s)\n" \
  -X POST "${BASE}/generate" \
  -H "Content-Type: application/json" \
  -d 'not json'

divider "POST /generate (concurrent x3)"
for i in 1 2 3; do
  (
    resp=$(curl -s -w "\n  [req $i] HTTP %{http_code} (%{time_total}s)" \
      -X POST "${BASE}/generate" \
      -H "Content-Type: application/json" \
      -d "{\"prompt\": \"${PROMPT}\"}")
    printf '%s\n' "--- req $i ---" "$resp"
  ) &
done
wait

printf '\nDone.\n'
