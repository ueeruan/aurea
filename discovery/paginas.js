// =============================================================================
//  Páginas públicas exigidas pelo Google Play (e pela App Store):
//    /privacidade  (/privacy)        — política de privacidade
//    /excluir-conta (/delete-account) — pedido de exclusão de conta pela web
//  A exclusão pela web usa as MESMAS rotas do app (/api/auth/login e
//  /api/auth/delete, contas.js): nada de caminho paralelo no servidor.
//  O texto descreve o que o código faz (ver output/playstore/data-safety.md).
// =============================================================================

const CONTATO = 'ruanpablombl@gmail.com';
const ATUALIZADO = '19/10/2026';

const ESTILO = `
  :root { color-scheme: light dark; --fg:#1b1f24; --bg:#ffffff; --muted:#5b6470; --line:#d9dee5; --accent:#7c5cff; }
  @media (prefers-color-scheme: dark) { :root { --fg:#e9edf2; --bg:#0f141a; --muted:#9aa4b1; --line:#2a323c; } }
  * { box-sizing: border-box; }
  body { margin:0; background:var(--bg); color:var(--fg); font: 16px/1.6 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
  main { max-width: 760px; margin: 0 auto; padding: 24px 16px 64px; }
  h1 { font-size: 1.6rem; margin: 0 0 4px; } h2 { font-size: 1.15rem; margin: 28px 0 8px; }
  p, li { color: var(--fg); } small, .muted { color: var(--muted); }
  nav { display:flex; gap:12px; flex-wrap:wrap; margin: 8px 0 20px; }
  nav a, a { color: var(--accent); }
  section[lang] { border-top: 1px solid var(--line); padding-top: 12px; margin-top: 20px; }
  table { width:100%; border-collapse: collapse; font-size: .95rem; }
  td, th { border:1px solid var(--line); padding:8px; vertical-align: top; text-align:left; }
  form { display:grid; gap:12px; max-width: 420px; }
  input[type=email], input[type=password] { padding:10px; border:1px solid var(--line); border-radius:8px; background:transparent; color:var(--fg); font-size:1rem; }
  button { padding: 12px; border:0; border-radius: 8px; background:#d93a3a; color:#fff; font-weight:600; font-size:1rem; }
  button:disabled { opacity:.5; }
  #resultado { font-weight:600; }
`;

function pagina(titulo, corpo, script = '') {
  return `<!doctype html><html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${titulo}</title><style>${ESTILO}</style></head>
<body><main>${corpo}</main>${script ? `<script>${script}</script>` : ''}</body></html>`;
}

const PRIVACIDADE = pagina('Aurea — Política de Privacidade / Privacy Policy', `
<h1>Aurea — Política de Privacidade</h1>
<p class="muted">Aplicativo Aurea (Android: com.aurea.editor na Google Play e com.aurea.aurea no APK; iOS: com.aurea.aurea). Última atualização: ${ATUALIZADO}.</p>
<nav><a href="#pt">Português</a><a href="#en">English</a><a href="/excluir-conta">Excluir conta / Delete account</a></nav>

<section lang="pt-BR" id="pt">
<h2>1. Quem somos</h2>
<p>O Aurea é um editor de vídeo e motion graphics feito por Ruan Pablo (desenvolvedor independente). Contato sobre privacidade: <a href="mailto:${CONTATO}">${CONTATO}</a>.</p>

<h2>2. Resumo</h2>
<ul>
<li>Seus projetos, vídeos, fotos e áudios ficam no seu aparelho. Eles só saem dele quando <b>você</b> publica algo na Comunidade ou usa o Vídeo com IA.</li>
<li>A conta (e-mail e senha) é <b>opcional</b>: é usada para a Comunidade, o Vídeo com IA e o envio de relatórios de falha.</li>
<li>Legendas automáticas, recorte com IA, mapa de profundidade e ampliação são processados no próprio aparelho.</li>
<li>O app mostra anúncios de parceiros, com a tela de consentimento do Google (UMP) quando a lei exige.</li>
<li>Não vendemos dados pessoais.</li>
</ul>

<h2>3. Dados que tratamos</h2>
<table>
<tr><th>Dado</th><th>Para quê</th><th>Por quanto tempo</th></tr>
<tr><td><b>Conta</b>: e-mail, senha (guardada só como hash PBKDF2 no servidor) e identificador da conta.</td><td>Criar a conta e entrar.</td><td>Até você excluir a conta.</td></tr>
<tr><td><b>Sessão</b>: token de acesso (no aparelho, criptografado).</td><td>Manter você conectado.</td><td>Expira em 30 dias ou ao sair; apagado com a conta.</td></tr>
<tr><td><b>Relatórios de falha</b> (automáticos com a conta conectada): versão do app e do sistema, modelo e fabricante do aparelho, tipo de CPU, rastreamento técnico do erro, identificador aleatório de instalação, e-mail da conta e país (pela conexão).</td><td>Corrigir falhas. O relatório chega ao desenvolvedor por e-mail.</td><td>30 dias no servidor.</td></tr>
<tr><td><b>Relatar um problema</b> (você envia): seu texto, dados do aparelho, idioma, identificador de instalação e e-mail da conta.</td><td>Atender o seu relato.</td><td>Até você excluir a conta ou pedir a exclusão.</td></tr>
<tr><td><b>Comunidade</b> (opcional): nome de usuário, nome, bio, foto, publicações (texto, presets e arquivos de projeto de até 50 MB, que podem conter suas mídias), comentários, curtidas, seguidores e bloqueios.</td><td>Mostrar o que você publica aos outros usuários. O que você publica é público.</td><td>Até você apagar a publicação ou a conta.</td></tr>
<tr><td><b>Denúncias</b>: quem denunciou, o conteúdo denunciado, o motivo e o texto opcional.</td><td>Moderar a Comunidade e o Vídeo com IA.</td><td>Até a revisão; apagadas com a conta.</td></tr>
<tr><td><b>Presets de legenda compartilhados</b> (opcional): o preset, o nome de autor que você digita, um token aleatório do aparelho e um hash do IP.</td><td>Publicar presets e evitar abuso.</td><td>Enquanto o preset estiver publicado.</td></tr>
<tr><td><b>Vídeo com IA</b> (opcional): o texto (prompt), a imagem que você escolher (até 8 MB), o identificador App Set do aparelho e a conta.</td><td>Gerar o vídeo. O pedido é processado pela 8Scale (fornecedor). Antes de gerar é exibido um anúncio com recompensa.</td><td>Imagem: 1 hora. O vídeo gerado não fica guardado no nosso servidor.</td></tr>
<tr><td><b>Proteção contra abuso</b>: hash do endereço IP em limites de tentativas.</td><td>Segurança do serviço.</td><td>Só pelo tempo da proteção.</td></tr>
<tr><td><b>Anúncios</b>: os SDKs dos parceiros (Unity LevelPlay/ironSource, Unity Ads, Google Mobile Ads e UMP) podem coletar o ID de publicidade, endereço IP, dados do aparelho e interações com anúncios.</td><td>Mostrar anúncios (por exemplo, depois de exportar), medir e prevenir fraude. Anúncios personalizados dependem do seu consentimento.</td><td>Conforme a política de cada parceiro.</td></tr>
</table>

<h2>4. Permissões do aparelho</h2>
<ul>
<li><b>Fotos e vídeos</b>: para a galeria do editor, pedida quando você abre a galeria. O seletor do sistema continua disponível sem permissão.</li>
<li><b>Microfone</b>: só no “Gravar voz”, depois que você toca em Gravar. O áudio fica no projeto, no aparelho.</li>
<li><b>Câmera</b>: só na Câmera Real com rastreamento (ARCore, opcional). As imagens não são enviadas para nós; o ARCore segue a política do Google.</li>
<li><b>Internet</b> e <b>ID de publicidade</b>: para os serviços acima e para os anúncios.</li>
</ul>
<p>O app não usa localização, contatos nem notificações. O backup automático do Android está desativado.</p>

<h2>5. Com quem compartilhamos</h2>
<p>Fornecedores que tratam dados em nosso nome: Cloudflare (servidor, contas, Comunidade, arquivos), Resend (e-mail dos relatórios ao desenvolvedor), 8Scale (Vídeo com IA), Unity e Google (anúncios e consentimento), Hugging Face (download do modelo de legendas; nenhum dado pessoal além da conexão). Podemos divulgar dados se a lei exigir. Os servidores podem ficar fora do Brasil; a transferência segue a LGPD.</p>

<h2>6. Seus direitos</h2>
<p>Você pode acessar, corrigir e excluir seus dados. Para excluir a conta e os dados ligados a ela: no app, <b>Início → menu → Excluir conta</b>, ou pela web em <a href="/excluir-conta">/excluir-conta</a>. Os relatórios de falha ficam até 30 dias no servidor; os e-mails já enviados ao desenvolvedor não podem ser recolhidos. Para outros pedidos, escreva para <a href="mailto:${CONTATO}">${CONTATO}</a>.</p>

<h2>7. Crianças</h2>
<p>O Aurea não é destinado a menores de 13 anos e não coleta dados de crianças de propósito.</p>

<h2>8. Mudanças</h2>
<p>Quando esta política mudar, a data no topo é atualizada.</p>
</section>

<section lang="en" id="en">
<h1>Aurea — Privacy Policy</h1>
<h2>1. Who we are</h2>
<p>Aurea is a video and motion graphics editor made by Ruan Pablo (independent developer). Privacy contact: <a href="mailto:${CONTATO}">${CONTATO}</a>.</p>
<h2>2. Summary</h2>
<ul>
<li>Your projects, videos, photos and audio stay on your device. They only leave it when <b>you</b> publish to the Community or use AI Video.</li>
<li>The account (email and password) is <b>optional</b>: it is used for the Community, AI Video and sending crash reports.</li>
<li>Auto captions, AI cutout, depth maps and upscaling run on the device.</li>
<li>The app shows partner ads, with Google's consent screen (UMP) where the law requires it.</li>
<li>We do not sell personal data.</li>
</ul>
<h2>3. Data we process</h2>
<ul>
<li><b>Account</b>: email, password (stored only as a PBKDF2 hash on the server) and account ID — to sign in; kept until you delete the account.</li>
<li><b>Session</b>: access token (encrypted on the device) — expires in 30 days or on sign-out; deleted with the account.</li>
<li><b>Crash reports</b> (automatic while signed in): app and OS version, device model and maker, CPU type, technical stack trace, random install ID, account email and country (from the connection) — to fix crashes; emailed to the developer; kept 30 days on the server.</li>
<li><b>Report a problem</b> (you send it): your text, device data, language, install ID and account email — kept until you delete the account or ask us to.</li>
<li><b>Community</b> (optional): username, name, bio, photo, posts (text, presets and project files up to 50 MB that may contain your media), comments, likes, follows and blocks — what you publish is public; kept until you delete it or your account.</li>
<li><b>Reports</b>: reporter, reported content, reason and optional text — for moderation; deleted with the account.</li>
<li><b>Shared caption presets</b> (optional): the preset, the author name you type, a random device token and an IP hash.</li>
<li><b>AI Video</b> (optional): prompt, the image you choose (up to 8 MB), the device App Set ID and the account — processed by 8Scale (provider); a rewarded ad is shown first; the image is kept 1 hour and the generated video is not stored on our server.</li>
<li><b>Ads</b>: partner SDKs (Unity LevelPlay/ironSource, Unity Ads, Google Mobile Ads and UMP) may collect the advertising ID, IP address, device data and ad interactions. Personalized ads depend on your consent.</li>
</ul>
<h2>4. Device permissions</h2>
<p>Photos and videos (the editor's gallery, asked when you open it), microphone (only “Record voice”, after you tap Record; audio stays on the device), camera (only the optional ARCore Real Camera tracking; images are not sent to us), internet and advertising ID. No location, contacts or notifications. Android auto backup is off.</p>
<h2>5. Sharing</h2>
<p>Providers acting for us: Cloudflare, Resend, 8Scale, Unity, Google and Hugging Face (model download only). We may disclose data when the law requires it.</p>
<h2>6. Your rights</h2>
<p>You can access, correct and delete your data. Delete your account in the app (<b>Home → menu → Delete account</b>) or on the web at <a href="/delete-account">/delete-account</a>. Crash reports stay up to 30 days on the server; emails already sent to the developer cannot be recalled. Other requests: <a href="mailto:${CONTATO}">${CONTATO}</a>.</p>
<h2>7. Children</h2>
<p>Aurea is not directed to children under 13 and does not knowingly collect their data.</p>
<h2>8. Changes</h2>
<p>When this policy changes, the date at the top is updated.</p>
</section>`);

const EXCLUIR = pagina('Aurea — Excluir conta / Delete account', `
<h1>Excluir a conta do Aurea</h1>
<p class="muted">Delete your Aurea account — English below.</p>

<section lang="pt-BR">
<h2>No app</h2>
<p>Abra o Aurea → <b>Início</b> → menu → <b>Excluir conta</b> → confirme.</p>
<h2>Pela web</h2>
<p>Entre com o e-mail e a senha da conta. A exclusão é imediata e não pode ser desfeita.</p>
<h2>O que é apagado</h2>
<ul>
<li>A conta (e-mail e senha), as sessões e os relatos de problema enviados com ela.</li>
<li>Na Comunidade: perfil, foto, publicações e seus arquivos, comentários (seus e os feitos nas suas publicações), curtidas, seguidores, bloqueios e denúncias.</li>
</ul>
<p>Fica por um tempo: relatórios de falha, até 30 dias no servidor; e-mails já enviados ao desenvolvedor não podem ser recolhidos. Projetos e mídias no seu aparelho não são tocados.</p>
<p>Esqueceu a senha? Escreva para <a href="mailto:${CONTATO}?subject=Excluir%20conta%20Aurea">${CONTATO}</a> com o e-mail da conta.</p>
</section>

<section lang="en">
<h2>Delete your account</h2>
<p>In the app: <b>Home</b> → menu → <b>Delete account</b> → confirm. On the web: sign in below with the account email and password. Deletion is immediate and cannot be undone. It removes the account, sessions, problem reports, and in the Community your profile, photo, posts and files, comments, likes, follows, blocks and reports. Crash reports stay up to 30 days on the server; emails already sent to the developer cannot be recalled. Forgot your password? Email <a href="mailto:${CONTATO}?subject=Delete%20Aurea%20account">${CONTATO}</a>.</p>
</section>

<section lang="pt-BR">
<form id="excluir" autocomplete="on">
  <label>E-mail / Email <input type="email" name="email" required autocomplete="username"></label>
  <label>Senha / Password <input type="password" name="password" required autocomplete="current-password"></label>
  <label><input type="checkbox" name="confirm" required> Entendo que a exclusão é definitiva. / I understand deletion is permanent.</label>
  <button type="submit">Excluir minha conta / Delete my account</button>
  <p id="resultado" role="status"></p>
</form>
</section>`, `
const form = document.getElementById('excluir'), out = document.getElementById('resultado');
form.addEventListener('submit', async (e) => {
  e.preventDefault();
  const button = form.querySelector('button'); button.disabled = true; out.textContent = '…';
  try {
    const login = await fetch('/api/auth/login', { method: 'POST', headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ email: form.email.value.trim(), password: form.password.value }) });
    if (!login.ok) { out.textContent = login.status === 401 ? 'E-mail ou senha incorretos. / Wrong email or password.' : 'Tente de novo mais tarde. / Try again later.'; return; }
    const { token } = await login.json();
    const del = await fetch('/api/auth/delete', { method: 'POST', headers: { 'content-type': 'application/json', authorization: 'Bearer ' + token },
      body: JSON.stringify({ confirm: true }) });
    out.textContent = del.ok ? 'Conta excluída. / Account deleted.' : 'Não foi possível excluir agora. / Could not delete now.';
    if (del.ok) form.reset();
  } catch { out.textContent = 'Sem conexão. / No connection.'; }
  finally { button.disabled = false; }
});`);

const SEGURANCA = {
  'content-type': 'text/html; charset=utf-8',
  'cache-control': 'public, max-age=300',
  'x-content-type-options': 'nosniff',
  'referrer-policy': 'no-referrer',
  'content-security-policy': "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'",
};

/** Devolve a página quando a rota é uma delas; null para o Worker seguir. */
export function rotaDePaginas(req, url) {
  if (req.method !== 'GET' && req.method !== 'HEAD') return null;
  const rota = url.pathname.replace(/\/+$/, '');
  const html = rota === '/privacidade' || rota === '/privacy' ? PRIVACIDADE
    : rota === '/excluir-conta' || rota === '/delete-account' ? EXCLUIR : null;
  if (!html) return null;
  return new Response(req.method === 'HEAD' ? null : html, { headers: SEGURANCA });
}
