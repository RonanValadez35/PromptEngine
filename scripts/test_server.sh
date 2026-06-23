#!/usr/bin/env bash
#
# Functional check for the async PromptEngine API (submit + poll).
# Usage:
#   ./scripts/test_server.sh                 # default host/prompt
#   ./scripts/test_server.sh "your prompt"   # custom prompt
#   HOST=localhost PORT=18080 ./scripts/test_server.sh

set -u

HOST="${HOST:-localhost}"
PORT="${PORT:-18080}"
BASE="http://${HOST}:${PORT}"
PROMPT="${1:-Say hello in one short sentence}"
POLL_INTERVAL="${POLL_INTERVAL:-1}"   # seconds between /job polls
POLL_MAX="${POLL_MAX:-120}"           # max polls before giving up

divider() { printf '\n========== %s ==========\n' "$1"; }

# Submit prompt; print job id. Returns 0 on 200, 1 otherwise.
submit_job() {
  local label="$1"
  local body="$2"
  local resp http_code job_id

  resp=$(curl -s -w "\nHTTP %{http_code} (%{time_total}s)" \
    -X POST "${BASE}/generate" \
    -H "Content-Type: application/json" \
    -d "$body")

  http_code=$(printf '%s\n' "$resp" | tail -n1 | sed -n 's/.*HTTP \([0-9]*\).*/\1/p')
  job_id=$(printf '%s\n' "$resp" | head -n1)

  printf '%s\n' "--- $label ---" "$resp" >&2

  if [[ "$http_code" != "200" ]]; then
    return 1
  fi
  printf '%s\n' "$job_id"
}

# Poll GET /job/<id> until 200 (done), 500 (failed), 404, or timeout.
poll_job() {
  local job_id="$1"
  local label="$2"
  local i code body

  divider "GET /job/${job_id} (${label})"

  for ((i = 1; i <= POLL_MAX; i++)); do
    body=$(curl -s -w "\nHTTP %{http_code}" "${BASE}/job/${job_id}")
    code=$(printf '%s\n' "$body" | tail -n1 | sed -n 's/.*HTTP \([0-9]*\).*/\1/p')
    body=$(printf '%s\n' "$body" | sed '$d')

    case "$code" in
      200)
        printf 'Completed (poll %d):\n%s\nHTTP 200\n' "$i" "$body"
        return 0
        ;;
      202)
        printf '  poll %d: still processing (202)\n' "$i"
        sleep "$POLL_INTERVAL"
        ;;
      404)
        printf 'Job not found (404)\n'
        return 1
        ;;
      500)
        printf 'Failed (500):\n%s\n' "$body"
        return 1
        ;;
      *)
        printf 'Unexpected HTTP %s:\n%s\n' "$code" "$body"
        return 1
        ;;
    esac
  done

  printf 'Timed out after %d polls\n' "$POLL_MAX"
  return 1
}

divider "POST /generate (valid prompt)"
JOB_ID=$(submit_job "valid prompt" "{\"prompt\": \"${PROMPT}\"}") || exit 1
poll_job "$JOB_ID" "valid prompt"

divider "POST /generate (invalid JSON -> expect 400)"
resp=$(curl -s -w "\nHTTP %{http_code} (%{time_total}s)" \
  -X POST "${BASE}/generate" \
  -H "Content-Type: application/json" \
  -d 'not json')
printf '%s\n' "$resp"

divider "POST /generate (concurrent x3)"
job_ids=()
for i in 1 2 3; do
  (
    id=$(submit_job "req $i" "{\"prompt\": \"${PROMPT} (req $i)\"}") || exit 1
    poll_job "$id" "req $i"
  ) &
done
wait

printf '\nDone.\n'