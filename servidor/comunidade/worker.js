/**
 * O SERVIDOR DO MURAL DO AUREA.
 *
 * Um arquivo. Roda no Cloudflare Workers (camada gratuita, sem cartao).
 * Ele existe para uma coisa so: guardar a chave de escrita FORA do
 * aplicativo. Dentro do APK, qualquer chave e publica — um APK e um zip,
 * e achar a chave leva minutos. Aqui, quem tem a chave e o servidor.
 *
 * O QUE ELE FAZ:
 *
 *   POST   /conta          cria a conta, devolve o codigo de acesso
 *   POST   /conta/entrar   volta a entrar com o codigo, noutro aparelho
 *   PATCH  /conta          troca o apelido
 *   GET    /feed           os posts de cima (sem as respostas)
 *   GET    /respostas/:id  as respostas de um post
 *   POST   /post           publica (post, resposta ou repost)
 *   DELETE /post/:id       apaga (o dono, ou a senha de moderacao)
 *   POST   /midia          sobe uma imagem, um video ou um projeto
 *   GET    /midia/:id      serve o arquivo
 *   GET    /aviso          os recados ao vivo (lista, e o primeiro solto)
 *   PUT    /aviso          troca tudo por um recado (senha de moderacao)
 *   POST   /aviso          poe mais um recado embaixo (senha de moderacao)
 *   DELETE /aviso          apaga todos os recados (senha de moderacao)
 *   DELETE /aviso/:id      apaga um recado (senha de moderacao)
 *   POST   /transcricao    transcreve um audio na nuvem (conta + cota)
 *   GET    /transcricao/cota      quanto da cota do dia ainda resta
 *   GET    /transcricao/registro  o registro tecnico (senha de moderacao)
 *
 * A CONTA E DE VERDADE, e nao um apelido digitado a cada post. O
 * servidor guarda a conta, garante que o apelido e unico e devolve um
 * CODIGO DE ACESSO. Dali em diante o autor de um post NAO vem do corpo
 * da requisicao: vem do codigo. E por isso que ninguem se passa por
 * outro — nem mesmo quem monta a requisicao a mao.
 *
 * Nao ha senha nem e-mail de proposito. Senha pede recuperacao,
 * recuperacao pede e-mail, e-mail pede caixa de saida: tres pecas novas
 * para um mural de beta. O codigo de acesso faz o mesmo papel e cabe num
 * bloco de notas.
 *
 * OS PORTOES de quem publica, na ordem:
 *   1. TAMANHO. Corpo acima do teto nem e lido.
 *   2. CONTA. Sem codigo valido, nao passa.
 *   3. FILTRO. O mesmo do aplicativo, repetido aqui — e aqui que vale.
 *   4. LIMITE. Por conta, por hora.
 *   5. FORMATO. So sai o que o aplicativo sabe ler.
 */

const LIMITE_POR_HORA = 20;

/// Contas por IP e por hora. Alto porque um IP pode ser uma casa inteira
/// atras do mesmo roteador; baixo o bastante para nao dar para criar
/// contas em serie e usar cada uma como cota nova de transcricao.
const CONTAS_POR_HORA = 30;

/// Arquivos por conta e por hora. O portao por requisicao ja limita o
/// tamanho; este limita a QUANTIDADE, que e o que enche o KV.
const MIDIAS_POR_HORA = 40;
const TAMANHO_MAXIMO = 16 * 1024;
const MIDIA_MAXIMA = 40 * 1024 * 1024;

/// O que cabe no KV enquanto o R2 nao esta ligado.
const IMAGEM_NO_KV = 2 * 1024 * 1024;
const PROJETO_NO_KV = 2 * 1024 * 1024;

// ------------------------------------------------- transcricao (Groq)

/**
 * A TRANSCRICAO NA NUVEM.
 *
 * A chave da Groq mora AQUI, como secret do Worker (GROQ_API_KEY), e em
 * lugar nenhum do aplicativo. Um APK e um zip: uma chave dentro dele e
 * publica em minutos — codificada, partida em pedacos ou "escondida" em
 * codigo nativo, tanto faz. O aplicativo manda so o audio para ca, com
 * o codigo de acesso da conta; este servidor confere a conta, o ritmo e
 * a cota, repassa o audio para a Groq com a chave dele e devolve o texto
 * com os tempos. O audio vive so na memoria desta requisicao.
 *
 * O que fica guardado e um registro TECNICO por tentativa (conta,
 * duracao, quando, modelo, status, tempo) por trinta dias, e os
 * contadores de cota do dia. Nunca o audio, nunca o texto.
 *
 * O modelo e os limites vem das variaveis do wrangler.toml: trocar o
 * modelo nao pede build novo do app. O formato que sai daqui e o do
 * app (texto, segmentos, palavras), e nao o da Groq — se a Groq mudar,
 * ou for trocada, quem muda e este arquivo.
 */
const GROQ_TRANSCRICAO = 'https://api.groq.com/openai/v1/audio/transcriptions';
const MODELO_DE_TRANSCRICAO_PADRAO = 'whisper-large-v3-turbo';
const AUDIO_MAXIMO = 25 * 1024 * 1024;
const TEMPO_LIMITE_GROQ = 120000;

/** So audio entra — e, por descuido comum, o mp4 so de audio tambem. */
const TIPOS_DE_AUDIO = {
  'audio/mp4': 'm4a',
  'audio/x-m4a': 'm4a',
  'audio/m4a': 'm4a',
  'audio/aac': 'aac',
  'audio/mpeg': 'mp3',
  'audio/mp3': 'mp3',
  'audio/wav': 'wav',
  'audio/x-wav': 'wav',
  'audio/wave': 'wav',
  'audio/flac': 'flac',
  'audio/x-flac': 'flac',
  'audio/ogg': 'ogg',
  'audio/opus': 'ogg',
  'audio/webm': 'webm',
  'video/mp4': 'mp4',
};

const numeroOu = (valor, padrao) => {
  const n = Number(valor);
  return Number.isFinite(n) && n > 0 ? n : padrao;
};

/** O que pode mudar sem build novo do app: vem do wrangler.toml. */
function limitesDeTranscricao(env) {
  return {
    modelo:
      String(env.MODELO_DE_TRANSCRICAO ?? '').trim() || MODELO_DE_TRANSCRICAO_PADRAO,
    porHora: numeroOu(env.TRANSCRICOES_POR_HORA, 10),
    porDia: numeroOu(env.TRANSCRICOES_POR_DIA, 30),
    segundosPorDia: numeroOu(env.SEGUNDOS_DE_AUDIO_POR_DIA, 1800),
    segundosPorDiaTodos: numeroOu(env.SEGUNDOS_DE_AUDIO_POR_DIA_TODOS, 36000),
  };
}

const diaDeHoje = () => new Date().toISOString().slice(0, 10);

// ------------------------------------------------------------ avisos

/// Quantos avisos ficam no ar ao mesmo tempo. Tres ja e uma pilha de
/// recado na frente do trabalho de quem so queria editar um video.
const AVISOS_NO_AR = 3;
const CHAVE_AVISOS = 'aviso:lista';

/**
 * Os avisos no ar, do mais antigo para o mais novo (a ordem em que
 * aparecem na tela), ja sem os vencidos.
 *
 * A chave antiga (`aviso:atual`, um aviso so) continua sendo lida: um
 * servidor que ja estava no ar nao perde o recado ao atualizar.
 */
async function lerAvisos(env) {
  let lista = [];
  const bruto = await env.MURAL.get(CHAVE_AVISOS);
  if (bruto) {
    try {
      const lido = JSON.parse(bruto);
      if (Array.isArray(lido)) lista = lido;
    } catch {
      lista = [];
    }
  } else {
    const antigo = await env.MURAL.get('aviso:atual');
    if (antigo) {
      try {
        lista = [JSON.parse(antigo)];
      } catch {
        lista = [];
      }
    }
  }
  const agora = Date.now();
  return lista.filter(
    (a) => a && a.texto && (!a.ate || Date.parse(a.ate) > agora),
  );
}

async function gravarAvisos(env, lista) {
  await env.MURAL.put(CHAVE_AVISOS, JSON.stringify(lista));
  // A chave antiga acompanha o primeiro: aparelho velho continua vendo
  // alguma coisa, e nao um recado de semanas atras.
  if (lista.length === 0) {
    await env.MURAL.delete('aviso:atual');
  } else {
    await env.MURAL.put('aviso:atual', JSON.stringify(lista[0]));
  }
}

function segundosAteAmanha() {
  const agora = new Date();
  const amanha = Date.UTC(
    agora.getUTCFullYear(),
    agora.getUTCMonth(),
    agora.getUTCDate() + 1,
  );
  return Math.max(1, Math.ceil((amanha - agora.getTime()) / 1000));
}

async function lerUso(env, chave) {
  const bruto = await env.MURAL.get(chave);
  if (!bruto) return { n: 0, segundos: 0 };
  try {
    const u = JSON.parse(bruto);
    return { n: Number(u.n) || 0, segundos: Number(u.segundos) || 0 };
  } catch {
    return { n: 0, segundos: 0 };
  }
}

async function somarUso(env, chave, segundos) {
  const uso = await lerUso(env, chave);
  await env.MURAL.put(
    chave,
    JSON.stringify({ n: uso.n + 1, segundos: uso.segundos + segundos }),
    { expirationTtl: 2 * 86400 },
  );
}

/**
 * So o tecnico: conta, duracao, quando, modelo, status, tempo. Nunca o
 * audio, nunca o texto. Some sozinho em trinta dias.
 */
async function registrarTranscricao(env, contaId, duracao, modelo, status, ms) {
  const quando = new Date().toISOString();
  // O sufixo sorteado separa duas tentativas no mesmo milissegundo —
  // sem ele, a segunda apagaria a primeira.
  const sufixo = crypto.randomUUID().slice(0, 8);
  await env.MURAL.put(
    `registro:transcricao:${ordemDe(quando)}:${contaId}:${sufixo}`,
    JSON.stringify({
      conta: contaId,
      duracao: Math.round(duracao * 10) / 10,
      quando,
      modelo,
      status,
      ms,
    }),
    { expirationTtl: 30 * 86400 },
  );
}

/**
 * Do verbose_json da Groq para o formato do app — e so o que o app usa.
 */
function normalizarTranscricao(bruto, modelo) {
  const segmento = (s) => ({
    inicio: Number(s.start) || 0,
    fim: Number(s.end) || 0,
    texto: String(s.text ?? '').trim(),
  });
  const palavra = (w) => ({
    inicio: Number(w.start) || 0,
    fim: Number(w.end) || 0,
    texto: String(w.word ?? '').trim(),
  });
  return {
    texto: String(bruto.text ?? '').trim(),
    idioma: bruto.language ?? null,
    duracao: Number(bruto.duration) || 0,
    modelo,
    segmentos: (Array.isArray(bruto.segments) ? bruto.segments : [])
      .map(segmento)
      .filter((s) => s.texto && s.fim > s.inicio),
    palavras: (Array.isArray(bruto.words) ? bruto.words : [])
      .map(palavra)
      .filter((p) => p.texto && p.fim >= p.inicio),
  };
}

/** O tipo que sai no cabecalho, tirado da extensao do nome sorteado. */
const TIPO_DA_EXTENSAO = {
  jpg: 'image/jpeg',
  png: 'image/png',
  webp: 'image/webp',
  mp4: 'video/mp4',
  mov: 'video/quicktime',
  json: 'application/json; charset=utf-8',
};

const POSTS_NO_FEED = 200;
const RESPOSTAS_POR_POST = 100;
const TEXTO_MAXIMO = 1200;

// ---------------------------------------------------------------- filtro

/**
 * As mesmas raizes do aplicativo. Curta de proposito: lista longa vira
 * censura de conversa normal, e "que porcaria de render" nao e o
 * problema que este mural tem. O que se bloqueia e ofensa a alguem.
 */
const RAIZES = [
  'viado', 'bicha', 'traveco', 'macaco preto', 'crioulo', 'preto imundo',
  'retardado', 'mongoloide', 'aleijado de merda',
  'puta que pariu voce', 'vai se foder', 'vai tomar no cu', 'filho da puta',
  'arrombado', 'corno manso', 'vagabunda', 'piranha do caralho',
  'matar voce', 'te matar', 'estupr', 'pedofil', 'nazis', 'hitler tinha razao',
];

const RESERVADOS = ['aurea', 'admin', 'suporte', 'oficial', 'equipe', 'moderacao'];

const DISFARCES = {
  '0': 'o', '1': 'i', '3': 'e', '4': 'a', '5': 's', '7': 't',
  '@': 'a', '$': 's', '!': 'i',
};

/**
 * Deixa o texto na forma em que a comparacao e justa: minusculas, sem
 * acento, sem disfarce de numero (v1@d0) e com letra repetida tres ou
 * mais vezes reduzida a uma (viiiiado vira viado).
 *
 * Duas letras iguais SOBREVIVEM. Reduzir tudo a uma estragaria "carro" e
 * "nossa", e ai o filtro passaria a acusar portugues normal.
 */
function normalizar(bruto) {
  return String(bruto ?? '')
    .toLowerCase()
    .normalize('NFD')
    .replace(/[̀-ͯ]/g, '')
    .replace(/[0134579@$!]/g, (c) => DISFARCES[c] ?? c)
    .replace(/[^a-z0-9 ]/g, ' ')
    .replace(/(.)\1{2,}/g, '$1')
    .replace(/\s+/g, ' ')
    .trim();
}

const TELEFONE = /(?:\(?\d{2}\)?\s?)?9?\d{4}[\s.-]?\d{4}/;
const EMAIL = /[a-z0-9._%+-]+@[a-z0-9.-]+\.[a-z]{2,}/i;
const CPF = /\d{3}\.?\d{3}\.?\d{3}-?\d{2}/;

function ofensivo(texto) {
  const n = normalizar(texto);
  return RAIZES.some((raiz) => n.includes(normalizar(raiz)));
}

/** Devolve o motivo da recusa, ou null se pode publicar. */
function recusarTexto(texto, { permiteVazio = false } = {}) {
  const t = String(texto ?? '').trim();
  if (permiteVazio && t.length === 0) return null;
  if (t.length < 3) return 'Escreva um pouco mais.';
  if (t.length > TEXTO_MAXIMO) return 'Texto longo demais para um mural.';
  if (ofensivo(t)) return 'Ofensa, ameaca e ataque a alguem ficam de fora.';
  // Dado pessoal nao e sobre bom-tom: e para ninguem publicar o proprio
  // numero num mural que qualquer um le.
  if (CPF.test(t)) return 'Tire o CPF: o mural e publico.';
  if (EMAIL.test(t)) return 'Tire o e-mail: o mural e publico.';
  if (TELEFONE.test(t)) return 'Tire o telefone: o mural e publico.';
  return null;
}

function recusarApelido(apelido) {
  const a = String(apelido ?? '').trim();
  if (a.length < 3) return 'O apelido precisa de pelo menos 3 letras.';
  if (a.length > 20) return 'Apelido de ate 20 letras.';
  if (!/^[A-Za-z0-9._ À-ÿ-]+$/.test(a)) {
    return 'Use letras, numeros, ponto, traco e espaco no apelido.';
  }
  if (ofensivo(a)) return 'Esse apelido nao passa. Escolha outro.';
  const n = normalizar(a);
  if (RESERVADOS.some((r) => n === r || n.startsWith(`${r} `))) {
    return 'Esse apelido e reservado — ele passaria por conta oficial.';
  }
  return null;
}

// ------------------------------------------------------------- ajudantes

const json = (corpo, status = 200) =>
  new Response(JSON.stringify(corpo), {
    status,
    headers: {
      'content-type': 'application/json; charset=utf-8',
      'access-control-allow-origin': '*',
      'cache-control': 'no-store',
    },
  });

const erro = (mensagem, status) => json({ erro: mensagem }, status);

function codigoNovo() {
  const bytes = crypto.getRandomValues(new Uint8Array(24));
  return [...bytes].map((b) => b.toString(16).padStart(2, '0')).join('');
}

/**
 * Quem esta falando, a partir do codigo de acesso.
 *
 * Devolve null quando nao ha codigo ou ele nao vale. O AUTOR NUNCA VEM
 * DO CORPO da requisicao: vem daqui. Aceitar o autor que o cliente
 * manda seria o mesmo que nao ter conta nenhuma.
 */
async function quemFala(request, env) {
  const cabecalho = request.headers.get('authorization') ?? '';
  const codigo = cabecalho.replace(/^Bearer\s+/i, '').trim();
  if (!/^[0-9a-f]{48}$/.test(codigo)) return null;
  const id = await env.MURAL.get(`codigo:${codigo}`);
  if (!id) return null;
  const bruto = await env.MURAL.get(`conta:${id}`);
  if (!bruto) return null;
  try {
    return { ...JSON.parse(bruto), id };
  } catch {
    return null;
  }
}

/** Um contador por hora, para qualquer coisa que tenha ritmo maximo. */
/// O IP de quem pediu, para os tetos que valem antes de haver conta.
/// O Cloudflare sempre manda `cf-connecting-ip`; `x-forwarded-for` fica de
/// reserva. Sem nenhum dos dois, "desconhecido" — que agrupa todo mundo, e
/// por isso mesmo o teto tem de ser folgado.
function ipDoPedido(request) {
  return (
    request.headers.get('cf-connecting-ip') ??
    (request.headers.get('x-forwarded-for') ?? 'desconhecido').split(',')[0].trim()
  );
}

async function passouDoLimiteDe(env, prefixo, teto) {
  const hora = new Date().toISOString().slice(0, 13);
  const chave = `${prefixo}:${hora}`;
  const atual = Number((await env.MURAL.get(chave)) ?? 0);
  if (atual >= teto) return true;
  await env.MURAL.put(chave, String(atual + 1), { expirationTtl: 7200 });
  return false;
}

const passouDoLimite = (env, quem) =>
  passouDoLimiteDe(env, `limite:${quem}`, LIMITE_POR_HORA);

/**
 * A chave de ordenacao: o instante ao contrario, para o `list` ja vir
 * com o mais novo na frente sem precisar ordenar depois.
 */
const ordemDe = (quando) =>
  String(1e13 - Date.parse(quando)).padStart(14, '0');

async function lerLista(env, prefixo, limite) {
  const lista = await env.MURAL.list({ prefix: prefixo, limit: limite });
  const saida = [];
  for (const chave of lista.keys) {
    const bruto = await env.MURAL.get(chave.name);
    if (!bruto) continue;
    try {
      saida.push(JSON.parse(bruto));
    } catch {
      // Um post estragado nao pode derrubar o mural inteiro.
    }
  }
  return saida;
}

/** O post que o aplicativo recebe. Campo desconhecido nao passa daqui. */
function montarPost(corpo, conta, extras = {}) {
  const quando = new Date().toISOString();
  const post = {
    id: crypto.randomUUID(),
    autor: conta.apelido,
    autorId: conta.id,
    texto: String(corpo.texto ?? '').trim().slice(0, TEXTO_MAXIMO),
    quando,
    etiquetas: Array.isArray(corpo.etiquetas)
      ? corpo.etiquetas.slice(0, 5).map((e) => String(e).slice(0, 20))
      : [],
    ...extras,
  };
  if (typeof corpo.imagem === 'string' && corpo.imagem.startsWith('https://')) {
    post.imagem = corpo.imagem.slice(0, 500);
    if (['video', 'projeto'].includes(corpo.midia)) post.midia = corpo.midia;
    if (Number.isFinite(corpo.duracao)) post.duracao = corpo.duracao;
    if (typeof corpo.nomeDoProjeto === 'string') {
      post.nomeDoProjeto = corpo.nomeDoProjeto.slice(0, 80);
    }
  }
  return post;
}

// ------------------------------------------------------------------ rotas

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const caminho = url.pathname.replace(/\/+$/, '') || '/';

    if (request.method === 'OPTIONS') {
      return new Response(null, {
        headers: {
          'access-control-allow-origin': '*',
          'access-control-allow-methods': 'GET,POST,PATCH,DELETE,OPTIONS',
          'access-control-allow-headers':
            'content-type,authorization,x-aurea-midia,x-moderacao,x-duracao,x-idioma',
        },
      });
    }

    // =========================================================== conta

    if (request.method === 'POST' && caminho === '/conta') {
      // TETO DE CRiacao DE CONTA, por IP e por hora.
      //
      // Nao havia nenhum. Criar conta nao pede nada — sem e-mail, sem
      // senha, sem captcha — e por isso e o primeiro passo de qualquer
      // abuso do mural: cada conta nova abre uma cota propria de
      // transcricao e um espaco proprio de midia. Sem este teto, dava
      // para criar contas sem parar e usar cada uma para consumir o poco
      // COMPARTILHADO da conta Groq do dono, deixando os usuarios de
      // verdade sem transcricao no meio do dia.
      //
      // O teto e alto de proposito: um aparelho atras de NAT compartilha
      // o IP com a casa inteira, e uma familia nao pode ser barrada.
      if (await passouDoLimiteDe(env, `limite:conta:${ipDoPedido(request)}`, CONTAS_POR_HORA)) {
        return erro('Muitas contas criadas deste aparelho. Tente mais tarde.', 429);
      }
      let corpo;
      try {
        corpo = await request.json();
      } catch {
        return erro('Corpo invalido.', 400);
      }
      const motivo = recusarApelido(corpo.apelido);
      if (motivo) return erro(motivo, 422);

      // O APELIDO E UNICO. Sem isto, dois "Ana" no mural e ninguem sabe
      // qual e qual — e o segundo pode se passar pelo primeiro.
      const chaveDoNome = `apelido:${normalizar(corpo.apelido)}`;
      if (await env.MURAL.get(chaveDoNome)) {
        return erro('Esse apelido ja esta em uso. Escolha outro.', 409);
      }

      const id = crypto.randomUUID();
      const codigo = codigoNovo();
      const conta = {
        apelido: String(corpo.apelido).trim(),
        criadaEm: new Date().toISOString(),
      };
      await env.MURAL.put(`conta:${id}`, JSON.stringify(conta));
      await env.MURAL.put(`codigo:${codigo}`, id);
      await env.MURAL.put(chaveDoNome, id);
      return json({ id, apelido: conta.apelido, codigo }, 201);
    }

    if (request.method === 'POST' && caminho === '/conta/entrar') {
      const conta = await quemFala(request, env);
      if (!conta) return erro('Codigo de acesso invalido.', 401);
      return json({ id: conta.id, apelido: conta.apelido });
    }

    if (request.method === 'PATCH' && caminho === '/conta') {
      const conta = await quemFala(request, env);
      if (!conta) return erro('Codigo de acesso invalido.', 401);
      let corpo;
      try {
        corpo = await request.json();
      } catch {
        return erro('Corpo invalido.', 400);
      }
      const motivo = recusarApelido(corpo.apelido);
      if (motivo) return erro(motivo, 422);
      const nova = `apelido:${normalizar(corpo.apelido)}`;
      const dono = await env.MURAL.get(nova);
      if (dono && dono !== conta.id) {
        return erro('Esse apelido ja esta em uso. Escolha outro.', 409);
      }
      await env.MURAL.delete(`apelido:${normalizar(conta.apelido)}`);
      await env.MURAL.put(nova, conta.id);
      await env.MURAL.put(
        `conta:${conta.id}`,
        JSON.stringify({ apelido: String(corpo.apelido).trim(), criadaEm: conta.criadaEm }),
      );
      return json({ id: conta.id, apelido: String(corpo.apelido).trim() });
    }

    // ============================================================ ler

    // ===================================================== transcricao

    if (request.method === 'GET' && caminho === '/transcricao/cota') {
      const conta = await quemFala(request, env);
      if (!conta) return erro('Crie sua conta antes de transcrever.', 401);
      const limites = limitesDeTranscricao(env);
      const uso = await lerUso(env, `uso:transcricao:${conta.id}:${diaDeHoje()}`);
      return json({
        ligada: Boolean(env.GROQ_API_KEY),
        modelo: limites.modelo,
        porDia: limites.porDia,
        usadasHoje: uso.n,
        segundosPorDia: limites.segundosPorDia,
        segundosHoje: Math.round(uso.segundos),
      });
    }

    if (request.method === 'GET' && caminho === '/transcricao/registro') {
      const senha = (request.headers.get('x-moderacao') ?? '').trim();
      if (!env.SENHA_DE_MODERACAO || senha !== env.SENHA_DE_MODERACAO) {
        return erro('Sem permissao.', 401);
      }
      const limite = Math.min(500, numeroOu(url.searchParams.get('limite'), 100));
      return json({ registro: await lerLista(env, 'registro:transcricao:', limite) });
    }

    if (request.method === 'POST' && caminho === '/transcricao') {
      const comecou = Date.now();
      // OS PORTOES, na ordem: conta, chave, tipo, tamanho, ritmo, cota da
      // conta, cota do servidor. So depois de todos o audio e lido.
      const conta = await quemFala(request, env);
      if (!conta) return erro('Crie sua conta antes de transcrever.', 401);
      if (!env.GROQ_API_KEY) {
        return erro('A transcricao na nuvem esta desligada neste servidor.', 503);
      }
      const tipo = (request.headers.get('content-type') ?? '')
        .split(';')[0]
        .trim()
        .toLowerCase();
      const extensao = TIPOS_DE_AUDIO[tipo];
      if (!extensao) {
        return erro('Mande so o audio (m4a, mp3, wav, flac, ogg ou webm).', 415);
      }
      const anunciado = Number(request.headers.get('content-length') ?? 0);
      if (anunciado > AUDIO_MAXIMO) {
        return erro(
          'Audio grande demais (maximo 25 MB). Corte o video ou transcreva no aparelho.',
          413,
        );
      }

      const limites = limitesDeTranscricao(env);
      const hoje = diaDeHoje();
      const chaveConta = `uso:transcricao:${conta.id}:${hoje}`;
      const chaveTodos = `uso:transcricao:todos:${hoje}`;
      // A estimativa vem do app e so serve para barrar ANTES de gastar;
      // a cota de verdade e cobrada pela duracao que a nuvem mede.
      const estimativa = Math.max(0, Number(request.headers.get('x-duracao'))) || 0;

      if (
        await passouDoLimiteDe(env, `limite:transcricao:${conta.id}`, limites.porHora)
      ) {
        return json(
          {
            erro: 'Muitas transcricoes seguidas. Espere uma hora, ou transcreva no aparelho.',
            tenteEm: 3600,
          },
          429,
        );
      }
      const usoConta = await lerUso(env, chaveConta);
      if (
        usoConta.n >= limites.porDia
        || usoConta.segundos + estimativa > limites.segundosPorDia
      ) {
        return json(
          {
            erro: 'Sua cota de transcricao de hoje acabou. Amanha volta — ou transcreva no aparelho.',
            tenteEm: segundosAteAmanha(),
          },
          429,
        );
      }
      const usoTodos = await lerUso(env, chaveTodos);
      if (usoTodos.segundos + estimativa > limites.segundosPorDiaTodos) {
        return json(
          {
            erro: 'O servidor atingiu a cota de transcricao de hoje. Transcreva no aparelho.',
            tenteEm: segundosAteAmanha(),
          },
          429,
        );
      }

      const audio = await request.arrayBuffer();
      if (audio.byteLength === 0) return erro('O audio veio vazio.', 400);
      if (audio.byteLength > AUDIO_MAXIMO) {
        return erro(
          'Audio grande demais (maximo 25 MB). Corte o video ou transcreva no aparelho.',
          413,
        );
      }

      const form = new FormData();
      form.append('file', new Blob([audio], { type: tipo }), `audio.${extensao}`);
      form.append('model', limites.modelo);
      form.append('response_format', 'verbose_json');
      form.append('timestamp_granularities[]', 'word');
      form.append('timestamp_granularities[]', 'segment');
      form.append('temperature', '0');
      const idioma = (request.headers.get('x-idioma') ?? '').trim().toLowerCase();
      if (/^[a-z]{2}$/.test(idioma)) form.append('language', idioma);

      const registrar = (status, duracao) =>
        registrarTranscricao(
          env,
          conta.id,
          duracao,
          limites.modelo,
          status,
          Date.now() - comecou,
        );

      let resposta;
      try {
        resposta = await fetch(GROQ_TRANSCRICAO, {
          method: 'POST',
          headers: { authorization: `Bearer ${env.GROQ_API_KEY}` },
          body: form,
          signal: AbortSignal.timeout(TEMPO_LIMITE_GROQ),
        });
      } catch {
        await registrar('sem-resposta', estimativa);
        return json(
          { erro: 'A transcricao na nuvem nao respondeu. Tente de novo.', detalhe: 'sem-resposta' },
          502,
        );
      }
      if (resposta.status === 429) {
        await registrar('ocupado', estimativa);
        return json(
          {
            erro: 'A transcricao na nuvem esta ocupada. Tente de novo em instantes.',
            tenteEm: numeroOu(resposta.headers.get('retry-after'), 30),
          },
          503,
        );
      }
      if (!resposta.ok) {
        await registrar(`groq-${resposta.status}`, estimativa);
        return json(
          { erro: 'A transcricao na nuvem esta indisponivel.', detalhe: `groq-${resposta.status}` },
          502,
        );
      }
      let bruto;
      try {
        bruto = await resposta.json();
      } catch {
        await registrar('resposta-ilegivel', estimativa);
        return json(
          { erro: 'A transcricao na nuvem devolveu algo ilegivel.', detalhe: 'resposta-ilegivel' },
          502,
        );
      }
      const saida = normalizarTranscricao(bruto, limites.modelo);
      const duracao = saida.duracao || estimativa;
      await somarUso(env, chaveConta, duracao);
      await somarUso(env, chaveTodos, duracao);
      await registrar('ok', duracao);
      // O AUDIO MORRE AQUI: viveu so na memoria desta requisicao.
      return json(saida);
    }

    // ========================================================== aviso
    //
    // UM RECADO PARA TODO APARELHO, escrito de fora. "Estamos resolvendo
    // um bug na exportacao" precisa chegar a quem tem o app instalado
    // hoje, sem build novo. Quem escreve e quem tem a senha de moderacao
    // — o mesmo canal que apaga post. Ler e publico e sem cache curto,
    // porque o app pergunta de dez em dez minutos.

    if (request.method === 'GET' && caminho === '/aviso') {
      const lista = await lerAvisos(env);
      // `aviso` (um so) continua saindo para os aparelhos antigos, que
      // nao sabem ler a lista; `avisos` e o que o app novo mostra.
      return json({ aviso: lista[0] ?? null, avisos: lista });
    }

    if (
      ['PUT', 'POST', 'DELETE'].includes(request.method)
      && (caminho === '/aviso' || caminho.startsWith('/aviso/'))
    ) {
      const senha = (request.headers.get('x-moderacao') ?? '').trim();
      if (!env.SENHA_DE_MODERACAO || senha !== env.SENHA_DE_MODERACAO) {
        return erro('Sem permissao.', 401);
      }
      // DELETE /aviso        tira todos do ar
      // DELETE /aviso/:id    tira so aquele
      if (request.method === 'DELETE') {
        const id = caminho.slice('/aviso/'.length);
        if (caminho === '/aviso') {
          // As DUAS chaves: a lista e a antiga (de um aviso so). Sem
          // apagar a antiga, o proximo GET a leria de volta e o recado
          // "apagado" reapareceria.
          await env.MURAL.delete(CHAVE_AVISOS);
          await env.MURAL.delete('aviso:atual');
          return json({ ok: true, avisos: [] });
        }
        const restantes = (await lerAvisos(env)).filter((a) => a.id !== id);
        await gravarAvisos(env, restantes);
        return json({ ok: true, avisos: restantes });
      }
      let corpo;
      try {
        corpo = await request.json();
      } catch {
        return erro('Corpo invalido.', 400);
      }
      const texto = String(corpo.texto ?? '').trim().slice(0, 280);
      if (texto.length < 3) return erro('Escreva o aviso.', 422);
      const nivel = ['info', 'atencao', 'problema'].includes(corpo.nivel)
        ? corpo.nivel
        : 'info';
      const aviso = {
        // O id e o que deixa o aparelho dispensar ESTE aviso e ainda
        // mostrar o proximo.
        id: crypto.randomUUID(),
        texto,
        nivel,
        ...(typeof corpo.link === 'string' && corpo.link.startsWith('https://')
          ? { link: corpo.link.slice(0, 300) }
          : {}),
        ...(typeof corpo.ate === 'string' && !Number.isNaN(Date.parse(corpo.ate))
          ? { ate: new Date(corpo.ate).toISOString() }
          : {}),
        // POPUP: alem da faixa, aparece como janela na primeira vez que
        // o app abrir. E para o recado que nao pode passar batido — e
        // por isso mesmo se usa pouco.
        ...(corpo.popup === true ? { popup: true } : {}),
        quando: new Date().toISOString(),
      };
      // PUT troca tudo por este; POST poe mais um embaixo.
      const lista = request.method === 'POST'
        ? [...(await lerAvisos(env)), aviso].slice(-AVISOS_NO_AR)
        : [aviso];
      await gravarAvisos(env, lista);
      return json({ ok: true, aviso, avisos: lista }, 201);
    }

    if (request.method === 'GET' && (caminho === '/feed' || caminho === '/')) {
      const cache = await env.MURAL.get('cache:feed');
      const corpo = cache ?? JSON.stringify({
        posts: await lerLista(env, 'post:', POSTS_NO_FEED),
      });
      if (!cache) {
        await env.MURAL.put('cache:feed', corpo, { expirationTtl: 60 });
      }
      return new Response(corpo, {
        headers: {
          'content-type': 'application/json; charset=utf-8',
          'access-control-allow-origin': '*',
          'cache-control': 'public, max-age=30',
        },
      });
    }

    if (request.method === 'GET' && caminho.startsWith('/respostas/')) {
      const pai = decodeURIComponent(caminho.slice('/respostas/'.length));
      // As respostas de um post moram sob o prefixo dele: buscar as de um
      // post nao custa varrer o mural inteiro.
      const respostas = await lerLista(
        env,
        `resp:${pai}:`,
        RESPOSTAS_POR_POST,
      );
      return json({ posts: respostas });
    }

    // ======================================================== publicar

    if (request.method === 'POST' && caminho === '/post') {
      if (Number(request.headers.get('content-length') ?? 0) > TAMANHO_MAXIMO) {
        return erro('Post grande demais.', 413);
      }
      const conta = await quemFala(request, env);
      if (!conta) return erro('Crie sua conta antes de publicar.', 401);

      let corpo;
      try {
        corpo = await request.json();
      } catch {
        return erro('Corpo invalido.', 400);
      }

      const ehRepost = typeof corpo.repostaDe === 'string';
      // NO REPOST O TEXTO PODE SER VAZIO: repostar sem comentar e o uso
      // normal, e obrigar a escrever alguma coisa faria todo mundo
      // digitar um ponto.
      const motivo = recusarTexto(corpo.texto, { permiteVazio: ehRepost });
      if (motivo) return erro(motivo, 422);

      if (await passouDoLimite(env, conta.id)) {
        return erro(`Limite de ${LIMITE_POR_HORA} publicacoes por hora.`, 429);
      }

      // ---- resposta
      if (typeof corpo.respondeA === 'string') {
        const pai = await env.MURAL.get(`indice:${corpo.respondeA}`);
        if (!pai) return erro('Esse post nao existe mais.', 404);
        const post = montarPost(corpo, conta, { respondeA: corpo.respondeA });
        const chave = `resp:${corpo.respondeA}:${ordemDe(post.quando)}:${post.id}`;
        await env.MURAL.put(chave, JSON.stringify(post));
        await env.MURAL.put(`indice:${post.id}`, chave);
        await env.MURAL.delete('cache:feed');
        return json({ ok: true, post }, 201);
      }

      // ---- repost
      let extras = {};
      if (ehRepost) {
        const chaveOriginal = await env.MURAL.get(`indice:${corpo.repostaDe}`);
        if (!chaveOriginal) return erro('Esse post nao existe mais.', 404);
        const bruto = await env.MURAL.get(chaveOriginal);
        if (!bruto) return erro('Esse post nao existe mais.', 404);
        const original = JSON.parse(bruto);
        if (original.repostaDe) {
          return erro('Nao da para repostar um repost. Reposte o original.', 422);
        }
        // GUARDA UMA COPIA do que foi repostado, em vez de so o id.
        //
        // O feed devolve duzentos posts; o original pode ser mais antigo
        // que isso, e ai o cartao apareceria vazio. Com a copia, o repost
        // continua legivel para sempre — inclusive se o original for
        // apagado depois, que e o comportamento que as pessoas esperam de
        // uma citacao.
        extras = {
          repostaDe: original.id,
          original: {
            id: original.id,
            autor: original.autor,
            texto: original.texto,
            quando: original.quando,
            ...(original.imagem ? { imagem: original.imagem } : {}),
            ...(original.midia ? { midia: original.midia } : {}),
          },
        };
      }

      const post = montarPost(corpo, conta, extras);
      const chave = `post:${ordemDe(post.quando)}:${post.id}`;
      await env.MURAL.put(chave, JSON.stringify(post));
      await env.MURAL.put(`indice:${post.id}`, chave);
      await env.MURAL.delete('cache:feed');
      return json({ ok: true, post }, 201);
    }

    // ========================================================== apagar

    if (request.method === 'DELETE' && caminho.startsWith('/post/')) {
      const alvo = decodeURIComponent(caminho.slice('/post/'.length));
      const chave = await env.MURAL.get(`indice:${alvo}`);
      if (!chave) return erro('Nao achei esse post.', 404);
      const bruto = await env.MURAL.get(chave);
      if (!bruto) return erro('Nao achei esse post.', 404);
      const post = JSON.parse(bruto);

      // O DONO APAGA O QUE E DELE. Antes so a senha de moderacao apagava,
      // e ai quem se arrependeu de um post precisava pedir para outra
      // pessoa — num mural, isso e pior do que o post.
      const senha = (request.headers.get('x-moderacao') ?? '').trim();
      const conta = await quemFala(request, env);
      const eDono = conta && conta.id === post.autorId;
      const eModerador =
        env.SENHA_DE_MODERACAO && senha === env.SENHA_DE_MODERACAO;
      if (!eDono && !eModerador) return erro('Sem permissao.', 401);

      await env.MURAL.delete(chave);
      await env.MURAL.delete(`indice:${alvo}`);
      await env.MURAL.delete('cache:feed');
      return json({ ok: true });
    }

    // =========================================================== midia

    if (request.method === 'POST' && caminho === '/midia') {
      const conta = await quemFala(request, env);
      if (!conta) return erro('Crie sua conta antes de enviar arquivo.', 401);

      // TETO DE ENVIO POR CONTA. Antes so havia o limite por REQUISICAO
      // (2 MB): uma conta podia mandar 2 MB quantas vezes quisesse, e o KV
      // tem 1 GB no plano gratuito — quando ele enche, o mural para para
      // TODO MUNDO, nao so para quem abusou.
      if (
        await passouDoLimiteDe(
          env,
          `limite:midia:${conta.id}`,
          MIDIAS_POR_HORA,
        )
      ) {
        return erro('Muitos arquivos desta conta. Tente mais tarde.', 429);
      }

      const tipo = (request.headers.get('content-type') ?? '').split(';')[0];
      const permitidos = {
        'image/jpeg': 'jpg',
        'image/png': 'png',
        'image/webp': 'webp',
        'video/mp4': 'mp4',
        'video/quicktime': 'mov',
        'application/json': 'json',
      };
      const extensao = permitidos[tipo];
      // LISTA DO QUE ENTRA, e nao lista do que nao entra. Bloquear o que
      // se conhece deixa passar o que ainda nao se conhece.
      if (!extensao) return erro('Tipo de arquivo nao aceito.', 415);

      const tamanho = Number(request.headers.get('content-length') ?? 0);
      if (tamanho > MIDIA_MAXIMA) {
        return erro('Arquivo grande demais (maximo 40 MB).', 413);
      }

      const nome = `${crypto.randomUUID()}.${extensao}`;

      // SEM R2: FOTO E PROJETO CABEM NO KV; VIDEO NAO.
      //
      // O R2 precisa ser ligado a mao no painel da Cloudflare, e ate la o
      // mural nao pode ficar sem foto — um mural de gente que faz video
      // sem imagem nenhuma nao e um mural. O KV guarda ate 25 MB por
      // chave e da 1 GB no plano gratuito: uma foto ja comprimida pelo
      // aplicativo cabe com folga, e mil delas ainda cabem no total.
      //
      // Video continua esperando o R2 porque ai a conta nao fecha mesmo:
      // um unico video de trinta megabytes ocupa o espaco de vinte fotos,
      // e o KV cobra por leitura de valor inteiro — ele teria de ser lido
      // por completo a cada play.
      if (!env.ARQUIVOS) {
        const cabeNoKv =
          (extensao === 'json' && tamanho <= PROJETO_NO_KV)
          || (['jpg', 'png', 'webp'].includes(extensao)
              && tamanho <= IMAGEM_NO_KV);
        if (!cabeNoKv) {
          return erro(
            extensao === 'mp4' || extensao === 'mov'
              ? 'Este servidor ainda nao guarda video. '
                + 'Falta ligar o R2 no painel da Cloudflare.'
              : 'Arquivo grande demais para este servidor (maximo 2 MB '
                + 'enquanto o R2 nao esta ligado).',
            extensao === 'mp4' || extensao === 'mov' ? 501 : 413,
          );
        }
        // JSON entra como texto e imagem como bytes: sao os dois formatos
        // que o KV guarda, e cada um volta do jeito que entrou.
        await env.MURAL.put(
          `arquivo:${nome}`,
          extensao === 'json' ? await request.text() : await request.arrayBuffer(),
        );
        return json({ url: `${url.origin}/midia/${nome}` }, 201);
      }

      await env.ARQUIVOS.put(nome, request.body, {
        httpMetadata: { contentType: tipo },
      });
      return json({ url: `${url.origin}/midia/${nome}` }, 201);
    }

    if (request.method === 'GET' && caminho.startsWith('/midia/')) {
      const nome = decodeURIComponent(caminho.slice('/midia/'.length));
      // So o nome, nunca um caminho: sem isto, "../" vira uma porta.
      if (!/^[0-9a-f-]{36}\.(jpg|png|webp|mp4|mov|json)$/.test(nome)) {
        return erro('Arquivo invalido.', 400);
      }
      // O QUE ESTA NO KV VEM PRIMEIRO, mesmo com o R2 ligado: um arquivo
      // guardado antes de o R2 existir continua tendo de abrir depois.
      const noKv = await env.MURAL.get(
        `arquivo:${nome}`,
        nome.endsWith('.json') ? 'text' : 'arrayBuffer',
      );
      if (noKv != null) {
        return new Response(noKv, {
          headers: {
            'content-type': TIPO_DA_EXTENSAO[nome.split('.').pop()],
            'access-control-allow-origin': '*',
            'cache-control': 'public, max-age=31536000, immutable',
          },
        });
      }
      if (!env.ARQUIVOS) return erro('Arquivo nao encontrado.', 404);
      const objeto = await env.ARQUIVOS.get(nome);
      if (!objeto) return erro('Arquivo nao encontrado.', 404);
      return new Response(objeto.body, {
        headers: {
          'content-type':
            objeto.httpMetadata?.contentType ?? 'application/octet-stream',
          'access-control-allow-origin': '*',
          // O arquivo nunca muda: o nome dele e um sorteio.
          'cache-control': 'public, max-age=31536000, immutable',
        },
      });
    }

    return erro('Endereco desconhecido.', 404);
  },
};
