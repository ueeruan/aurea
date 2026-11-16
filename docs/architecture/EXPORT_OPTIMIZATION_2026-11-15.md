# Exportação de vídeo e medições

Atualização de 15/11/2026. A especificação da referência está em `docs/parity/alight-export-20261115.md`. A implementação usa o motor C++ do Aurea e suas pontes Android e iOS.

## Alterações

- `NV12Copy.hpp` oferece cópia completa para o armazenamento próprio do alimentador do encoder. Quadros contíguos com pelo menos 8 MiB usam duas cópias grandes. Quadros menores e planos com padding usam cópias por linha.
- `AndroidVideoEncoder.cpp` usa essa função dentro do lock e do ciclo de propriedade existentes. Os buffers continuam pertencendo ao alimentador até a conclusão de seu trabalho.
- `OfflineFrameRenderer` mede preparação, gravação, espera por mídia, espera por recursos e espera por fences em cada chamada, incluindo erro e cancelamento.
- `Engine.cpp` publica espera efetiva de mídia em `decodeWaitMs` e separa preparação/gravação das esperas disponíveis. O perfil informa duração de parede nas seções CPU. A disponibilidade da medição GPU aparece explicitamente no log.
- A ponte iOS publica `decodeWaitMs`, `renderMs`, `readbackMs`, `encodeMs`, `audioMs` e `pipelineDepth`, campos já existentes no motor e na integração Android.
- A composição usa a barra inferior original do Aurea. A barra superior reúne assistente/plugins no menu do projeto e 3D/desfoque/plugins no menu da camada.

Os parâmetros de resolução, FPS, áudio, cores e efeitos passam pelas APIs existentes. A otimização de cópia transfere os mesmos bytes.

## Cópia isolada de quadros

Foram realizadas duas execuções finais por ambiente, com 30 amostras por cenário, aquecimento, seis ordens balanceadas, buffers pré-alocados e comparação integral dos pixels e guardas. Todas as variantes usam os mesmos endereços de origem e destino. Um byte de origem muda fora do trecho cronometrado para verificar escrita efetiva. O benchmark chama a função usada na produção.

| Ambiente e quadro contíguo | Mediana anterior | Mediana adaptativa | Razão anterior/atual |
|---|---|---|---|
| Windows x64, 4K, execução 1 | 0,943 ms | 0,536 ms | 1,76× |
| Windows x64, 4K, execução 2 | 0,893 ms | 0,517 ms | 1,73× |
| Android API 35 emulado x86_64, 4K, execução 1 | 1,015 ms | 0,652 ms | 1,56× |
| Android API 35 emulado x86_64, 4K, execução 2 | 1,071 ms | 0,745 ms | 1,44× |

A primeira tentativa aplicou cópia grande também em 1080p e apresentou regressão no host. O ramo final de 8 MiB teve variação entre −1,1% e +3,0% nesse cenário na medição final. Os resultados do host com 4K padded tiveram regressão aproximada de 3–4%, enquanto as execuções Android correspondentes variaram entre −3,5% e +3,2% de throughput. Os CSVs preservam todos os resultados.

As medições preliminares usavam destinos diferentes para cada variante. Elas permanecem nas pastas `adaptive-*` para rastreabilidade. A tabela acima usa as repetições corrigidas `same-buffer-*`.

O host usa Ryzen 5 5500 e GeForce RTX 3050. Esses números medem cópia em memória heap reutilizada. GPU, leitura de memória mapeada do driver, upload GLES, locks e encoder têm custos adicionais. Desempenho ARM em aparelho físico permanece pendente de medição.

## Correção e exportação completa

- 37 testes C++ passaram, com 9.356 verificações. Cobrem cópia NV12, guards, medições, erros, cancelamento, scheduler, propriedade de buffers, fallback, vídeo e áudio FFmpeg.
- Nove cenas Vulkan em 1080p/4K, com efeitos, modelos 3D e motion blur, preservaram os hashes das sequências NV12 de 990 quadros. Os logs do host incluem throughput e tempos por fase. O executável anterior é identificado por SHA-256. Sua correspondência exata com o snapshot da fonte permanece pendente de verificação, conforme o registro de proveniência.
- A matriz Android exportou 11 cenas antes da alteração e duas vezes com o binário atualizado, totalizando 33 arquivos. Inclui grupos, ajuste com movimento, cor serial, optical flow, 3D, motion blur e projeto antigo. Todas as execuções passaram.
- Os 11 pares mantiveram contagem, tamanhos dos quadros, timestamps de vídeo e metadados de cor. O fluxo de áudio manteve timestamps e tamanhos dos blocos.
- Seis pares de vídeo decodificado foram idênticos. Nos cinco restantes, PSNR médio ficou entre 51,51 e 71,55 dB. A repetição da mesma matriz no mesmo binário apresentou variação equivalente, entre 51,57 e 72,73 dB. O áudio decodificado teve SNR de 80,36 dB entre versões e 80,60 dB na repetição. A causa dessas pequenas variações permanece sem isolamento.
- A matriz Android utilizou `libopenh264`. O microbenchmark Bionic testa a função de cópia efetivamente usada no caminho MediaCodec Surface.

## Plataformas e artefatos

Android compilou em x86_64 e ARM64. O APK x86_64 foi instalado e validado no emulador API 35. As assinaturas e o alinhamento ZIP 16 KiB de ambos os APKs foram verificados. A identificação REA dos pacotes confirma `com.aurea.aurea.alpha`, versão `1.0.0-alpha`, build 2220, mínimo 26 e alvo 36.

iOS recebeu a organização superior da composição e os campos de desempenho na ponte. As auditorias estáticas de layout, painéis e início assíncrono do export passaram. Compilação Swift/Objective-C++ e execução Metal/VideoToolbox dependem de Xcode. A validação física Android e iOS permanece pendente.

Os registros reproduzíveis estão em `build/validation/export-adaptive-20261115/`. Os APKs e seus hashes estão em `build/test-composition-export/manifest.json`.
