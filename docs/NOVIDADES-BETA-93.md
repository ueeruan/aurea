# Aurea 1.1.8 (build 93) — o que mudou

Notas para mandar aos testadores. Sem promessa que não foi medida.

---

## Atualizar pelo próprio app

Quando sair versão nova, aparece uma faixa no alto da Início com o botão
**Atualizar**: o app baixa, confere o arquivo e abre o instalador — sem
procurar APK, sem entrar em grupo, sem pedir link. Esta é a primeira
versão que chega por esse caminho.

**Ela instala por cima da que você tem**, sem desinstalar e sem perder
projeto: o arquivo é assinado com a mesma chave dos betas anteriores. Se
o Android pedir a permissão de "instalar aplicativo de fora da loja", o
app leva direto à tela do ajuste.

---

## 1. Gizmo 3D: mover e girar direto na tela

Camada com o **3D ligado** agora mostra os **três eixos** no palco:

* **X vermelho**, **Y verde**, **Z azul** — as cores de sempre das
  ferramentas 3D;
* arrastar um eixo **move** a camada naquele eixo, e só nele;
* o **anel** em volta de cada eixo **gira** a camada naquele eixo;
* o **Z é profundidade de verdade**: arrastar o azul muda a profundidade,
  e a camada encolhe para o ponto de fuga como o resto da cena.

Detalhes que você vai notar:

* a sensibilidade é **1:1 com o dedo** — o objeto anda o que o dedo anda,
  em qualquer ângulo de câmera;
* quando um eixo está apontado para a câmera (é um ponto na tela), a alça
  **recusa o gesto** em vez de dar um salto; o eixo continua desenhado,
  apagado, para você saber que ele existe;
* camada **bloqueada**: o gizmo fica apagado e não arrasta nada.

## 2. Objetos 3D com material de metal de verdade

A ficha do objeto 3D trocou cinco botões ("Sólido", "Brilhante", "Vidro",
"Metal", "Fosco") por um **acabamento** que é o mesmo sistema de materiais
do texto 3D:

**Cor lisa · Ouro · Cromo · Aço escovado · Branco fosco**

O que isso muda na tela:

* o **ambiente** agora responde pelo metal: um cromo no estúdio e um cromo
  na noite **não** saem iguais, porque quem dá a cor do metal é o reflexo
  do que está em volta;
* o **brilho tem tamanho**: o cromo tem um ponto de luz apertado, o aço
  escovado um espalhado — a rugosidade não é só um número guardado;
* um cubo de cromo e uma letra de cromo agora saem **do mesmo material**.

"Cor lisa" continua exatamente o que era: um projeto antigo abre igual.

## 3. Quatro objetos novos, e os modelos importados saíram

Entraram quatro objetos feitos para o Aurea:

**Lente · Anel de luz · Diafragma · Placa**

Saíram os três modelos que vinham dentro do app como provisórios (o
astronauta, o portal e a árvore escaneados). Eles eram arquivos de
terceiros viajando dentro do APK, e o "· provisório" do nome era literal.
O explorador do Abismo, a floresta e o bloco já eram feitos em código — os
arquivos eram o primeiro plano e o portal.

**Você vai notar:** o **Campo 3D** e o **Monolito** abrem **direto**, sem
a espera de "Preparando o campo 3D". Essa espera existia só para copiar um
arquivo do bundle para o disco antes de abrir.

## 4. Motor de partículas novo

As partículas ganharam um motor próprio, escrito em C++. O que isso quer
dizer na prática:

* **arrastar o cabeçote para trás devolve exatamente o mesmo quadro** — a
  simulação não acumula nada entre quadros, então o scrub é exato;
* o **mesmo motor** desenha a prévia e a exportação (antes eram duas
  contas iguais em lugares diferentes, que podiam discordar);
* a nuvem entra na mesma conversa da qualidade: a **resolução da prévia**
  (Full / 1/2 / 1/4) também limita quantas partículas o aparelho desenha.

**A ficha de Partículas ficou maior** — e de propósito:

* **10 receitas prontas**: Fogo, Faíscas, Neve, Chuva, Estrelas, Fumaça,
  Magia, Confete, Poeira, Explosão;
* campos novos: **taxa de nascimento**, vento em Z, **atração e repulsão**
  (a nuvem puxa para um ponto ou foge dele), giro próprio, cor final;
* o que já existia continua: emissor caixa/ponto/esfera/linha/anel,
  turbulência, rastro, faíscas, cintilar.


## 5. Quatro efeitos novos, com a conta lida do After Effects

Os quatro foram feitos **medindo o resultado no After Effects**, e não
copiando a aparência de olho. O que a medição não deixou medir está dito
na ficha de cada um.

**Varredura de luz** (`CC Light Sweep`) — um facho reto de luz passando
pela camada, do tipo reflexo correndo num logo. A luz **soma** um valor:
a mesma faixa clareia um cinza escuro e um claro na mesma quantidade. O
perfil da queda é quadrático, e a largura da faixa é 2× o que você pede
em Largura.

**Dobra de página** (`CC Page Turn`) — levanta um lado da camada e enrola
num cilindro, como quem vira a folha de um livro. O lado de lá do vinco
fica intacto; o de cá sobe, curva e **comprime** o conteúdo, projetando de
volta por cima. Passando de 90° aparece o **verso** da folha, na cor de
papel. A Direção da luz é o que dá o reflexo correndo pela dobra.

**Sombra projetada** (`ADBE Drop Shadow`) — a sombra da **forma** da
camada, e não da caixa dela: um recorte vazado projeta sombra vazada, e
meia transparência projeta meia sombra. Tem os três presets do After
(Suave, Longa, Difusa) e "Só a sombra".

**Sombra longa** — a silhueta **esticada** numa direção até um
comprimento, chapada numa cor só: o efeito de capa de aplicativo, cartaz
e logo. **Este não existe no After Effects do seu AE** — é feito aqui, e
a ficha diz isso. O Ângulo é o mesmo da Sombra projetada (135° põe a luz
em cima à esquerda), e a Queda faz a sombra desvanecer até a ponta em vez
de acabar de repente.

**RGB no tempo** — cada canal de cor mostra a camada num instante
diferente: vermelho adiantado, azul atrasado, verde no lugar. Dá a
separação de cor que corre nas bordas do movimento. **Aqui vale um aviso
honesto:** o efeito do After (`S_TimeWarpRGB`) existe, e os parâmetros
dele foram lidos de lá — mas o plugin não renderiza na bancada, então a
direção do deslocamento (positivo = mais para frente) é escolha nossa e
está escrita na ficha. Custa três montagens da camada por quadro: é
efeito de acabamento, não de rascunho.

## 6. Dois efeitos que existiam e não apareciam

**Fatias no tempo** (`S_TimeSlice`) e **Desfoque de movimento**
(`CC Force Motion Blur`) estavam prontos por dentro desde 14/09 e **não
apareciam na galeria** — faltava a ficha, e sem ficha não havia como pedir
o efeito nem mexer num parâmetro. Agora aparecem, com os nomes e os
valores de fábrica lidos do plugin.

* **Fatias no tempo**: o quadro vira faixas paralelas e cada faixa mostra
  a camada num instante diferente — a escada do plugin, e também linear,
  do centro, aleatória e onda, que são nossas. Tem 8 receitas prontas.
* **Desfoque de movimento**: borra o que se move montando a camada várias
  vezes dentro da janela de exposição. Como cada amostra é a camada no seu
  instante, vale também para o que se move **dentro** do quadro (um carro
  passando no vídeo), que é onde o desfoque da composição não pega. A
  **Fase** decide para onde o arrasto cai: 0 arrasta para frente, −90
  centra no quadro, −180 pega o que já passou.

## 7. O PNG que não importava, e o "fundo branco"

Eram o **mesmo defeito**, e o segundo escondia o primeiro.

A conferência que provava o arquivo importado pedia uma miniatura de 1×1 —
e um pedido de 1×1 não prova que o arquivo abre. Em vários PNGs ela não
devolvia quadro nenhum, o arquivo era apagado e a importação morria
inteira. Agora a prova é **decodificar o primeiro quadro de verdade**.

E o "fundo branco" era o desenho da falha: quando a imagem não abre, o
palco desenhava uma caixa clara de 400×300. Quem via um PNG que não abria
descrevia exatamente isso — e o defeito de verdade ficava atrás de um
sintoma que parecia outro. Agora a caixa diz o que houve, em fundo escuro.

## 8. O Motion Tile agora cobre o quadro

Relato de testador, com print: com o Motion Tile aplicado e o **zoom da
camada reduzido**, a imagem não se repetia como no After Effects.

A matemática do ladrilho estava certa; o problema era **onde** a repetição
acontecia. Efeito age na fonte e o transform vem depois (a mesma ordem do
After), então a região ladrilhada era a caixa da camada: com a camada em
50%, a parede de ladrilhos encolhia junto e o quadro ficava com a moldura
vazia em volta. Agora a região é calculada para cobrir a **composição**
depois da escala — o suficiente e nem um pixel a mais.

## 9. A música parava o play

Relato de testador: "quando aparece a onda da música, começa a dar lag".

A causa não era o áudio: era **a onda**. Dando play, a timeline repinta a
faixa visível a cada quadro, e a onda reconstruía três listas e dois
caminhos com milhares de pontos **por clipe, por quadro**, para desenhar
exatamente a mesma figura — porque rolar não muda nada na onda. Agora ela
é um desenho pronto, gravado uma vez; o play não a reconstrói mais.

## 10. O texto em todos os idiomas

O relato era do árabe, mas o caminho de desenho é o mesmo para coreano,
japonês, chinês, hindi, hebraico, russo e emoji — e cada um tem a
armadilha dele. Foi conferido em **12 escritas**: a quebra por unidade
reconstrói o texto caractere a caractere, a direção sai da escrita (RTL no
árabe e no hebraico) e a primeira letra forte decide em texto misturado.

Do lado da tradução, o relatório anterior exagerava: dizia "836 literais
fora da tradução" contando comentário e contando texto que **já estava
traduzido**. O número real são duas listas diferentes — 366 que só
precisavam ser envolvidos (tradução já existia) e 463 que precisam de
tradução nova. Os 366 já foram.

---

## O que ainda NÃO está pronto

* **Nada foi medido num aparelho.** Tudo o que está aqui foi verificado
  nos testes e na bancada do computador. Não há número de FPS, de memória
  ou de temperatura de celular — e eu não vou inventar um.
* **As partículas não desenham por GPU.** Elas são calculadas em C++ e
  desenhadas em lote, que é o formato que uma chamada de GPU consome — mas
  quem desenha hoje ainda é o Flutter. O backend Vulkan desenhando
  partículas é o passo seguinte.
* **O iPhone ainda não tem caminho próprio de GPU.** O mesmo código roda,
  mas não foi compilado nem testado em Metal.
* **A FASE 3 do motor C++ (texturas de vídeo e texto) não começou.** O
  núcleo ainda compõe caixas e cor sólida.

---

## Como testar

1. **Gizmo** — abra um projeto, selecione uma imagem, ligue o **3D** na
   ficha dela. Os três eixos aparecem no centro. Arraste o azul e veja a
   imagem recuar para o meio. Gire o anel do vermelho.
2. **Material** — adicione um objeto 3D pelo menu **+**, abra a ficha e
   troque o acabamento entre Ouro e Cromo. Ponha dois objetos lado a lado
   com acabamentos diferentes.
3. **Partículas** — abra a ficha de Partículas e toque nas receitas. A
   nuvem troca na hora. Arraste o cabeçote para trás e para frente: o
   quadro tem de voltar idêntico.
4. **Campo 3D e Monolito** — abra os dois pelo menu de projetos: eles
   devem abrir sem a espera de "Preparando".

5. **Efeitos novos** — abra a galeria de efeitos e procure por
   "Varredura de luz" (aba Luz), "Dobra de página" (Distorcer), "Sombra
   longa" e "Sombra projetada" (Perspectiva) e "RGB no tempo" (Tempo).
   Aplique cada um num clipe e mexa nos presets.
6. **As duas fichas que faltavam** — procure "Fatias no tempo" e
   "Desfoque de movimento". Elas não apareciam na galeria antes; agora
   aparecem com os presets.
7. **Atualização pelo app** — quando a faixa aparecer, toque em
   **Atualizar** e conte se o instalador abriu e se o app abriu igual
   depois, **com os projetos no lugar**.

Se algo travar, fechar sozinho, a nuvem não voltar igual ao arrastar o
cabeçote, ou a atualização falhar no meio, é isso que interessa saber — e
nesse último caso me diga em que tela parou.

* **Dos efeitos novos, nada foi medido em celular** — a conta foi conferida
  contra o render do After Effects no computador, e o custo por quadro é
  estimado pela quantidade de montagens da camada (o RGB no tempo faz três,
  o desfoque de movimento faz de 2 a 64 amostras). Se algum pesar no seu
  aparelho, me diga qual e em que situação.
* **O `S_TimeWarpRGB` do plugin não renderiza na bancada do AE** (o render
  sai idêntico com o efeito ligado e desligado). Por isso a direção do
  deslocamento no RGB no tempo é escolha nossa, e está escrita na ficha do
  efeito.
