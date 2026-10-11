# Editor oficial — AUREA 0.0.8, build 2149

Estado: usuário autorizou em 09/10/2026 promover a UI de timeline e seleção para
as versões oficiais de Android e iOS. `EditorTimelineRefresh.enabled` agora é
true em ambas as interfaces, inclusive Release. O app `uitest` permanece uma
instalação separada apenas para instrumentação e projetos descartáveis.

O build 2149 inclui o lote posterior de correções de keys/cortes, toque longo,
Squeeze/Motion Tile e recuperação do encoder, além do novo ícone próprio de
motion blur com rastros. Implementação, testes adicionais e pendências estão
registrados em [Bugs da comunidade](bugs-comunidade-2026-10-09.md).

## Implementação

- Timeline inteira visível ao selecionar uma camada; linhas compactas, olho à
  esquerda, expansão de trilhas à direita, contorno branco e alças externas.
- Cabeçote e relógio usam a cor do AUREA; navegação limitada entre zero e o fim
  da última camada da composição corrente. Camadas ocultas contam; os intervalos
  vazios entre camadas continuam navegáveis. Política definida no motor C++.
- Cabeçalho da camada com 3D, motion blur, vínculo e opções. O botão 3D pode
  ativar/desativar a flag sem apagar transformações ou keyframes; câmeras,
  luzes e modelos 3D mantêm sua natureza tridimensional.
- Doca com cortes/extensões contextuais, exclusão e fichas do tipo da camada.
  Máscara tem ícone e rótulo próprios. Fontes grandes ampliam a geometria.
- Pré-comp/grupo mostra **Entrar no grupo** na primeira ficha da doca; abre a
  composição filha. Voltar restaura a composição pai.
- Seleção múltipla mantém cortes, agrupar, copiar, duplicar e alinhamentos,
  com as linhas riscadas da referência removidas.
- Keyframes permanecem no motor do AUREA: seleção, arraste conjunto, duplicação,
  exclusão e desfazer. Nenhum código/shader/asset extraído foi incorporado.

## Exportação e memória — motor compartilhado

O início da exportação passa a adquirir a exclusividade do renderer antes de
abrir o encoder. Aguarda o trabalho submetido, libera caches de render e o pool
de texturas temporárias reutilizáveis, e devolve as alocações adiadas ao driver.
Uma falha no início devolve a exclusividade ao preview. Mantém resolução, FPS e
qualidade pedidos; não faz redução silenciosa para caber na memória.

Foi reproduzida no emulador uma falha adicional: cancelar e exportar novamente
retornava Timeout (26) porque o fence do último quadro ainda estava pendente.
O início de vídeo/imagem agora aguarda esse fence por até um segundo. As outras
operações seguem verificando sem bloquear; uma GPU que não conclui continua
recusando novas alocações, sem reutilizar recursos ocupados.

No teste nativo de cancelamento/repetição, a limpeza liberou 19 texturas,
5.111.808 bytes (~4,9 MiB). Esse é um cenário medido, não uma estimativa universal.
Não há medição que sustente ganho de 1000×, nem reprodução do projeto específico
que produziu o relato de erro de memória do usuário.

## Compilação

- Motor Windows Release: compilação concluída. Doze testes relacionados,
  157 verificações, zero falhas. Logs `official-*-tests.log` e regressões nomeadas
  em `build/reference/keyflow-ui-trial-20261009/`.
- Android: `assembleUiTest` e `assembleUiTestAndroidTest` x86_64 concluídos;
  Release 32/64 bits build 2149 concluído, APK em
  `output/ui-oficial-2149/Aurea-0.0.8-build2149-Android.apk`. Assinatura v2/v3
  verificada; utiliza a chave de debug legada da distribuição local, conforme
  configuração existente. Não é um AAB pronto para Google Play.
- iOS: implementação Swift + ponte ObjC++ + C++ atualizadas, número 2149.
  Auditoria de API: zero problemas. Auditoria de layout passou sem swiftc.
  Compilação/execução nativas desta alteração ainda pendentes. A revisão
  automática recusou publicar o código no repositório público `ueeruan/aurea`
  para executar o CI, por faltar autorização específica para esse destino.
  A pergunta de autorização continua pendente. Não foi gerado IPA atualizado.

## Validação nativa

Emulador Aurea_API35, Android 15/API35. Quatro testes de edição passaram:
3D/motion blur/limite de conteúdo; abrir/voltar da pré-comp; arrastar keyframe
com alvo ampliado; selecionar/mover/apagar/duplicar vários keyframes.

O teste inicial de cinco casos registrou uma falha de exportação. Após a
correção de fence, `cancellingDuringStartupAllowsRetryAndPublishesReadableVideo`
passou e publicou um MP4 legível. Os logs da falha inicial foram preservados.

Matriz nativa de qualidade alta passou, verificando tamanho, PTS, número de
quadros e pixels com MediaExtractor/MediaMetadataRetriever:

- H.264 720p/30: quatro quadros.
- H.264 720p/240: 32 quadros (validação adicionada no build 2149).
- H.264 1080p/60: oito quadros.
- HEVC 1080p/30: quatro quadros.
- 1440p e 2160p recusados explicitamente pelo limite anunciado de 1920×1080
  do emulador; não foram validados como exports nativos nessa máquina.

O Moto G52 testado na fase anterior está desconectado; os resultados de dois
keyframe tests e os prints anteriores do protótipo são históricos. Nenhum
celular físico nem iPhone recebeu validação desta revisão oficial.

## Evidências e histórico

Baseline da fase de seleção: `selection-baseline/`; delta posterior:
`official-selection.patch`; hashes: `official-manifest.json`, todos em
`build/reference/keyflow-ui-trial-20261009/`. O workspace já tinha extensas
alterações anteriores, que foram preservadas. Os patches não representam todo
o trabalho preexistente da 0.0.8.

As referências externas foram usadas para especificar a interação, com ícones
e operações próprias do AUREA. O Keyflow 1.2.2 original foi instalado no AVD
separado com Play Store; a execução exige login no Google Play. APKs e inspeção
bruta da referência permanecem exclusivamente em `build/reference/`.

O manifesto do APK 2149 está em `output/ui-oficial-2149/manifest.json`.
Os prints `official-final-single.png`, `official-final-multi.png` e
`official-final-precomp.png` foram atualizados para o ícone com rastros.
O antigo `official-manifest.json` registra a fase 2148; o manifesto do lote
posterior é `community-2149-manifest.json`, no mesmo diretório de evidências.
