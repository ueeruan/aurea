// Limite diário por aparelho no livro-caixa (CofreDeVideo), com storage em
// memória. As chamadas passam por uma fila, como o Durable Object entrega os
// eventos (input gates: nada intercala durante as operações de storage).
import { test } from "node:test";
import assert from "node:assert/strict";
import { CofreDeVideo } from "../cofre.js";
import { chaveDoAparelho } from "../ai_video.js";

function cofreDeTeste() {
  const m = new Map();
  const storage = {
    get: async (k) => structuredClone(m.get(k)),
    put: async (k, v) => { m.set(k, structuredClone(v)); },
    delete: async (k) => { for (const x of [].concat(k)) m.delete(x); },
    list: async ({ prefix }) => new Map([...m].filter(([k]) => k.startsWith(prefix))),
    getAlarm: async () => 1, setAlarm: async () => {},
  };
  const c = new CofreDeVideo({ storage }, {});
  let fila = Promise.resolve();
  return new Proxy(c, { get: (o, n) => typeof o[n] === "function"
    ? (...a) => (fila = fila.then(() => o[n](...a))) : o[n] });
}
const limites = { orcamentoDiarioUsd: 100, jobsGlobaisPorDia: 1000, jobsPorAparelhoPorDia: 5, jobsPorIpPorDia: 1000,
  simultaneosGlobais: 100, simultaneosPorAparelho: 100, ticketsPorHora: 100 };
const agora = Date.parse("2026-09-24T12:00:00Z");
const pedido = { mode: "text_to_video", prompt: "x" };

async function geracaoCompleta(c, dev, n, ev = `ev-${dev}-${n}`) {
  const id = `t-${dev}-${n}-xxxxxxxxxxxx`;
  await c.emitirTicket({ id, aparelho: dev, ip: "1.1.1.1", pedido, limites, agora });
  await c.registrarRecompensa({ ticketId: id, eventId: ev, agora });
  return { id, r: await c.reservar({ ticketId: id, aparelho: dev, ip: "1.1.1.1", exigirRecompensa: true, admin: false, custoUsd: 0.1, limites, agora }) };
}

test("5 por aparelho por dia, e 10 pedidos SIMULTANEOS nao passam de 5", async () => {
  const c = cofreDeTeste();
  const rs = await Promise.all(Array.from({ length: 10 }, (_, i) => geracaoCompleta(c, "dA", i)));
  const aceitas = rs.filter((x) => x.r.ok).length;
  assert.equal(aceitas, 5);
  assert.ok(rs.filter((x) => !x.r.ok).every((x) => x.r.erro === "limite_diario" || x.r.erro === undefined));
  assert.deepEqual(await c.cota({ aparelho: "dA", limites, agora }), { used: 5, limit: 5, remaining: 0 });
  // Outro aparelho não é afetado.
  assert.deepEqual(await c.cota({ aparelho: "dB", limites, agora }), { used: 0, limit: 5, remaining: 5 });
  // Dia seguinte (UTC): zera sozinho.
  assert.equal((await c.cota({ aparelho: "dA", limites, agora: agora + 86400000 })).used, 0);
});

test("o mesmo EVENT_ID nao autoriza duas geracoes", async () => {
  const c = cofreDeTeste();
  const a = await geracaoCompleta(c, "dC", 1, "mesmo-evento");
  assert.equal(a.r.ok, true);
  const id2 = "t-dC-2-xxxxxxxxxxxx";
  await c.emitirTicket({ id: id2, aparelho: "dC", ip: "1.1.1.1", pedido, limites, agora });
  const rep = await c.registrarRecompensa({ ticketId: id2, eventId: "mesmo-evento", agora });
  assert.equal(rep.repetido, true);
  const r2 = await c.reservar({ ticketId: id2, aparelho: "dC", ip: "1.1.1.1", exigirRecompensa: true, admin: false, custoUsd: 0.1, limites, agora });
  assert.equal(r2.erro, "recompensa_pendente");
});

test("falha tecnica devolve a geracao do usuario, uma vez so (idempotente)", async () => {
  const c = cofreDeTeste();
  const { id } = await geracaoCompleta(c, "dD", 1);
  await c.confirmar({ ticketId: id, jobId: "job-dD-1-xxxxxxxxx", requestId: "rq1", modelo: "m", agora });
  assert.equal((await c.cota({ aparelho: "dD", limites, agora })).used, 1);
  await c.atualizarJob({ jobId: "job-dD-1-xxxxxxxxx", estado: "FAILED", agora });
  await c.atualizarJob({ jobId: "job-dD-1-xxxxxxxxx", estado: "FAILED", agora });   // repetido
  assert.equal((await c.cota({ aparelho: "dD", limites, agora })).used, 0);
  // Sucesso NÃO devolve.
  const b = await geracaoCompleta(c, "dD", 2);
  await c.confirmar({ ticketId: b.id, jobId: "job-dD-2-xxxxxxxxx", requestId: "rq2", modelo: "m", agora });
  await c.atualizarJob({ jobId: "job-dD-2-xxxxxxxxx", estado: "COMPLETED", saida: "https://x/y.mp4", agora });
  assert.equal((await c.cota({ aparelho: "dD", limites, agora })).used, 1);
});

test("sem recompensa S2S nao ha geracao", async () => {
  const c = cofreDeTeste();
  const id = "t-dE-1-xxxxxxxxxxxx";
  await c.emitirTicket({ id, aparelho: "dE", ip: "1.1.1.1", pedido, limites, agora });
  const r = await c.reservar({ ticketId: id, aparelho: "dE", ip: "1.1.1.1", exigirRecompensa: true, admin: false, custoUsd: 0.1, limites, agora });
  assert.equal(r.erro, "recompensa_pendente");
});

test("chave do aparelho: HMAC do servidor, sem o id bruto, separada por plataforma", async () => {
  const a = await chaveDoAparelho("segredo", "android", "4b1c1d2e-0000-1111-2222-333344445555");
  assert.equal(a, await chaveDoAparelho("segredo", "android", "4b1c1d2e-0000-1111-2222-333344445555"));
  assert.notEqual(a, await chaveDoAparelho("segredo", "ios", "4b1c1d2e-0000-1111-2222-333344445555"));
  assert.notEqual(a, await chaveDoAparelho("outro", "android", "4b1c1d2e-0000-1111-2222-333344445555"));
  assert.ok(!a.includes("4b1c1d2e"));
  assert.match(a, /^d_[A-Za-z0-9_-]{43}$/);
});
