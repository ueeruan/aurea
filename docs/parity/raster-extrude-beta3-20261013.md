# Extrusão raster — implementação própria

Os quatro vídeos de referência e o build2183 pertencem a outro aplicativo.
Eles especificam o comportamento desejado; não são regressões comprovadas do
AUREA2179 e não há projeto editável desse material. Nenhum shader, código ou
asset extraído entrou no produto. As inspeções REA das referências permanecem
em `build/reference/beta3-followup-3d-20261013/`.

O novo efeito `aurea.generate.raster_extrude` extruda o alfa final de texto
raster, shape/vector, imagem, vídeo e composição/grupo. A imagem passa primeiro
pelo grafo2D completo de máscaras/efeitos, que fornece a face e sua silhueta.
O volume ocupa Z local de0 até a profundidade escolhida no mesmo grupo3D da
camada, com rotação, pais/Null, câmera e profundidade reversa existentes. A
interseção escreve `gl_FragDepth`; ela pode passar diante, atrás e cruzar outros
planos/modelos. Não converte texto em glTF nem perde sua fonte raster.

| Índice | ID | Padrão | Faixa slider / digitada |
| --- | --- | --- | --- |
|0|depth|32px|0..512 /0..4096px|
|1|side_color|RGBA0,45/0,45/0,45/1|0..1|
|2|shading|0,7|0..1|
|3|quality|1: Média|0: Baixa,1: Média,2: Alta|

Stroke Outline acrescenta somente `depth` no índice5, com padrão0 e as mesmas
faixas. Índices0..4, keyframes e valores antigos permanecem endereçados. No
Stroke, a cor lateral vem da cor do contorno; aplica-se à silhueta raster final.
Model3D/Null e outros tipos sem plano raster não recebem este volume: os
comandos rejeitam ativar profundidade e a consulta compartilhada esconde apenas
o slot5 nesses tipos. Android e Swift já respeitam o bit `kParamHidden`.

Adicionar o efeito ativa `threeD` na mesma transação de undo. Ativar profundidade
positiva no Stroke ou escrever uma chave de profundidade positiva faz o mesmo.
Remover/desligar o efeito ou zerar sua profundidade não desliga o modo3D que já
foi escolhido. O desfazer restaura o efeito, parâmetro/chave e flag juntos.

Se houver várias operações de extrusão positivas, a última na ordem da pilha
escolhe os parâmetros de **um** volume da imagem final; não há soma silenciosa
de profundidades. Uma transformação2D final dobrada também é aplicada a esse
volume. Motion Tile com extrusão usa sua saída raster finita estabelecida;
o caminho periódico infinito do Motion Tile é reservado ao plano sem volume.

A geometria lateral usa o contorno a50% do alfa máximo da fonte; as tampas
preservam seu alfa fracionário e cor linear pré-multiplicada. As paredes usam
a cobertura de alfa dos texels próximos e a cor lateral convertida de sRGB.
O controle Sombreamento aplica uma luz de estúdio limitada às paredes. Aceita
luzes continua usando as luzes do grupo; normais vêm da inversa transposta do
transform do subquadro, incluindo escala não uniforme e a câmera.

O algoritmo resolve raios em um volume alfa, sem readback para CPU. Uma
pirâmide de **máximos** conserva regiões ocupadas e permite saltar blocos vazios.
Os níveis reduzidos ocupam no máximo uma área de fonte mais14texels
(aproximadamente um terço para imagens2D grandes). A fonte tem no máximo8192
texels por eixo, respeita o orçamento existente do FrameGraph e é emprestada
no nível zero; não há novo cache persistente. Até14 níveis chegam a1×1 e usam
12bindings constantes: níveis mais finos e máximo global. UBO816bytes/push112
cabem nos contratos existentes. Não há indexação não uniforme de samplers.

O laço percorre no máximo64/192/512 células por raio em Baixa/Média/Alta.
Dentro da célula, a interseção do alfa bilinear é resolvida por uma quadrática.
Texturas com trajetos transparentes muito detalhados podem atingir esse teto;
a qualidade mais alta amplia a precisão, sem criar execução ilimitada. Escala
degenerada produz geometria vazia e não deixa um frame eternamente pendente.
Transparência usa as duas fases e a ordenação por profundidade existentes;
não acrescenta ordenação exata por fragmento nem bevel/sombra projetada própria.

## Validação

`frozen-hidden-host` passou **9 casos/259 verificações** no host Vulkan e comandos
headless, com fonte/binário estáveis. Inclui o hint contextual `hidden` de
Stroke.depth5 em tipos sem plano raster e os cinco tipos raster (texto com
glifos nativos, shape, imagem, vídeo sintético e precomp), grafo de cor posterior,
Stroke com profundidade, layout Stroke antigo, schema, comando, undo/redo e
keyframe. Um projeto próprio gravado com5slots ganhou apenas depth0 ao carregar,
preservando o keyframe de opacidade e sem ativar3D.

O teste de tampas preservou alfa128/255, furos e RGBA com erro máximo0,000488.
Nas três qualidades e rotações Y±60°, a face teve1.622pixels e a parede1.816;
um plano situado na metade física da parede deixou992pixels visíveis. Plano
diante cobriu o objeto, e plano atrás manteve os interiores. Texto nativo teve
922pixels de face e2.056 de parede. Stroke plano antigo deu diferença zero;
sua extrusão aumentou pixels laterais de368 para1.402. Escala zero não deixou
recursos pendentes.

A primeira fixture comparava paredes internas de furos em perspectiva com uma
imagem2D e posicionava o plano de corte emZ+20. Esses oráculos falharam e foram
preservados. As tampas agora são comparadas nos interiores ocupados, mantendo
a tolerância0,003; a parede voltada à câmera após rotação60° situa-se em
Z−32sin60 até Z−32sin60+40cos60, por isso o corte usa a metade desse intervalo.
Não foi necessário afrouxar limites nem mudar a geometria para fazê-los passar.

Revalidação compartilhada: `Reference3DGpu` **3/304PASS**, com textura própria
em cubo/pirâmide, blur animado, igualdade direto/precomp e profundidade dos
filhos do Null. Esse caso já passava BEFORE da extrusão: não se atribui sua
execução a uma correção dos vídeos externos. PreRenderCache **8/1763PASS** é
uma validação independente do novo cache, não da interface ou integração.

Evidências em `build/validation/raster-extrude-20261013/`: manifesto, logs e
`native-catalog.json` emitido pelo registro compilado. Traduções finais próprias
SPIR-V/ESSL310/MSL estão em `shaders/manifest.json`; MRT0/1 e variante MSLc0
mantêm profundidade e bindings0..11. Isso não comprova execução nativa iOS.
Android 2180: **2/2 testes nativos passaram em10,151s**, no APK x86_64
`da7d1ba636f524120c6d170522c6268a4a047ff21a36e4afbd56967d6adbd62d`.
A captura nativa verificou alfa fracionário e buraco,2.786pixels laterais nas
três qualidades e818pixels laterais de texto com Stroke.depth5, incluindo
ativação3D e undo. É uma captura offscreen própria, não execução dos controles
da interface. REA inventariou o APK com
`ev_6c041d5d550a8d5d423599ca3f089f6a281b7e96ce95ef49fdf465caa3ad5615`;
o dispositivo foi Vulkan SwiftShader API31, sem aparelho OEM físico.
O conjunto JVM inteiro passou411testes. A primeira fixture Android falhou por
ler flags antes de drenar comandos aceitos e por usar um ponto no buraco após
o ajuste de importImage à composição. Esses logs e PNG foram preservados;
a correção foi apenas no teste, sem mudar o APK de produção ou a tolerância.
O APK imutável da fase Motion Tile não contém este efeito. Não há validação
de compatibilidade em celulares OEM afetados ou execução nativa iOS.
