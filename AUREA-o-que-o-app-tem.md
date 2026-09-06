# Aurea — o que o app tem

Editor de vídeo e motion graphics para celular (Android e iOS). Tudo roda
no aparelho: sem nuvem, sem conta, sem enviar arquivo para lugar nenhum.

**Versão 1.0.0-beta** · 77 arquivos Dart · ~39 mil linhas · 318 testes

---

## 1. Projeto e linha do tempo

- **Formatos prontos**: 16:9 (YouTube/TV), 9:16 (Reels/TikTok), 1:1 (Feed);
  resolução e fps escolhidos na criação.
- **Timeline multi-trilha** com playhead central, régua, zoom e arraste.
- **Cortar** (decupa de verdade — o trecho cortado não volta ao início),
  duplicar, agrupar, vincular, excluir com **Desfazer**.
- **Desfazer/refazer** em toda edição.
- **Salvamento automático**: um JSON por projeto, escrito de forma atômica
  (nunca fica pela metade).

## 2. Camadas — 12 tipos

| Camada | Para quê |
|---|---|
| **Vídeo** | Clipe com corte de origem e volume |
| **Imagem** | Foto ou PNG da galeria |
| **Texto** | Com o animador novo (seção 5) |
| **Forma** | Vetorial: retângulo, elipse, polígono, estrela, setor, anel, coração, onda, arco |
| **Legendas** | Automáticas por voz (seção 8) |
| **Áudio** | Trilha, narração, efeito sonoro |
| **Nulo 3D** | Controlador: move um, move todos os filhos |
| **Partículas** | Sistema 3D com rotação resolvida no simulador |
| **Ícones** | Biblioteca de ícones vetoriais |
| **Ajuste** | Efeito aplicado em tudo que está abaixo |
| **Elementos 3D** | Cubo, pirâmide, cone, esfera, cilindro, prisma, diamante, anel 3D, estrela 3D |
| **Cena 3D** | Renderizador 3D completo dentro de uma camada (seção 6) |

## 3. Animação — o motor

- **Keyframe em tudo**: posição, escala X/Y, rotação X/Y/Z, opacidade,
  inclinação, pivô, e todo parâmetro de todo efeito.
- **Curva por segmento** (modelo Alight Motion: N keyframes = N−1 curvas):
  bezier cúbica, **bounce**, **elástico**, **cíclico**, **aleatório**,
  **degraus** e **degraus elásticos**.
- **Editor de curvas** gráfico, com alças arrastáveis.
- **Loop de propriedade**: ciclo, ping-pong, offset e continuar — antes,
  depois ou nos dois lados dos keyframes.
- **Vínculo entre propriedades** (pickwhip): a rotação de uma camada
  dirige a escala de outra, com multiplicador e offset.
- **Expressões numéricas** nos campos (`120*2`, `1080/3`, `50%`).
- **Motion blur** com janela de exposição real.

## 4. Efeitos — 38, com busca

Busca por sinônimo: digitar "bloom" acha Glow, "pixelate" acha Mosaico,
"fita" acha VHS, "congelar" acha Remapear tempo.

- **Cor** — Níveis, Curvas, Vibração (com proteção de tom de pele), Balanço
  de branco, Rodas de cor, Tonalizar, Posterizar
- **Luz** — Brilho de luz, Glow volumétrico, Raios volumétricos, Unmult
  (tira o fundo preto de fogo e fumaça), Vinheta
- **Lente** — Separação RGB, Aberração cromática, **Máscara de nitidez**
- **Desfoque** — Gaussiano, Direcional, Radial, Eco / rastro
- **Distorção** — Tremor, Zoom warp, **Deslocar turbulento**, **Entortar**,
  **CC Split**
- **Estilizar** — Glitch modular, Eco espacial, Mosaico, Grão de filme,
  Ruído fractal, Dano digital, **Ordenar pixels**, **CC Semear**, **Mosaico
  de movimento**, **VHS**, **Filme danificado**, **Glitchify**, **Rastreador
  de blobs**
- **Tempo** — **Remapear tempo** (igual ao do After Effects: anima qual
  instante da camada aparece agora — congelar, reverter, rampa de velocidade)

Os quatro que redistribuem pixel (turbulento, entortar, ordenar, semear)
rodam sobre a camada rasterizada e deformada em malha — é deformação de
verdade, não filtro de cor.

**Presets de efeito**: salve a pilha inteira e reaplique. Keyframes são
gravados relativos ao início (aplicar aos 12 s funciona) e distâncias são
normalizadas (preset feito em 9:16 não sai errado em 16:9). Vem com 6
prontos: Cor de filme, Sonho suave, Glitch de impacto, Câmera na mão,
Overlay de fogo, Zoom de soco.

**Assar em keyframes**: converte movimento procedural em keyframes reais,
editáveis.

## 5. Animação de texto — modelo Alight Motion

Escolha **Entrada**, **Ênfase** ou **Saída** e toque numa animação numa
grade que **mostra cada uma se mexendo**.

**35 animações**: Aparecer · Subir · Descer · Vir da direita · Vir da
esquerda · Palavra deslizando · Crescer · Encolher · Estourar · Quicar por
letra · Quicar por palavra · Cair · Aparecer em desfoque · Desfoque por
palavra · Desfoque subindo · Desfoque descendo · Máquina de escrever ·
Girar · Tombar · Virar · Inclinar · Abrir espaçamento · Aparecer devagar ·
Glitch · Entrar em cor · Onda · Flutuar · Pulsar · Respirar · Tremer ·
Tremilique · Piscar · Blink · Arco-íris · Ambientação viral

**Seis controles**: unidade (letra, letra sem espaço, palavra, linha ou
tudo junto), início, duração, atraso entre unidades, ordem (do início, do
fim, do centro, das bordas, aleatória) e curva.

**Curva Mola** — a mesma conta da extensão MultiTools do After Effects
(amplitude, frequência, decaimento): a letra passa do alvo e volta.

**Modo Avançado (After Effects)**: animador cru com seletor de faixa,
seletor Wiggly, modos de combinação e propriedades na mão. As animações do
catálogo compilam para esses mesmos animadores — nada é caixa-preta.

O motor anima por unidade: posição X/Y, escala, escala X, escala Y,
rotação, opacidade, espaçamento, **desfoque**, **inclinação**, **matiz**,
**saturação** e **brilho**. Segmentação por grapheme cluster — emoji com
ZWJ conta como uma unidade só.

## 6. Cena 3D e câmera

Uma camada que por dentro tem um renderizador próprio.

- **Ordenação por triângulo**: dois cubos que se cruzam mostram a
  interseção correta — o que camada 3D comum não faz.
- **Passe opaco e passe transparente** separados; vidro na frente de
  opaco mostra o opaco.
- **Instanciação**: 200 cópias da mesma malha = **uma** chamada de desenho.
- **Culling** por volume envolvente; **culling de luz por objeto**.
- **Profundidade exportada**: dá para pôr uma camada 2D *entre* dois
  objetos 3D, com oclusão correta.
- **Materiais** PBR: cor, metálico, rugosidade, emissivo, opacidade, sem
  luz, vidro, recorte.
- **Luzes**: direcional, ponto (com alcance) e ambiente; sombra por luz.
- **Duplicar em array**: grade 3D onde tudo vira instância.
- **Modo de isolamento** e visibilidade por objeto.
- **Orçamento visível**: selo com chamadas de desenho e triângulos.

### Câmera (parâmetros fiéis ao After Effects)

- **Dois nós** (com ponto de interesse) ou **um nó** (livre) — converter
  entre os dois não muda um pixel do enquadramento.
- **Lentes** 15/20/24/28/35/50/80/135/200 mm; distância focal, ângulo de
  visão e zoom mostrados juntos e sincronizados.
- **Profundidade de campo** completa: distância de foco, abertura,
  diafragma f/, nível de desfoque, **formato da íris** (9 formatos),
  rotação, arredondamento, proporção, franja de difração e — o que separa
  lente de borrão — **ganho, limiar e saturação de realce**.
- **Auto-orientação**: desligada, seguir caminho ou apontar para o alvo.

### Estúdio 3D (navegação por toque)

- 1 dedo em área vazia **orbita**; sobre o objeto selecionado **move**;
  sobre outro **seleciona**. Mais um botão de modo navegação.
- 2 dedos **deslocam**; **pinça move a câmera no Z e não mexe na lente**.
- Toque duplo enquadra e vira o pivô. Pivô fixado no início do gesto.
- **Mini-vista** arrastável e redimensionável (topo/lateral) com câmera,
  frustum e objetos.
- **Eixos tocáveis** no canto: tocar no X vai para a lateral, no Y para o
  topo.
- **9 vistas**: câmera ativa, frente, trás, esquerda, direita, topo, base
  e duas livres.
- **Ajudas**: grade do chão, frustum, plano de foco, linhas de
  profundidade, volume do selecionado — **nenhuma aparece na exportação**.
- **Modo rascunho** que liga sozinho durante o gesto e volta ao soltar.
- **Rigs em um toque**: Órbita, Tripé, Dolly, Câmera na mão, Dolly zoom —
  todos geram keyframes reais e editáveis.
- **Comandos**: enquadrar tudo, enquadrar selecionado, focar no
  selecionado, salvar vista e **alinhar câmera à vista**.

## 7. Formas vetoriais

- **Tamanho ≠ Escala**: Tamanho é parâmetro do caminho e não engorda o
  traço; Escala engorda tudo junto.
- Arredondamento em px ou %, pontos fracionários na estrela.
- **Preenchimento** sólido ou gradiente, **traço** com junção, limite de
  emenda e opacidade própria.
- **Operadores**: Trim Paths (com offset) e Repeater.
- **Morph** entre caminhos, **importação de SVG**.
- Todo número é animável, com diamante e curva.

## 8. Legendas automáticas

- Transcrição **no aparelho** com Whisper — o áudio não sai do celular.
- Modelo baixado uma vez, validado por magic bytes antes de tocar no
  código nativo.
- Três modos: **frases**, **curtas** e **palavra por palavra** (karaokê).
- Cada fala vira uma legenda editável na timeline.

## 9. Máscaras, mescla e estilos

- **Máscaras** com modos: somar, subtrair, interseção, clarear, escurecer,
  diferença.
- **Track matte** (alfa e luminância) — a camada usada some da composição.
- **17 modos de mesclagem**.
- **Estilos de camada**: sombra, brilho, sobreposição de cor, sobreposição
  de gradiente e contorno.

## 10. Ofício — o que separa amador de profissional

- **Alinhar e distribuir** (por centro ou por vão), espaçar, sequenciar no
  tempo.
- **Rótulos de cor**, busca por camada, solo, bloqueio.
- **Guias, grade e áreas seguras** — nunca entram no render.
- **Encaixe** com feedback visual.
- **Nulos e rig de grade** (Grid): 200 objetos numa esfera é um slider.
- **Cinemática inversa** de duas ossadas (solução analítica).
- **Propriedades expostas**: monte um template e exponha só o que o
  outro pode mexer.
- **Contador numérico** com formato (moeda, porcentagem, separador).
- **Dados de CSV** ligados a propriedades.
- **Paleta do projeto** e estilos de texto reutilizáveis.

## 11. Exportar

**Vídeo (MP4)** — a composição inteira, renderizada quadro a quadro na
resolução do projeto e codificada em H.264 com o áudio mixado. Como usa a
mesma árvore do preview, sai exatamente o que se vê: texto animado,
formas, Cena 3D, efeitos — inclusive **efeito aplicado em cima de vídeo**.
Barra de progresso, cancelar e três qualidades.

**Lottie (.json)** — com **validador honesto**: mostra camada por camada o
que não sobrevive, antes de exportar. Tem um "modo compatível" que avisa
enquanto você monta.

**SVG animado**.

## 12. Motor de preview

- Compõe na **taxa da composição**, não na da tela.
- **Arquitetura de marchas**: cena parada reusa a árvore composta e o
  "compõe X/s" cai para perto de zero.
- **Relógio mestre ancorado na mídia**: a deriva é corrigida
  continuamente, não em blocos — foi o que acabou com a travada periódica.
- **Overlay de diagnóstico** com composições por segundo, camadas no
  frame e marcha atual.
- A UI **nunca cobre o preview**: os painéis são medidos contra o palco
  real.

## 13. App

- Tela inicial com projetos recentes, formatos rápidos, **aviso de beta** e
  **"O que há de novo"**.
- **Reportar erro ou sugestão** — vai direto para o criador, com modelo e
  versão do sistema já preenchidos.
- Perfil local, ajustes (salvar na galeria, vibração, limpar cache) e
  Sobre com licenças de código aberto.
- Visual próprio: fundo `#12151A`, verde-lima `#B8FF3D`, violeta `#7C62FF`.
- **Seletor de cor completo** (espectro, transparência e HEX) em texto,
  formas, elementos 3D, materiais, luzes e efeitos.

## 14. Segurança

- Permissões enxutas; nada de acesso amplo a arquivos.
- **HTTPS obrigatório** nos dois sistemas; a única conexão é o download do
  modelo de voz.
- FFmpeg recebe **lista de argumentos**, nunca linha de comando montada
  com aspas.
- Id de projeto é higienizado antes de virar nome de arquivo.
- Nenhuma chave no aplicativo; tudo em armazenamento privado.

---

## O que ainda não tem

Dito na cara, porque beta com lista de desejos escondida é pior que beta.

- **Importar Lottie** (só exporta).
- **LUT 3D**, deband, mapa de deslocamento e mesh warp por pino — precisam
  de shader de fragmento.
- **Gráfico de velocidade** e **casca de cebola**.
- **Exportar pacote de template**.
- **Miniatura, favoritos e recentes** no catálogo de efeitos.
- **Importar modelo .glb** na Cena 3D (por ora, só os sólidos nativos).
- Sombra projetada e ambiente por imagem na Cena 3D.
