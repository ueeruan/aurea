# Aurea beta — build 2109

## Novidades

- Expanda camadas na timeline para acessar transformações, efeitos e keyframes.
- Proxies automáticos reduzem o peso do preview; a exportação usa os originais.
- Graph Editor com curvas de valor e velocidade.
- Novo efeito Halation com controles animáveis.
- Melhorias de progresso, cancelamento e erros na geração IA.

## Correções de bugs

- Animação de texto criada sem movimento e presets começando antes do playhead.
- Animação de digitação com duração incorreta em caracteres UTF-8.
- Botões anterior/próximo ignorando marcadores da composição.
- Expansão das camadas escondendo detalhes ao trocar a seleção.
- Câmera 3D distorcida por escala no objeto pai.
- Sombras de modelos deformados e normais de materiais espelhados incorretas.
- Tracking confundindo objetos móveis com movimento da câmera.
- Motion blur incorreto em freeze e mudanças de velocidade.
- Emissão e contagem incorretas de partículas.
- Upload de imagem sem limite de memória e erros pouco claros na geração IA.

## Validação e limites

696 testes do core, 166 testes nativos GLES no emulador Android e 93 testes
Android JVM passaram. Interações de expansão e criação de animação de texto
verificadas no app Android. Não são medições de celular físico.

Mudanças aplicáveis incluídas no core compartilhado e interfaces Android/iOS.
AI Video Upscaler, validação completa de tracking/estabilização/flow/3D,
medições em celulares fracos e regressão nativa iOS permanecem pendentes.
O IPA do workflow é sem assinatura e precisa ser assinado para instalar.

Erro conhecido no ambiente de teste: após salvar corretamente o MP4, o anúncio
Unity provocou uma falha interna do WebView 124 do emulador. A causa dentro do
SDK/WebView não foi corrigida neste build; ainda requer reprodução em aparelho
com WebView atualizado. Não confundir isso com falha na codificação do vídeo.
