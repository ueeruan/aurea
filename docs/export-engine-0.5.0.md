# Motor de exportação universal (0.5.0)

## Conclusão da auditoria

O Aurea já tinha a arquitetura pedida: escada de encoders, autoteste do primeiro quadro, watchdog do encoder, retomada por GOP e validação independente. As falhas estavam na integração, então o trabalho corrigiu essas peças e manteve o RenderCore.

| Pedido | Situação |
| --- | --- |
| Android: Surface, ByteBuffer, outro encoder, software | Já existia: surface-direct (desligado), Surface, ByteBuffer, `*_mediacodec` de todos os encoders, OpenH264/Kvazaar, software do sistema |
| iOS: VideoToolbox, software | Já existia, via FFmpeg `*_videotoolbox`, depois OpenH264/Kvazaar. Não há caminho AVAssetWriter |
| Detecção e autoteste de encoder | Já existia; agora o degrau que falhou vale por 30 min, e falta de memória não condena o encoder |
| Watchdog e timeouts | Encoder (45 s) já existia. **Novo:** render com vigia próprio (150 s; 20 s depois de cancelar) |
| Render por trechos e retomada | Retomada por GOP já existia; agora respeita limite de falhas ao iniciar. Não há render por trechos de tempo |
| RAM, GPU, buffers | Já existia; a validação agora usa 2 threads nas tentativas de segurança |
| Vídeo verde, preto, sem áudio | **Novo:** conferência do arquivo final |
| Validação antes de concluir | **Corrigido:** o modo tolerante documentado nunca era ligado |
| 16 KB | `arm64-v8a` e `x86_64` com alinhamento `0x4000` nos `.so` conferidos |

## O que mudou

- **Conteúdo do arquivo final (`ExportContentCheck.hpp`):** uma amostra a cada 15 quadros, até 240, compara o quadro entregue ao encoder com o quadro decodificado do arquivo. Verde, preto ou cor trocada reprovam, em qualquer degrau, e a escada tenta o próximo.
- **Áudio:** projeto com som e arquivo sem a faixa reprova. Som que entrou com nível e saiu mudo também reprova.
- **Validação:** divergência pequena de relógio virou aviso no diagnóstico, como os apps deviam fazer desde o começo. Antes, ela jogava fora um export já pronto. Arquivo que não abre, sem quadros, com imagem errada ou sem som continua reprovado.
- **Retomada:** `resume_allowed` vale na tela e na partida. Antes, um crash sempre no mesmo GOP podia retomar para sempre.
- **Encoder condenado:** dura 30 min em vez do processo inteiro, e falta de memória não conta.
- **Sem thread disponível:** o export é recusado; antes derrubava o app.
- **Android:** o modo de taxa agora vem do encoder de hardware, não do primeiro da lista.
- **Render travado:** o render bate a cada passo do quadro. Sem batida, a sessão termina com a falha `RenderHung`, a tela pede para reabrir o app e o checkpoint é mantido. Nenhuma espera fica presa na thread travada; se ela voltar, o próximo export funciona.
- **OpenH264 com NEON:** `USE_ASM=Yes` em arm64 e armv7. O x86_64 continua sem assembly.

## FFmpegKitNext e licenças

Não foi integrado. Fontes: [documentação](https://arthenica.github.io/ffmpeg-kit-next/), [licenças](https://github.com/arthenica/ffmpeg-kit-next/wiki/Licenses-and-Notices).

- O projeto é ativo e LGPL v3 por padrão; vira GPL v3 com `--enable-gpl`. Só é distribuído como código para compilar.
- A API roda comandos do FFmpeg em sessões. O motor precisa entregar cada quadro em memória, registrar pacotes e retomar por GOP, e faz isso direto na libavcodec.
- O Aurea já compila FFmpeg 9.0.2 com OpenH264 2.6.0 e já usa esse caminho: foi o encoder do teste no emulador. O kit traria uma segunda cópia do FFmpeg sem encoder novo.

Precisa de revisão jurídica antes de publicar:

- FFmpeg LGPL está ligado estaticamente, e o projeto não tem kit de religação.
- A cobertura de patentes da Cisco vale para os binários dela; o Aurea compila o OpenH264 do código.
- Não há aviso de patentes de H.264, HEVC e AAC nas telas de licença.

Dossiê para o advogado: [docs/legal/0.5.0-licencas.md](legal/0.5.0-licencas.md). O patch antigo da pasta de licenças já saiu.

## Resultados

| Plataforma | Implementação | Compilação | Execução |
| --- | --- | --- | --- |
| Motor (host) | Feita | Aprovada | Grupo `Export`: 255 testes, 1 falha intermitente. Outros grupos abaixo |
| Android x86_64 | Feita | `assembleDebug` aprovado | Emulador API 35: export real de 22 s |
| Android arm64 e armv7 | Feita | `assembleDebug` aprovado nas duas | Não executado (sem aparelho ARM) |
| iOS | Mudanças do motor compartilhado | Não compilado (depende de Mac) | Não executado |

Testes específicos, 0 falhas: `ExportContent` 7, `FFmpegExport` 14, `ExportAttempts` 17, `ExportRecovery` 18, `ExportSinkStartup` 21, `ExportPacketLog` 5, `ExportWatchdog` 15, `Checkpoint` 15, `ProjectPackage` 9, `OfflineAudio` 10.

`ExportWatchdog` passou a 16 testes com o de render travado, 0 falhas. Depois da última alteração, só uma asserção de teste mudou, e esse binário final não rodou: o Windows o bloqueou por política (Device Guard).

16 KB: no APK arm64, as 7 bibliotecas têm alinhamento `0x4000` e estão alinhadas no zip. No armv7, 4 bibliotecas de terceiros têm `0x1000`, o que não afeta aparelhos de 16 KB, que são 64 bits.

A falha intermitente é `ExportWatchdog.inl:140`: apareceu uma vez rodando o grupo inteiro e não voltou em 4 repetições isoladas.

Export real no emulador (`Recriado Instagram`, 1404×792, 25 qps): encoder `libopenh264`, 552 de 552 quadros, imagem conferida em 37 de 37 amostras. Conferido à parte com ffprobe: H.264 e AAC, 22,08 s nas duas faixas, nível médio -16 dB.

## Não testado e limitações

- **MediaTek, Qualcomm, Exynos e baixa RAM: não testados.** Não havia aparelho físico conectado. O emulador não tem encoder H.264 de hardware, então os degraus MediaCodec não rodaram.
- Enquanto a thread do render estiver presa no driver, uma nova exportação é recusada: só reabrir o app solta a GPU.
- Um defeito só entre as amostras (15 quadros ou mais, em vídeo longo) pode não ser visto.
- Encoder com surface direta não tem NV12 para amostrar; esse degrau segue desligado.
- O ganho de velocidade do NEON não foi medido: não há aparelho ARM aqui. Os símbolos NEON estão nas bibliotecas arm64 e armv7.
- O caminho Legacy não limita a taxa pelo máximo do encoder.
- `EveryCatalogEffect…` segue acusando `aurea.generate.raster_extrude` como inerte em camada 2D, sem relação com a exportação.
