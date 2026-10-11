# AUREA 0.0.9 — build 2150

Registro de 09/10/2026. Este documento substitui o estado de pausa das pendências
2150 e complementa a validação anterior da comunidade. Implementação, compilação
e execução nativa são registradas separadamente.

## Implementação

- **Keyframe no fim da camada:** o motor mantém visível o ponto no fim exato
  do clipe. Android e iOS usam a mesma informação compartilhada; pontos fora
  do corte ficam ocultos sem apagar a animação necessária para interpolação e desfazer.
- **Curves editável:** gráfico com pontos arrastáveis, curva RGB e canais R/G/B,
  valores de entrada/saída, exclusão de pontos internos e redefinição do canal.
  O motor define limites, interpolação, histórico e persistência. Android/Kotlin
  e iOS/Swift recebem o editor e os textos acessíveis em sete idiomas.
- **Recuperação da GPU:** aguarda a conclusão do trabalho pendente antes de
  reconstruir recursos; preserva projeto e animação e permite tentar novamente
  após falha de reinicialização. Exportações de vídeo e imagem têm posse exclusiva
  do renderer, incluindo a fase de preparação.
- **iOS/Metal:** buffers de upload voltam ao tamanho de base após uso leve
  sustentado, sem liberar recursos ainda em uso pela GPU. Slots de frames que
  terminaram com erro são aposentados corretamente para permitir novas tentativas.
- **Desfoque 3D:** preservada a política compartilhada de poses e exposição por
  camada, com orçamento de memória e liberação antecipada de alvos intermediários.
  A otimização de buffers Metal também cobre essa renderização no iOS.
- **Interface oficial:** mantidas a timeline compacta, ferramentas contextuais
  de camada e seleção múltipla, ações 3D/desfoque no cabeçalho, novo ícone próprio
  de desfoque e botão de entrada na precomp. Não há código, shader ou asset extraído
  de outro aplicativo no produto.
- **Home e tracker:** home com organização mais compacta dos projetos; rastreio
  2D e câmera 3D em modos separados, etapas de análise/posicionamento e controles
  avançados recolhidos. São alterações nas duas interfaces; não representam uma
  nova correção do solver de tracking neste lote.
- **Catálogo de efeitos:** incluídas implementações próprias de WipeFlux,
  WipePlasma, S_Distort, Pixel Encoder, BCC Glitch, PixDither, Displace e Starglow,
  com busca e parâmetros nas duas plataformas. Compilar esses efeitos não
  comprova identidade visual com plugins externos em todas as configurações.
- **Exportação:** mantidas as correções de diagnóstico, cancelamento, repetição,
  codec Android e filas limitadas do VideoToolbox. Não há redução silenciosa de
  resolução ou FPS para fazer a exportação passar.
- Notas de atualização e versão nas duas plataformas: **0.0.9 / 2150**.

## Compilação e arquivos

- C++ Windows Release compilado.
- Android Release: APK universal ARM32/ARM64, APK ARM64 e APK ARM32 compilados.
  Assinatura, ZIP, manifesto 2150/0.0.9 e ABIs verificados individualmente.
  Hashes e certificados constam em `output/0.0.9-build2150/android-verification.json`.
- Android UITest e testes instrumentados compilados com ABI x86_64.
- Auditoria do produto: 758 arquivos de código/catálogos, JNI/shaders e os três
  APKs sem ocorrência do nome de aplicativo proibido pelo usuário.
- iOS: publicação de C++ e iOS autorizada explicitamente na branch pública
  `codex/beta-2147-build`. O payload efetivamente publicado tem 39 arquivos;
  manifesto preservado em `build/reference/stability-2150-20261009/ios-publication-payload.json`.
- CI iOS: https://github.com/ueeruan/aurea/actions/runs/38000966739.
  Job `build-ipa` concluído com sucesso: Xcode Release/iphoneos, validação do
  bundle com Foundation, **166/166 shaders** no compilador/linker Apple Metal,
  empacotamento e verificação do IPA. O arquivo foi baixado e verificado localmente:
  versão 0.0.9/2150, ARM64, integridade ZIP, assets e ausência do nome proibido.
  Hash e metadados em `output/0.0.9-build2150/ios-verification.json`.
  Logs de compilação em `build/reference/stability-2150-20261009/ios-build-diagnostics/`.

## Verificações executadas neste lote

| Verificação compartilhada | Casos | Verificações | Falhas |
|---|---:|---:|---:|
| Curves, persistência/histórico e texto 3D relacionado | 2 | 71 | 0 |
| Curves em GPU real | 1 | 3 | 0 |
| Recuperação e repetição após falha de inicialização | 1 | 17 | 0 |
| Falhas de gravação Vulkan e recursos pendentes | 1 | 132 | 0 |
| Política de redução de buffers de upload | 1 | 37 | 0 |
| Limites de timeline, corte e keyframe no fim | 5 | 85 | 0 |
| Preparação/repetição de exportação | 2 | 39 | 0 |
| Exposição de desfoque e câmera por camada | 1 | 13 | 0 |
| Memória em composição densa com desfoque | 1 | 682 | 0 |

Total desta seleção: **15 casos, 1.079 verificações, zero falhas**. Não representa
a suíte completa do motor. Logs individuais em `build/reference/stability-2150-20261009/`.

### Android nativo

- Curves: teste instrumentado passou em 18,549 s. Edita e arrasta pontos pela UI,
  desfaz/refaz, preserva RGB ao redefinir R, salva/reabre e exporta quatro frames
  H.264. Um decoder independente compara os pixels exportados com a captura da prévia.
- Seis testes relacionados passaram em 39,434 s: keyframe no fim com arraste e
  desfazer; cancelamento/repetição de exportação; suspensão do worker de preview
  durante exportação; imagem e vídeo 3D com câmera/null e inclinação zero;
  exportação H.264 de texto extrudado.
- Teste adicional de posse do renderer em sequência PNG passou em 6,378 s:
  bloqueia exportação concorrente, cancela a sequência, exporta PNG legível na
  repetição e confirma que a captura da prévia volta a funcionar.
- Execução em emulador API 35 com GPU Vulkan/SwiftShader. Não equivale a validação
  no Moto G84 ou em todos os modelos de celular.

### iOS nativo

O teste nativo de conclusão dos frames Metal passou no Mac do CI. A compilação
do aplicativo para o simulador iOS e a compilação Release para iPhone passaram.
Seis cenas foram capturadas com sucesso: home, ferramentas da camada, efeitos,
texto 3D, composição 3D e interior de precomp. As capturas do motor apresentaram
conteúdo visível e nenhum erro de renderização.

O probe de exportação falhou antes de abrir o encoder: tentava medir o viewport
enquanto o app ainda estava na Home, sem superfície Metal. A preparação DEBUG
foi corrigida para criar o projeto pelo modelo e abrir o editor antes do probe,
preservando suas verificações de pixels, tamanho de preview, exportação e decoder.
Correção de teste publicada em `6f11c2f9`; não altera o código compilado no IPA Release.
O primeiro reteste isolado expirou em 210 s sem readiness, logs do aplicativo,
relatório intermediário de exportação ou crash report. O comando de encerramento
do aplicativo no simulador também expirou. Isso não identifica uma falha do
encoder nem comprova que a exportação funciona; a validação continua pendente.
Evidência em `build/reference/stability-2150-20261009/ios-export-retest/`.
Somente o job remoto que falhou foi reexecutado, usando o cache de compilação:
https://github.com/ueeruan/aurea/actions/runs/38002899970.
Não foi conectado um iPhone físico neste lote.
O IPA deste fluxo é **sem assinatura**, destinado a assinatura posterior/sideload.

## Limites e relatos ainda sem reprodução específica

As correções e testes acima reduzem falhas conhecidas; não comprovam ausência de
todo crash em qualquer aparelho. Reflexos com aspecto de gelatina, tracking
intermitente e a cena 3D que falha ao abrir ainda precisam dos projetos/modelos
ou vídeos específicos afetados. Os testes disponíveis desses componentes passaram,
mas não justificam marcar esses relatos como resolvidos universalmente.

O lote anterior contém a reprodução e correção de Motion Tile com Squeeze,
testes de Tiles com Oscilar, importação/textura/luz, shapes, seleção múltipla e
exportação com vários formatos/FPS. Evidência e limites constam em
`docs/validacao/bugs-comunidade-2026-10-09.md`.
