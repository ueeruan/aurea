# Aurea atual no iOS — registro de verificação

## Port do Android — revisão em andamento de 23/09/2026

### Situação mais recente

- `35916264387` / `3f8be13`: aparelho e simulador compilaram; 18 estados
  foram capturados e quatro testes de gestos passaram (42,8 s). O texto 3D
  tem geometria visível, mas está mais escuro que o APK Android. A seleção
  3D usa coordenadas zeradas indevidamente. Particular tinha 451 partículas
  nas métricas, mas nenhum pixel visível no frame: isso **não** aprova render.
  A exportação encerrou o app antes do relatório final e falhou na validação.
- As causas encontradas depois desse run incluem: conversão ausente de
  frames para nanossegundos em seek/scrub; uso de corners mesmo com geomFlags
  inválido; MP4 passthrough sem sourceFormatHint; descarte de amostras quando
  o gravador não estava pronto; contagem/duração PCM incorretas.
- `e815826` acrescenta as correções de timeline/export e prova AAC com WAV
  sintético importado pela rota real. `35918592352` falhou no target Debug
  por usar o nome ObjC `scrubToFrame` em Swift; corrigido para `scrub(toFrame:)`.
- `333c5ad` / `35919309045` está em validação nativa: incorpora backdrop da
  Home com a mesma faixa/sigma/tinta do Android, ajustes Particular/Export,
  seleção 3D, dois controles de culling, precomp e seis testes de gestos.
  Não há aprovação antecipada de desempenho, render ou paridade.
- A fixture HDRI foi criada e renderizada no Android pelas telas normais
  (capturas 29/30). Seu arquivo externo foi gerado matematicamente, sem mídia
  pessoal. A abertura iOS ainda está sendo validada: o arquivo Android guarda
  um caminho privado da plataforma que precisa ser resolvido no core.

Comparação visual de 15 pares reais: [galeria local](parity/comparison.html).
As dimensões nativas e insets estão preservados; as diferenças registradas
se referem ao run indicado em cada imagem, não às alterações posteriores.

### Histórico da execução

O usuário rejeitou a interface do build 2106. A revisão `73c5a23` substitui
os painéis, menus, timeline e gestos usando os fontes Kotlin do produto;
reutiliza os ícones, presets e textos Android, além da fonte Roboto do AVD
com licença e SHA-256 registrados. O backend continua no mesmo C++.

O primeiro ensaio de execução iOS (`35909245668`, commit `67bd9dd`) compilou
os targets de aparelho e simulador, mas falhou no upload GPU inicial com
`tempo esgotado: submissao imediata`. As oito capturas reais ficaram na Home
com o erro visível. Portanto não provaram paridade do editor nem abertura
da fixture. Estão em `build/ios-parity/35909245668` para diagnóstico.

A revisão seguinte substitui a espera no evento Metal pela conclusão real
do command buffer, preserva recursos até o fim da GPU e grava logs por cena.
O teste executa 16 estados do app e salva também frames renderizados pelo
motor, JSON de estado e `.aurea`. Na execução `35912469704`, os dois targets
compilaram, `coreStarted` foi verdadeiro e a fixture Android abriu. Contudo,
os 15 frames do motor foram preto opaco uniforme, todos com o mesmo SHA-256
`4c5672787185…`. As capturas reais do editor também mostram o preview preto.
O sucesso desse workflow NÃO comprova renderização nem paridade visual.

A revisão `ae4ce16`, execução `35914411601`, corrigiu a seleção do SDK Metal (simulador/aparelho), a
conversão do eixo Y e a atualização das push constants; espelha os erros
reais do motor no stderr do teste e registra contadores de renderização.
A verificação independente por ImageIO rejeita frames vazios/uniformes nas
fixtures com conteúdo. Essa execução passou em 16 estados e os quatro
testes XCTest de toque, arrasto, pinça e resize de forma/desfazer passaram
em 43,4 segundos. A inspeção das capturas confirmou renderização 2D real:
forma, Glow, texto, vetor e máscara. O cenário 3D revelou profundidade de
teste incorreta (28 alturas de letra, atravessando a câmera, em vez de
0,25 do Android); isso não foi aprovado como imagem 3D válida. A revisão
`db08005` corrige esse cenário e o texto inicial, carrega uma segunda
fixture Android com Texto3D + Particular e verifica um MP4 H.264 real.
O IPA dessa revisão validada em 2D foi preservado separadamente em
`entrega/aurea-beta2-2107-ae4ce16-ios-arm64.ipa`; ainda não é declaração
de paridade integral do aplicativo.

O `.aurea` salvo pelo iOS foi aberto no APK Android 2103 como uma cópia nova,
`iOS-Roundtrip-35912469704`. Forma e Glow foram restaurados, mas a rotação e
opacidade alteradas não persistiram: o Android mostrou 0°/100%, enquanto o
JSON em memória do iOS mostrava 15°/75%. A seção Timeline do arquivo editado
era idêntica à original. A causa é salvar antes de consumir os comandos
enfileirados. A correção no C++ agora consome a fila sob o mutex do modelo
antes de serializar; a bridge envia seu lote local antes de chamar save.
A regressão reproduziu sete falhas na versão anterior e passou com 25
verificações após a correção, preservando 15°/75% e 30°/50% sem nenhum frame
ou GPU. Os filtros Engine (48), Serialization (19) e dois testes de
concorrência passaram no MSVC Release. A repetição nativa iOS→Android foi
concluída com o arquivo de `35914411601`: o APK2103 exibiu 15° e75%, com a
forma e Glow preservados (capturas25/26). O iOS também reabriu o próprio
arquivo antes de registrar os valores no JSON. Esse caso de roundtrip
passou; não comprova todos os tipos de asset/projeto. As capturas20/21
preservam a evidência da falha anterior.

A referência Android contém agora 30 capturas reais e quatro projetos sintéticos:
forma + Glow, Texto3D + Particular, Precomp e HDRI. O último acompanha um HDR
gerado de 8.239 bytes; nenhum usa mídia pessoal, autor ou descrição. Ver manifesto
`docs/parity/android-2103/manifest.json` e auditoria da fixture. Código
compilável e controles implementados não constituem aprovação visual ou
funcional; os resultados nativos e seus limites estão discriminados acima.

## Build de teste 2106 — 23/09/2026

Compilação Xcode concluída no commit `2ed7116`, workflow
[35904906601](https://github.com/ueeruan/aurea/actions/runs/35904906601).
Validações Foundation do bundle, shaders Metal e IPA empacotado passaram.
IPA sem assinatura: `entrega/aurea-beta2-2106-ios-arm64.ipa`; SHA-256 em
`entrega/aurea-beta2-2106-ios-arm64.sha256`.

O compilador revelou dependências incompletas da Home, nomes de cores ausentes,
um erro de sintaxe da timeline e acesso incorreto a marcadores. Foram corrigidos;
as abas conectam biblioteca, seleção, importação e criação reais. A folha de
criação e Ajustes reutilizam implementações do build anterior adaptadas à Home.
Essas telas ainda exigem comparação visual com Android; compilar não aprova
paridade. Não houve instalação nem teste funcional no iPhone nesta entrega.

## Estado atual — correção de rumo de 23/09/2026

**Paridade NÃO aprovada.** Os registros de Xcode/IPA abaixo são históricos,
referentes às revisões indicadas, e não validam a árvore de trabalho atual.
Uma compilação bem-sucedida também não aprova interface, gestos ou resultados.

A referência executada nesta revisão foi o APK de entrega
`entrega/aurea-beta2-2103-arm64-v8a.apk`, `com.aurea.aurea`, build 2103,
instalado no emulador `am2test` (API 34). A instalação debug foi preservada.
Foram feitas dez capturas reais em [parity/android-2103](parity/android-2103/manifest.json):
Home, criação, editor vazio, adicionar camada, dock/timeline com forma,
Transform, Effects, Effect Browser, ficha Glow e Glow aplicado.
O manifesto registra o SHA-256 do APK e de cada imagem. O idioma do emulador
é inglês; os rótulos mistos capturados são do próprio APK, sem edição.

### Correções feitas no código

- Incluídos no target os oito arquivos Swift existentes que estavam fora da
  fase Sources; removida a declaração duplicada do painel Particular.
- Empacotada e registrada a fonte de ícones original do Android, com bytes
  idênticos. As três abas da Home usam os mesmos glifos, com tamanho fixo.
- Particular usa `keyParameter`/`editTrackKey` com o endereço completo
  (camada, propriedade, efeito, parâmetro e tempo local). O caminho anterior
  omitia o índice do parâmetro. Vínculos consultam e alteram o core.
- Painel 3D passa a chamar os métodos existentes de `AureaEngine` em vez de
  métodos inexistentes de `AureaModel`.
- A CI confere fontes, presets, target, símbolos Swift e tipos duplicados.
  A verificação do bundle usa CoreText; a do IPA exige a fonte original.
  O IPA histórico 2105 é rejeitado por não registrar essa fonte.

### Continuação — navegador e parâmetros de efeitos

- O catálogo iOS agora abre em tela cheia, com busca, categorias, favoritos,
  recentes e grade adaptável de 2 a 5 colunas. Tocar no cartão abre a ficha;
  aplicar usa a seleção de camadas desbloqueadas e os comandos do motor.
- As prévias usam `effectPreview` do core, fora da thread principal, com a
  mesma foto Android, cache de memória/disco e cancelamento de solicitações
  ainda na fila. A placa de carregamento/indisponibilidade segue o Android.
  O funcionamento deste caminho em Metal ainda precisa de execução iOS.
- A ponte expõe classe de custo e flags dos parâmetros. Corrigida a leitura
  de ParamType: Bool, Color, Point2D/3D, Angle e Enum estavam confundidos por
  números incompatíveis com o enum C++. A CI verifica esses identificadores.
- Os grupos principal/avançado e a apresentação de unidades seguem as regras
  Android. Campos numéricos mantêm unidades fora do texto editável; parâmetros
  não animáveis não oferecem keyframe/curva. Cores são convertidas para RGBA
  linear ao escrever no core.
- Os textos antes literais do catálogo Android passaram ao catálogo de strings
  compartilhado, preservando seus textos. Kotlin compilou com sucesso.
- Verificados no host: 6 testes de prévias / 23 verificações e 1 teste do
  catálogo / 49 verificações, sem falhas (44 efeitos desenhados, 4 recusados
  por classe). Isso não testa o carregador Swift nem os shaders Metal.
- Auditorias estáticas passaram: 30 fontes Swift no target, API/strings,
  tipos, dependências, 396 símbolos C++ e 6 recursos idênticos ao Android
  (fonte, foto e quatro catálogos de presets).
- O painel ainda usa controles SwiftUI que exigem comparação e ajuste visual;
  esta implementação não constitui aprovação de paridade. O IPA 2106 foi gerado posteriormente, conforme registro acima.

### Evidência desta revisão

- Core recompilado no Windows: 19 testes de serialização, 199 verificações,
  sem falhas; teste de endereço de curvas/tempo local, 20 verificações, sem falhas.
- O projeto representativo manteve seus 8.159 bytes após abrir e serializar.
  O teste preserva `modifiedUnixMs`: uma segunda chamada a `save_project`
  atualiza a data por definição e não pode exigir bytes idênticos de metadados.
  Isso não prova cobertura de todos os recursos nem intercâmbio entre aparelhos.
- Auditorias de target, API Swift, tipos, dependências e símbolos C++ passaram.
  Fonte e quatro catálogos de presets são idênticos aos arquivos Android.
- **Xcode agora passou no build 2106 acima; execução no iPhone permanece pendente.**

### Bloqueios para aceitar o mesmo produto

| Critério | Evidência atual | Falta |
| --- | --- | --- |
| Home/editor/timeline | Capturas Android; correções de integração Swift | Capturas iOS no mesmo tamanho lógico, estado e projeto; corrigir todas as diferenças |
| Effects → catálogo → ficha → aplicar | Fluxo implementado em Swift; prévias ligadas ao core e à foto Android | Compilar no Xcode e comparar capturas, gestos e resultados no iOS |
| Assets | Fonte, foto de efeitos e presets verificados byte a byte | Conferir logo, todas as famílias de ícones e recursos em uso no iOS |
| Core/Metal/preview | Implementação existente; verificações estáticas | Compilar e executar esta revisão no iOS; validar preview e shaders reais |
| `.aurea` Android ↔ iOS | Testes do serializador compartilhado no host | Projeto criado no Android aberto/editado/salvo no iOS e inverso, incluindo mídias e fontes |
| Ferramentas e render | Correções Particular/3D; screenshots Transform/Glow | Validar keyframes, curvas, vetores, máscaras, 3D/HDRI, Particular, precomp, temporal e export em ambas as plataformas |

Nenhuma linha acima autoriza chamar o iOS de pronto. O restante deste documento
preserva o histórico de builds; não é uma declaração de aceitação da versão atual.

Fonte: `C:/Users/SnyX/Documents/Projetos - Claude/Aureabeta`.
Referência de interface: `android/app/src/main/java/com/aurea/aurea/`.
Motor compartilhado: `engine/`; iOS usa Metal, VideoToolbox e AVFoundation.
O projeto Flutter descontinuado não participa deste IPA.

## Critério de entrega

Cada operação precisa usar o motor real, preservar desfazer/refazer e salvar
no mesmo formato `.aurea`. Compilar não comprova funcionamento no iPhone nem
paridade visual. Os IPAs anteriores foram recusados pelo usuário.

## Implementação atual

| Área Android | Implementação iOS | Verificação |
| --- | --- | --- |
| Home, criação e projetos | HomeView, AureaModel | Xcode aprovado; repetir fluxo físico |
| Editor, dock, FAB, retorno, layout largo | EditorView, Theme, ToolbarView | Xcode aprovado |
| Timeline, miniaturas, waveform, trim, seleção múltipla | TimelineView | Xcode aprovado; encaixe e conflito de gestos corrigidos no build 2104 |
| Importação vídeo/foto/áudio/3D/HDRI | AureaModel | Trabalho fora da thread principal; Xcode aprovado |
| Transformação e keyframes | TransformView, PreviewMetalView | Xcode aprovado; valores avaliados e tempo local |
| Efeitos, aparência, velocidade e áudio | EffectsView, LayerPanels | Xcode aprovado |
| Texto, fontes, estilo, animadores, caminho e trechos | EditorView, LayerPanels, VectorPanel | Xcode aprovado |
| Texto 3D, chanfro, material, sombras e animação de letras | Panel3DView | Xcode aprovado |
| HDRI global e por objeto | Panel3DView, bridge, Engine | Xcode e testes nativos aprovados |
| Máscaras, pontos, tangentes, matte, rastreamento | LayerPanels, StageOverlay, MediaPanels | Xcode aprovado |
| Vetores, 20 parâmetros, desenho livre e SVG | VectorPanel | Xcode aprovado |
| Partículas | ParticleControls, LayerPanels | 70 controles, fontes e curvas; Xcode aprovado |
| Rastreamento de câmera e ponto | MediaPanels | Xcode aprovado; execução no aparelho pendente |
| Legendas SRT, revisão, estilos e Groq | MediaPanels | Xcode aprovado; transcrição só por ação explícita; rede/API não testadas |
| Presets animação/efeitos/texto | LayerPanels, Resources | JSONs reais do Android; Xcode aprovado |
| Favoritos/recentes, presets de curvas/legendas | LayerPanels | Xcode aprovado no build 2104 |
| Curvas completas, expressões, remapeamento temporal | AnimationTools, LayerPanels | Xcode e teste nativo aprovados |
| Configurações e avisos Cloudflare | AppPanels | Xcode aprovado |
| Exportação e compartilhamento | ExportView | Compilado; vídeo exportado precisa de teste físico |

## Validações concluídas

- Motor: 46 testes / 1084 verificações sem falhas.
- Presets: 10 testes / 393 verificações sem falhas; 23 presets embutidos validados.
- Curvas por endereço e tempo local: 1 teste / 20 verificações sem falhas.
- Importação de HDRI por objeto: 1 teste / 12 verificações sem falhas.
- Ambiente independente e reabertura: 1 teste / 20 verificações sem falhas.
- Auditorias Swift/ponte, dependências e projeto Xcode: zero problemas.
- Xcode aprovou `8d731c0`, `6520cb8`, `66baf79`, `dfee2bb`.
- `a8efa68`, build 2104: Xcode e compilador Metal aprovados; run `35896110979`.
- IPA validado: ZIP íntegro, executável Mach-O ARM64, bundle `com.aurea.aurea`, iOS 16.3+, build 2104.

## Validação física necessária

Criar projeto; adicionar vídeo/foto/som/forma/texto/3D; editar e reproduzir;
aparar/dividir/reordenar; keyframes/curvas; desfazer/refazer; salvar e reabrir;
exportar e conferir vídeo e áudio. Usar iPhone real, sem abrir emulador.

Não há aprovação do usuário nem teste físico concluído desta revisão.
Ícones, seletores e diálogos nativos têm diferenças visuais; não há comprovação
de equivalência pixel a pixel ou teste de todas as combinações de painéis.

## Instalação desta revisão

O iPhone foi reconectado e a consulta restrita ao Aurea confirmou o build antigo
2103. O build 2104 foi baixado e verificado. O controle do Sideloadly foi
interrompido pelo Esc físico antes da instalação; nenhum teste físico desta
revisão foi concluído. IPA disponível em `entrega/aurea-beta2-2104-ios-arm64.ipa`.

## Correção do instalador — build 2105

O usuário confirmou falha no Sideloadly com IXErrorDomain Code 13 / Missing
bundle ID no build 2104. O Info.plist tinha o identificador, mas o bundle
continha uma pasta raiz `Resources`, estrutura proibida em apps iOS.
A referência do Xcode agora copia apenas `presets/` para a raiz e o carregador
usa Bundle.url(forResource:withExtension:subdirectory:).

A CI passa a abrir o app compilado via Foundation para verificar o identificador,
o executável e os quatro catálogos, além de validar o IPA empacotado. O novo
validador rejeitou o 2104 com a mensagem específica de pasta proibida.
Compilação 2105: commit `a5c33ea`, run `35897023532`, aprovado no Xcode, Foundation, compilador Metal e validação do IPA.
Referência: https://developer.apple.com/go/?id=bundle-structure

IPA corrigido: `entrega/aurea-beta2-2105-ios-arm64.ipa`. Instalacao manual pelo usuario, conforme solicitado; nao houve teste no aparelho deste build.
