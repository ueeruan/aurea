-- Preserve ownership independently of admin grants and additional store receipts.
CREATE TABLE IF NOT EXISTS entitlement_purchases (
  product TEXT NOT NULL,
  purchase_hash TEXT NOT NULL,
  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  created_at INTEGER NOT NULL,
  PRIMARY KEY (product, purchase_hash)
);
INSERT OR IGNORE INTO entitlement_purchases (product, purchase_hash, user_id, created_at)
SELECT product, substr(note, 10), user_id, granted_at FROM entitlements
WHERE source IN ('google', 'apple') AND note LIKE 'purchase:%';
