#!/usr/bin/env bash
#
# Concurrency / load test for the PromptEngine server.
# Fires N requests in parallel against /generate, then reports aggregate stats:
# success/failure counts, total wall-clock time, throughput, and latency min/avg/max.
#
# Usage:
#   ./scripts/load_test.sh                      # 20 requests, default prompt
#   ./scripts/load_test.sh 50                   # 50 concurrent requests
#   ./scripts/load_test.sh 50 "Say hi"         # 50 requests with a custom prompt
#   HOST=localhost PORT=18080 ./scripts/load_test.sh 50

set -u

HOST="${HOST:-localhost}"
PORT="${PORT:-18080}"
BASE="http://${HOST}:${PORT}"
REQUESTS="${1:-20}"
PROMPT="${2:-Say hello in one short sentence}"

if ! [[ "$REQUESTS" =~ ^[0-9]+$ ]] || [ "$REQUESTS" -lt 1 ]; then
  echo "Error: number of requests must be a positive integer (got '$REQUESTS')" >&2
  exit 1
fi

RESULTS_DIR="$(mktemp -d)"
trap 'rm -rf "$RESULTS_DIR"' EXIT

echo "Firing ${REQUESTS} concurrent requests at ${BASE}/generate ..."

# Each request writes a single line: "<http_code> <time_total>" to its own file.
# Note: macOS BSD `date` has no %N, so use perl's Time::HiRes for sub-second wall time.
now() { perl -MTime::HiRes -e 'printf "%.6f", Time::HiRes::time()'; }
wall_start=$(now)
for i in $(seq 1 "$REQUESTS"); do
  (
    curl -s -o /dev/null \
      -w "%{http_code} %{time_total}\n" \
      -X POST "${BASE}/generate" \
      -H "Content-Type: application/json" \
      -d "{\"prompt\": \"${PROMPT}\"}" \
      > "${RESULTS_DIR}/req_${i}"
  ) &
done
wait
wall_end=$(now)

cat "${RESULTS_DIR}"/req_* > "${RESULTS_DIR}/all"

awk -v wall="$(echo "$wall_end - $wall_start" | bc -l)" -v total="$REQUESTS" '
{
  code[NR] = $1
  t = $2
  times[NR] = t
  if ($1 >= 200 && $1 < 300) { ok++ } else { fail++ }
  sum += t
  if (min == "" || t < min) min = t
  if (t > max) max = t
}
END {
  printf "\n================ Load Test Results ================\n"
  printf "Total requests : %d\n", total
  printf "Successful     : %d\n", ok + 0
  printf "Failed         : %d\n", fail + 0
  printf "Wall-clock time: %.3fs\n", wall
  if (wall > 0) printf "Throughput     : %.2f req/s\n", total / wall
  if (NR > 0) {
    printf "Latency min    : %.3fs\n", min
    printf "Latency avg    : %.3fs\n", sum / NR
    printf "Latency max    : %.3fs\n", max
  }
  printf "===================================================\n"
}
' "${RESULTS_DIR}/all"
