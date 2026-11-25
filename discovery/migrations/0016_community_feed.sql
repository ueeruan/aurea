ALTER TABLE community_posts ADD COLUMN category TEXT NOT NULL DEFAULT 'text'
  CHECK (category IN ('text', 'showcase', 'project', 'preset', 'announcement'));
ALTER TABLE community_posts ADD COLUMN popularity INTEGER NOT NULL DEFAULT 0;

UPDATE community_posts SET category = (SELECT kind FROM community_assets WHERE id = community_posts.asset)
WHERE asset IN (SELECT id FROM community_assets WHERE kind IN ('project', 'preset'));
UPDATE community_posts SET popularity =
  (SELECT COUNT(*) FROM community_likes WHERE post = community_posts.id) +
  (SELECT COUNT(*) FROM community_comments WHERE post = community_posts.id);

CREATE INDEX community_posts_category ON community_posts(category, created_at DESC, id DESC);
CREATE INDEX community_posts_popular ON community_posts(popularity DESC, created_at DESC, id DESC);
CREATE INDEX community_posts_category_popular ON community_posts(category, popularity DESC, created_at DESC, id DESC);

CREATE TRIGGER community_classify_attached_post AFTER INSERT ON community_posts
WHEN NEW.category = 'text' AND NEW.asset IS NOT NULL BEGIN
  UPDATE community_posts SET category = COALESCE(
    (SELECT kind FROM community_assets WHERE id = NEW.asset AND kind IN ('project', 'preset')), 'text')
  WHERE id = NEW.id;
END;

CREATE TRIGGER community_popularity_like_add AFTER INSERT ON community_likes BEGIN
  UPDATE community_posts SET popularity = popularity + 1 WHERE id = NEW.post;
END;
CREATE TRIGGER community_popularity_like_remove AFTER DELETE ON community_likes BEGIN
  UPDATE community_posts SET popularity = popularity - 1 WHERE id = OLD.post;
END;
CREATE TRIGGER community_popularity_comment_add AFTER INSERT ON community_comments BEGIN
  UPDATE community_posts SET popularity = popularity + 1 WHERE id = NEW.post;
END;
CREATE TRIGGER community_popularity_comment_remove AFTER DELETE ON community_comments BEGIN
  UPDATE community_posts SET popularity = popularity - 1 WHERE id = OLD.post;
END;
