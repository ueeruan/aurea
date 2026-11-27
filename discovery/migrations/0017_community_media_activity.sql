CREATE TABLE community_media (
  id TEXT PRIMARY KEY,
  uid TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  kind TEXT NOT NULL CHECK(kind IN ('image', 'video')),
  name TEXT NOT NULL,
  mime TEXT NOT NULL,
  bytes INTEGER NOT NULL,
  created_at INTEGER NOT NULL
);
CREATE INDEX community_media_owner ON community_media(uid);
ALTER TABLE community_posts ADD COLUMN media TEXT REFERENCES community_media(id) ON DELETE SET NULL;
CREATE UNIQUE INDEX community_posts_media ON community_posts(media) WHERE media IS NOT NULL;
CREATE TRIGGER community_drop_post_media AFTER DELETE ON community_posts
WHEN OLD.media IS NOT NULL BEGIN
  DELETE FROM community_media WHERE id=OLD.media;
END;

ALTER TABLE community_comments ADD COLUMN reply_to TEXT REFERENCES community_comments(id) ON DELETE SET NULL;
CREATE INDEX community_comment_replies ON community_comments(reply_to) WHERE reply_to IS NOT NULL;
CREATE TABLE community_comment_activity (
  recipient TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  comment TEXT NOT NULL REFERENCES community_comments(id) ON DELETE CASCADE,
  kind TEXT NOT NULL CHECK(kind IN ('comment', 'reply')),
  created_at INTEGER NOT NULL,
  PRIMARY KEY(recipient, comment)
);
CREATE INDEX community_activity_recent ON community_comment_activity(recipient, created_at DESC, comment DESC);
CREATE INDEX community_activity_comment ON community_comment_activity(comment);
CREATE TABLE community_activity_read (
  uid TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
  seen_at INTEGER NOT NULL
);

CREATE TRIGGER community_comment_notify AFTER INSERT ON community_comments BEGIN
  INSERT INTO community_comment_activity(recipient, comment, kind, created_at)
  SELECT uid, NEW.id, 'reply', NEW.created_at FROM community_comments
  WHERE id=NEW.reply_to AND uid<>NEW.uid;
  INSERT OR IGNORE INTO community_comment_activity(recipient, comment, kind, created_at)
  SELECT uid, NEW.id, 'comment', NEW.created_at FROM community_posts
  WHERE id=NEW.post AND uid<>NEW.uid;
END;
