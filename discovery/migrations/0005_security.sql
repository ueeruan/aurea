-- Privileges belong to an immutable account ID, never to a client field or a
-- newly registered replacement for an old email address. Seed the existing
-- owner's account once; changing privileges afterwards requires server access.
CREATE TABLE IF NOT EXISTS account_roles (
  uid TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  role TEXT NOT NULL CHECK (role IN ('community_admin')),
  PRIMARY KEY (uid, role)
);
INSERT OR IGNORE INTO account_roles(uid, role)
SELECT id, 'community_admin' FROM users WHERE email = 'ruanpablombl@gmail.com';
CREATE INDEX IF NOT EXISTS sessions_uid ON sessions(uid);
CREATE INDEX IF NOT EXISTS sessions_expiry ON sessions(expires_at);
