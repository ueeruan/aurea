# Efeitos solicitados em 09/10/2026

Pedido: WipeFlux, WipePlasma, S_Distort, Pixel Encoder, BCC Glitch,
PixDither, Displace e Starglow, com fidelidade visual aos originais.
Depois: Home semelhante à organização do Alight Motion e tracker de câmera
mais claro. Essas duas telas continuam no escopo do mesmo trabalho.

## Estado real

Pesquisa de documentação e YouTube feita para os oito candidatos.
Os oito receberam implementações próprias no C++ e metadados equivalentes
nas interfaces Android/iOS. Todos compilaram no host e passaram nos testes
de propriedades GPU: 11 testes, 40.729 verificações. A difusão Floyd Steinberg
também foi comparada com uma referência CPU independente, sem pixels diferentes
na entrada de 37×21. Foram geradas sequências próprias de 60 quadros por efeito.
Displacer Pro é a hipótese de trabalho para Displace, ainda sem confirmação
da identidade pelo usuário. Home e tracker receberam o redesenho em Kotlin e
Swift; Android compilou e foi instalado no emulador API 35. O registro detalhado
de execução e os limites estão em `docs/validation/efeitos-home-tracker-2026-10-09.md`.
A UI compacta do trabalho anterior é independente destes
efeitos. O emulador Android continua disponível para revisão dessa UI.

Documentação descreve comportamento, mas não determina os campos de ruído,
pesos de filtragem, padrões de pixels nem resultados aleatórios dos plugins.
Sem renders originais equivalentes não há evidência para chamar uma
recriação de idêntica. O shader existente de displacement map não equivale
automaticamente a S_Distort; o efeito existente de 8 bits não cobre PixDither.

## Referências candidatas

A pedido do usuário, a pesquisa foi estendida ao YouTube. Metadados e limites
de inspeção por vídeo estão em
`build/reference/requested-effects/references/catalog.json`.

| Efeito | Vídeo observado | Autor/canal | Limite |
|---|---|---|---|
| WipeFlux | [Tutorial específico](https://www.youtube.com/watch?v=My_3x8yMDQY) | inside the mind of dylan | Identidade e duração verificadas; inspeção temporal completa pendente |
| WipePlasma | [S_WipePlasma](https://www.youtube.com/watch?v=oCYSc9KtIrw) | nikvfx | Amostra em 1:02: animação do Wipe Percent de 0 a 100 |
| S_Distort | [Warp com S_Distort](https://www.youtube.com/watch?v=qelbO7sJQy0) | ecspression | Combina escala, expressões e Motion Tile; efeito não isolado |
| Pixel Encoder | [Apresentação oficial](https://www.youtube.com/watch?v=yXjI7iGwhpU) | aescripts + aeplugins | Promo de 31 s; padrões observados, parâmetros numéricos ausentes |
| BCC Glitch | [Curves View em Video Glitch](https://www.youtube.com/watch?v=Jh632SUXG98) | Boris FX | Demonstra BCC+ Video Glitch; confirmar painel visualmente antes de fixar variante |
| PixDither | [Tutorial encontrado](https://www.youtube.com/watch?v=iArYISJphkc) | SAIBA | Principalmente instalação; referência visual insuficiente para equivalência |
| Displace | [Displacer Pro oficial](https://www.youtube.com/watch?v=FLMdpxrKqlw) | Plugin Everything | Candidato; não comprova a identidade pretendida |
| Starglow | [Trapcode Starglow em texto](https://www.youtube.com/watch?v=9zSakRsTH78) | Kreativ Tuts | Vários efeitos na composição; não é benchmark isolado |

Descrições automáticas de cenas são estimativas [E], não transcrições verificadas
nem medições [M]. No WipePlasma, a descrição automática atribui narração a
instruções que a amostra visual mostra em texto; ela não deve fixar controles.
WipeFlux/WipePlasma preliminares usam campos matemáticos próprios [D]; a
topologia e a kernel do brilho ainda não têm comparação numérica com o original.

| Pedido | Candidato identificado | Fonte primária | Confirmação do usuário |
|---|---|---|---|
| WipeFlux | Sapphire S_WipeFlux | [Boris FX](https://borisfx.com/documentation/sapphire/ae/wipeflux/) | Pendente |
| WipePlasma | Sapphire S_WipePlasma | [Boris FX](https://borisfx.com/documentation/sapphire/ae/wipeplasma/) | Pendente |
| S_Distort | Sapphire S_Distort | [Boris FX](https://borisfx.com/documentation/sapphire/ae/distort/) | Pendente |
| Pixel Encoder | MEGAKRUNCH Pixel_Encoder | [Autor/aescripts](https://aescripts.com/pixel-encoder/) | Pendente |
| BCC Glitch | Continuum BCC Video Glitch ou BCC+ Video Glitch | [BCC](https://borisfx.com/documentation/continuum/bcc-video-glitch/), [BCC+](https://borisfx.com/documentation/continuum/bcc-plus-video-glitch/) | Variante pendente |
| PixDither | Wunkolo PixDither | [Autor](https://wunkolo.itch.io/pixdither) | Pendente |
| Displace | Nome ambíguo | Displacement Map / Displacer Pro / BCC Displacement Map são candidatos, não escolhas | Pendente |
| Starglow | Trapcode Starglow | [Maxon](https://help.maxon.net/rg/en-us/Content/html/01-Starglow-trapcode-starglow.html) | Pendente |

## Comportamento a preservar

Os valores abaixo são **publicados na documentação**, não medições de
renders. Valores medidos serão marcados [M]; ajustes [A]; estimativas [E];
decisões próprias [D]. Ainda não há valores [M].

### S_WipeFlux

Transição entre foreground e background, com células arredondadas em uma
textura de fluxo. O progresso pode ser manual ou automático pela duração da
camada; direção e gradiente alteram a revelação. Há borda e brilho de borda.
Padrões publicados: frequência 6, frequência relativa X 1, seed 0,432,
oitavas 2, bubble amount 1, bubble smooth 0,25, morph [0,1], morph speed 0,3,
grad add 0. Controles adicionais: deslocamento XY, rotação da deformação,
suavidade, borda/cor/opacidade/deslocamento e brilho/cor/largura por canal,
ruído de brilho e processamento de alfa.

Precisa de comparação de topologia das células, movimento do campo e avanço
da borda com a mesma seed. Um dissolve genérico por ruído não comprova isso.

### S_WipePlasma

Transição por filamentos de plasma, com fase animada. Padrões publicados:
frequência 4, frequência relativa X 1, oitavas 4, seed 0,12, plasma grad 0,
layers 8, phase start 0, phase speed 2, grad add 0,5. Há ângulo do plasma,
deslocamento XY, ângulo do gradiente, suavidade, borda e brilho com controles
por canal. O campo de filamentos precisa ser avaliado ao longo do tempo.

### S_Distort

Deslocamento pelo **gradiente de brilho** da entrada Lens. Matte opcional
limita o efeito. Padrões publicados: amount 1, fine desligado (quando ligado,
reduz a força por 100), blur lens 0,4, rotate warp dir 0, amount rel [1,1],
wrap [Reflect,Reflect], filter ligado. Canal alfa/luma do matte, inversão,
suavidade do matte, recorte das bordas e opções de alfa também afetam o
resultado. Integração Mocha é recurso externo do host e não foi decidida
como parte do AUREA.

Benchmark: lente constante deve produzir deslocamento nulo; rampa desloca
uniformemente; bordas de formas produzem refração localizada. Testar amount
negativo, Fine, rotação 90°, wraps independentes e matte.

### Pixel_Encoder

Converte luminância em células com padrões. O autor lista dez famílias:
High Detail, Numbers & Letters, Horizontal Lines, Dots, Vertical Lines,
Extra Grunge, Rings, Solid e diagonais esquerda/direita. Controles descritos:
escala/trava de escala, brilho/contraste, cores frente/fundo, cores originais,
fundo transparente, Chaos/seed/randomize, shading detail, offsets XY e
inversão. A versão 1.7 adiciona canais de detecção Rec.601, média, R, G e B.

A geometria e os limiares de cada padrão precisam de renders originais;
um shader de mosaic ou ASCII com outra fonte não demonstra equivalência.

### BCC Video Glitch

Grupos independentes de Block Damage, Shift, Shake e Flicker. Intervalo
expresso em frames; duração expressa como porcentagem do intervalo.
Aleatoriedade e seeds mudam momento, intensidade, duração e cada grupo.
Shift inclui duplicação/queda de linhas, skew e jitter; Shake inclui posição,
rotação e RGB split; Flicker modifica brilho e saturação. Block Damage pode
congelar a fonte durante o evento. Há disparo automático ou cruzamento de
limiar, bordas e composição de fundo. Beat Reactor e a visualização de curvas
do host precisam de decisão separada.

Benchmark exige entrada animada para testar congelamento, repetição após
seek, exportação e curvas de crescimento/desaparecimento dos eventos.

### PixDither

Quantização de cores com paletas predefinidas e customizadas; suporta entrada
8/16 bits por canal. A lista pública inclui 26 métodos: Threshold; Floyd
Steinberg; False Floyd Steinberg; Filter Lite; Zhigang Fan; Shiau-Fan; Jarvis,
Judice & Ninke; Stucki; Burkes; Sierra; Two Row Sierra; Atkinson; Bayer
2×2/3×3/5×3/4×4/8×8; Cluster Dot 4×4/8×8; Horizontal 2×2/8×1/12×4;
Vertical 2×2/1×8/4×12; Diagonal 5×5. A v1.1 carrega paletas ASE e as salva
no projeto. Há famílias Gray/RGB, PICO-8, Gameboy, AppleII, CGA/VGA, VIC-20,
Amstrad, MSX, ZX Spectrum, C64/Pepto, NES, FamiCube, Web Safe e Flipnote.

Difusão de erro exige vizinhança e ordem determinística; não pode ser
substituída por matriz Bayer. Conferir cores exatas, empates na quantização,
espaço de cor, matrizes, alfa e continuação entre tiles.

### Displace

Displacer Pro foi adotado como hipótese após a pesquisa no YouTube. O novo
`aurea.distort.displace_transform` expõe mapa, deslocamento XY, escala,
rotação, força, canais, ponto neutro, blur, iterações, aberração cromática,
bordas, filtragem e mistura. A identidade e a calibração numérica do plugin
continuam pendentes; o efeito existente `aurea.distort.displacement_map`
permanece disponível.

### Starglow

Oito direções com comprimento individual e escolha entre três mapas de cor.
Cada mapa pode ter uma, três ou cinco cores. Canal de entrada selecionável,
limiar/suavidade e máscara circular controlam os highlights. Comprimento
mestre, Boost Light, Shimmer com detalhe/fase/loop, opacidades separadas da
fonte e brilho e modo de composição completam o comportamento descrito.
Fonte: [controles Maxon](https://help.maxon.net/rg/en-us/Content/html/05-Starglow-main-controls.html).

Benchmark: impulso isolado identifica comprimento, perfil de queda e cor de
cada direção; pontos próximos testam sobreposição; alfa e bordas testam
expansão. Oito linhas sólidas sobrepostas não reproduzem o kernel de brilho.

## Implementação nativa e integração

- Regras e passes novos em `engine/src/effects/builtin/` e `engine/shaders/`.
- Registro acrescentado ao fim do catálogo, com IDs estáveis; preservar
  índices de parâmetros e serialização dos efeitos existentes.
- O pipeline atual traduz os shaders GLSL para SPIR-V e MSL no build.
- A coleta de entradas foi ampliada com `input_layer_count/at`; S_Distort
  resolve Lens e Matte separadamente, inclusive camadas ocultas.
- UI humana, parâmetros principais/avançados, buscas, textos e
  acessibilidade em Kotlin e Swift no mesmo trabalho.
- Home e tracker: inspecionar referência AM e telas atuais; preservar
  navegação, projetos, estado de análise, criação de câmera e undo.

## Comparação exigida

Usar mesma entrada, versão, controles, seed, FPS, resolução, gerenciamento de
cores, alfa e instantes. Preferir PNG/EXR sem perdas, parâmetros visíveis e
sequência inteira. Registrar diferenças por efeito (MAE/SSIM e medidas de
contornos, perfil de luz, distribuição de cores ou eventos conforme o caso),
com tolerância explícita, sem um único número genérico decidir a fidelidade.

Entradas próprias e manifestos ficam em `build/reference/requested-effects/`.
Capturas e inspeções brutas de terceiros devem ficar somente em
`build/reference/`. Nenhum código, shader ou asset do plugin entra no AUREA.

| Plataforma | Implementação | Compilação | Validação nativa dos oito novos efeitos |
|---|---|---|---|
| Motor compartilhado | Oito implementações próprias | Host Release passou | Propriedades Vulkan e sequências próprias; fidelidade ao fornecedor pendente |
| Android/Kotlin | Catálogo, controles, Home e tracker | APK uiTest passou | Emulador API 35, registros em relatório de validação; sem aparelho físico |
| iOS/Swift | Controles, textos, Home e tracker | Não executada | Auditorias estáticas passaram; execução nativa requer macOS/Xcode e dispositivo |

## Informações ainda necessárias

1. Confirmar candidatos e versões, especialmente BCC/BCC+ e Displace.
2. Referência visual/projeto original com controles para comparação.
3. Registrar quais recursos externos do host precisam existir no AUREA.
