# PromptEngine

A multithreaded HTTP inference gateway written in modern C++20. PromptEngine
accepts prompts over HTTP POST requests, stores them in a PostgreSQL database, 
and dispatches them across a pool of worker threads to an [Ollama](https://ollama.com) 
LLM backend. The API is **asynchronous**: submitting a prompt returns a job ID immediately, and the
caller polls a separate endpoint for the result. CROW is a c++ open source HTTP webframework that enables me to create simple API endpoints. PostgreSQL is using libpqxx 8.0.1 which requires C++20.

The project is intentionally built from low-level primitives (`std::mutex`,
`std::condition_variable`, `std::thread`, `std::atomic`) to
demonstrate the producer/consumer pattern, thread-pool design, RAII resource
ownership, and safe cross-thread state sharing rather than leaning on a
higher-level job framework.

This project went through multiple design improvements, which can be seen from the various branches
in this repo. The branches are listed in order of first to last below along with what they acomplished:

1. version_1:
    This was the first rendition of the project which sought to implement a multi threaded server that
    could forward prompts to an ollama cloud model (gemma4:31b-cloud). The server would them return the response back to the client via the response message of the POST request. This version only had one POST endpoint /generate. A thread safe queue was built and utalized along with std::promise and std::future to transport the ollama response back to the client. 

    A basic flow of the project looked like the following:
    send POST /generate with prompt --> create Job struct and push onto thread safe queue --> CROW thread waits for ollama_resposne field --> worker thread pops Job sends to ollama via CURL(libcurl) --> worker thread sets promise (ollam_response) --> CROW thread gets response from corresponding future and returns it to client.

    Included in every version is a folder called scripts which includes two bash scripts. By running load_test.sh we can see many metrics for our server. The script showed that with 20 concurrent requests it had a 100% success rate, averaging 6.1s end-to-end latency and 1.68 requests/sec throughput.

    While this approach acheived the goals I had originally set in mind, that being learning lower level design, it was too far away from something that could be seen in production. Mainly having one POST endpoint and having the client wait for the resposne. This was wasteful as I have only two CROW threads running, meaning that not every request message could be pushed onto the queue due to the waiting of the std::promise. 

2. async_client_verion:
      This was the second iteration of the project. The API was redesigned from a single blocking endpoint into an asynchronous job based architecture consisting of POST /generate and GET /job/<id>. Instead of holding an HTTP connection open while inference was running, the server immediately returns a job ID that can later be used to retrieve the result.
      
      To support this change, the design moved away from a std::promise/std::future-based workflow and introduced a shared JobState object. Each Job contains a std::shared_ptr<JobState>, allowing worker threads to update the job status and result as processing progresses. A JobRegistry class was added to maintain a hash map of job IDs to their corresponding JobState objects. This enables clients to poll the GET /job/<id> endpoint for job status and results, a pattern commonly used for long-running tasks in production systems.

      A basic flow of the project looked like the following: 
      send POST /generate with prompt --> create Job struct and push onto thread safe queue --> put jobId and corresponding job state in jobRegistry --> return jobId from POST response --> worker thread pops from thread safe queue --> worker thread calls ollama(libcurl) and puts response into job state object --> client polls GET endpoint with requested jobId for ollama response.   

      Running that same script we get 20 concurrent jobs with a 100% success rate, achieving 3.42 jobs/sec throughput and 3.34s average end-to-end latency.

      This is a drastic improvement from the previous version, but there is still one large feature missing that would make this project more in line with a production grade system. Currently, if a job fails during processing, it remains failed indefinitely. Likewise, if the server shuts down while jobs are queued or in progress, those jobs are lost.

      To address this limitation, the next iteration will introduce persistent storage and a retry mechanism. Persisting jobs in a database would allow them to survive server restarts, while a retry policy would enable transient failures to be automatically reprocessed. Together, these additions would improve the reliability and fault tolerance of the system, bringing it closer to the architecture used in production job-processing services.

3. main(data_persistance):
      This is the most up to date version of the inference server. This version introduces a Postgres local database that stores Job Record structs. The database replaces the Job Registry and the thread safe queue. A db_pool class is introduced that holds a queue containing unique pointers with a custom deleter of a pqxx object. This approach utalizes RAII


      This is the most recent version of the inference server. The architecture was redesigned to use PostgreSQL as the system of record for all job records, replacing the in-memory queue and job registry. Each inference request is persisted in a jobs table, allowing work and results to survive process restarts.

      Worker threads no longer consume jobs from an in-process queue. Instead, they claim work directly from the database using `SELECT ... FOR UPDATE SKIP LOCKED`, enabling multiple workers to safely process distinct jobs concurrently without duplicate work. Database access is managed through a connection pool, reducing connection overhead and allowing workers to efficiently share a fixed number of PostgreSQL connections.

      A dedicated JobStore layer encapsulates all database operations, while a background reaper thread periodically requeues jobs that have been stuck in PROCESSING beyond a lease timeout or have FAILED less than 3 times. This prevents work from being permanently lost if a worker crashes during execution.

      The resulting workflow is:

      POST /generate --> store job as in database as QUEUED --> return job ID --> worker claims job from PostgreSQL --> run inference through Ollama(libcurl) --> update job status to COMPLETED or FAILED --> client retrieves results through GET /job/<id>.

      This redesign transforms the system from an in-memory asynchronous service into a durable job-processing platform. By adding persistent storage, distributed-safe job claiming, connection pooling, and automatic recovery of abandoned work, the architecture more closely resembles the job execution systems used in production environments.

      From running the same script we get 20 concurrent jobs with a 100% success rate, achieving 2.99 jobs/sec throughput and 3.70s average end-to-end latency. So a slightly worse performance than the previous design, but the difference is negligible for the benefits the system recieves by implementing PostgreSQL.

      While this is still nowhere near a production-level project, it replicates many concepts commonly found in production systems, such as asynchronous request handling, persistent job storage, connection pooling, fault tolerant job recovery, concurrent worker processing, and database-backed coordination between services.

      For future improvements check the future work section.


## Architecture Diagram

![PromptEngine high-level architecture](images/PromptEngineHighLevel.drawio.png)

### Design notes

- **Durable queue + registry:** the `jobs` table is both the work queue and the
  result store. No in-memory state is required for correctness across restarts.
- **Async submit/poll:** `POST /generate` commits a `QUEUED` row and returns
  immediately; the caller polls `GET /job/<id>` for `202` (in progress) or
  `200`/`500` (done/failed).
- **Concurrent dispatch:** `claimNextJob` uses `SELECT … FOR UPDATE SKIP
  LOCKED` so each worker atomically claims a distinct job without blocking peers.
- **Lease recovery:** a background reaper calls `requeueJobs()` to reset jobs
  left `PROCESSING` past a timeout, or retry `FAILED` jobs under an attempt cap.
- **RAII connection pool:** `DBPool::ConnectionPtr` is a `unique_ptr` with a
  custom deleter that returns connections to the pool on scope exit, including
  on exceptions.
- **Per-worker curl handle:** each worker owns its own `OllamaRunner`, so there
  is no shared mutable curl state across threads.

## Requirements

For full step-by-step setup, see [Set up steps](#set-up-steps).

- A C++20 compiler (`g++` / `clang++`)
- [libcurl](https://curl.se/libcurl/)
- [Crow](https://github.com/CrowCpp/Crow) (header-only HTTP framework)
- [nlohmann/json](https://github.com/nlohmann/json) (JSON parsing in `OllamaRunner`)
- [libpqxx](https://github.com/jtv/libpqxx) 8.x (PostgreSQL C++ client; requires C++20)
- [PostgreSQL](https://www.postgresql.org/) (job storage)
- [GoogleTest](https://github.com/google/googletest) (for the test targets)
- `DATABASE_URL` and an Ollama API key (`OLLAMA_KEY`)

On macOS (Homebrew):

```bash
brew install curl crow nlohmann-json libpqxx postgresql@16 googletest
```

The `Makefile` targets Homebrew's default prefix (`/opt/homebrew`). Adjust the
`-I` / `-L` paths if your dependencies live elsewhere. See [Set up steps](#set-up-steps)
for database creation and environment variable setup.

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
make test_db_pool
make test_job_store
```

for older versions:
```bash
make test_queue
make test_thread_pool
make run_tests

make test_ollama_runner
make run_ollama_test
```

## Project layout

```
include/   public headers (job, thread_pool, ollama_runner, job_store, db_pool)
src/       implementations + server entry point
tests/     gtest unit tests
scripts/   load_test.sh and test_server.sh
Makefile   build targets
```

### Future Work

- Eviction / TTL for old completed jobs so the registry does not grow forever.
- DB pool hardening on Postgres outage — recover when the database is fully down
  (pool drain leaving `acquire()` blocked) or when `checkin` drops a connection
  slot after a failed reconnect, permanently shrinking the pool even after
  recovery.

## Set up steps

End-to-end instructions for getting PromptEngine running locally. The commands
below assume **macOS with Homebrew** on Apple Silicon (`/opt/homebrew`). On
Intel Macs, replace `/opt/homebrew` with `/usr/local` throughout.

### 1. Install dependencies

| Dependency | Purpose | Install (Homebrew) |
| ---------- | ------- | ------------------ |
| C++20 compiler | Build | Xcode Command Line Tools: `xcode-select --install` |
| [libcurl](https://curl.se/libcurl/) | Ollama HTTP client | `brew install curl` |
| [Crow](https://github.com/CrowCpp/Crow) | HTTP server (header-only) | `brew install crow` |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON parsing in `OllamaRunner` | Usually installed as a Crow dependency; if needed: `brew install nlohmann-json` |
| [libpqxx](https://github.com/jtv/libpqxx) 8.x | Postgres C++ client (requires C++20) | `brew install libpqxx` |
| [PostgreSQL](https://www.postgresql.org/) | Job storage | `brew install postgresql@16` |
| [GoogleTest](https://github.com/google/googletest) | Unit tests (optional) | `brew install googletest` |

Start Postgres and confirm it is listening on port 5432:

```bash
brew services start postgresql@16
pg_isready -h localhost -p 5432
```

If another Postgres install is already bound to port 5432, stop it first or
point `DATABASE_URL` at the instance you intend to use.

#### Crow include path

Crow is header-only. After `brew install crow`, headers land under
`/opt/homebrew/include` (e.g. `crow.h`, `crow/...`). The Makefile already adds
`-I/opt/homebrew/include`, so no extra step is needed on a default Homebrew
layout.

If you clone Crow manually instead:

```bash
git clone https://github.com/CrowCpp/Crow.git ~/Crow
```

add the Crow `include` directory to `CXXFLAGS` in the `Makefile` (and to
`.vscode/c_cpp_properties.json` if you use VS Code IntelliSense):

```makefile
CXXFLAGS = ... -I/path/to/Crow/include
```

### 2. Create the database

Create a database and apply the schema from `sql/schema.sql`:

```bash
createdb promptengine
psql promptengine -f sql/schema.sql
```

Verify the table exists:

```bash
psql promptengine -c "\dt"
```

You should see a `jobs` table with columns for status, prompt, result, error,
timestamps, and claim metadata.

### 3. Environment variables

The server reads two variables at startup/runtime:

| Variable | Required | Description |
| -------- | -------- | ----------- |
| `DATABASE_URL` | Yes | libpqxx connection string for the `promptengine` database. |
| `OLLAMA_KEY` | Yes | API key for [Ollama Cloud](https://ollama.com) (`Authorization: Bearer …`). |

Create a local `.env` file in the project root (this file is gitignored):

```bash
# .env — do not commit
export OLLAMA_KEY="your-ollama-api-key"
export DATABASE_URL="postgresql://YOUR_USER@localhost:5432/promptengine"
```

Replace `YOUR_USER` with your macOS username (or a dedicated Postgres role).
If your database requires a password:

```bash
export DATABASE_URL="postgresql://USER:PASSWORD@localhost:5432/promptengine"
```

Load the variables into your shell before building/running:

```bash
set -a
source .env
set +a
```

`DBPool` automatically appends connection timeouts (`connect_timeout`,
keepalives, `statement_timeout`) to `DATABASE_URL`; you do not need to set
those yourself.

Optional (benchmarks only):

| Variable | Purpose |
| -------- | ------- |
| `MOCK_DELAY_MS` | Artificial delay per job in `make bench` mock runner. |

### 4. Fix include and library paths

The `Makefile` hardcodes Homebrew paths for Apple Silicon:

```makefile
CXXFLAGS = -std=c++20 -I/opt/homebrew/include -Iinclude -I/opt/homebrew/opt/curl/include/curl
LDFLAGS  = -L/opt/homebrew/lib -L/opt/homebrew/opt/libpq/lib
```

Adjust if your layout differs:

- **Intel Mac:** change `/opt/homebrew` → `/usr/local`.
- **Linux (apt):** typical flags are `-I/usr/include/postgresql` and
  `-lpqxx -lpq`; library paths vary by distro.
- **libpqxx not found:** ensure `brew install libpqxx` completed and add
  `-L/opt/homebrew/opt/libpq/lib` (or `$(pg_config --libdir)`) to `LDFLAGS`.

If VS Code IntelliSense cannot find headers, mirror the same `-I` paths in
`.vscode/c_cpp_properties.json` under `includePath` (the repo includes a
macOS template pointing at `/opt/homebrew/include` and
`/opt/homebrew/opt/curl/include/curl`).

Ensure the `build/` directory exists (or let `make` create it on first build):

```bash
mkdir -p build
```

### 5. Build

With `DATABASE_URL` and `OLLAMA_KEY` exported (needed for some test targets,
not for compiling the server itself):

```bash
make server
```

This produces `build/server`.

Optional test binaries:

```bash
make test_db_pool test_job_store
./build/test_db_pool
./build/test_job_store
```

### 6. Run the server

```bash
set -a && source .env && set +a
make run_server
```

The server listens on **http://localhost:18080**.

Quick smoke test in another terminal:

```bash
./scripts/test_server.sh
```

Load test (server must already be running):

```bash
./scripts/load_test.sh 20 "Say hi"
```

Manual curl flow:

```bash
ID=$(curl -s -X POST http://localhost:18080/generate \
  -H "Content-Type: application/json" \
  -d '{"prompt": "Say hello in one short sentence"}')
curl -i "http://localhost:18080/job/$ID"
```

Poll until you get `200` (completed) or `500` (failed). Expect `202` while the
job is still `QUEUED` or `PROCESSING`.

### 7. Common issues

| Symptom | Likely cause | Fix |
| ------- | ------------ | --- |
| `DATABASE_URL not set` on startup | Env var missing | `source .env` before `make run_server`. |
| `OLLAMA_KEY environment variable not set` | Key not exported | Set `OLLAMA_KEY` in `.env` and source it. |
| `fatal error: 'crow.h' file not found` | Crow not on include path | `brew install crow` or add `-I` to `CXXFLAGS`. |
| `fatal error: 'pqxx/pqxx' file not found` | libpqxx not installed | `brew install libpqxx`. |
| Connection refused on 5432 | Postgres not running | `brew services start postgresql@16`. |
| `make server` linker errors for `-lpqxx` | Wrong `-L` path | Add `-L/opt/homebrew/opt/libpq/lib` to `LDFLAGS`. |
| Jobs fail with HTTP 429 on load test | Ollama rate limit | Reduce concurrency or worker count in `src/server.cpp`. |

Inspect persisted jobs directly:

```bash
psql promptengine -c "SELECT id, status, attempts, created_at FROM jobs ORDER BY id DESC LIMIT 10;"
```
