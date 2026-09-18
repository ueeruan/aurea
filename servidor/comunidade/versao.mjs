/**
 * PUBLICA UMA VERSAO NOVA PARA OS APARELHOS SE ATUALIZAREM SOZINHOS.
 *
 * O aplicativo pergunta ao servidor do mural qual e a ultima versao. Se a
 * de la for mais nova do que a instalada, ele mostra a faixa e baixa o
 * APK — sem passar por loja, sem ninguem clicar em link nenhum.
 *
 * O ENDERECO DO ARQUIVO FICA NO SERVIDOR, e nao no APK, pelo mesmo motivo
 * que a chave do mural: dentro do APK qualquer endereco e publico e
 * qualquer um o troca; aqui, quem troca precisa da senha de moderacao.
 *
 * Rodar, a partir de servidor/comunidade:
 *
 *   SENHA_DE_MODERACAO=... node versao.mjs --apk https://.../aurea.apk \
 *       --codigo 92 --nome 1.1.7-beta --notas "O texto arabe voltou a ligar."
 *
 *   # a partir do arquivo local: calcula o tamanho e o sha256 sozinho
 *   SENHA_DE_MODERACAO=... node versao.mjs --arquivo ../../build/app/outputs/flutter-apk/app-arm64-v8a-release.apk \
 *       --url https://.../aurea.apk --codigo 92 --nome 1.1.7-beta
 *
 *   # o que esta publicado agora
 *   node versao.mjs --ver
 *
 *   # tirar do ar (o app para de oferecer atualizacao)
 *   SENHA_DE_MODERACAO=... node versao.mjs --apagar
 *
 * Opcoes:
 *   --apk URL        endereco https do arquivo ja hospedado
 *   --arquivo CAMINHO  o APK local, para medir tamanho e sha256
 *   --url URL        onde esse arquivo vai estar (com --arquivo)
 *   --codigo N       o versionCode (inteiro). E ele que decide se a
 *                    versao e mais nova — comparar "1.1.10" com "1.1.9"
 *                    por texto diria que 1.1.9 vem depois.
 *   --nome X         o nome da versao, como aparece na faixa (1.1.7-beta)
 *   --notas TEXTO    o que a pessoa le na faixa (curto)
 *   --obrigatoria    a faixa nao fecha: so segue depois de atualizar
 *   --servidor URL   outro servidor (padrao: o do mural)
 *
 * No PowerShell: $env:SENHA_DE_MODERACAO="..."; node versao.mjs --ver
 */

import { createHash } from 'node:crypto';
import { readFile, stat } from 'node:fs/promises';

const PADRAO = 'https://mural-do-aurea.aureaapp.workers.dev';

const args = process.argv.slice(2);
const opcoes = { servidor: PADRAO };
const SOZINHAS = new Set(['obrigatoria', 'apagar', 'ver']);
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a.startsWith('--') && SOZINHAS.has(a.slice(2))) opcoes[a.slice(2)] = true;
  else if (a.startsWith('--')) opcoes[a.slice(2)] = args[++i];
  else console.error(`Ignorando "${a}": as opcoes comecam com --`);
}

const endereco = `${opcoes.servidor.replace(/\/+$/, '')}/versao`;

// --------------------------------------------------------------- ler

if (opcoes.ver) {
  const r = await fetch(endereco);
  const corpo = await r.json();
  if (!corpo.versao) {
    console.log('Nenhuma versao publicada. O aplicativo nao oferece atualizacao.');
  } else {
    const v = corpo.versao;
    console.log(`codigo ${v.codigo} · ${v.versao}`);
    console.log(`  arquivo   ${v.apk}`);
    console.log(`  tamanho   ${v.tamanho ? `${(v.tamanho / 1048576).toFixed(1)} MB` : 'nao informado'}`);
    console.log(`  sha256    ${v.sha256 || 'nao informado'}`);
    console.log(`  obrigatoria  ${v.obrigatoria ? 'SIM' : 'nao'}`);
    if (v.notas) console.log(`  notas     ${v.notas}`);
    console.log(`  publicado ${v.quando}`);
  }
  process.exit(0);
}

const senha = process.env.SENHA_DE_MODERACAO;
if (!senha) {
  console.error('Falta a senha: SENHA_DE_MODERACAO=... node versao.mjs ...');
  process.exit(2);
}

const cabecalhos = {
  'x-moderacao': senha,
  'content-type': 'application/json',
};

// ------------------------------------------------------------ apagar

if (opcoes.apagar) {
  // A rota nao tem DELETE de proposito: apagar e publicar "nada". Um
  // caminho a menos e uma forma a menos de errar.
  const r = await fetch(endereco, {
    method: 'PUT',
    headers: cabecalhos,
    body: JSON.stringify({ codigo: 0, versao: 'apagada', apk: '' }),
  });
  console.log(r.ok ? 'Fora do ar.' : `Recusado (${r.status}): ${await r.text()}`);
  process.exit(r.ok ? 0 : 1);
}

// --------------------------------------------------------- publicar

const codigo = Number(opcoes.codigo);
const nome = (opcoes.nome ?? '').trim();
let apk = (opcoes.apk ?? '').trim();
let tamanho = Number(opcoes.tamanho) || 0;
let sha256 = (opcoes.sha256 ?? '').trim().toLowerCase();

if (!Number.isInteger(codigo) || codigo <= 0) {
  console.error('Falta --codigo N (o versionCode, um inteiro).');
  process.exit(2);
}
if (nome.length < 3) {
  console.error('Falta --nome 1.1.7-beta.');
  process.exit(2);
}

if (opcoes.arquivo) {
  // MEDIR O ARQUIVO E O QUE TORNA A CONFERENCIA POSSIVEL. Sem isto o
  // aparelho baixa, instala e descobre no fim que veio cortado — e a
  // pessoa fica sem o app que tinha.
  const bytes = await readFile(opcoes.arquivo);
  const info = await stat(opcoes.arquivo);
  tamanho = info.size;
  sha256 = createHash('sha256').update(bytes).digest('hex');
  apk = (opcoes.url ?? '').trim();
  if (!apk.startsWith('https://')) {
    console.error('Com --arquivo, falta --url https://... (onde o arquivo vai ficar).');
    process.exit(2);
  }
  console.log(`${info.size} bytes · sha256 ${sha256}`);
}

if (!apk.startsWith('https://')) {
  console.error('Falta --apk https://... (o endereco do arquivo).');
  process.exit(2);
}

const r = await fetch(endereco, {
  method: 'PUT',
  headers: cabecalhos,
  body: JSON.stringify({
    codigo,
    versao: nome,
    apk,
    notas: (opcoes.notas ?? '').trim(),
    obrigatoria: opcoes.obrigatoria === true,
    tamanho,
    sha256,
  }),
});

if (!r.ok) {
  console.error(`Recusado (${r.status}): ${await r.text()}`);
  process.exit(1);
}
console.log(`Publicado: codigo ${codigo} · ${nome}`);
