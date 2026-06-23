#!/usr/bin/env bash
#
# Concurrency / load test for the async PromptEngine API.
# Each worker POSTs /generate (submit), polls GET /job/<id> until done, and records
# end-to-end latency (submit through completion). Reports success/failure counts,
# wall-clock time, throughput, and latency min/avg/max.
#
# Usage:
#   ./scripts/load_test.sh                      # 20 jobs, default prompt
#   ./scripts/load_test.sh 50                   # 50 concurrent jobs
#   ./scripts/load_test.sh 50 "Say hi"          # 50 jobs with a custom prompt
#   HOST=localhost PORT=18080 ./scripts/load_test.sh 50
#   POLL_INTERVAL=0.5 POLL_MAX=180 ./scripts/load_test.sh 50

set -u

HOST="${HOST:-localhost}"
PORT="${PORT:-18080}"
BASE="http://${HOST}:${PORT}"
REQUESTS="${1:-20}"
PROMPT="${2:-Say hello in one short sentence}"
POLL_INTERVAL="${POLL_INTERVAL:-1}"
POLL_MAX="${POLL_MAX:-120}"

if ! [[ "$REQUESTS" =~ ^[0-9]+$ ]] || [ "$REQUESTS" -lt 1 ]; then
  echo "Error: number of requests must be a positive integer (got '$REQUESTS')" >&2
  exit 1
fi

RESULTS_DIR="$(mktemp -d)"
trap 'rm -rf "$RESULTS_DIR"' EXIT

# macOS BSD `date` has no %N, so use perl's Time::HiRes for sub-second wall time.
now() { perl -MTime::HiRes -e 'printf "%.6f", Time::HiRes::time()'; }

# Submit + poll until COMPLETED (200), FAILED (500), error, or timeout.
# Writes one line to the result file: "<final_http_code> <elapsed_seconds>"
run_job() {
  local i="$1"
  local start end elapsed submit_resp submit_code job_id poll_resp poll_code

  start=$(now)

  submit_resp=$(curl -s -w "\n%{http_code}" \
    -X POST "${BASE}/generate" \
    -H "Content-Type: application/json" \
    -d "{\"prompt\": \"${PROMPT}\"}")

  submit_code=$(printf '%s\n' "$submit_resp" | tail -n1)
  job_id=$(printf '%s\n' "$submit_resp" | sed '$d' | head -n1 | tr -d '[:space:]')

  if [[ "$submit_code" != "200" ]] || [[ ! "$job_id" =~ ^[0-9]+$ ]]; then
    end=$(now)
    elapsed=$(echo "$end - $start" | bc -l)
    printf '%s %.6f\n' "${submit_code:-000}" "$elapsed" > "${RESULTS_DIR}/req_${i}"
    return
  fi

  poll_code="000"
  for ((poll = 1; poll <= POLL_MAX; poll++)); do
    poll_resp=$(curl -s -w "\n%{http_code}" "${BASE}/job/${job_id}")
    poll_code=$(printf '%s\n' "$poll_resp" | tail -n1)

    case "$poll_code" in
      200|404|500) break ;;
      202) sleep "$POLL_INTERVAL" ;;
      *) break ;;
    esac
  done

  if [[ "$poll_code" == "202" ]]; then
    poll_code="000"
  fi

  end=$(now)
  elapsed=$(echo "$end - $start" | bc -l)
  printf '%s %.6f\n' "$poll_code" "$elapsed" > "${RESULTS_DIR}/req_${i}"
}

echo "Firing ${REQUESTS} concurrent submit+poll jobs at ${BASE} ..."

wall_start=$(now)
for i in $(seq 1 "$REQUESTS"); do
  run_job "$i" &
done
wait
wall_end=$(now)

cat "${RESULTS_DIR}"/req_* > "${RESULTS_DIR}/all"

awk -v wall="$(echo "$wall_end - $wall_start" | bc -l)" -v total="$REQUESTS" '
{
  code[NR] = $1
  t = $2
  times[NR] = t
  if ($1 == 200) { ok++ } else { fail++ }
  sum += t
  if (min == "" || t < min) min = t
  if (t > max) max = t
}
END {
  printf "\n================ Load Test Results ================\n"
  printf "Total jobs     : %d\n", total
  printf "Completed (200): %d\n", ok + 0
  printf "Failed/other   : %d\n", fail + 0
  printf "Wall-clock time: %.3fs\n", wall
  if (wall > 0) printf "Throughput     : %.2f completed jobs/s\n", (ok + 0) / wall
  if (NR > 0) {
    printf "E2E latency min: %.3fs\n", min
    printf "E2E latency avg: %.3fs\n", sum / NR
    printf "E2E latency max: %.3fs\n", max
  }
  printf "(E2E = POST /generate through GET /job/<id> completion)\n"
  printf "===================================================\n"
}
' "${RESULTS_DIR}/all"