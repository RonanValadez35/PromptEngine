# PromptEngine

A multithreaded HTTP inference gateway written in modern C++20. PromptEngine
accepts prompts over HTTP, queues them, and dispatches them across a pool of
worker threads to an [Ollama](https://ollama.com) LLM backend, returning the
generated text to the caller.

The project is intentionally built from low-level primitives (`std::mutex`,
`std::condition_variable`, `std::thread`, `std::promise`/`std::future`) to
demonstrate the producer/consumer pattern, thread-pool design, RAII resource
ownership, and async result propagation — rather than leaning on a higher-level
job framework.

## Architecture

```
                 ┌──────────────────────────────────────────────┐
                 │                 server (Crow)                │
   HTTP POST     │                                              │
  /generate ───▶ │  handler: parse JSON ─▶ build Job ─▶ push ──┐│
                 │                                             ││
                 │   ◀── future.get() (blocks for result)  ◀───┘│
                 └───────────────────┬──────────────────────────┘
                                     │ push(Job&&)
                                     ▼
                         ┌───────────────────────┐
                         │   TSQueue (thread-safe │
                         │   queue: mutex + condv)│
                         └───────────┬───────────┘
                                     │ pop()
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

Each component maps to a source file:

| Component      | Files                                          | Responsibility                                                                 |
| -------------- | ---------------------------------------------- | ----------------------------------------------------------------------------- |
| HTTP server    | `src/server.cpp`                               | Crow app; parses requests, creates `Job`s, returns results.                   |
| Job            | `include/job.h`                                | POD carrying id, prompt, `std::promise<std::string>`, and a status enum.       |
| Thread-safe queue | `include/ts_queue.h`, `src/ts_queue.cpp`    | Bounded-blocking FIFO using `std::mutex` + `std::condition_variable`.          |
| Thread pool    | `include/thread_pool.h`, `src/thread_pool.cpp` | Spawns N workers, each draining the queue; clean shutdown via sentinel jobs.   |
| Ollama client  | `include/ollama_runner.h`, `src/ollama_runner.cpp` | libcurl wrapper that POSTs to the Ollama chat API and extracts the response. |

### Design notes

- **Producer/consumer:** the HTTP handler is the producer, worker threads are
  consumers. They communicate only through the thread-safe queue, keeping
  request acceptance decoupled from inference.
- **Async result delivery:** each `Job` carries a `std::promise`. The handler
  holds the matching `std::future`; a worker fulfills the promise (value or
  exception) when inference completes, and the handler unblocks.
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

### `POST /generate`

Submit a prompt for completion. The request blocks until the worker returns a
result.

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

| Status | Meaning                                      |
| ------ | -------------------------------------------- |
| `200`  | Generated text (plain string body).          |
| `400`  | Invalid / unparseable JSON body.             |
| `500`  | Upstream Ollama error (message in body).     |

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
process restarts, both are lost — queued jobs disappear and `GET /jobs/:id`
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
  cache for high-volume `GET /jobs/:id` polling, or as a faster queue broker in
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
