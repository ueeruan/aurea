# Bugs da comunidade — AUREA 0.0.8 / 2149

Registro histórico do lote 2149. Estado posterior, keyframe no fim exato,
Curves e CI iOS: [AUREA 0.0.9 / 2150](aurea-0.0.9-2150.md).

## Implementação

As regras de edição e recuperação da exportação ficam no motor C++ compartilhado.
Android/Kotlin e iOS/Swift foram atualizados no mesmo trabalho. Referências de
outros aplicativos especificam comportamento; nenhum código, shader ou asset
extraído foi incorporado.

| Relato | Alteração ou evidência obtida | Limite da conclusão |
|---|---|---|
| Motion Tile com Squeeze | Reproduzidos buracos pretos em 12 combinações de força/eixo. Squeeze informa a margem exigida pelo seu mapeamento inverso ao grafo; Motion Tile gera a região necessária. A saída é recortada à região visível e à margem exigida pelos efeitos seguintes. | Correção medida em GPU compartilhada e Android; requer validar os projetos específicos dos usuários. |
| Tiles com Oscilar | Teste GPU de parede preenchida com amplitude, fase e rotação passou. Cadeia com imagem e espelhamento também passou. | É validação da implementação atual, não uma nova correção do caso enviado pelo fórum. |
| Keyframes depois de cortar / flutuantes | O motor marca keys fora de `[início, fim)` como ocultos na timeline. Ambas as interfaces filtram esses keys nas linhas e na navegação. Os dados continuam disponíveis para interpolação, curvas e desfazer. ABI KeyframeRow: 28 bytes, sinalizador no offset 24, leitores Android/iOS atualizados. | Validação nativa Android inclui corte, ocultação e desfazer. |
| Pressionar e segurar não seleciona várias camadas | Soltar um toque longo imóvel no corpo da camada entra em seleção múltipla; toques longos seguintes alternam a camada. Android e iOS mantêm arraste/reordenação e outros gestos. | Execução Android confirmada; iOS ainda sem execução nativa. |
| Arraste de keys e setas | Alvo ampliado, arraste/seleção conjunta e desfazer passaram nos testes de gestos. Navegação agora ignora keys ocultos pelo corte. | Não foi reproduzido todo caso de setas inativas do fórum; a regra existente de navegação entre marcadores não foi alterada. |
| Shapes voltam à posição/escala | Posição e escala X/Y sobreviveram ao seek enfileirado e ao retorno ao início em teste nativo. Testes de controles com keyframes também passaram. | Os cenários testados passaram na implementação atual; isso não prova que todos os shapes dos usuários estão corrigidos. |
| Câmera 3D / null | Imagem e vídeo com 3D ativo respondem à câmera vinculada a null com inclinação zero. Camadas 2D mantêm comportamento independente. Undo e persistência testados. | Não foi necessário girar X no caso testado. Sem reprodução do projeto específico do fórum. |
| Exportação / aviso H.264 / memória | Falha ao abrir o encoder publica progresso e causa novos, sem reaproveitar estado de uma tentativa anterior. O motor classifica se cabe tentar novamente. Android e iOS obedecem essa política. Android recria o codec entre tentativas de formato e tenta software quando a criação por MIME falha. Mantém resolução/FPS solicitados. Mensagem de formato agora cita a combinação resolução/FPS/formato em sete idiomas. | Não há medição de melhoria universal ou validação no Moto G84/iOS; nem toda falha de memória permite recuperação. |
| Modelo invisível / textura importada | Testes compartilhados de resolução de texturas escolhidas para OBJ/FBX passaram. Teste nativo de importação GLB/GLTF e luz direcional verifica pixels reais. | Falta o modelo e o formato afetado para reproduzir o relato de textura. |
| Reflexos com gelatina | Teste GPU de reflexão com normal map espelhado coincide com a referência preparada. | Não reproduzido; não houve nova correção de material/reflexo neste lote. |
| Tracking inconsistente | Solver recuperou caminho/FOV conhecido; cenário de tripé também passou. | Não reproduzido com o vídeo do usuário; não houve nova correção do solver neste lote. |
| Preview piscando / erro ao abrir cena 3D | Reprodução e seeks preservaram pixels visíveis no Android. Texto 3D com transformação/preset foi exportado e reaberto em teste compartilhado. | Não houve reprodução da cena que falha na 0.0.8. Precisamos do arquivo ou mensagem de erro desse caso. |
| Ícone de motion blur parece relógio | Novo vetor próprio: círculo com três rastros horizontais, com geometria equivalente no Android e iOS. Aplicado ao cabeçalho e menus de desfoque da camada, vídeo e composição. Cores, nomes acessíveis e alvos de toque mantidos. | iOS implementado, mas sem compilação nativa neste host. |

## Compilação

- Motor Windows Release compilado, incluindo o novo ABI e a margem de Squeeze.
- JVM: 390 testes em 73 suítes, zero falhas/erros.
- Android 2149: compilação Release e UITest registrada nos logs abaixo; o APK
  final deve conter os dois ABIs ARM e o novo ícone, verificados no manifesto
  de entrega. Uma compilação Release x86_64 intermediária serve somente ao
  emulador e não é o APK de distribuição.
- iOS 2149: C++, ponte ObjC++ e Swift atualizados. Auditorias estáticas de API,
  tipos, textos e layout passaram. **Essas auditorias não comprovam compilação
  nem execução nativa.** Windows não dispõe de Xcode.
- A revisão automática recusou o envio ao repositório público `ueeruan/aurea`
  por faltar autorização específica para publicar esse código no destino. A
  pergunta de autorização permanece pendente. Não há IPA atualizado deste lote.

## Testes compartilhados

Os 16 casos relacionados abaixo somam 421 verificações sem falhas nos logs
finais. Não representam a suíte inteira do motor.

- ContentBounds: quatro casos, 67 verificações.
- ExportStartup: dois casos, 39 verificações.
- Diagnóstico preservado após destruição do sink: 14 verificações.
- Imagem/vídeo 3D, câmera e null: 112 verificações.
- Motion Tile + Squeeze: 36 verificações; antes da correção falhavam as 12
  combinações, depois passaram todas.
- Motion Tile alimentando efeitos seguintes, inclusive Oscilar: 42 verificações.
- Motion Tile com espelhamento e imagem: 35 verificações.
- Solver de câmera: sete verificações; tripé: 25.
- Texturas OBJ/FBX escolhidas pelo usuário: 21 verificações.
- Texto 3D exportado e reaberto: 12 verificações.
- Reflexão com normal map espelhado: 11 verificações.

Os testes que salvam arquivos usam TEMP dentro do workspace. Uma execução
inicial falhou por negar escrita no TEMP externo; a repetição com o diretório
correto passou. Uma primeira compilação incremental reteve o objeto antigo de
Engine.cpp; o rebuild explícito e os testes finais passaram. Ambos os logs
iniciais foram preservados.

## Validação nativa Android

Emulador Aurea_API35, Android 15/API35, GPU de software. Não é validação no
Moto G84 nem no iOS.

- Vinte testes de timeline e cancelamento/repetição de exportação passaram,
  incluindo toque longo para multisseleção, corte/keys/desfazer e estabilidade
  de posição/escala de shape após seek.
- Cinco casos de câmera/transformações/Motion Tile passaram.
- Motion Tile + Squeeze passou em pixels reais nas 12 combinações.
- Tint e cadeias de efeitos em imagem, texto e texto extrudado passaram em
  execução isolada (30,87 segundos).
- Preview de vídeo manteve pixels visíveis durante reprodução e seeks.
- Matriz de qualidade passou em execução isolada: H.264 720p/30 (4 quadros),
  720p/240 (32 quadros), 1080p/60 (8 quadros) e HEVC 1080p/30 (4 quadros).
  MediaExtractor verificou tamanho, MIME, contagem e PTS; frames decodificados
  foram inspecionados por pixels. O limite anunciado do emulador é 1080p;
  resoluções superiores foram recusadas explicitamente.
- O teste de importação no fim da composição foi ajustado para criar conteúdo
  até esse ponto: em timeline vazia o novo limite de navegação é zero.
- Texto extrudado exportado como H.264 360p/30: quatro quadros e glifos visíveis
  no frame decodificado; passou no build 2149.
- Importação GLB no fim, cena GLTF selecionada e intensidade de luz direcional
  passaram em execução isolada (11,56 segundos) no build 2149. Na execução
  concorrente à compilação, o preview atingiu o timeout de dois segundos de
  um batch Vulkan no emulador; a captura retornou zero. Isso é uma limitação
  observada sob carga, sem correção adicional de timeout neste lote.

Uma execução combinada não serve como resultado integral: a matriz de qualidade
teve timeout transitório na criação do projeto; o caso de importação usava a
fixture incompatível com o novo limite; o runner final foi interrompido pelo
início prematuro de outra instrumentação. Os casos afetados são repetidos
isoladamente/sequencialmente. Não há evidência de crash do aplicativo nesse
último encerramento do runner.

Logs brutos, prints, manifesto e resultado dos últimos testes:
`build/reference/keyflow-ui-trial-20261009/`.

## Referências de comportamento

- [Camera Objects Guide — Alight Motion](https://support.alightmotion.com/hc/en-us/articles/10536993608977-Camera-Objects-Guide): câmera e vínculo a null.
- [VideoCapabilities — Android](https://developer.android.com/reference/android/media/MediaCodecInfo.VideoCapabilities): suporte depende da combinação de tamanho e frame rate.
