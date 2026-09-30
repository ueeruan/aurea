CREATE TABLE community_profiles (
  uid TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
  username TEXT UNIQUE COLLATE NOCASE,
  name TEXT NOT NULL DEFAULT '',
  bio TEXT NOT NULL DEFAULT '',
  avatar TEXT,
  verification TEXT NOT NULL DEFAULT '' CHECK(verification IN ('', 'blue', 'green', 'gold')),
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
CREATE TABLE community_assets (
  id TEXT PRIMARY KEY,
  uid TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  kind TEXT NOT NULL CHECK(kind IN ('avatar', 'preset', 'project')),
  name TEXT NOT NULL,
  mime TEXT NOT NULL,
  bytes INTEGER NOT NULL,
  created_at INTEGER NOT NULL
);
CREATE INDEX community_assets_owner ON community_assets(uid, created_at);
CREATE TABLE community_posts (
  id TEXT PRIMARY KEY,
  uid TEXT NOT NULL REFERENCES community_profiles(uid) ON DELETE CASCADE,
  body TEXT NOT NULL,
  asset TEXT REFERENCES community_assets(id),
  created_at INTEGER NOT NULL
);
CREATE INDEX community_posts_feed ON community_posts(created_at DESC, id DESC);
CREATE INDEX community_posts_author ON community_posts(uid, created_at DESC, id DESC);
CREATE TABLE community_comments (
  id TEXT PRIMARY KEY,
  post TEXT NOT NULL REFERENCES community_posts(id) ON DELETE CASCADE,
  uid TEXT NOT NULL REFERENCES community_profiles(uid) ON DELETE CASCADE,
  body TEXT NOT NULL,
  created_at INTEGER NOT NULL
);
CREATE INDEX community_comments_post ON community_comments(post, created_at, id);
CREATE TABLE community_likes (
  post TEXT NOT NULL REFERENCES community_posts(id) ON DELETE CASCADE,
  uid TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  PRIMARY KEY(post, uid)
);
CREATE TABLE community_follows (
  follower TEXT NOT NULL REFERENCES community_profiles(uid) ON DELETE CASCADE,
  followed TEXT NOT NULL REFERENCES community_profiles(uid) ON DELETE CASCADE,
  PRIMARY KEY(follower, followed),
  CHECK(follower != followed)
);
CREATE INDEX community_followers ON community_follows(followed, follower);
CREATE TABLE community_verifications (
  id TEXT PRIMARY KEY,
  admin TEXT NOT NULL REFERENCES users(id),
  uid TEXT NOT NULL REFERENCES community_profiles(uid),
  verification TEXT NOT NULL,
  created_at INTEGER NOT NULL
);
