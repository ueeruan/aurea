# PreRender integrado no motor — 13/10/2026

O motor gera uma composição inteira em 540p por uma sessão offline própria,
sem encoder de vídeo e sem conversão do cache para 8 bits. O cache comprimido
RGBA16F fica antes do passe normal de saída: zoom, deslocamento e fundo
quadriculado continuam sendo desenhados pela saída do renderer.

O sentido define a ordem da geração, do início ao fim ou do fim ao início.
Os arquivos mantêm o índice lógico do frame, independentemente dessa ordem.
PreRender usa seu próprio progresso e não abre nem marca a exportação pública
como uma exportação em andamento. A composição original, mídia e keyframes
permanecem no modelo original; uma revisão nova invalida a sessão de cache.

## Defeitos comprovados na integração e correções

Três regressões próprias executaram 150 verificações antes da correção e
encontraram seis falhas. Sem superfície, o motor marcava uma textura como
pronta antes de registrar o upload; após anexar a superfície, essa identidade
impedia um novo upload. Além disso, SurfaceLost/Timeout no início de um frame
apagavam um cache de disco válido, e um erro de alocação GPU aparecia como
cancelamento.

A leitura de cache para exibição agora exige superfície anexada. Uma textura
recebe o tempo lógico apenas após o renderer retornar sucesso e registrar uma
nova submissão, confirmando que o upload entrou no frame. A perda transitória
da superfície mantém o cache de disco e permite tentar o upload novamente.
Erros reais de leitura, corrupção, alocação e render mantêm seu código no
progresso antes de retornar à composição normal.

A consulta de progresso também precisa ignorar Busy: `PreRenderCache::stats`
usa try-lock e Busy representa contenção temporária, não erro da sessão. A
reprodução isolada desse limite falhou em três verificações antes e passou nas
mesmas três depois da correção. Esses checks não se somam aos da integração.

Uma auditoria posterior encontrou 66 incrementos de revisão em 14 outros
arquivos do Engine que ainda não passavam pelo hook de cancelamento imediato.
Eles foram trocados pelo mesmo helper, conservando o incremento e os locks
originais. Todos têm o mutex do modelo; o helper de commit da câmera é chamado
somente pelos métodos sample/hold, que já possuem esse lock.

A regressão pública comprovou o efeito sem consultar progresso nem renderizar:
após adicionar um animador de camada a um projeto com cache completo, iniciar
outra sessão retornava Busy32. Após a troca, inicia com Ok0 e termina. Esse
caso passou 1/11, e a rodada dirigida após o delta passou 18 casos / 542
verificações, incluindo 9/306 dos testes existentes da câmera. Nenhuma regra
de gravação/pose da câmera nem o motor de preview foi alterado. Evidências:
`build/validation/prerender-integration-20261013/revision-hook-final-manifest.json`.

## Validação integrada

O conjunto final executou **9 casos / 2.615 verificações, sem falhas**, usando
o Engine, o renderer e os shaders Vulkan reais para composições próprias, e
um backend de teste explícito somente para injetar falhas de superfície/GPU.

- Composição de 307 frames em 540×540 nos dois sentidos; alfa nos frames
  0/153/306, índices, manifesto completo e 307 arquivos por sessão conferidos
  por leitor de CRC/zlib independente. Cada sessão usou 100.576.846 bytes.
- Revisão, posição e keyframes originais preservados durante a geração.
- Edição, undo, redo, adição de camada e troca de projeto invalidam o cache.
- Múltiplas camadas, Gaussian Blur, cubo animado e motion blur temporal:
  frames 0/2/3 em 960×540 ficaram idênticos à captura direta, máximo e média
  de diferenças iguais a zero.
- Exportação PNG comum continuou concluindo com um frame enquanto o cache
  completo permanecia válido.
- Saída do cache idêntica à composição offline equivalente em zoom 1/1,7,
  deslocamento 5/−3 e checkerboard; o cache executa só o passe de saída.
- Anexação tardia de superfície, SurfaceLost/Timeout antes de begin_frame,
  retry do upload pendente, erro GPU real e frame de disco corrompido.

No mesmo executável, as regressões relacionadas passaram separadamente:
CurrentSourceGetter 2/31, ExportGpuRetirement 6/109, OriginalModelExport 5/90 e
ExportSetupErrors 1/40. A união é 23 casos / 2.885 verificações; não inclui
reprises nem soma novamente as suítes ASTRA históricas.

Essa rodada de nove casos precede somente o delta dos hooks em outros TUs.
Renderer, shaders e o consumo do cache não mudaram nesse delta; os cinco casos
de recuperação e o getter foram repetidos no executável posterior. O novo
caso de invalidação imediata é o décimo caso próprio de integração; as
contagens de diferentes rodadas não são apresentadas como uma execução única.

Identidade, logs e contagens estão em
`build/validation/prerender-integration-20261013/integration-final-manifest.json`.

## Correções dos próprios testes e limites

O primeiro oráculo de efeitos esperava que `capture_frame_rgba(max960)`
ampliasse uma composição de 320×180, mas essa API só reduz a imagem. O teste
continuou além dessa asserção e acessou fora do buffer de referência. Foi
corrigido para uma composição nativa de 960×540 e agora verifica os tamanhos
antes da comparação. O registro da exceção anterior foi preservado.

Outro oráculo preparava uma elipse de 32 pixels com `previewFactor=540/32`,
enquanto a composição offline usa fator de raster de origem nativo e um alvo
540p. As bordas tinham anti-aliasing diferente. O oráculo foi corrigido para
comparar o mesmo contrato offline; nenhum shader nem comportamento do produto
foi alterado para acomodar essa diferença.

A rodada compartilhada do cache independente teve uma asserção exigindo
NotFound quando a API podia retornar Busy por contenção de try-lock. A fixture
foi corrigida para tentar novamente dentro de cinco segundos e depois exigir
exatamente NotFound/OutOfRange; a produção não mudou. A rodada posterior
passou **11 casos / 1.805 verificações**, e cinco regressões de recuperação e
os dois testes do getter também passaram nesse novo executável. O registro
anterior com uma falha foi preservado. Essas reprises não se somam aos nove
casos integrados. As evidências de memória limitada, cota cumulativa, leases
e proteção de órfãos ficam na documentação separada do cache.

Esta validação Windows não comprova os gestos, a ponte nativa móvel, o driver
Metal do iPhone nem aparelhos OPPO/Vivo/Realme. Kotlin/Compose compilou e os
25 testes JVM associados passaram; compilação Apple e execução dos gestos
Android/iOS precisam dos registros nativos do conjunto cumulativo.
