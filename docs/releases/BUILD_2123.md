# Build 2123

Pacotes gerados e conferidos. Android `com.aurea.aurea`, ARMv7 e ARM64, mínimo API 26. iOS `com.aurea.aurea`, ARM64, mínimo iOS 16.3, IPA sem assinatura para sideload.

## Artefatos

Diretório local: `output/build-2123/`. O manifesto e `SHA256SUMS.txt` acompanham os arquivos.

| Arquivo | Bytes | SHA-256 |
| --- | ---: | --- |
| Aurea-2123-arm64-v8a.apk | 23.136.895 | `ffce9e8c27b72ea126008ab5f398a93d741176a5e094a2ef313304a9d081073c` |
| Aurea-2123-armeabi-v7a.apk | 20.177.499 | `52d9486263fb5ebd329ae292415e7b9e9a710c16a65fe297a54fc6efe472e12e` |
| Aurea-2123-unsigned.ipa | 16.045.994 | `053e845a107dadad08de47166d49c6f9868173b87e7d255982a5be8fb6cee501` |

Android foi compilado em `0988a7072a1fe82aa367cb60af617fd8b8486014`. O IPA veio de `d8853f59da07af883d5ac5f123026b7b7905cb4d`, cujo único acréscimo ao commit Android explicita o nome Swift da chamada Objective-C `placeModelOnTrack`. As alterações posteriores de testes/documentação não mudam os pacotes.

Execução Apple: [36288501960](https://github.com/ueeruan/aurea/actions/runs/36288501960). Compilação Release, validação do bundle, áudio estéreo/conversão de amostragem, compilação Metal (81/81 shaders) e validação do IPA passaram. A cópia baixada foi validada novamente, incluindo número 2123 e arquitetura ARM64. Os testes nativos de interação terminaram com 20 de 22 aprovados. Permanecem duas falhas de UI: acesso ao preset de material cinematográfico após rolar o painel e altura do editor de curvas (313 px frente ao limite de 281 px do teste). Não são declaradas resolvidas.

Reteste das capturas: [36289181172](https://github.com/ueeruan/aurea/actions/runs/36289181172), concluído com sucesso. Home e HDRI passaram, após ampliar a janela de captura e corrigir a auditoria de HDRI para os quatro campos da API atual. A preservação do asset, dos parâmetros e da imagem renderizada ao salvar/reabrir também foi conferida localmente. Esse reteste não substitui os dois testes de interação que falharam.

## Evidência antes da publicação

- Efeitos MediaLab: 7 testes / 100.187 verificações, zero falhas, Vulkan real no host. Inclui benchmark 720p com readback, não medição de celular.
- Tracking: 10 entradas / 173 verificações, zero falhas. MotionGeometry: 11 entradas / 1.965 verificações, zero falhas. Benchmarks opt-in não contam como executados.
- Os seis shaders novos traduzidos para Metal 2.1 e GLES 3.1. Verificadores locais de APIs Swift/ObjC, tipos, projeto Xcode, parâmetros e recursos compartilhados: zero problemas.
- Android: compilação debug anterior e 124 testes JVM passaram. Releases ARM64 e ARMv7 concluídos, versão 2123, mínimo API 26. Assinaturas v2/v3 conferidas com a mesma chave de atualização; cada APK contém somente a ABI correspondente, confirmada também nos cabeçalhos ELF. O pacote de instrumentação separado compila.
- Suíte completa no host: 834 entradas, 5.574.729 verificações, duas falhas em fixtures antigas. A contagem esperava 67 efeitos em vez de 73. A auditoria visual usava imagem opaca em tela cheia sobre fundo preto para sombra, camada sem pai para Parenting Helper e Corner Pin neutro. As fixtures foram corrigidas; Text 3D Layout é verificado com geometria de texto real no teste dedicado, pois imagens não o aceitam. Reteste: registro 2 verificações, auditoria visual 256, texto 3D na GPU 23; zero falhas nos três. A suíte completa foi então repetida: 834 entradas, 5.574.767 verificações, zero falhas (`build/effects-packages/host-release-accepted.log`). Benchmarks opt-in e trecho Devanagari sem fonte no host foram pulados.
- Emulador Android reservado ao usuário. O teste novo de tracking compila, mas sua execução conectada aguarda disponibilidade.

A suíte ampla identificou divisão por zero em `audio::frame_to_sample` ao receber FPS positivo extremamente pequeno. Correção adicional: não usar denominador inteiro arredondado a zero; tratar taxas não finitas e saturar conversões fora de `i64`, preservando o caminho inteiro exato para taxas usuais/NTSC. A reprodução de 20.000 comandos agora passa (8 verificações). Suíte de áudio: 22 entradas/98.488 verificações, zero falhas. Os pacotes são regenerados com essa correção; os hashes preliminares não representam os arquivos finais.

A bateria GPU identificou espera de até 12,5 s no RGB no tempo ao pedir o fim de uma mídia CFR. O limite anterior podia ficar 1 µs além da tolerância do decoder. O renderer agora limita ao timestamp do último frame. Regressão de comparação da imagem: erro zero, 0,7 ms; filtro TimeWarpRgb: 3 testes/24 verificações, zero falhas. Fuzz final: 5 testes/6.297 verificações, zero falhas, incluindo 6.000 arquivos mutados, 300 projetos, 20.000 comandos, 4.380 planos e 438 frames GPU. O teste do Text 3D Layout usa texto 3D real e verifica que o efeito é recusado em vídeo; não acessa mais uma pilha vazia depois da recusa esperada.

## Limites conhecidos

Não equivale à conclusão integral do Prompt 4 ou à reprodução idêntica dos plugins desktop. Ver `PROMPT04_TRACKING_PROGRESS_2026-09-26.md` e `MEDIA_LAB_EFFECTS_2026-09-26.md` em `docs/architecture` para recursos faltantes e evidência. A51 5G e iPhone físicos não estão disponíveis nesta máquina. A mitigação Samsung não comprova a resolução do fechamento relatado.

Certificado público SHA-256 dos APKs: `55bf3cc844050df48dff27e71a70cb54e5bca40a5c0db9e43f9d21cfff52b5c8`. Assinaturas v2/v3 válidas, pacote não depurável. Nenhum binário de plugin desktop está incluído nos APKs. O IPA não contém assinatura de distribuição e exige assinatura pelo método de sideload escolhido pelo usuário.
