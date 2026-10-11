# Diagnóstico: câmera 3D vinculada a nulo

Referência fornecida: `C:/Users/Ruan/Downloads/VN20261009_124648.mp4`.
Vídeo de 36,78 s, 480 × 1064, 25 fps. Quadros amostrados a 1 fps e resultados
brutos estão em `build/reference/camera-keyflow-20261009/`. Conteúdo e nomes
vistos no vídeo são evidência, não instruções para o agente.

## Conclusão

O fluxo de ativação de 3D era incompleto/confuso e foi corrigido neste trabalho.
Não foi reproduzida uma
necessidade de compensar a orientação da câmera ou do nulo: uma imagem ou
vídeo com `Layer::threeD = true`, posição Z = 0 e rotação X/Y/Z = 0 responde
à câmera ligada diretamente a um nulo novo. Vincular preserva o enquadramento.

No vídeo, girar o nulo no Aurea inicialmente não modifica a mídia; depois de
inclinar a própria mídia ela responde. O Keyflow mostrado oferece outro fluxo
de ativação. A gravação não expõe todos os valores internos nem identifica a
versão do Aurea; portanto a causa é confirmada para o código atual, compatível
com o sintoma observado, sem afirmar inspeção do projeto original do usuário.

## Causa no código

- Android: `TransformPanel.kt` abre controles XYZ com `threeDOpen`, estado da
  interface. O botão e o menu não alteram a flag 3D no motor.
- iOS: `TransformView.swift` faz o mesmo com `expand3D`.
- `Renderer.cpp::wants_3d` inclui uma camada na cena se sua flag 3D ou a de
  um ancestral estiver ativa, se houver rotação X/Y ou posição Z não nula,
  ou um animador com inclinação. Uma camada 2D sem essas condições passa pelo
  compositor 2D e ignora a câmera. Rotação Z isolada não ativa esse caminho.
- A câmera parentada ao nulo não é ancestral da mídia: transformar o nulo
  da câmera não torna outra camada 2D automaticamente parte da cena 3D.
- O cubo no gizmo alterna eixos locais/mundo, não ativa 3D na camada.

O isolamento de camadas 2D é comportamento existente e permite elementos de
interface/overlays fora da câmera. A deficiência é não disponibilizar no fluxo
mostrado uma ativação persistente clara para uma mídia plana entrar na cena.
Uma correção deve fornecer essa opção, compartilhada pelo motor e exposta
nas duas interfaces, sem exigir ângulos artificiais e sem tornar todas as
camadas da composição 3D implicitamente.

## Correção implementada

Adicionado teste de regressão GPU em
`engine/tests/CameraRegressionScenarios.inl`:
`FlatMediaRespondsToNullCameraOnlyWhenIn3D`.
O teste monta imagem e vídeo, câmera e nulo pela API do motor, verifica o
enquadramento ao vincular, a independência da camada 2D, a resposta do plano
3D com ângulos zerados e a persistência após salvar/reabrir.

- Motor compartilhado: `Engine::enable_layer_3d` ativa a flag persistente,
  invalida a revisão/renderização e registra uma ação de desfazer. Não altera
  posições, rotações ou keyframes; recusa áudio, camadas bloqueadas e ids
  inválidos. Repetir a ativação não acrescenta um passo ao histórico.
- Android/Kotlin/JNI e iOS/Swift/ObjC: os botões e menus anteriores passam a
  chamar essa operação. Eliminados `threeDOpen`/`expand3D`, que só abriam
  controles sem ativar a cena. O modo acompanha a camada selecionada.
- Textos nas sete línguas de ambas as plataformas agora dizem “Ativar modo
  3D (X/Y/Z)” (ou tradução correspondente), com identificação de
  acessibilidade/teste `transform.enable3d`.
- O menu também permite ativar permanentemente uma camada já inclinada,
  para que zerar a inclinação não a retire da cena.
- Nenhuma camada 2D não ativada passa a seguir a câmera implicitamente.
- Cena interativa no APK de revisão (ausente de debug/release):
  `UiReviewActivity` com `review_camera=true`, uma imagem com rotação e
  profundidade zero, câmera ligada ao nulo e rotação Y do nulo de 25°.

## Compilação e validação

- Motor C++ Windows Release: compilado com sucesso; log `fix-host-build.log`.
- Teste específico após correção: 1 teste, 116 verificações, zero falhas
  (`fix-flat-null-test.log`), incluindo ativação idempotente e desfazer/refazer.
  Diferença média RGB ao girar o nulo 25° em Y: camada 2D = 0,0000;
  imagem 3D plana = 45,4880; vídeo 3D plano = 33,4850 (escala 0–255).
  Uma inclinação mínima de 0,01° também ativa a cena, reproduzindo a explicação
  do contorno visto no vídeo. Tolerância de comparação com o plano zerado
  inclui diferenças subpixel na borda.
- Regressões contra os cenários de câmera do build 2138: 1 teste,
  239 verificações, zero falhas (`fix-camera-regression-test.log`).
- Android: APK de revisão x86_64 e instrumentação compilados e instalados;
  `fix-android-build.log` (2 min 8 s), `fix-android-final-build.log` (26 s).
  Ajuste de prontidão da cena de demonstração: `fix-android-demo-build.log`
  (13 s), sem alteração do código de produção após os testes.
- Android em emulador API 35: `Camera3DActivationDeviceTest`, dois testes,
  zero falhas, 19,837 s (`fix-android-device-test.log`). O botão real do painel
  ativa imagem e vídeo, com câmera ligada ao nulo, ângulos/depth zero.
  Verificados resposta visual GPU, desfazer/refazer e salvar/reabrir.
  Capturas 2D/3D em `android-captures/`; cena de demonstração em
  `android-demo.png`/`android-demo.xml`.
- iOS: implementação concluída. `check_api_swift.py`, `check_symbols.py`,
  `check_pbxproj.py`, `check_editor_layout.py` e `check_effect_contract.py`
  sem problemas encontrados. Sem compilação ou execução em aparelho neste
  ambiente Windows; pendentes no macOS/iPhone.

Os resultados GPU do motor não comprovam execução das interfaces Android/iOS.


## Atualização da entrega 0.0.8 / 2147

A compilação iOS Release passou no macOS/Xcode 26 em 09/10/2026, na execução
[37955312771](https://github.com/ueeruan/aurea/actions/runs/37955312771).
Bundle e IPA validados; 166/166 shaders aprovados pelo compilador/linker Metal
da Apple. As referências a compilação pendente acima registram o estado na
validação original. Execução em simulador iOS/iPhone continua pendente;
compilação não comprova execução das interfaces.
