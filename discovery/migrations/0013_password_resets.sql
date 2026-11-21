ALTER TABLE users ADD COLUMN password_changed_at INTEGER NOT NULL DEFAULT 0;

CREATE TABLE password_resets (
    uid TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    token_hash TEXT NOT NULL UNIQUE,
    expires_at INTEGER NOT NULL
);
