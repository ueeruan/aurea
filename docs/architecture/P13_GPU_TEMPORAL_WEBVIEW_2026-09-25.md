# Build 2112 — GPU, estabilização temporal, fontes e exportação

## Mudanças implementadas

- Inferência ncnn Vulkan habilitada no Android e host. Apple continua CPU;
  não foi adicionado MoltenVK/CoreML/Metal neural. `AUREA_AI_VULKAN=OFF`
  conserva a compilação CPU. Auto evita dispositivos Vulkan de software.
- Carregamento/falha de tile Vulkan retorna à CPU quando possível, antes de
  entregar o tile. Logs identificam o backend efetivo.
- O passe bicúbico final do modelo 2× falhou inicialmente (saída preta na
  RTX 3050). Somente esse passe foi direcionado ao operador CPU original;
  as convoluções e o pixel shuffle continuam na GPU. Pesos inalterados.
- Estabilização temporal de luminância em regiões estáticas. Histórico
  descartado para movimento detectado, cortes, fades, dimensões ou sequência
  alteradas. Orçamento adicional de 64 MiB; sem memória, exporta sem histórico.
  Não é reconstrução neural temporal nem modelo fotográfico. Não elimina
  garantidamente todo flicker ou artefato em conteúdo real.
- UI Android deixa de invalidar o estado geométrico do preview por mudanças
  de FPS que não eram exibidas. iOS não consulta novamente a pilha de efeitos
  quando apenas o playhead muda; os valores animados continuam atualizados.
- Android usa uma Roboto embarcada, idêntica à do iOS, instalada antes do
  motor iniciar. Fallback deixa de depender de caminhos de fontes da Samsung.
- Removido `AdWebViewCrashGuard`: reentrar em `Looper.loop()` após exceção
  fatal JNI não é recuperação confiável. LevelPlay não inicializa no provedor
  ausente ou no WebView **124.0.6367.219**, versão da reprodução conhecida.
  Outras versões mantêm o fluxo habitual. Não se afirma que Chromium foi
  corrigido; anúncios, inclusive recompensados, ficam indisponíveis nessa versão.
- Exportação publica o resultado salvo antes do anúncio. Callback atrasado
  do SDK não pode sobrescrever o estado de uma exportação posterior.

## Verificação

- Host: **737 testes, 4.558.590 verificações, zero falhas** em
  `build/gpu-temporal-full-tests.log`.
- IA: **4 testes, 68 verificações, zero falhas**, inclusive cancelamento e
  mosaico de seis tiles com dimensões ímpares. RTX 3050, FP32: diferença máxima
  de **1 nível RGB8** contra CPU; média 0,000105 em 2× e 0,000059 em 4×.
  Fixture 65×49: CPU 218,8/210,5 ms, Vulkan 23,0/14,6 ms (2×/4×).
  São medidas de fixture no host, não FPS sustentado em telefone.
- Android: **110 testes JVM, zero falhas**; debug x86_64 e release ARM64
  compilados. Logs `build/gpu-android-final.log`, `build/gpu-arm64-release.log`.
- iOS: API Swift, tipos e projeto Xcode verificados estaticamente. Não houve
  novo build/teste nativo iOS desta rodada; o resultado 8/8 anterior é do 2111.
- Emulador API 35, WebView **124.0.6367.219**: log confirmou LevelPlay
  desativado antes do SDK iniciar. Mesmo PID **11547** antes e depois da
  exportação; tela **Vídeo pronto**, arquivo salvo em Filmes/Aurea.
- Exportação de teste com texto: **1280×720, 30 fps, 300 quadros, 10 segundos**.
  FFmpeg decodificou todos os quadros sem erros. Inspeção do frame 0,5 s:
  “Texto” branco legível, sem quadrados. Fonte no log:
  `/data/user/0/com.aurea.aurea.debug/cache/motor/Roboto-Regular.ttf`.
- Evidências: `build/export-2112-smoke.mp4`, `build/export-2112-frame.png`,
  `build/export-2112-decode.log`, `build/gpu-emulator-startup.log`,
  `build/gpu-emulator-text.log`, `build/gpu-emulator-export.log`,
  `build/aurea-export-done-2112.xml`.

## Limites restantes

Somente o emulador estava conectado. Samsung físico e iPhone físico não foram
testados; não há garantia de fluidez/temperatura para toda mídia e efeito.
O teste Android acima não usa upscale; inferência GPU/temporal e integração de
upscale na exportação foram exercitadas no host. iOS neural permanece CPU.
O modelo continua animevideov3. O bloqueio da versão conhecida do WebView é
mitigação localizada, não prova sobre anúncios de todas as versões/provedores.

Referências de implementação: código ncnn fixado por hash em
`engine/cmake/AureaNcnn.cmake`, [build oficial ncnn](https://github.com/Tencent/ncnn/wiki/how-to-build)
e [tratamento Android de término do renderer](https://developer.android.com/develop/ui/views/layout/webapps/handle-termination).
A última API se aplica a WebViews controlados pelo app; o WebView Unity pertence
ao SDK, e a falha reproduzida é uma exceção JNI na main thread.
