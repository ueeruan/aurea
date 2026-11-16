# Exportação Alight Motion e aplicação no AUREA

Registro de 15/11/2026. A análise REA é estática e usa Alight Motion 5.0.280, pacote `com.alightcreative.motion`, build 1028519.
O alvo é `build/reference/alight-motion-5.0.280/reference.apk`, SHA-256 `4caeb6dea44e8f123b88833aef7b28dd54f4a48fb0cc6a9e5085ba4c1bbf4159`, igual ao alvo da análise de composição em `docs/parity/composition-alight-20261115.md`.
Os resultados brutos permanecem em `build/reference/am-export-20261115/`. Nos resultados de métodos, o texto está em `data.normalized_result.source.text`, acompanhado de `source.status`, `body_status`, representação e limitações.

## Observado estaticamente

| Etapa | Evidência REA e arquivo bruto | Observação e cobertura |
|---|---|---|
| Parâmetros | `ev_cd77aa416af095c1f809cb967b14c6966620ab6fc2c93f934f43504c272c0be0`, `params-video.json` | `SceneExporterKt.exportParamsVideo` constrói parâmetros MPEG-4, seleciona MIME de vídeo e AAC e ajusta uma dimensão para par. A associação completa dos argumentos depende dos getters de `ExportParams`. |
| Configuração | `ev_bbc0526fab19287524d753e20d129bb2d94e1fcade86cd9d4bf6da6f40f74c7c`, `export-config.json` | `ud.Ok.H` transforma getters de dimensões, taxa, bitrate e áudio em descritores para o exportador. |
| Capacidade | `ev_880737666553b2cc5d66d6379c0e11caa2f23f9f34c7d0b3fe6918c560c15420`, `encoder-supported.json` | `SceneExporterKt.isEncoderSupported` consulta encoder por formato com MIME e dimensões. Este método isolado cobre essa consulta. |
| Orquestração | `ev_50eaff581fc5630ee40a86e2da7e84f4a2d9f5991d0e7d2e701dfc70b5d228f9`, `coroutine-export.json` | `ud.mpd.Rw` contém suspensão de coroutine, criação do exportador e tratamento de resultado/cancelamento. JADX apresenta avisos de duplicação e inferência de tipos. |
| Render | `ev_af0535106641e800ec3cb8093d869118d90a4402cd97dc685a9b0cd2c6e6f690`, `render-frame.json` | `ud.Ok.pY` chama o render GPU com `RenderMode.EXPORT` e parâmetros da cena/exportação. |
| Entrada Surface | `ev_d3789776c49430d2fbfce0f791cfdc52595be3cecd388baa9ae3130e503b31e2`, `surface-create-inner-dot.json` | Smali de `O0d.sK.SfT.mY0.invokeSuspend` contém registro de callback/Handler, criação da input Surface, passagem dos objetos ao worker e operações de liberação. A representação disponível é smali. |
| Callback e worker | `ev_cf40811fe559c97d0642d6e3c0f34b310c623566f13afe0f1ad7ed23cef80606`, `codec-output-callback.json`; `ev_f91c020430c294ee59a8302d3ed73bf513833b27f9618c2fb55256ad9c26485f`, `codec-format-callback.json`; `ev_258834e6aa858ece9cfd6bc566c2b8f145d1ed126eb52cdd97a7bdddda755829`, `codec-stream.json` | Os callbacks publicam eventos de buffer e formato. O worker inicia uma coroutine auxiliar e coleta o fluxo de saída. Capacidades e limites das filas dependem da implementação de seus canais. |
| Timestamp EGL | `ev_273bafbdb62e971d24e977fe3e5d6f9041aa8ba14acf833fad597db3d6e023f7`, `surface-present.json` | `zu.fs.u` chama `EGLExt.eglPresentationTimeANDROID` com Surface e timestamp e trata falha EGL. |
| Multiplexação | `ev_8bddec77af6565f4fdb5dd02f711c8c9f5e3faf71692bbcb08d1d560d4f38b42`, `mux-packet.json` | `O0d.rs.Hfr` referencia adição de trilhas, início do muxer e escrita de amostras. Os avisos de blocos duplicados impedem confirmar condições e ordenação do controle de fluxo sem conferir o smali correspondente. |

## Inferido

O conjunto sugere exportação GPU alimentando encoder por Surface, com processamento assíncrono de eventos e multiplexação separada. A divisão de responsabilidades orienta a implementação própria do AUREA. Custos internos, número de cópias, capacidade das filas e ganho de desempenho dependem de outras evidências.

## Não comprovado e limitações

- `surface-feed.json`, REA `ev_ca5bb7abb6d4a4fbb36fb16752e3690900641edc8a87b3232b96c8b716a0e37a`, informa `body_status=not_available` para `O0d.sK.Hfr`. O texto substitutivo do provedor mantém o corpo sem cobertura.
- A consulta com `O0d.sK$SfT$mY0` foi rejeitada como classe inexistente em `surface-create-inner.json`. A consulta pelo nome com pontos encontrou o método e recuperou smali após falha da decompilação Java. O erro inicial e o resultado recuperado permanecem registrados.
- O APK de referência não foi executado nesta análise. Velocidade, limites práticos, qualidade visual, robustez de cancelamento e equivalência de exportação permanecem sem comprovação experimental.

## Contrato da plataforma

O contrato Android exige criação da input Surface após configuração e antes de iniciar o codec, liberação dessa Surface pelo aplicativo e devolução dos buffers de saída após processamento. O modo assíncrono recebe buffers por callbacks e exige configuração compatível com esse modelo. Essas regras fundamentam propriedade e lifecycle no código próprio. [MediaCodec — Android Developers](https://developer.android.com/reference/android/media/MediaCodec)

## Implementação e validação próprias

O AUREA mantém motor C++ compartilhado, bridge Android e bridge iOS. Código próprio, regressões e benchmarks devem registrar revisão, configuração, dispositivo e resultados separadamente da evidência REA. A instrumentação distingue esperas de mídia, gravação CPU e fences. Compilação e execução iOS dependem de Xcode, com desempenho mobile sujeito a medição em aparelho físico.
