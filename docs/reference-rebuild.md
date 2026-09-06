# Recriacao nova da referencia — Codex

Fonte: motiondesigners2d_pindown.io_1788409211.mp4, fornecido pelo usuario.
Video: 720 x 1278, 30 fps, 280 quadros; audio 9,4 s.
SHA-256: 2074485d0ae57ad313bef2bf51fec3f52ba64181a706500fbda8d8886180a085.

Esta composicao e independente de `pindown_motion_template.dart`. O modelo
anterior foi preservado. As coordenadas e tempos partem do MP4 original.
O video original nao e usado como uma camada de imagem que simula recriacao.

## Mapa observado

- 0–43: casa se desloca lateralmente e ganha perspectiva, estrela orbitante.
- 44–69: pulsos de iluminacao alternados, troca para casa incendiada.
- 70–139: estrela ocupa a tela e recua, olho acompanha a orbita, lagrima.
- 140–207: espada entra, reflexos percorrem as faces, maos chegam e solo racha.
- 208–279: coroas inclinadas surgem em sequencia; aproximacao final.

## Verificacao

`tool/reference_study.py` mede a fonte. A recriacao tem render de teste
independente; comparar os mesmos indices de quadro, sem realinhar o tempo.
Os numeros de similaridade nao equivalem a uma certificacao de identidade.
Nao declarar o trabalho pixel-identico sem essa verificacao.

## Recurso do motor

Gradientes vetoriais agora aceitam posicoes individuais das cores, centro
e alcance. Isso permite distribuir precisamente o horizonte e os reflexos,
mantendo a compatibilidade com os gradientes uniformes existentes.

O painel esta em Forma > Gradiente e usa as reguas de arrasto do AUREA.
A primitiva Coroa fina foi adicionada ao final do catalogo 3D, preservando
os indices antigos. A rotacao da nova composicao foi calculada para girar
no eixo local da coroa antes da inclinacao.

Corrigido tambem o filtro de cor do Glow: srcATop apagava o RGB extraido
pelo threshold e pela intensidade; modulate preserva esse sinal. O teste
glow_intensity_regression_test verifica os dois controles no renderer real.

## Estado da comparacao

A primeira comparacao completa (iteracao 04) cobriu todos os 280 quadros:
erro medio RGB 22,38 em escala 0–255; zero quadros pixel-identicos. Isso NAO
e um percentual de similaridade. Ha diferencas de desenho, iluminacao e
coreografia, principalmente na espada e na transicao. A iteracao seguinte
corrige o recorte local antes do glow dos reflexos da lamina.

A iteracao 05 tambem foi renderizada e comparada nos 280 quadros: MAE RGB
22,13/255, ainda zero quadros pixel-identicos. Saidas em
`build/render/reference-rebuild/05`: MP4 da recriacao, MP4 lado a lado
(original a esquerda), pacote JSON editavel e CSV/JSON das medicoes.
O MP4 foi verificado: 720 x 1278, 30 fps, 280 quadros e 9,333333 s.

Validacao local: analise sem problemas e 1.046 testes aprovados na suite
completa. O teste de gesto do gradiente foi reforcado e aprovado novamente
para exigir mudanca real no valor, nao apenas ausencia de erro.

O cartao Modelos > Nova recriacao · Codex abre a composicao independente,
com audio da referencia empacotado e copiado para armazenamento persistente.
Em 05/09/2026 o usuario autorizou enviar o codigo e os novos assets para
ruanpablo9928-sys/aurea e gerar o IPA. A compilacao 1.1.0+33 inclui este
modelo; abrir o cartao cria uma copia editavel nos projetos do aparelho.
Testes locais nao substituem teste num iPhone real. A recriacao ainda nao
e identica, conforme a comparacao acima.
