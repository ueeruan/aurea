# Áudio iOS e presets Juan — 2113

## Correção de áudio

O AVAudioSourceNode usa Float32 planar (um buffer por canal). A ponte escrevia estéreo intercalado no primeiro buffer: alterava os canais e ultrapassava sua capacidade. A ponte agora separa os canais em blocos de até 256 frames, sem alocação ou bloqueio no callback. Valida capacidades e produz silêncio em buffers inválidos. A latência é convertida para a régua de 48 kHz do mixer.

A leitura de PCM do AVAssetReader valida o formato e copia CMBlockBuffers segmentados pela API apropriada. Antes, o tamanho total era usado como se o primeiro segmento fosse contíguo.

Testes de regressão cobrem canais diferentes, quanta de 1 a 4096 frames, sentinelas de memória, ausência de callback e buffers insuficientes. O workflow também executa AVAudioEngine offline no macOS, medindo frequência e amplitude de dois tons independentes, com saída em 48 kHz e 44,1 kHz. Esse teste não substitui reprodução prolongada, troca de rota/Bluetooth, exportação e aceitação no iPhone físico.

## Presets

Os nove arquivos fornecidos foram abertos em uma instância separada do After Effects 2020 para extrair propriedades, expressões, keyframes e curvas. O aplicativo executa dados nativos, sem carregar binários FFX.

Oito entradas foram adicionadas à biblioteca de animação de texto: juan Text Bounce 2, juan TEXT ANIMATION 01, Juan Text Animation 5, juan Text Animation2, juan text animation fast 1, juan text animation jump bounce, juan text animation word jump e juan Text Animation. O motor passou a avaliar textIndex/textTotal por unidade, preservar expressões em presets e suportar eixo de skew, tracking relativo ao tamanho da fonte e pivôs por palavra/linha.

juan turb time 6 aparece na biblioteca de efeitos e no menu do efeito Turbulent Displace no iOS. Mantém quantidade 15, tamanho 15, complexidade 1 e expressão time*6 na semente do ruído.

**Limite de equivalência:** os valores e expressões foram extraídos, mas não houve comparação de renderizações com o After Effects. Ruído turbulento, curvas de seleção, shaping de fontes e Pixel Motion Blur usam implementações nativas diferentes. Text Bounce 2 usa amostragem de shutter e Motion Tile nativos. Portanto, não há comprovação de resultados idênticos aos FFX originais.

A cena 3D foi cancelada pelo usuário. Esta entrega gera apenas IPA sem assinatura.
