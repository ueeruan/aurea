-- 1.0.6: o download de arquivo da Comunidade (community.js, download) confere se o
-- arquivo é avatar de alguém ou anexo de um post. Sem estes índices cada imagem
-- varria as duas tabelas inteiras (~3,7 mil linhas por pedido) e, em 23/11/2026,
-- estourou o teto diário de leituras do D1 grátis: login por e-mail ficou fora.
CREATE INDEX IF NOT EXISTS community_profiles_avatar ON community_profiles(avatar);
CREATE INDEX IF NOT EXISTS community_posts_asset ON community_posts(asset);
