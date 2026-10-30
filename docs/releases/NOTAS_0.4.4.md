# Aurea Beta 0.4.4 (build 2207) · 29/10/2026

Android (AAB, Google Play) e iPhone (IPA). Mesmas mudanças nas duas plataformas. Inclui tudo da
0.4.2 (Chão reflexivo, legendas na nuvem, tela de legendas nova).

## Novidades
- **Explodir texto** (Animação do texto › Explodir texto › Letras / Palavras / Linhas): a camada de
  texto vira uma camada por pedaço. Comportamento de referência: TextExploder (aescripts);
  a implementação é própria, no motor.
  - Cada camada nova tem só o próprio texto: sem animador escondendo o resto, e com as cores
    e estilos de trecho preservados.
  - O primeiro glifo de cada pedaço fica no mesmo ponto da tela, com rotação, escala e
    inclinação.
  - Camada 2D parada: a âncora vai para o centro do pedaço, para animar cada letra em volta
    dela.
  - Camada 3D ou com transform animado: o transform fica igual e a âncora compensa, então
    vale em qualquer instante.
  - As linhas seguem as quebras reais da caixa de texto. A original sai, e um desfazer volta
    tudo.
  - Não se aplica a legendas nem a texto no caminho.
- **Preset "Salto suave"** (Animação do texto, o último da lista): cada palavra cai 50 px
  aparecendo e assenta com um quique amortecido (seno × exponencial), 0,08 s depois da anterior.
  É o "smooth bounce text" do After Effects (seletor de expressão por palavras), feito com os
  animadores do Aurea.
- **Entrar com o Google no app da Play**: as impressões SHA-1 e SHA-256 da assinatura da Play
  foram adicionadas ao app `com.aurea.editor` no Firebase.

## Validação
- Motor (host):
  - Novos e passando: TextExplode (3 testes: palavras no lugar em 2D e em 3D, letras e linhas,
    um desfazer) e TextPresets.SmoothBounceDropsWordsOneAfterAnother.
  - Também passam: os testes de presets que percorrem todos os presets (Juan, SharedText3D).
  - Falhas que já existiam antes desta versão, sem relação com ela: BetaHeavy.TrackText3D… e
    OriginalModelExport (GPU/exportação no host).
- Android: Kotlin compila. O AAB de release foi gerado.
- iPhone: o IPA do GitHub Actions é a compilação das mudanças em Swift.
- **Não testado em aparelho**:
  - Explodir texto e o Salto suave nas telas do Android e do iPhone.
  - O login com o Google no app da Play.
