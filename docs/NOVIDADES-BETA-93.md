# Aurea 1.1.8 (build 93) — o que mudou

Notas para mandar aos testadores. Sem promessa que não foi medida.

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

Se algo travar, fechar sozinho ou a nuvem não voltar igual ao arrastar o
cabeçote, é isso que interessa saber.
