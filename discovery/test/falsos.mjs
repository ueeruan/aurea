// KV e D1 de mentira para os testes do Node (nada aqui fala com a Cloudflare).
//
// O D1 falso roda a MIGRAÇÃO de verdade num SQLite em memória (node:sqlite):
// UNIQUE, ON CONFLICT e a transação do batch se comportam como no D1.
import { DatabaseSync } from "node:sqlite";
import { readFileSync } from "node:fs";

export function kvFalso(relogio = () => Date.now()) {
  const m = new Map();
  const vivo = (k) => {
    const v = m.get(k);
    if (!v) return null;
    if (v.expira && v.expira <= relogio()) { m.delete(k); return null; }
    return v;
  };
  return {
    mapa: m,
    escritas: 0,
    async get(k, tipo) {
      const v = vivo(k);
      if (!v) return null;
      return tipo === "json" ? JSON.parse(v.valor) : v.valor;
    },
    async put(k, valor, opcoes = {}) {
      this.escritas++;
      if (opcoes.expirationTtl !== undefined && opcoes.expirationTtl < 60) throw new Error("KV: expirationTtl minimo 60");
      m.set(k, { valor: String(valor), expira: opcoes.expirationTtl ? relogio() + opcoes.expirationTtl * 1000 : 0, metadata: opcoes.metadata ?? null });
    },
    async delete(k) { m.delete(k); },
    async list({ prefix = "", limit = 1000, cursor } = {}) {
      const nomes = [...m.keys()].filter((k) => k.startsWith(prefix) && vivo(k)).sort();
      const inicio = cursor ? Number(cursor) : 0;
      const fatia = nomes.slice(inicio, inicio + limit);
      const fim = inicio + fatia.length >= nomes.length;
      return { keys: fatia.map((name) => ({ name, metadata: m.get(name).metadata })), list_complete: fim, cursor: fim ? undefined : String(inicio + fatia.length) };
    },
  };
}

export function d1Falso() {
  const db = new DatabaseSync(":memory:");
  db.exec(readFileSync(new URL("../migrations/0001_contas.sql", import.meta.url), "utf8"));
  db.exec(readFileSync(new URL("../migrations/0002_sessoes_limites.sql", import.meta.url), "utf8"));
  db.exec(readFileSync(new URL("../migrations/0003_relatos.sql", import.meta.url), "utf8"));
  db.exec(readFileSync(new URL("../migrations/0004_comunidade.sql", import.meta.url), "utf8"));
  const limpar = (r) => (r === undefined ? null : { ...r });
  const declaracao = (sql, params = []) => ({
    bind: (...p) => declaracao(sql, p),
    first: async (coluna) => {
      const r = limpar(db.prepare(sql).get(...params));
      return r && coluna ? r[coluna] : r;
    },
    all: async () => ({ results: db.prepare(sql).all(...params).map(limpar), success: true }),
    run: async () => ({ success: true, meta: { changes: Number(db.prepare(sql).run(...params).changes) } }),
    executar: () => db.prepare(sql).run(...params),
  });
  return {
    sqlite: db,
    prepare: (sql) => declaracao(sql),
    async batch(lista) {
      db.exec("BEGIN");
      try {
        const out = lista.map((d) => d.executar());
        db.exec("COMMIT");
        return out.map((r) => ({ success: true, meta: { changes: Number(r.changes) } }));
      } catch (e) {
        db.exec("ROLLBACK");
        throw new Error("D1_ERROR: " + e.message);
      }
    },
  };
}

export function pedido(caminho, { metodo = "GET", corpo, token, ip = "198.51.100.7", cabecalhos = {} } = {}) {
  const h = { "cf-connecting-ip": ip, ...cabecalhos };
  if (corpo !== undefined && !h["content-type"]) h["content-type"] = "application/json";
  if (token) h.authorization = `Bearer ${token}`;
  return new Request("https://aurea-ai-discovery.aureaapp.workers.dev" + caminho, {
    method: metodo,
    headers: h,
    body: corpo === undefined ? undefined : typeof corpo === "string" ? corpo : JSON.stringify(corpo),
  });
}
