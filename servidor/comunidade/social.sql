CREATE TABLE IF NOT EXISTS profiles (
 id TEXT PRIMARY KEY, handle TEXT NOT NULL, handle_key TEXT NOT NULL UNIQUE,
 name TEXT NOT NULL DEFAULT '', bio TEXT NOT NULL DEFAULT '', avatar TEXT,
 verified INTEGER NOT NULL DEFAULT 0 CHECK(verified IN (0,1)),
 role TEXT NOT NULL DEFAULT 'user' CHECK(role IN ('user','owner','official')),
 created_at TEXT NOT NULL, deleted_at TEXT
);
CREATE TABLE IF NOT EXISTS credentials (
 hash TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES profiles(id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS follows (
 follower TEXT NOT NULL REFERENCES profiles(id), followed TEXT NOT NULL REFERENCES profiles(id),
 PRIMARY KEY(follower,followed), CHECK(follower != followed)
);
CREATE INDEX IF NOT EXISTS follows_target ON follows(followed,follower);
CREATE TABLE IF NOT EXISTS likes (post_id TEXT NOT NULL, user_id TEXT NOT NULL REFERENCES profiles(id), PRIMARY KEY(post_id,user_id));
CREATE TABLE IF NOT EXISTS blocks (user_id TEXT NOT NULL REFERENCES profiles(id), target TEXT NOT NULL REFERENCES profiles(id), PRIMARY KEY(user_id,target), CHECK(user_id != target));
CREATE TABLE IF NOT EXISTS messages (
 id INTEGER PRIMARY KEY AUTOINCREMENT, sender TEXT NOT NULL REFERENCES profiles(id),
 recipient TEXT NOT NULL REFERENCES profiles(id), text TEXT NOT NULL,
 created_at TEXT NOT NULL, read_at TEXT, client_id TEXT NOT NULL,
 UNIQUE(sender, client_id), CHECK(sender != recipient)
);
CREATE INDEX IF NOT EXISTS messages_pair ON messages(sender,recipient,id);
CREATE INDEX IF NOT EXISTS messages_inbox ON messages(recipient,id);
CREATE TABLE IF NOT EXISTS reports (
 id TEXT PRIMARY KEY, reporter TEXT NOT NULL REFERENCES profiles(id), target TEXT NOT NULL,
 reason TEXT NOT NULL, created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS social_posts (
 id TEXT PRIMARY KEY, author TEXT NOT NULL, parent TEXT,
 created_at TEXT NOT NULL, payload TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS social_posts_feed ON social_posts(parent,created_at DESC,id DESC);
CREATE INDEX IF NOT EXISTS social_posts_author ON social_posts(author,parent,created_at DESC,id DESC);
CREATE TABLE IF NOT EXISTS social_limits (key TEXT PRIMARY KEY, count INTEGER NOT NULL, expires INTEGER NOT NULL);
