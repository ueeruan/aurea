// =============================================================================
//  CofreDeVideo — o livro-caixa da geração de vídeo paga (Durable Object).
//
//  Existe porque há DINHEIRO do outro lado. O KV é eventualmente consistente:
//  duas requisições no mesmo instante leriam o mesmo "gasto do dia" e as duas
//  passariam do teto. Um Durable Object único serializa: cada método roda
//  inteiro, sem `await` externo no meio, então ler-conferir-reservar é atômico.
//
//  O que mora aqui (tudo no storage do objeto, nada no cliente):
//    ticket:<id>   o direito a UMA geração, preso ao aparelho e aos parâmetros
//    evento:<id>   eventId do LevelPlay já usado (um anúncio = uma recompensa)
//    job:<id>      o job aceito pela 8Scale, com o dono e o custo reservado
//    dia:<data>    contadores do dia (UTC): gasto, jobs, por aparelho, por IP
//    ativos        jobs ainda não terminados (limite de simultâneos)
//
//  Nenhum método chama a 8Scale. Quem chama é o Worker, ENTRE `reservar` e
//  `confirmar`/`liberar` — assim uma chamada lenta à 8Scale não segura o cofre.
// =============================================================================

import { DurableObject } from "cloudflare:workers";

const TICKET_TTL_MS = 30 * 60 * 1000;       // o anúncio tem meia hora para acontecer
const RESERVA_TTL_MS = 2 * 60 * 1000;       // "enviando" preso há mais que isso = envio morto
const JOB_TTL_MS = 48 * 60 * 60 * 1000;     // a 8Scale guarda o resultado 24 h; nós, 48
const ATIVO_MAX_MS = 20 * 60 * 1000;        // job sem fim há 20 min deixa de ocupar vaga
const EVENTO_TTL_MS = 8 * 24 * 60 * 60 * 1000; // o LevelPlay reenvia por até 7 dias

const hoje = (agora) => new Date(agora).toISOString().slice(0, 10);

export class CofreDeVideo extends DurableObject {
  async _dia(agora) {
    const chave = `dia:${hoje(agora)}`;
    const d = (await this.ctx.storage.get(chave)) ?? {
      gastoUsd: 0, jobs: 0, porAparelho: {}, porIp: {}, tickets: {},
      // Recompensas S2S válidas que liberaram um ticket, por aparelho (auditoria).
      eventosPorAparelho: {},
    };
    return { chave, d };
  }

  async _ativos(agora) {
    const lista = (await this.ctx.storage.get("ativos")) ?? [];
    // Um job que nunca foi consultado até o fim não pode ocupar vaga para sempre.
    return lista.filter((a) => agora - a.desde < ATIVO_MAX_MS);
  }

  /**
   * Emite o ticket de UMA geração. Nada é gasto aqui: só confere se ainda cabe
   * no dia (para o usuário não assistir a um anúncio à toa) e prende os
   * parâmetros ao ticket — o que for gerado é o que foi validado agora.
   */
  async emitirTicket({ id, aparelho, ip, pedido, limites, agora }) {
    const { chave, d } = await this._dia(agora);
    const hora = Math.floor(agora / 3600000);
    const chaveHora = `${aparelho}:${hora}`;
    const naHora = d.tickets[chaveHora] ?? 0;
    if (naHora >= limites.ticketsPorHora) return { ok: false, erro: "muitos_pedidos" };

    const barrado = this._barrado(d, aparelho, ip, limites, await this._ativos(agora));
    if (barrado) return { ok: false, erro: barrado };

    d.tickets[chaveHora] = naHora + 1;
    await this.ctx.storage.put(chave, d);
    const ticket = {
      id, aparelho, ip, pedido,
      estado: "pendente",          // pendente → recompensado → enviando → consumido
      criado: agora,
      expira: agora + TICKET_TTL_MS,
      eventId: null,
      jobId: null,
      reenvioGratis: true,         // uma nova tentativa sem anúncio se a 8Scale falhar
    };
    await this.ctx.storage.put(`ticket:${id}`, ticket);
    this._agendarLimpeza();
    return { ok: true, expira: ticket.expira };
  }

  /**
   * O callback ASSINADO do LevelPlay. A assinatura já foi conferida pelo Worker;
   * aqui é a regra de negócio: um eventId só vale uma vez, e só para um ticket
   * que existe e ainda espera recompensa.
   */
  async registrarRecompensa({ ticketId, eventId, agora }) {
    if (await this.ctx.storage.get(`evento:${eventId}`)) return { ok: true, repetido: true };
    await this.ctx.storage.put(`evento:${eventId}`, { ticketId, em: agora });

    const t = await this.ctx.storage.get(`ticket:${ticketId}`);
    if (!t) return { ok: true, ignorado: "ticket_desconhecido" };
    if (t.estado !== "pendente") return { ok: true, ignorado: `ticket_${t.estado}` };
    if (agora > t.expira) return { ok: true, ignorado: "ticket_expirado" };
    t.estado = "recompensado";
    t.eventId = eventId;
    await this.ctx.storage.put(`ticket:${ticketId}`, t);
    const { chave, d } = await this._dia(agora);
    d.eventosPorAparelho = d.eventosPorAparelho ?? {};
    d.eventosPorAparelho[t.aparelho] = (d.eventosPorAparelho[t.aparelho] ?? 0) + 1;
    await this.ctx.storage.put(chave, d);
    return { ok: true };
  }

  /**
   * Reserva a vaga e o custo ANTES de chamar a 8Scale. Idempotente: o mesmo
   * ticket duas vezes devolve o job que já existe — duplo toque, rede que
   * repete o POST ou o app que volta do fundo não geram (nem cobram) de novo.
   */
  async reservar({ ticketId, aparelho, ip, exigirRecompensa, admin, custoUsd, limites, agora }) {
    const t = await this.ctx.storage.get(`ticket:${ticketId}`);
    if (!t || t.aparelho !== aparelho) return { ok: false, erro: "ticket_invalido" };
    if (t.jobId) return { ok: true, jaExiste: true, jobId: t.jobId, pedido: t.pedido };
    if (agora > t.expira && t.estado !== "recompensado") return { ok: false, erro: "ticket_expirado" };

    if (t.estado === "enviando") {
      if (agora - (t.reservadoEm ?? 0) < RESERVA_TTL_MS) return { ok: false, erro: "em_andamento" };
      // Envio anterior morreu no meio (Worker caiu): a reserva dele volta.
      await this._desfazerReserva(t, agora);
      t.estado = t.eventId || t.admin ? "recompensado" : "pendente";
    }
    if (t.estado === "consumido") return { ok: false, erro: "ticket_usado" };

    const recompensado = t.estado === "recompensado" || admin;
    if (exigirRecompensa && !recompensado) return { ok: false, erro: "recompensa_pendente" };

    const { chave, d } = await this._dia(agora);
    const ativos = await this._ativos(agora);
    const barrado = this._barrado(d, aparelho, ip, limites, ativos);
    if (barrado) return { ok: false, erro: barrado };
    if (d.gastoUsd + custoUsd > limites.orcamentoDiarioUsd + 1e-9) return { ok: false, erro: "orcamento_diario" };

    d.gastoUsd += custoUsd;
    d.jobs += 1;
    d.porAparelho[aparelho] = (d.porAparelho[aparelho] ?? 0) + 1;
    d.porIp[ip] = (d.porIp[ip] ?? 0) + 1;
    await this.ctx.storage.put(chave, d);
    ativos.push({ ticketId, aparelho, desde: agora });
    await this.ctx.storage.put("ativos", ativos);

    t.estado = "enviando";
    t.admin = t.admin || admin;
    t.reservadoEm = agora;
    t.reserva = { dia: hoje(agora), custoUsd, ip };
    await this.ctx.storage.put(`ticket:${ticketId}`, t);
    return { ok: true, pedido: t.pedido };
  }

  /** A 8Scale aceitou: o ticket vira job e não serve para mais nada. */
  async confirmar({ ticketId, jobId, requestId, modelo, agora }) {
    const t = await this.ctx.storage.get(`ticket:${ticketId}`);
    if (!t) return { ok: false };
    t.estado = "consumido";
    t.jobId = jobId;
    await this.ctx.storage.put(`ticket:${ticketId}`, t);
    await this.ctx.storage.put(`job:${jobId}`, {
      jobId, requestId, modelo, ticketId, aparelho: t.aparelho,
      custoUsd: t.reserva?.custoUsd ?? 0, criado: agora, estado: "IN_QUEUE",
      diaDaReserva: t.reserva?.dia ?? hoje(agora), ipDaReserva: t.reserva?.ip ?? "", reembolsado: false,
      saida: null, execucaoMs: null, erro: null, fim: null,
    });
    const ativos = await this._ativos(agora);
    const a = ativos.find((x) => x.ticketId === ticketId);
    if (a) a.jobId = jobId;
    await this.ctx.storage.put("ativos", ativos);
    return { ok: true };
  }

  /**
   * A 8Scale NÃO aceitou (rede, 4xx, 5xx): nada foi cobrado. A reserva volta e
   * o ticket continua valendo — tentar de novo não pede outro anúncio.
   */
  async liberar({ ticketId, agora }) {
    const t = await this.ctx.storage.get(`ticket:${ticketId}`);
    if (!t || t.estado !== "enviando") return { ok: false };
    await this._desfazerReserva(t, agora);
    t.estado = t.eventId || t.admin ? "recompensado" : "pendente";
    await this.ctx.storage.put(`ticket:${ticketId}`, t);
    return { ok: true };
  }

  async job({ jobId }) {
    return (await this.ctx.storage.get(`job:${jobId}`)) ?? null;
  }

  /**
   * O que a consulta de status trouxe. Terminal libera a vaga; FAILED devolve
   * ao ticket UMA tentativa sem anúncio (a falha foi do provedor, não do
   * usuário) — mas o custo reservado fica: a 8Scale pode ter cobrado.
   */
  async atualizarJob({ jobId, estado, saida, execucaoMs, erro, agora }) {
    const j = await this.ctx.storage.get(`job:${jobId}`);
    if (!j) return null;
    if (j.fim) return j; // terminal é terminal
    j.estado = estado;
    if (saida !== undefined) j.saida = saida;
    if (execucaoMs !== undefined) j.execucaoMs = execucaoMs;
    if (erro !== undefined) j.erro = erro;
    const terminal = estado === "COMPLETED" || estado === "FAILED" || estado === "CANCELLED";
    if (terminal) {
      j.fim = agora;
      const ativos = (await this._ativos(agora)).filter((a) => a.jobId !== jobId);
      await this.ctx.storage.put("ativos", ativos);
      if (estado === "FAILED" || estado === "CANCELLED") {
        // Sem vídeo válido: a geração do USUÁRIO volta (idempotente — uma vez
        // por job). O gasto global fica: a 8Scale pode ter cobrado.
        if (!j.reembolsado) {
          const chaveDia = `dia:${j.diaDaReserva ?? hoje(agora)}`;
          const d = await this.ctx.storage.get(chaveDia);
          if (d) {
            d.porAparelho[j.aparelho] = Math.max(0, (d.porAparelho[j.aparelho] ?? 1) - 1);
            if (j.ipDaReserva) d.porIp[j.ipDaReserva] = Math.max(0, (d.porIp[j.ipDaReserva] ?? 1) - 1);
            await this.ctx.storage.put(chaveDia, d);
          }
          j.reembolsado = true;
        }
        const t = await this.ctx.storage.get(`ticket:${j.ticketId}`);
        if (t && t.reenvioGratis) {
          t.reenvioGratis = false;
          t.estado = "recompensado";
          t.jobId = null;
          t.expira = agora + TICKET_TTL_MS;
          await this.ctx.storage.put(`ticket:${j.ticketId}`, t);
          j.reenvioDisponivel = true;
        }
      }
    }
    await this.ctx.storage.put(`job:${jobId}`, j);
    return j;
  }

  /** Cota do aparelho HOJE (UTC): o que o app mostra como "Gerações de hoje". */
  async cota({ aparelho, limites, agora }) {
    const { d } = await this._dia(agora);
    const limite = limites.jobsPorAparelhoPorDia;
    const usadas = Math.min(limite, d.porAparelho[aparelho] ?? 0);
    return { used: usadas, limit: limite, remaining: Math.max(0, limite - usadas) };
  }

  async painel({ agora }) {
    const { d } = await this._dia(agora);
    return {
      dia: hoje(agora),
      gastoEstimadoUsd: Math.round(d.gastoUsd * 10000) / 10000,
      jobs: d.jobs,
      aparelhos: Object.keys(d.porAparelho).length,
      ativos: (await this._ativos(agora)).length,
    };
  }

  // -- por dentro ------------------------------------------------------------

  _barrado(d, aparelho, ip, limites, ativos) {
    if (d.jobs >= limites.jobsGlobaisPorDia) return "limite_global_diario";
    if ((d.porAparelho[aparelho] ?? 0) >= limites.jobsPorAparelhoPorDia) return "limite_diario";
    if ((d.porIp[ip] ?? 0) >= limites.jobsPorIpPorDia) return "limite_diario";
    if (ativos.length >= limites.simultaneosGlobais) return "servidor_ocupado";
    if (ativos.filter((a) => a.aparelho === aparelho).length >= limites.simultaneosPorAparelho) return "job_em_andamento";
    return null;
  }

  async _desfazerReserva(t, agora) {
    const r = t.reserva;
    if (!r) return;
    const chave = `dia:${r.dia}`;
    const d = await this.ctx.storage.get(chave);
    if (d) {
      d.gastoUsd = Math.max(0, d.gastoUsd - r.custoUsd);
      d.jobs = Math.max(0, d.jobs - 1);
      d.porAparelho[t.aparelho] = Math.max(0, (d.porAparelho[t.aparelho] ?? 1) - 1);
      d.porIp[r.ip] = Math.max(0, (d.porIp[r.ip] ?? 1) - 1);
      await this.ctx.storage.put(chave, d);
    }
    const ativos = (await this._ativos(agora)).filter((a) => a.ticketId !== t.id);
    await this.ctx.storage.put("ativos", ativos);
    t.reserva = null;
    t.reservadoEm = null;
  }

  _agendarLimpeza() {
    this.ctx.storage.getAlarm().then((a) => {
      if (a == null) this.ctx.storage.setAlarm(Date.now() + 60 * 60 * 1000);
    });
  }

  /** De hora em hora: tira ticket vencido, job velho, evento velho e dia velho. */
  async alarm() {
    const agora = Date.now();
    const apagar = [];
    for (const [k, v] of await this.ctx.storage.list({ prefix: "ticket:" })) {
      if (agora > v.expira + JOB_TTL_MS) apagar.push(k);
    }
    for (const [k, v] of await this.ctx.storage.list({ prefix: "job:" })) {
      if (agora - v.criado > JOB_TTL_MS) apagar.push(k);
    }
    for (const [k, v] of await this.ctx.storage.list({ prefix: "evento:" })) {
      if (agora - v.em > EVENTO_TTL_MS) apagar.push(k);
    }
    const limiteDia = hoje(agora - 3 * 24 * 60 * 60 * 1000);
    for (const [k] of await this.ctx.storage.list({ prefix: "dia:" })) {
      if (k.slice(4) < limiteDia) apagar.push(k);
    }
    for (let i = 0; i < apagar.length; i += 128) await this.ctx.storage.delete(apagar.slice(i, i + 128));
    await this.ctx.storage.setAlarm(agora + 60 * 60 * 1000);
  }
}
