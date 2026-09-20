/**
 * O TESTE DO SERVIDOR, sem servidor.
 *
 * O Worker inteiro depende de coisas que dao para exercitar aqui mesmo:
 * o filtro, a conta, quem pode apagar o que, e o formato do que sai.
 * Subir para a nuvem so para descobrir que uma frase normal esta sendo
 * recusada e o jeito caro de testar.
 *
 * Rodar:  node servidor/comunidade/teste.mjs
 */
import worker from './worker.js';

// Uma base de brinquedo com a mesma cara da KV do Cloudflare.
function kvFalsa() {
  const dados = new Map();
  return {
    dados,
    async get(k, tipo) {
      if (!dados.has(k)) return null;
      const valor = dados.get(k);
      // A KV de verdade devolve texto por padrao e bytes quando pedidos.
      if (tipo === 'arrayBuffer' && typeof valor === 'string') {
        return new TextEncoder().encode(valor).buffer;
      }
      return valor;
    },
    async put(k, v) {
      dados.set(k, v);
    },
    async delete(k) {
      dados.delete(k);
    },
    async list({ prefix = '', limit = 1000, cursor } = {}) {
      const todas = [...dados.keys()].filter((k) => k.startsWith(prefix)).sort();
      const inicio = cursor ? Number(cursor) : 0;
      const pagina = todas.slice(inicio, inicio + limit);
      const proximo = inicio + pagina.length;
      return {
        keys: pagina.map((name) => ({ name })),
        list_complete: proximo >= todas.length,
        ...(proximo < todas.length ? { cursor: String(proximo) } : {}),
      };
    },
  };
}

// E um R2 de brinquedo.
function r2Falso() {
  const dados = new Map();
  return {
    dados,
    async put(nome, corpo, opcoes) {
      dados.set(nome, { corpo, ...opcoes });
    },
    async get(nome) {
      const achado = dados.get(nome);
      return achado
        ? { body: achado.corpo, httpMetadata: achado.httpMetadata }
        : null;
    },
  };
}

let falhas = 0;
function confere(nome, real, esperado) {
  const ok = JSON.stringify(real) === JSON.stringify(esperado);
  if (!ok) {
    falhas++;
    console.log(
      `FALHOU  ${nome}\n  esperado: ${JSON.stringify(esperado)}\n  veio:     ${JSON.stringify(real)}`,
    );
  } else {
    console.log(`ok      ${nome}`);
  }
}

const env = {
  MURAL: kvFalsa(),
  ARQUIVOS: r2Falso(),
  SENHA_DE_MODERACAO: 'senha-de-teste',
  // Uma chave de brinquedo, sem a cara de uma chave de verdade — a
  // varredura do app (segredo_da_groq_test) acusa qualquer "gsk_".
  GROQ_API_KEY: 'chave-de-brinquedo',
};

const BASE = 'https://exemplo.workers.dev';
const chamar = (metodo, caminho, { corpo, codigo, cabecalhos } = {}) =>
  worker.fetch(
    new Request(`${BASE}${caminho}`, {
      method: metodo,
      headers: {
        ...(corpo !== undefined ? { 'content-type': 'application/json' } : {}),
        ...(codigo ? { authorization: `Bearer ${codigo}` } : {}),
        ...cabecalhos,
      },
      ...(corpo !== undefined ? { body: JSON.stringify(corpo) } : {}),
    }),
    env,
  );

// ======================================================== conta

let r = await chamar('POST', '/conta', { corpo: { apelido: 'Ana Motion' } });
const ana = await r.json();
confere('cria conta', r.status, 201);
confere('devolve o codigo de acesso', /^[0-9a-f]{48}$/.test(ana.codigo), true);

r = await chamar('POST', '/conta', { corpo: { apelido: 'ana motion' } });
confere('apelido repetido e recusado', r.status, 409);

r = await chamar('POST', '/conta', { corpo: { apelido: 'Aurea' } });
confere('apelido reservado e recusado', r.status, 422);

r = await chamar('POST', '/conta', { corpo: { apelido: 'vai se foder' } });
confere('apelido ofensivo e recusado', r.status, 422);

r = await chamar('POST', '/conta', { corpo: { apelido: 'Bruno 3D' } });
const bruno = await r.json();
confere('segunda conta', r.status, 201);

r = await chamar('GET', '/estatisticas');
confere('estatisticas contam todas as contas', (await r.json()).usuarios, 2);

r = await chamar('POST', '/conta/entrar', { codigo: ana.codigo });
confere('entrar com o codigo', (await r.json()).apelido, 'Ana Motion');

r = await chamar('POST', '/conta/entrar', { codigo: 'f'.repeat(48) });
confere('codigo inventado nao entra', r.status, 401);

// ======================================================== publicar

r = await chamar('POST', '/post', { corpo: { texto: 'oi' } });
confere('sem conta nao publica', r.status, 401);

r = await chamar('POST', '/post', {
  codigo: ana.codigo,
  corpo: {
    texto: 'Terminei minha primeira animacao com o rastreio 3D!',
    autor: 'Bruno 3D',
  },
});
const post = (await r.json()).post;
confere('publica', r.status, 201);
confere(
  'o autor vem da conta, nao do corpo',
  post.autor,
  'Ana Motion',
);

r = await chamar('POST', '/post', {
  codigo: ana.codigo,
  corpo: { texto: 'vai tomar no cu' },
});
confere('ofensa e recusada', r.status, 422);

r = await chamar('POST', '/post', {
  codigo: ana.codigo,
  corpo: { texto: 'me chama no 11 98765-4321' },
});
confere('telefone e recusado', r.status, 422);

// ======================================================== resposta

r = await chamar('POST', '/post', {
  codigo: bruno.codigo,
  corpo: { texto: 'Ficou muito bom! Como voce fez a camera?', respondeA: post.id },
});
const resposta = (await r.json()).post;
confere('responde', r.status, 201);
confere('a resposta aponta para o pai', resposta.respondeA, post.id);

r = await chamar('GET', `/respostas/${post.id}`);
const respostas = (await r.json()).posts;
confere('as respostas vem pelo pai', respostas.length, 1);
confere('e sao de quem respondeu', respostas[0].autor, 'Bruno 3D');

r = await chamar('POST', '/post', {
  codigo: bruno.codigo,
  // Texto valido de proposito: com "oi" o filtro recusaria antes, por
  // ser curto, e o teste passaria pelo motivo errado.
  corpo: {
    texto: 'respondendo um post que ja foi apagado',
    respondeA: 'post-que-nao-existe',
  },
});
confere('responder a post inexistente e recusado', r.status, 404);

// A resposta NAO aparece no mural de cima.
r = await chamar('GET', '/feed');
let feed = (await r.json()).posts;
confere('o feed nao mistura resposta', feed.length, 1);

// ========================================================== repost

r = await chamar('POST', '/post', {
  codigo: bruno.codigo,
  corpo: { texto: '', repostaDe: post.id },
});
const repost = (await r.json()).post;
confere('reposta sem comentar', r.status, 201);
confere('guarda copia do original', repost.original.autor, 'Ana Motion');
confere('e o texto do original', repost.original.texto, post.texto);

r = await chamar('POST', '/post', {
  codigo: ana.codigo,
  corpo: { texto: 'olhem isso', repostaDe: repost.id },
});
confere('nao reposta um repost', r.status, 422);

// ========================================================== apagar

r = await chamar('DELETE', `/post/${post.id}`, { codigo: bruno.codigo });
confere('nao apaga post dos outros', r.status, 401);

r = await chamar('DELETE', `/post/${post.id}`, { codigo: ana.codigo });
confere('o dono apaga o proprio post', r.status, 200);

r = await chamar('DELETE', `/post/${repost.id}`, {
  cabecalhos: { 'x-moderacao': 'senha-de-teste' },
});
confere('a moderacao apaga qualquer um', r.status, 200);

r = await chamar('DELETE', '/post/nao-existe', { codigo: ana.codigo });
confere('apagar o que nao existe', r.status, 404);

// =========================================================== midia

async function subir(tipo, bytes) {
  return worker.fetch(
    new Request(`${BASE}/midia`, {
      method: 'POST',
      headers: {
        'content-type': tipo,
        'content-length': String(bytes),
        authorization: `Bearer ${ana.codigo}`,
      },
      body: new Uint8Array(bytes),
    }),
    env,
  );
}

r = await subir('image/png', 1024);
const midia = await r.json();
confere('sobe imagem', r.status, 201);
confere('devolve endereco no proprio servidor', midia.url.startsWith(`${BASE}/midia/`), true);

r = await subir('application/x-msdownload', 1024);
confere('tipo de fora da lista e recusado', r.status, 415);

r = await subir('video/mp4', 60 * 1024 * 1024);
confere('arquivo grande demais e recusado', r.status, 413);

r = await chamar('GET', new URL(midia.url).pathname);
confere('serve o arquivo', r.status, 200);
confere('com o tipo certo', r.headers.get('content-type'), 'image/png');

r = await chamar('GET', '/midia/..%2F..%2Fetc%2Fpasswd');
confere('nome com caminho e recusado', r.status, 400);

// --- sem R2: projeto ainda funciona, foto e video nao
const semR2 = { MURAL: env.MURAL, SENHA_DE_MODERACAO: 'senha-de-teste' };
const subirSemR2 = (tipo, bytes) =>
  worker.fetch(
    new Request(`${BASE}/midia`, {
      method: 'POST',
      headers: {
        'content-type': tipo,
        'content-length': String(bytes),
        authorization: `Bearer ${ana.codigo}`,
      },
      body: tipo === 'application/json' ? '{"nome":"meu projeto"}' : new Uint8Array(bytes),
    }),
    semR2,
  );

r = await subirSemR2('application/json', 22);
const projeto = await r.json();
confere('projeto sobe sem R2 (vai para o KV)', r.status, 201);

r = await worker.fetch(new Request(projeto.url), semR2);
confere('e o projeto volta inteiro', await r.text(), '{"nome":"meu projeto"}');

r = await subirSemR2('image/png', 1024);
const fotoSemR2 = await r.json();
confere('foto sobe sem R2 (vai para o KV)', r.status, 201);

r = await worker.fetch(new Request(fotoSemR2.url), semR2);
confere('e a foto volta com o tipo certo', r.headers.get('content-type'), 'image/png');

r = await subirSemR2('video/mp4', 1024);
confere('video sem R2 explica o que falta', r.status, 501);

r = await subirSemR2('image/png', 5 * 1024 * 1024);
confere('foto grande demais para o KV e recusada', r.status, 413);

// =========================================================== aviso

r = await chamar('GET', '/aviso');
confere('sem aviso, vem null', (await r.json()).aviso, null);

r = await chamar('PUT', '/aviso', {
  corpo: { texto: 'Estamos resolvendo um bug na exportacao.', nivel: 'problema' },
});
confere('sem senha nao escreve aviso', r.status, 401);

r = await chamar('PUT', '/aviso', {
  corpo: { texto: 'Estamos resolvendo um bug na exportacao.', nivel: 'problema' },
  cabecalhos: { 'x-moderacao': 'senha-de-teste' },
});
const aviso = (await r.json()).aviso;
confere('a moderacao escreve o aviso', r.status, 201);
confere('o aviso tem id', typeof aviso.id === 'string' && aviso.id.length > 10, true);

r = await chamar('GET', '/aviso');
confere('todo aparelho le o aviso', (await r.json()).aviso.texto, aviso.texto);

r = await chamar('PUT', '/aviso', {
  corpo: { texto: 'oi' },
  cabecalhos: { 'x-moderacao': 'senha-de-teste' },
});
confere('aviso curto demais e recusado', r.status, 422);

// MAIS DE UM AVISO: o POST poe outro embaixo, sem tirar o primeiro.
r = await chamar('POST', '/aviso', {
  corpo: {
    texto: 'Entre no grupo do WhatsApp.',
    nivel: 'info',
    link: 'https://chat.whatsapp.com/exemplo',
    popup: true,
  },
  cabecalhos: { 'x-moderacao': 'senha-de-teste' },
});
confere('o POST poe mais um aviso', r.status, 201);
let avisos = (await r.json()).avisos;
confere('os dois ficam no ar, na ordem', avisos.length, 2);
confere('o primeiro continua sendo o primeiro', avisos[0].texto, aviso.texto);
confere('o novo entra embaixo', avisos[1].texto, 'Entre no grupo do WhatsApp.');
confere('e pode ser popup', avisos[1].popup, true);
confere('com link', avisos[1].link, 'https://chat.whatsapp.com/exemplo');

r = await chamar('GET', '/aviso');
const lidos = await r.json();
confere('o app novo le a lista', lidos.avisos.length, 2);
confere(
  'o app antigo continua lendo um so, o de cima',
  lidos.aviso.texto,
  aviso.texto,
);

r = await chamar('DELETE', `/aviso/${avisos[1].id}`, {
  cabecalhos: { 'x-moderacao': 'senha-de-teste' },
});
confere('da para apagar so um', (await r.json()).avisos.length, 1);

r = await chamar('POST', '/aviso', {
  corpo: { texto: 'aviso numero dois de novo' },
  cabecalhos: { 'x-moderacao': 'senha-de-teste' },
});
r = await chamar('POST', '/aviso', {
  corpo: { texto: 'aviso numero tres' },
  cabecalhos: { 'x-moderacao': 'senha-de-teste' },
});
r = await chamar('POST', '/aviso', {
  corpo: { texto: 'aviso numero quatro' },
  cabecalhos: { 'x-moderacao': 'senha-de-teste' },
});
avisos = (await r.json()).avisos;
confere('no maximo tres no ar', avisos.length, 3);
confere('e o mais velho sai', avisos[2].texto, 'aviso numero quatro');

r = await chamar('PUT', '/aviso', {
  corpo: { texto: 'Voltamos a um recado so.' },
  cabecalhos: { 'x-moderacao': 'senha-de-teste' },
});
confere('o PUT troca a lista inteira', (await r.json()).avisos.length, 1);

r = await chamar('DELETE', '/aviso', { cabecalhos: { 'x-moderacao': 'senha-de-teste' } });
confere('a moderacao apaga o aviso', r.status, 200);
r = await chamar('GET', '/aviso');
confere('e ele some para todo mundo', (await r.json()).aviso, null);

// =========================================================== limite

let ultimo = 0;
for (let i = 0; i < 25; i++) {
  const resposta = await chamar('POST', '/post', {
    codigo: bruno.codigo,
    corpo: { texto: `post numero ${i} do roteiro automatico` },
  });
  ultimo = resposta.status;
}
confere('o limite por hora barra o roteiro', ultimo, 429);


// ====================================================== transcricao
//
// A Groq de brinquedo: o worker chama fetch(api.groq.com); aqui a
// resposta e roteirizada por teste, e o que ele mandou fica registrado
// para conferencia — inclusive a chave, que tem de ir no cabecalho e em
// lugar nenhum mais.

const fetchDeVerdade = globalThis.fetch;
let groq = {};
let chamadaGroq = null;
const respostaGroqBoa = () => ({
  text: 'Olá mundo',
  language: 'pt',
  duration: 2.5,
  segments: [{ start: 0, end: 2.5, text: ' Olá mundo' }],
  words: [
    { word: 'Olá', start: 0, end: 1 },
    { word: 'mundo', start: 1, end: 2.5 },
  ],
});
globalThis.fetch = async (url, opcoes) => {
  if (!String(url).includes('api.groq.com')) return fetchDeVerdade(url, opcoes);
  chamadaGroq = { url: String(url), opcoes };
  if (groq.derruba) throw new TypeError('rede caiu');
  return new Response(JSON.stringify(groq.corpo ?? respostaGroqBoa()), {
    status: groq.status ?? 200,
    headers: { 'content-type': 'application/json', ...(groq.cabecalhos ?? {}) },
  });
};

const mandarAudio = (
  codigo,
  { tipo = 'audio/mp4', bytes = 4000, duracao = 2.5, idioma, ambiente } = {},
) =>
  worker.fetch(
    new Request(`${BASE}/transcricao`, {
      method: 'POST',
      headers: {
        'content-type': tipo,
        'content-length': String(bytes),
        'x-duracao': String(duracao),
        ...(idioma ? { 'x-idioma': idioma } : {}),
        ...(codigo ? { authorization: `Bearer ${codigo}` } : {}),
      },
      body: new Uint8Array(bytes),
    }),
    ambiente ?? env,
  );

r = await mandarAudio(null);
confere('sem conta nao transcreve', r.status, 401);

r = await mandarAudio(ana.codigo, { ambiente: { ...env, GROQ_API_KEY: undefined } });
confere('sem a chave no servidor a nuvem esta desligada (503)', r.status, 503);

r = await mandarAudio(ana.codigo, { tipo: 'application/json' });
confere('so audio entra', r.status, 415);

r = await mandarAudio(ana.codigo, { bytes: 26 * 1024 * 1024 });
confere('audio acima de 25 MB e recusado', r.status, 413);

groq = {};
r = await mandarAudio(ana.codigo, { idioma: 'pt' });
let t = await r.json();
confere('transcreve', r.status, 200);
confere('devolve o texto', t.texto, 'Olá mundo');
confere('devolve as palavras com tempo', t.palavras.map((p) => p.texto), ['Olá', 'mundo']);
confere('devolve os segmentos', t.segmentos.length, 1);
confere('o modelo padrao e o turbo', t.modelo, 'whisper-large-v3-turbo');
confere(
  'a chave vai no cabecalho para a Groq, e so para ela',
  chamadaGroq.opcoes.headers.authorization,
  'Bearer chave-de-brinquedo',
);
confere('o modelo vai no formulario', chamadaGroq.opcoes.body.get('model'), 'whisper-large-v3-turbo');
confere('o idioma vai no formulario', chamadaGroq.opcoes.body.get('language'), 'pt');
confere(
  'pede tempo por palavra e por segmento',
  chamadaGroq.opcoes.body.getAll('timestamp_granularities[]'),
  ['word', 'segment'],
);
confere('o audio vai inteiro', chamadaGroq.opcoes.body.get('file').size, 4000);

r = await mandarAudio(ana.codigo, {
  ambiente: { ...env, MODELO_DE_TRANSCRICAO: 'whisper-large-v3' },
});
confere('o modelo vem da configuracao do servidor', (await r.json()).modelo, 'whisper-large-v3');
confere('e chega assim na Groq', chamadaGroq.opcoes.body.get('model'), 'whisper-large-v3');

groq = { corpo: { text: '', duration: 3, segments: [], words: [] } };
r = await mandarAudio(ana.codigo);
t = await r.json();
confere('silencio: 200 com nada dentro', [r.status, t.texto, t.palavras.length], [200, '', 0]);

groq = { status: 500, corpo: { error: { message: 'boom' } } };
r = await mandarAudio(ana.codigo);
confere('Groq fora do ar vira 502', r.status, 502);
confere('e diz o que foi', (await r.json()).detalhe, 'groq-500');

groq = { status: 429, corpo: {}, cabecalhos: { 'retry-after': '12' } };
r = await mandarAudio(ana.codigo);
t = await r.json();
confere('Groq ocupada vira 503 com quando tentar', [r.status, t.tenteEm], [503, 12]);

groq = { derruba: true };
r = await mandarAudio(ana.codigo);
confere('rede ate a Groq caiu: 502', r.status, 502);
confere('sem-resposta no detalhe', (await r.json()).detalhe, 'sem-resposta');
groq = {};

r = await chamar('GET', '/transcricao/cota', { codigo: ana.codigo });
t = await r.json();
confere('a cota conta so o que deu certo', t.usadasHoje, 3);
confere('e soma os segundos medidos pela nuvem', t.segundosHoje, 8);
confere('a cota diz o modelo', t.modelo, 'whisper-large-v3-turbo');

r = await mandarAudio(ana.codigo, { ambiente: { ...env, SEGUNDOS_DE_AUDIO_POR_DIA: '5' } });
t = await r.json();
confere('cota de segundos do dia esgotada: 429', r.status, 429);
confere('com quando tentar', t.tenteEm > 0, true);

r = await mandarAudio(ana.codigo, { ambiente: { ...env, TRANSCRICOES_POR_DIA: '3' } });
confere('cota de transcricoes do dia esgotada: 429', r.status, 429);

r = await mandarAudio(ana.codigo, {
  ambiente: { ...env, SEGUNDOS_DE_AUDIO_POR_DIA_TODOS: '9' },
});
confere('a cota do servidor inteiro tambem barra', r.status, 429);

const apertado = { ...env, TRANSCRICOES_POR_HORA: '2' };
r = await mandarAudio(bruno.codigo, { ambiente: apertado });
confere('bruno: a primeira passa', r.status, 200);
r = await mandarAudio(bruno.codigo, { ambiente: apertado });
confere('bruno: a segunda passa', r.status, 200);
r = await mandarAudio(bruno.codigo, { ambiente: apertado });
confere('bruno: a terceira na mesma hora e barrada', r.status, 429);

r = await chamar('GET', '/transcricao/registro');
confere('o registro pede a senha', r.status, 401);
r = await chamar('GET', '/transcricao/registro', {
  cabecalhos: { 'x-moderacao': 'senha-de-teste' },
});
const registro = (await r.json()).registro;
confere('o registro tem uma linha por tentativa que chegou a Groq', registro.length, 8);
confere(
  'so o tecnico: nada de audio nem texto',
  Object.keys(registro[0]).sort(),
  ['conta', 'duracao', 'modelo', 'ms', 'quando', 'status'],
);
confere(
  'guarda o status de cada uma',
  registro.some((x) => x.status === 'groq-500') && registro.some((x) => x.status === 'ok'),
  true,
);
confere(
  'nada do audio fica guardado',
  [...env.MURAL.dados.keys()].some((k) => /audio|transcricao:corpo/.test(k)),
  false,
);
// ======================================================== versao
//
// O APARELHO SE ATUALIZA SOZINHO por este endereco. O que ele NAO pode
// fazer e mostrar "atualize" sem ter o que oferecer, nem aceitar
// endereco de APK que nao seja https — o endereco vive fora do APK, e um
// endereco que qualquer um troca e um aplicativo que qualquer um troca.

r = await chamar('GET', '/versao');
confere('sem ninguem publicar, a versao e nula', (await r.json()).versao, null);

r = await chamar('PUT', '/versao', {
  corpo: { codigo: 92, versao: '1.1.7-beta', apk: 'https://exemplo/apk' },
});
confere('publicar sem senha nao passa', r.status, 401);

r = await chamar('PUT', '/versao', {
  cabecalhos: { 'x-moderacao': 'senha-de-teste' },
  corpo: { versao: '1.1.7-beta', apk: 'https://exemplo/apk' },
});
confere('sem o codigo da versao nao passa', r.status, 422);

r = await chamar('PUT', '/versao', {
  cabecalhos: { 'x-moderacao': 'senha-de-teste' },
  corpo: { codigo: 92, versao: '1.1.7-beta', apk: 'http://exemplo/apk' },
});
confere('endereco que nao e https nao passa', r.status, 422);

r = await chamar('PUT', '/versao', {
  cabecalhos: { 'x-moderacao': 'senha-de-teste' },
  corpo: {
    codigo: 92,
    versao: '1.1.7-beta',
    apk: 'https://exemplo/aurea.apk',
    notas: 'O texto arabe voltou a ligar.',
    sha256: 'A'.repeat(64),
    tamanho: 93700000,
  },
});
confere('publicar com senha passa', r.status, 201);

r = await chamar('GET', '/versao');
const publicada = (await r.json()).versao;
confere('e o aparelho le o que foi publicado', publicada.codigo, 92);
confere('com o nome da versao', publicada.versao, '1.1.7-beta');
confere('e o endereco do arquivo', publicada.apk, 'https://exemplo/aurea.apk');
confere('sem obrigatoriedade por padrao', publicada.obrigatoria, false);
confere('o sha256 entra em minusculas', publicada.sha256, 'a'.repeat(64));

r = await chamar('PUT', '/versao', {
  cabecalhos: { 'x-moderacao': 'senha-de-teste' },
  corpo: {
    codigo: 93,
    versao: '1.1.8-beta',
    apk: 'https://exemplo/aurea.apk',
    obrigatoria: true,
    sha256: 'nem-e-hash',
  },
});
confere('a versao obrigatoria e publicada', r.status, 201);
r = await chamar('GET', '/versao');
const obrigatoria = (await r.json()).versao;
confere('com a marca de obrigatoria', obrigatoria.obrigatoria, true);
confere('e sha invalido vira vazio, e nao lixo', obrigatoria.sha256, '');

globalThis.fetch = fetchDeVerdade;

console.log(falhas === 0 ? '\nTudo certo.' : `\n${falhas} falha(s).`);
process.exit(falhas === 0 ? 0 : 1);
