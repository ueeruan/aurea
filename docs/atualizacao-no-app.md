# Atualizar dentro do app (Android)

O aparelho se atualiza sozinho: o app pergunta ao servidor do mural qual é
a última versão, e quando a de lá é mais nova ele mostra uma faixa na
Início com o botão **Atualizar**. O botão baixa o APK e abre o instalador
do sistema. Não passa por loja, e ninguém precisa procurar arquivo.

Só Android. O iOS não instala aplicativo fora da App Store — lá o caminho
é o TestFlight.

## Como funciona

```
tag apk-91  →  GitHub Actions
                 ├── builda os APKs (um por arquitetura)
                 ├── publica como release  ← é daqui que o app baixa
                 └── PUT /versao no Worker ← é assim que o app sabe
                                              ↓
app abre → GET /versao → compara com o versionCode instalado
         → mais novo? faixa na Início → baixa → confere o sha256 → instala
```

O **número que decide** é o `versionCode` do Android (o `+91` do
`pubspec.yaml`), e não o nome. Comparar `1.1.10` com `1.1.9` por texto
diria que `1.1.9` vem depois.

## O endereço do arquivo fica no servidor, e não no APK

Dentro do APK qualquer endereço é público — um APK é um zip, e trocar uma
linha no meio leva minutos. Com o endereço no servidor, trocar o que todo
mundo baixa exige a senha de moderação. É o mesmo motivo pelo qual a chave
do mural mora lá.

## Publicar à mão

O normal é o workflow fazer tudo. Para publicar sem CI:

```bash
cd servidor/comunidade
SENHA_DE_MODERACAO=... node versao.mjs --arquivo ../../build/app/outputs/flutter-apk/app-arm64-v8a-release.apk \
    --url https://github.com/OWNER/REPO/releases/download/apk-92/app-arm64-v8a-release.apk \
    --codigo 92 --nome 1.1.7-beta --notas "O texto árabe voltou a ligar."
```

Com `--arquivo`, o script calcula o tamanho e o sha256 sozinho. `--ver`
mostra o que está publicado hoje; `--apagar` tira do ar (o app para de
oferecer atualização).

### O que o servidor aceita

| campo | regra |
|---|---|
| `codigo` | inteiro > 0, obrigatório |
| `versao` | 3+ caracteres |
| `apk` | obrigatoriamente `https://` |
| `sha256` | 64 hex, em minúsculas. Inválido vira vazio |
| `obrigatoria` | a faixa não fecha: só segue depois de atualizar |

Sem nada publicado, `GET /versao` devolve `null` e **o app não mostra
faixa nenhuma**. Nunca "atualize" sem ter o que oferecer.

## A CHAVE DE ASSINATURA: o CI e a maquina NAO assinam igual

Medido em 18/09/2026, e é a pegadinha que quase estragou a publicação do
apk-93.

O Android **recusa instalar por cima** um APK assinado com outra chave: ou
desinstala (e a pessoa perde os projetos) ou não instala. Então a chave
não é detalhe de build, é a condição para a atualização existir.

| build | chave (SHA-256 do certificado) |
|---|---|
| local, `flutter build apk --release` nesta máquina | `55bf3cc8…b5c8` |
| CI, `build-apk.yml` no runner | `eaf9baae…8e5c` |

São chaves de debug **diferentes**: `android/key.properties` não existe em
nenhum dos dois, então o Gradle cai na debug de cada um — a daqui e a do
runner. Um APK do CI **não instala** sobre um instalado a partir do arquivo
local, e foi assim que os betas chegaram aos testadores (arquivo local).

**O que se faz hoje:** o APK que o app baixa é o **construído aqui**
(`flutter build apk --release --split-per-abi`), conferido com
`tool/verify_beta_package.py --certificate 55bf3cc8…`, publicado num
release com tag que **não** dispara o workflow (`beta-93`, porque o
workflow escuta `apk-*` e `v*`) e anunciado no servidor com o `versao.mjs`.
O release do CI continua valendo como artefato de build, mas **não** é o
que vai para o aparelho.

**O conserto de verdade**, para o dia em que valer: pôr a chave de debug
desta máquina nos secrets do repositório (base64) e o workflow escrever o
`key.properties` e o keystore antes de compilar. Aí o `apk-*` volta a
fechar o ciclo sozinho. Não foi feito porque a chave em Secrets é decisão
do dono, e porque gravar a chave em repositório público está fora de
questão.

---

## O que o app confere antes de instalar

1. **Tamanho** — um download cortado no meio vira uma instalação que falha
   no fim, depois de gastar a internet da pessoa.
2. **sha256** — quando o servidor publica um. Sem esta conferência, um
   arquivo trocado no caminho seria um aplicativo trocado.
3. **A permissão do sistema** — o Android só deixa instalar pacote de fora
   da loja com "instalar apps desconhecidos" ligado, e quem liga é a
   pessoa, numa tela de Ajustes. O app leva ela até lá e explica.

## Arquivos

| onde | o quê |
|---|---|
| `servidor/comunidade/worker.js` | as rotas `GET`/`PUT /versao` |
| `servidor/comunidade/versao.mjs` | publicar, ver, apagar |
| `servidor/comunidade/teste.mjs` | 10 testes das rotas novas |
| `.github/workflows/build-apk.yml` | build, release e aviso |
| `lib/src/core/atualizacao/atualizacao_service.dart` | procurar, baixar, conferir |
| `lib/src/features/projects/presentation/faixa_de_atualizacao.dart` | a faixa |
| `android/.../MainActivity.kt` | versão instalada e instalador |
| `android/.../res/xml/caminhos_do_apk.xml` | o que o instalador alcança |

## O segredo do CI

O último passo do workflow precisa de `SENHA_DE_MODERACAO` nos secrets do
repositório. Sem ele, o APK sai publicado e o passo avisa no log que
nenhum aparelho vai saber — e aí é publicar à mão com o `versao.mjs`.
