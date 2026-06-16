CREATE TABLE IF NOT EXISTS jobs (
    id              SERIAL PRIMARY KEY,
    prompt          TEXT NOT NULL,
    ollama_response TEXT,
    error           TEXT,
    status          TEXT NOT NULL DEFAULT 'QUEUED'
                    CHECK (status IN ('QUEUED', 'PROCESSING', 'COMPLETED', 'FAILED')),
    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    locked_at       TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS idx_jobs_queue ON jobs (status, created_at);