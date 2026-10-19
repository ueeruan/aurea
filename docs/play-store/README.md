# Aurea na Google Play — o que está pronto e o que falta fazer

Atualizado em 19/10/2026 (Aurea 0.2.6, versionCode 2194, `applicationId com.aurea.aurea`).

## Pronto no código

- **AAB** (`:app:bundleRelease`) com arm64-v8a e armeabi-v7a, R8 + encolhimento de
  recursos, símbolos nativos dentro do bundle (o Play Console simboliza os crashes),
  `isDebuggable=false`, `targetSdk 36`, `minSdk 26`.
- **16 KB de página** (exigência da Play para alvo Android 15+): todas as `.so`
  com segmentos alinhados em 0x4000, bundle com bibliotecas descomprimidas e
  alinhamento de 16 KB (conferido no APK e no `BundleConfig.pb`).
- **Idiomas no bundle sem divisão** (`bundle { language { enableSplit = false } }`):
  o app troca de idioma por dentro; sem isso a Play instalaria só o idioma do aparelho.
- **Build da Play** (`BuildConfig.PLAY_STORE`, automático no `bundleRelease`; para
  um APK de teste: `-PaureaStore=play`): sem a tela que exige abrir o TikTok e o
  Instagram e sem doação por PIX/PayPal. O APK de sideload não muda.
- **Moderação** (políticas de conteúdo do usuário e de IA generativa), no Android:
  denunciar post, comentário, perfil, preset de legenda e vídeo de IA; bloquear
  perfis; conteúdo denunciado por 3 pessoas sai do feed até a revisão; o admin
  (conta com papel `community_admin`) remove qualquer post ou comentário; cada
  denúncia chega por e-mail ao desenvolvedor.
- **Política de privacidade e exclusão de conta pela web** servidas pelo Worker
  (`discovery/paginas.js`), com link para a política em Ajustes → Sobre.
- Sem autoatualização fora da loja, sem `REQUEST_INSTALL_PACKAGES`, sem serviço em
  primeiro plano, sem localização. Backup do Android desligado. Só HTTPS.

## Antes de enviar (nesta ordem)

### 1. Publicar o servidor (você)

As rotas de denúncia, bloqueio, privacidade e exclusão de conta só existem depois do
deploy. Na pasta `discovery/`:

```bash
npx wrangler d1 migrations apply aurea-contas --remote
```

```bash
npx wrangler deploy
```

Depois abra `https://aurea-ai-discovery.aureaapp.workers.dev/privacidade` e
`.../excluir-conta` no navegador para conferir. A política usa
`ruanpablombl@gmail.com` como contato (o mesmo do servidor); troque em
`discovery/paginas.js` (`CONTATO`) se quiser outro e-mail público.

### 2. Criar a chave de upload (você, uma vez)

Guarde o `.jks` e as senhas fora do repositório. Sem a chave de upload não sai
atualização (o Google consegue trocá-la pelo Play Console, mas demora).

```bash
keytool -genkeypair -v -keystore C:/Users/Ruan/aurea-upload.jks -alias aurea-upload -keyalg RSA -keysize 4096 -validity 10000 -storetype PKCS12
```

Copie `android/keystore.properties.example` para `android/keystore.properties` e
preencha caminho e senhas (o arquivo está no `.gitignore`).

### 3. Gerar o AAB

Na pasta `android/` (PowerShell, com o JDK 21):

```bash
.\gradlew.bat :app:bundleRelease -PaureaStore=play -x prepareAureaFFmpegarm64v8a -x prepareAureaFFmpegarmeabiv7a -PaureaGlesCompiler=D:/Users/Ruan/Documents/Aureabeta/engine/build/shader-compilers/Release/aurea-gles-compiler.exe
```

Saída: `build/android/app/outputs/bundle/release/app-release.aab` (~199 MB por
causa dos símbolos de depuração; o download por aparelho fica em ~58 MB). Sem o
`keystore.properties` o comando falha de propósito: a Play recusa bundle assinado
com a chave de debug.

### 4. Play Console

1. **Criar o app:** nome "Aurea – Editor de Motion", idioma padrão Português
   (Brasil), App, Gratuito. Aceite as declarações.
2. **Assinatura de apps do Google Play:** ative (padrão). O Google guarda a chave de
   app; você envia com a chave de upload.
3. **Ficha da loja:** textos em `listing/*.md` (pt-BR, en-US, es-419, ru-RU, id,
   ar). Gráficos: ícone 512 e gráfico de recursos 1024×500 em
   `output/playstore/` (fora do Git). As capturas de lá são da 0.0.3: refaça com a
   0.2.6 (mínimo 2 por idioma, proporção até 2:1). Categoria: **Players e editores
   de vídeo**. E-mail de contato do desenvolvedor.
4. **Conteúdo do app:** responda com `formularios.md` (Segurança dos dados,
   anúncios, ID de publicidade, acesso ao app com a conta de teste, IARC, público
   16+, declaração de fotos e vídeos, IA generativa, exclusão de conta).
5. **Política de privacidade:** a URL `/privacidade`. **Exclusão de conta:** a URL
   `/excluir-conta`.
6. **Teste interno:** suba o AAB, instale pelo link e confira login, Comunidade
   (denunciar/bloquear), Vídeo com IA, anúncios e consentimento (UMP) e exportação.
7. **Teste fechado:** contas pessoais criadas depois de 13/11/2023 precisam de um
   teste fechado com pelo menos 12 testadores ativos por 14 dias seguidos antes de
   pedir produção (confira o número no Console; o Google já mudou essa regra).
8. **Produção:** lançamento gradual (10 % → 50 % → 100 %).

## Atenção: quem usa o APK de sideload

O APK distribuído hoje é assinado com a chave de debug desta máquina; o da Play é
assinado pelo Google. **Não dá para atualizar de um para o outro**: quem tem o APK
precisa desinstalar antes de instalar pela Play — e a desinstalação apaga os
projetos guardados no app. Avise para salvar os projetos antes ("Exportar arquivo do
projeto", que gera um `.aureaproj`) e importá-los depois.

## Pendências conhecidas (não bloqueiam o envio)

- **iOS pausado:** a moderação e o link da política no iOS estão guardados no
  `git stash` ("iOS: moderacao (denunciar/bloquear) e link de privacidade"). O
  `AureaStrings.swift` regenerado com os textos novos ficou fora do commit. Antes de
  publicar na App Store, aplique o stash, regenere os textos e compile.

- O App ID do AdMob no release ainda é o de teste do Google; o provedor ativo é o
  LevelPlay. Ponha o ID real ou tire o AdMob se não for usar.
- O campo de chave do Groq em Ajustes não é usado por nenhum recurso.
- Os relatórios de falha automáticos incluem o e-mail da conta (declarado em
  `formularios.md`).
