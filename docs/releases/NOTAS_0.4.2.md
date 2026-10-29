# Aurea Beta 0.4.2 (build 2206) · 28/10/2026

Android (AAB, Google Play). As mesmas mudanças estão no código do iPhone, mas o IPA não foi gerado.

## Novidades
- **Chão reflexivo** (+ › 3D › Chão reflexivo): uma camada de ajuste 3D com o efeito novo
  "Chão reflexivo". Ela reflete no chão tudo o que está abaixo dela, em 2D e em 3D, partículas
  incluídas.
  - Estilos do chão: liso, ladrilhos ou ladrilhos gastos. A textura também pode ser uma camada
    do projeto.
  - O chão é um plano 3D: acompanha a câmera 3D e o nulo 3D ligado a ele.
  - Ajustes em Efeitos: horizonte, reflexo, esmaecimento, desfoque, distorção, cor, ladrilho,
    perspectiva, brilho e opacidade.
- **Chão da cena 3D** (Ambiente › chip "Chão reflexivo"): agora também reflete malhas
  transparentes e textos, formas e imagens em 3D.
- **Legendas automáticas na nuvem**: com a conta conectada, a fala vai ao servidor do Aurea e é
  transcrita pelo Whisper Large v3 Turbo (Workers AI). O áudio é cortado nas pausas, em trechos
  de 25 a 45 s, e os silêncios não são enviados. O servidor não guarda o áudio.
  - Sem conta ou sem internet, o modelo do aparelho é usado sozinho, como antes.
  - Servidor: `POST /api/transcribe`, com limite diário por conta e por IP.
- **Tela de legendas nova**: estilo, idioma e um botão "Gerar legendas". O modelo do aparelho e os
  outros ajustes ficam em "Mais opções".
- A página de privacidade (PT/EN) explica o envio do áudio para as legendas.

## Validação
- Servidor: 85 testes, incluindo os de `/api/transcribe`. Publicado e conferido no ar.
- Motor:
  - Chão reflexivo: imagem de prévia (espelha tudo abaixo da camada) e um único desfazer ao
    adicionar.
  - Chão da cena 3D: planos sozinhos ganham chão sob o canto mais baixo.
  - Legendas: os trechos WAV cobrem toda a mídia.
- Android: o chão reflexivo foi conferido no emulador (x86_64).
- **Ainda não testado em aparelho**:
  - A transcrição na nuvem em uso real (o emulador para no login).
  - As mudanças em Swift desta versão, que ainda não foram compiladas para o iPhone.

## Pendências
- Play: copiar a SHA-1 da "assinatura de apps" para `com.aurea.editor` no Firebase (vem da 0.4.1).
