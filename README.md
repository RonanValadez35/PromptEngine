# PromptEngine

A multithreaded HTTP inference gateway written in modern C++20. PromptEngine
accepts prompts over HTTP, queues them, and dispatches them across a pool of
worker threads to an [Ollama](https://ollama.com) LLM backend. The API is
**asynchronous**: submitting a prompt returns a job ID immediately, and the
caller polls a separate endpoint for the result.

The project is intentionally built from low-level primitives (`std::mutex`,
`std::condition_variable`, `std::thread`, `std::atomic`, `std::shared_ptr`) to
demonstrate the producer/consumer pattern, thread-pool design, RAII resource
ownership, and safe cross-thread state sharing — rather than leaning on a
higher-level job framework.

## Architecture

```
   POST /generate  ┌──────────────────────────────────────────────┐
   {prompt} ─────▶ │                 server (Crow)                │
                   │  handler: parse JSON ─▶ build Job ─┐         │
   ◀── 200 {jobId} │   register JobState in JobRegistry │         │
                   │                                    │ push    │
   GET /job/<id>   │  handler: look up JobState ──▶ read status   │
   ─────────────▶  │   202 processing / 200 result / 500 error    │
   ◀── status/body └────────────────────┬─────────────────────────┘
                                         │ push(Job&&)         ▲
                                         ▼                     │ shared_ptr<JobState>
                             ┌───────────────────────┐         │ (status, result, error)
                             │   TSQueue (thread-safe │         │
                             │   queue: mutex + condv)│         │
                             └───────────┬───────────┘         │
                                         │ pop()               │
                  ┌──────────────────────┼──────────────────────┐
                  ▼                      ▼                      ▼
            ┌───────────┐          ┌───────────┐          ┌───────────┐
            │  worker 0 │   ...    │  worker k │   ...    │  worker N │
            │OllamaRunner│         │OllamaRunner│         │OllamaRunner│
            └─────┬─────┘          └─────┬─────┘          └─────┬─────┘
                  └──────────── HTTPS ───┴──────────────────────┘
                                         ▼
                                Ollama /api/chat
```

The HTTP handler and the worker share a `std::shared_ptr<JobState>`: the worker
writes status/result into it, and `GET /job/<id>` reads from the same object via
the `JobRegistry`.

Each component maps to a source file:

| Component      | Files                                          | Responsibility                                                                 |
| -------------- | ---------------------------------------------- | ----------------------------------------------------------------------------- |
| HTTP server    | `src/server.cpp`                               | Crow app; accepts prompts on `/generate`, returns a job ID, and serves status/results on `/job/<id>`. |
| Job / JobState | `include/job.h`                                | `Job` carries id, prompt, and a `shared_ptr<JobState>`; `JobState` holds an `atomic` status enum, the result, and any error message. |
| Job registry   | `include/job_registry.h`, `src/job_registry.cpp` | Mutex-guarded `id → shared_ptr<JobState>` map so the `/job/<id>` handler can look up a job's current state. |
| Thread-safe queue | `include/ts_queue.h`, `src/ts_queue.cpp`    | Blocking FIFO using `std::mutex` + `std::condition_variable`.                  |
| Thread pool    | `include/thread_pool.h`, `src/thread_pool.cpp` | Spawns N workers, each draining the queue; clean shutdown via sentinel jobs.   |
| Ollama client  | `include/ollama_runner.h`, `src/ollama_runner.cpp` | libcurl wrapper that POSTs to the Ollama chat API and extracts the response. |

### Design notes

- **Producer/consumer:** the HTTP handler is the producer, worker threads are
  consumers. They communicate only through the thread-safe queue, keeping
  request acceptance decoupled from inference.
- **Async submit/poll:** `POST /generate` registers a `JobState`, enqueues the
  job, and immediately returns a job ID — it never blocks on inference. The
  caller polls `GET /job/<id>` to observe progress and retrieve the result.
- **Shared state, not promises:** each `Job` and the registry hold a
  `std::shared_ptr<JobState>` to the *same* object. A worker writes the result
  (or error) and then sets an `std::atomic<status>`; the polling handler reads
  that atomic and, only once it observes `COMPLETED`/`FAILED`, reads the
  corresponding string. The atomic provides the ordering guarantee that makes
  the result visible without a data race.
- **Graceful shutdown:** the `ThreadPool` destructor pushes one sentinel job
  (`jobId == -1`) per worker as a poison pill, then joins every thread, so no
  request is dropped mid-flight and no thread is detached.
- **RAII everywhere:** `OllamaRunner` owns its curl handle and is non-copyable /
  non-movable; the curl handle is released in its destructor. The pool joins all
  threads on destruction.
- **Per-worker curl handle:** each worker constructs its own `OllamaRunner`, so
  there is no shared mutable curl state across threads.

## Requirements

- A C++20 compiler (`g++` / `clang++`)
- [libcurl](https://curl.se/libcurl/)
- [Crow](https://github.com/CrowCpp/Crow) (header-only HTTP framework)
- [GoogleTest](https://github.com/google/googletest) (for the test targets)
- An Ollama API key

On macOS (Homebrew):

```bash
brew install curl googletest
# Crow is header-only; install it or vendor crow.h on your include path.
```

The `Makefile` targets Homebrew's default prefix (`/opt/homebrew`). Adjust the
`-I` / `-L` paths if your dependencies live elsewhere.

## Configuration

The Ollama API key is read from the `OLLAMA_KEY` environment variable at
runtime:

```bash
export OLLAMA_KEY="your-ollama-api-key"
```

## Build & Run

```bash
# Build the server
make server

# Run it (listens on http://localhost:18080)
make run_server
```

The default model is set in `include/ollama_runner.h` and can be changed by
passing a model name to the `OllamaRunner` constructor.

## API

The API is asynchronous: submit a prompt to get a job ID, then poll for the
result.

### `POST /generate`

Submit a prompt for completion. Returns immediately with a job ID; the prompt is
processed in the background by a worker thread.

Request body:

```json
{ "prompt": "Say hello in one short sentence" }
```

Example:

```bash
curl -X POST http://localhost:18080/generate \
  -H "Content-Type: application/json" \
  -d '{"prompt": "Say hello in one short sentence"}'
```

Responses:

| Status | Meaning                                          |
| ------ | ------------------------------------------------ |
| `200`  | Job accepted; body is the job ID (plain integer). |
| `400`  | Invalid / unparseable JSON body.                 |

### `GET /job/<id>`

Poll the status and result of a previously submitted job.

```bash
curl -i http://localhost:18080/job/0
```

Responses:

| Status | Meaning                                                        |
| ------ | -------------------------------------------------------------- |
| `200`  | Job completed; body is the generated text.                    |
| `202`  | Job still `QUEUED` or `PROCESSING`; poll again.               |
| `404`  | No job with that ID in the registry.                          |
| `500`  | Job `FAILED` (e.g. upstream Ollama error); body is the message. |

Typical flow — submit, capture the ID, then poll until it completes:

```bash
ID=$(curl -s -X POST http://localhost:18080/generate \
  -H "Content-Type: application/json" \
  -d '{"prompt": "Say hello in one short sentence"}')
curl -i "http://localhost:18080/job/$ID"
```

## Testing & Benchmarking

Unit tests use GoogleTest:

```bash
make run_tests          # thread-safe queue + thread pool tests
make run_ollama_test    # Ollama client parsing test
```

### Throughput benchmark

`tests/bench_thread_pool.cpp` exercises the real `ThreadPool` + `TSQueue` with a
mock, sleep-based `OllamaRunner` (`tests/mock_ollama_runner.cpp`) so the pool's
scaling with worker count is observable without hitting the network:

```bash
make bench


With a fixed per-job delay, N workers complete the batch roughly N times faster,
making the pool's parallelism visible.

### Live load test

`scripts/load_test.sh` fires concurrent requests at a running server and reports
success/failure counts, wall-clock time, throughput, and latency min/avg/max:

```bash
./scripts/load_test.sh 50 "Say hi"
```

`scripts/test_server.sh` runs a quick functional check of the endpoints
(valid prompt, invalid JSON, and a small concurrent burst).

## Project layout

```
include/   public headers (job, ts_queue, thread_pool, ollama_runner)
src/       implementations + server entry point
tests/     gtest unit tests, mock runner, and the throughput benchmark
scripts/   load_test.sh and test_server.sh
Makefile   build targets
```

## Future architecture: durability

The current design is single-process and fully in-memory: pending work lives in
the `TSQueue` and job status/results live in the `JobRegistry` map. If the
process restarts, both are lost — queued jobs disappear and `GET /job/<id>`
returns `404` for work that had already completed.

Making the system durable means addressing two separate problems:

1. **Persist results (the registry).** Back `JobRegistry` with a durable store
   so completed/in-flight job records survive a restart. The existing
   `insertRegistry` / `getRegistry` interface is the seam — only the
   implementation changes, not the callers.
2. **Persist the queue (recover in-flight work).** If a worker dies mid-job, the
   job is stuck `PROCESSING` forever. A durable, recoverable queue is needed.

### Target design

- **Postgres = durable source of truth.** A `jobs` table
  (`id, status, prompt, result, error, created_at, updated_at`) holds every job.
  This is what actually guarantees no data loss on restart. The same table can
  act as the work queue using `SELECT … FOR UPDATE SKIP LOCKED`, letting N
  workers claim distinct jobs without contention.
- **Redis = optional speed layer, not durability.** Redis is primarily
  in-memory, so it is *not* the durability mechanism. It is justified only as a
  cache for high-volume `GET /job/<id>` polling, or as a faster queue broker in
  front of Postgres — a performance optimization, not the safety net.

A clean, defensible version of this is **Postgres only**: durable store +
`SKIP LOCKED` queue + a startup/sweeper recovery pass that requeues any job left
in `PROCESSING` past a lease timeout. Redis is added only when there is a
measured reason to.

### Correctness notes

- **Acknowledge after commit.** Only return `202 Accepted` once the job is
  committed to durable storage; acking after an in-memory push leaves a
  lost-write window.
- **At-least-once semantics.** Crash-recovery retries mean a job can run twice,
  so handlers should be idempotent (e.g. via an idempotency key on submit).
- **Crash recovery.** On startup, requeue anything not `COMPLETED`/`FAILED`.

## Other future work

- Eviction / TTL for old completed jobs so the registry does not grow forever.
- Request timeouts, retries, and bounded-queue backpressure under load.
- Replace `std::cout` debug logging with a leveled logger.
- Metrics/observability: queue depth, processing latency, failure rate.


Worker threads in the pool could block indefinitely because neither the outbound Ollama HTTP call nor the Postgres queries had any timeout configured — a hung curl_easy_perform or a stalled tx.exec/tx.commit would pin a worker (and the DB connection it held) forever, and once enough connections were pinned, DBPool::acquire() would deadlock the rest of the pool waiting on its unbounded condition variable. We bounded the common cases by adding CURLOPT_CONNECTTIMEOUT/CURLOPT_TIMEOUT to the Ollama client and folding connect_timeout, TCP keepalive/tcp_user_timeout, and statement_timeout into the DB connection string, so hung network or query calls now fail within a bounded time and release their connection instead of hanging. The remaining edge cases — a fully down DB eventually draining the pool, and a transient outage permanently shrinking it (since checkin drops a slot when reconnection fails), leaving acquire() blocked even after recovery — were deliberately left unaddressed as out-of-scope hardening for a controlled, local resume project.