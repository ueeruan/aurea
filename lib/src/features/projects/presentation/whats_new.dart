import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../editor/presentation/am/am_colors.dart';

/// Uma novidade da versao.
class NewsItem {
  const NewsItem(this.icon, this.title, this.body);

  final IconData icon;
  final String title;
  final String body;
}

/// TUDO que entrou nesta rodada, em linguagem de quem usa — nao em nome
/// de PR. A lista completa abre no toque.
const aureaNews = <NewsItem>[
  NewsItem(
    CupertinoIcons.photo,
    'Mídia importada com prévia ao vivo',
    'Fotos e vídeos, inclusive dentro de grupos, não passam mais pela '
        'captura automática que podia manter uma imagem antiga na prévia. '
        'A galeria ganhou álbuns, carregamento gradual e acesso às fotos '
        'selecionadas. Os arquivos importados ficam guardados no app.',
  ),
  NewsItem(
    CupertinoIcons.move,
    'Mais espaço para transformar e animar',
    'Prévia grande ao selecionar camadas, controles compactos nas laterais, '
        'área maior de movimento e pivô, duas réguas de escala e gráfico '
        'de curvas ampliado. Auto-key, navegação e reset ficam no menu •••.',
  ),
  NewsItem(
    CupertinoIcons.wand_stars,
    'Orientação da imagem nos efeitos',
    'Amostragem corrigida nos shaders de efeitos e de luz linear para a '
        'orientação das texturas do Flutter atual, evitando inversão vertical.',
  ),
  NewsItem(
    CupertinoIcons.cube,
    'A cena 3D abre no Estudio, e mais leve',
    'Tocar em Cena 3D no menu da camada leva DIRETO ao Estudio — antes o '
        'Estudio ficava atras de um botao de texto dentro da ficha de '
        'parametros, tres niveis abaixo. E os modelos empacotados foram '
        'reduzidos (astronauta, portal e arvore): o quadro custava 103 ms '
        'na Deriva e 288 ms no Monolito, agora custa 40 e 78. O cracha da '
        'ficha diz GPU ou CPU: se disser CPU, um aviso explica que este '
        'aparelho nao esta usando o motor 3D — e por isso engasga.',
  ),
  NewsItem(
    CupertinoIcons.sparkles,
    'Modelo DERIVA: o astronauta perdido, em tres tomadas',
    'Dezoito segundos de cinema numa cena 3D so: o plano aberto onde ele '
        'e pequeno, o retrato de perto com as estrelas fora de foco, e o '
        'plano em que a camera para de segui-lo e ele vira um ponto. O '
        'corte e de CAMERA, nao de projeto — por isso o corpo continua o '
        'mesmo giro quando a tomada troca. Luz de vacuo: uma fonte dura e '
        'a sombra sem preenchimento.',
  ),
  NewsItem(
    CupertinoIcons.circle_lefthalf_fill,
    'A vinheta nascia vermelha',
    'Efeitos que modulam a imagem com uma cor — vinheta, os dois brilhos, '
        'raios e ruido fractal — saiam todos no mesmo rosa de saida. Uma '
        'vinheta recem-adicionada pintava a borda do quadro de vermelho '
        'ate alguem achar o seletor. Agora cada um nasce na cor neutra da '
        'operacao: preto para escurecer, branco para multiplicar.',
  ),
  NewsItem(
    CupertinoIcons.moon_stars,
    'Modelo MONOLITO: o astronauta e a porta',
    'Floresta noturna na neblina, um bloco de concreto com um portal '
        'aceso em magenta, um astronauta flutuando em contraluz e a camera '
        'balancando por dezesseis segundos. Tres modelos importados de '
        'verdade (astronauta, portal voxel e arvore escaneada, ~110 mil '
        'faces ao todo) passam pelo MESMO importador que voce usa — que '
        'agora aceita FBX com textura em vez de recusar o arquivo. A porta '
        'e material emissivo E uma luz spot com sombra; as camadas de luz '
        'no cone dela sao o volume atravessando a neblina.',
  ),
  NewsItem(
    CupertinoIcons.cube_box_fill,
    'Motor 3D em GPU: luz de verdade',
    'A cena 3D passou a ser desenhada pela GPU (Metal no iPhone): '
        'profundidade real, materiais fisicos, luz por imagem, sombras '
        'suaves do sol, neblina e profundidade de campo — o mesmo motor '
        'na timeline (onde o 3D vira um quadro 2D) e no Estudio. Onde nao '
        'ha GPU, o pintor antigo continua. Primeira versao: as ajudas de '
        'cena (grade, frustum) ainda so aparecem no pintor antigo.',
  ),
  NewsItem(
    CupertinoIcons.tv,
    'Modelo COLINA: a TV no morro, em 3D de verdade',
    'Uma colina de grama com flores, a TV de tubo acesa no topo, rochas '
        'no primeiro plano, serra na neblina e a camera orbitando por '
        'baixo — tudo camada 3D editavel: terreno com textura, grama '
        'instanciada acesa em contraluz, profundidade de campo com bokeh '
        'na poeira, bloom na tela. O bokeh aprendeu duas regras: face com '
        'imagem e superficie grande nao viram bola.',
  ),
  NewsItem(
    CupertinoIcons.cube_box,
    'O 3D nao derruba mais o iPhone',
    'Neblina sobre faces com imagem, reflexo do chao e sombra de contato '
        'abriam uma camada de GPU por TRIANGULO. Com alguns milhares de '
        'faces o driver desistia, a tela apagava e o iOS reiniciava. Agora '
        'cada um custa uma camada por quadro, seja qual for o modelo. O '
        'contorno suavizado sai em modelos acima de 4 mil faces, e a '
        'largura do contorno de estilo tem teto de 100 px (a dilatacao '
        'na GPU crescia junto com ela).',
  ),
  NewsItem(
    CupertinoIcons.wand_stars,
    'FX V2: processamento por pixel',
    '30 efeitos ganharam kernels novos: cor, ruido, distorcao, blur e glitch. '
        'Os dois glows ganharam extracao por brilho e resposta tonal. '
        'Os outros operadores continuam em seus compositores especializados. '
        'O processamento e SDR; nao e uma equivalencia certificada ao After Effects.',
  ),
  NewsItem(
    CupertinoIcons.book,
    'Como usar o AUREA, dentro do app',
    'Abra Sobre > Como usar o AUREA, ou toque em ? no painel de efeitos. '
        'Guia rapido para criar, animar, combinar efeitos e exportar, '
        'com busca e explicacao dos 43 efeitos. Funciona sem internet.',
  ),
  NewsItem(
    CupertinoIcons.square_grid_2x2,
    'De volta ao nucleo',
    'O app abre no NUCLEO: projeto, video, imagem e audio, cortes com '
        'ripple, juntar, marcadores (tocando no ritmo, com encaixe de '
        'clipe e de cabecote), keyframes em posicao, escala, rotacao e '
        'opacidade, loop e exportar. Todo o estudio — efeitos, texto, '
        'formas, 3D, particulas e o resto — continua no app, atras de '
        'um interruptor: Ajustes > Modo > Estudio completo. Cada '
        'ferramenta volta ao nucleo quando passar na propria prova.',
  ),
  NewsItem(
    CupertinoIcons.cube_box_fill,
    'Mundo 3D de verdade: um solido entra no outro',
    'Elementos 3D vizinhos na pilha agora viram uma cena so, com a '
        'profundidade compartilhada: um cubo atravessa a esfera, o vidro '
        'deixa ver o que esta atras. Entraram os materiais brilhante (o '
        'degrade roxo/azul/rosa das referencias), vidro, metal e fosco, '
        'sombreamento liso, e modelos importados em OBJ e FBX (com aviso '
        'quando o modelo e pesado demais para celular fraco).',
  ),
  NewsItem(
    CupertinoIcons.sparkles,
    'Particulas do jeito do Particular',
    'Emissor em caixa, ponto, esfera ou anel; saida em cone, todas as '
        'direcoes ou para fora; vento, resistencia do ar e turbulencia '
        '3D; tamanho, opacidade e cor ao longo da vida; formas (esfera, '
        'estrela, risco, nuvem, quadrado, anel), giro, brilho e rastro. '
        'Tudo em simulacao pura: arrastar a regua da o mesmo quadro.',
  ),
  NewsItem(
    CupertinoIcons.textformat_abc,
    'Texto 3D, Liquid Glass, Correcoes, Flicker e degrade de 4 cores',
    'O animador de texto ganhou Rotacao X, Rotacao Y e Posicao Z por '
        'letra, palavra ou frase. Efeitos novos: Liquid Glass (vidro com '
        'desfoque, lente, brilho na borda e sombra), Correcoes (exposicao, '
        'contraste, altas, sombras, temperatura, saturacao, gama), Flicker '
        'e Gradiente de 4 cores.',
  ),
  NewsItem(
    CupertinoIcons.cube,
    'Extrude 3D em qualquer camada',
    'Texto, forma, imagem ou grupo ganham espessura: ligue o 3D da '
        'camada, gire em X ou Y e a lateral aparece. Botoes novos na '
        'fileira da camada: Ligar 3D, Motion blur (o de verdade, por '
        'amostras) e Extrude 3D.',
  ),
  NewsItem(
    CupertinoIcons.house,
    'Tela inicial nova e preset do Alight Motion',
    'A inicial mostra a miniatura real de cada projeto, os modelos com '
        'um quadro renderizado e os formatos como pilulas. Da para importar '
        'um preset do Alight Motion em XML: o app le o que reconhece e diz '
        'o que ficou de fora. Marcadores no transporte (avancar e voltar '
        'caem neles), timeline mais compacta e abas alinhadas.',
  ),
  NewsItem(
    CupertinoIcons.eye,
    'O preview voltou a mostrar video',
    'Eram tres defeitos somados. Abrir um projeto salvo nunca montava '
        'os tocadores, entao a tela ficava preta ate alguem apertar '
        'play. O cache da composicao era compartilhado entre o preview, '
        'a exportacao e a casca de cebola, e as vistas mostravam o '
        'projeto uma da outra. E o passe de dithering, que rasteriza a '
        'composicao para rodar o shader, apagava a textura do video — '
        'ela nao entra nesse tipo de foto e virava um buraco preto. '
        'Agora, com video na cena, o dithering nao acontece: perder ele '
        'e pequeno, perder o video e o app nao funcionar.',
  ),
  NewsItem(
    CupertinoIcons.videocam,
    'Camera 3D e Cena 3D animam com nulo',
    'Parentear a camera a um objeto nulo simplesmente nao funcionava, e '
        'com isso TODOS os rigs estavam quebrados — orbita, tripe, '
        'dolly, camera na mao. Agora funcionam os tres niveis: nulo da '
        'composicao movendo a camera, nulo movendo a Cena 3D inteira, e '
        'nulos DENTRO da cena, que e o que faltava para rigging la '
        'dentro. A camera herda posicao e rotacao do pai, mas nunca '
        'escala — herdar escala era o que fazia o enquadramento '
        'explodir. Tem tambem rig de orbita em um toque, com keyframes '
        'de verdade, editaveis.',
  ),
  NewsItem(
    CupertinoIcons.scissors,
    'Corta, apaga e junta de volta',
    'Agora a linha do tempo e MAGNETICA por padrao: apagar fecha o '
        'buraco e puxa o que vinha depois, com interruptor visivel para '
        'desligar quando outra trilha precisa continuar no lugar. E '
        'entrou o que faltava: JUNTAR dois pedacos do mesmo arquivo de '
        'volta num clipe so. A marca verde na juncao mostra onde da, e '
        'um toque nela desfaz o corte.',
  ),
  NewsItem(
    CupertinoIcons.waveform,
    'Forma de onda de verdade, e scrub de audio',
    'A forma de onda passou a guardar seis niveis de detalhe de uma '
        'vez: ampliar troca de nivel em vez de recalcular, entao a linha '
        'do tempo nao engasga mais em audio longo. Cada pedaco guarda '
        'pico E volume, entao a onda mostra o transiente e o corpo — a '
        'leitura que qualquer editor de audio da. E arrastar a regua '
        'agora TOCA o som: achar a silaba de ouvido e muito mais rapido '
        'do que procurar no olho.',
  ),
  NewsItem(
    CupertinoIcons.hand_raised,
    'Estabilizar, reenquadrar e pulsar na batida',
    'Tres coisas que antes eram keyframe na mao. Estabilizar tira o '
        'tremor (e amplia junto, senao aparece borda preta). Reenquadrar '
        'sozinho segue o assunto ao virar vertical, em vez de cortar no '
        'centro e decepar a cabeca de quem esta na lateral. E pulsar na '
        'batida poe a camada crescendo em cada ataque da musica — feito '
        'na mao seriam centenas de keyframes.',
  ),
  NewsItem(
    CupertinoIcons.sparkles,
    'Os efeitos ficaram certos por dentro',
    'Glow e desfoque estavam somando luz no espaco errado, e e por isso '
        'que sempre saiam acinzentados e com halo escuro na borda. Agora '
        'a conta acontece em espaco linear, que e onde luz soma. O raio '
        'tambem virou fracao do lado da tela: o mesmo numero da o mesmo '
        'tamanho em 720p e em 4K, o que antes nao acontecia.',
  ),
  NewsItem(
    CupertinoIcons.wand_stars,
    'Seis efeitos com a ficha completa',
    'Deep Glow (piramide com energia conservada, limiar suave, '
        'tonemap), Shake (tres estilos, aleatorio e onda separados por '
        'eixo, RGB com fase propria), Motion Tile (com a semantica do '
        'After Effects, em % da camada), Motion Blur da composicao (que '
        'existia so no papel e agora acontece), Pixel Sorter e Blob '
        'Tracker com identidade estavel e analise gravada.',
  ),
  NewsItem(
    CupertinoIcons.textformat_abc,
    'Nomes dos efeitos em ingles',
    'Os 38 efeitos passaram a ter o nome que o resto do mundo usa — '
        'Gaussian Blur, Deep Glow, RGB Split, Shake. A busca continua '
        'aceitando portugues: digitar "desfoque" acha Gaussian Blur. '
        'Nenhum projeto ou preset antigo quebrou.',
  ),
  NewsItem(
    CupertinoIcons.cube_box,
    'Modelo 3D, extrusao e template',
    'Da para trazer um modelo .glb pronto para dentro da cena, '
        'transformar uma forma plana em volume com espessura, e '
        'empacotar um projeto como TEMPLATE para abrir noutro aparelho '
        'com os campos preenchiveis.',
  ),
  NewsItem(
    CupertinoIcons.rectangle_grid_2x2,
    'Painel na altura do polegar',
    'Todo painel de parametro agora tem tres alturas pela alca, botao '
        'de voltar no canto inferior esquerdo (onde o polegar alcanca), '
        'deslizar para fechar e atalho para os quatro ultimos paineis. '
        'A grade do catalogo parou de travar: as miniaturas fora da tela '
        'nao existem mais.',
  ),
  NewsItem(
    CupertinoIcons.film,
    'Exportar video (MP4) — agora existe de verdade',
    'Antes o app so exportava Lottie e SVG, e o "exportar video" nao '
        'compunha nada. Agora a composicao inteira e desenhada quadro a '
        'quadro na resolucao do projeto e codificada em MP4 (H.264) com '
        'o audio mixado. Como e a MESMA arvore do preview, sai exatamente '
        'o que voce ve: texto animado, formas, Cena 3D, efeitos e '
        'inclusive efeito aplicado em cima de video. Tem barra de '
        'progresso, cancelar e escolha de qualidade.',
  ),
  NewsItem(
    CupertinoIcons.exclamationmark_triangle,
    'Versao beta para testes',
    'Esta versao serve para testar e achar problema. Pode travar, dar '
        'erro ou perder alteracao nao salva — salve o projeto sempre que '
        'terminar algo importante. Achou um bug ou quer sugerir uma '
        'ferramenta ou efeito? Tem um botao de Reportar em Sobre, e o '
        'relato chega direto no criador.',
  ),
  NewsItem(
    CupertinoIcons.textformat,
    'Animacao de texto refeita',
    'O animador antigo saiu. No lugar entrou o modelo do Alight '
        'Motion: escolha ENTRADA, ENFASE ou SAIDA e toque numa animacao '
        'de uma grade que mostra cada uma se mexendo. Sao 34 animacoes, '
        'entre elas quicar por letra e por palavra, aparecer em '
        'desfoque, maquina de escrever, onda, tremilique, glitch, '
        'piscar e ambientacao viral. Seis controles: unidade (letra, '
        'palavra, linha ou tudo), inicio, duracao, atraso, ordem (do '
        'inicio, do fim, do centro, das bordas ou aleatoria) e curva.',
  ),
  NewsItem(
    CupertinoIcons.bolt_horizontal,
    'Mola de verdade, com overshoot',
    'A curva "Mola" usa a mesma conta da extensao MultiTools do After '
        'Effects — amplitude, frequencia e decaimento — entao a letra '
        'passa do alvo e volta, em vez de so chegar. O modelo do AE '
        '(animador cru com seletor na mao) continua ali, em Avancado: '
        'as animacoes do catalogo sao compiladas para ele.',
  ),
  NewsItem(
    CupertinoIcons.paintbrush,
    'Qualquer cor, em qualquer objeto',
    'Entrou um seletor de cor completo: espectro de matiz, area de '
        'saturacao e brilho, transparencia e campo HEX. Vale para '
        'texto, formas, elementos 3D, materiais e luzes da Cena 3D e '
        'para a cor dos efeitos. As paletas rapidas continuam para o '
        'caso comum.',
  ),
  NewsItem(
    CupertinoIcons.wand_stars,
    '12 efeitos novos',
    'Ordenar pixels, Deslocar turbulento, Entortar, CC Semear, CC '
        'Split, Mosaico de movimento, Mascara de nitidez, VHS, Filme '
        'danificado, Glitchify, Rastreador de blobs e Remapear tempo. '
        'Os que redistribuem pixel (turbulento, entortar, ordenar, '
        'semear) rodam sobre a camada rasterizada e deformada em malha, '
        'entao sao deformacao de verdade, nao filtro de cor.',
  ),
  NewsItem(
    CupertinoIcons.time,
    'Remapear tempo, igual ao do AE',
    'Em vez de mexer na velocidade, voce anima QUAL instante da camada '
        'aparece agora. Congelar um quadro, voltar de tras para frente '
        'ou fazer rampa de velocidade vira keyframe de tempo. Os '
        'keyframes de transformacao continuam lendo o tempo da '
        'composicao, como no After Effects.',
  ),
  NewsItem(
    CupertinoIcons.cube_box,
    'Cena 3D e camera',
    'Uma camada Cena 3D com renderizador proprio: dois cubos que se '
        'cruzam mostram a intersecao certa, coisa que camada 3D nao '
        'faz. Camera de um ou dois nos, lentes de 15 a 200 mm com '
        'focal, angulo e zoom ligados, profundidade de campo com '
        'formato de iris e ganho de realce (bokeh de verdade), estudio '
        'com orbita, mini-vista, eixos tocaveis e rigs de camera em um '
        'toque.',
  ),
  NewsItem(
    CupertinoIcons.checkmark_seal,
    'Aviso de exclusao que sai da tela',
    'O aviso "Camada excluida / Desfazer" ficava preso quando o preview '
        'reconstruia a tela no meio da animacao dele. Agora o '
        'fechamento tem relogio proprio e nao depende disso.',
  ),
  NewsItem(
    CupertinoIcons.wand_stars,
    'Catalogo de efeitos com busca',
    'Agora sao 26 efeitos organizados por categoria, com busca que '
        'entende sinonimo: procure "bloom" e acha Glow, "pixelate" acha '
        'Mosaico, "shake" acha Tremor. Novos: Niveis, Curvas, Vibracao '
        'com protecao de tom de pele, Balanco de branco, Rodas de cor, '
        'Unmult (tira o fundo preto de fogo e fumaca), Vinheta, '
        'Desfoque direcional e radial, Raios de luz, Mosaico, Grao de '
        'filme, Ruido fractal, Dano digital, Zoom warp e Posterizar.',
  ),
  NewsItem(
    CupertinoIcons.square_stack_3d_down_right,
    'Presets de efeito',
    'Salve uma pilha inteira e reaplique quando quiser. Os keyframes '
        'sao gravados relativos ao inicio, entao aplicar aos 12 s '
        'funciona; e distancias sao normalizadas, entao um preset feito '
        'em 9:16 nao sai errado em 16:9. Vem com 6 presets prontos.',
  ),
  NewsItem(
    CupertinoIcons.slider_horizontal_3,
    'Controles que faltavam',
    'Parametro de escolha virou chip, semente ganhou botao de sortear, '
        'e ha liga/desliga. Antes so existia numero — efeito que '
        'precisava de opcao ficava na tela sem funcionar.',
  ),
  NewsItem(
    CupertinoIcons.repeat,
    'Loop de keyframes e assar',
    'Dois keyframes e um Ciclo ja sao animacao infinita: Ciclo, '
        'Vai-e-volta, Deslocado (esteira) e Continuar. E "assar em '
        'keyframes" converte o Tremor em keyframes reais, para voce '
        'ajustar quadro a quadro.',
  ),
  NewsItem(
    CupertinoIcons.square_grid_3x2,
    'Alinhar e distribuir',
    'Alinhamento exato ao pixel pelas bordas reais das camadas, '
        'distribuicao por centro OU por vao igual (sao diferentes), e '
        'espacamento exato em px. O encaixe ao arrastar agora gruda nas '
        'bordas das outras camadas e nas guias.',
  ),
  NewsItem(
    CupertinoIcons.textformat,
    'Animacao de texto por receita',
    '12 receitas prontas (Apple, Maquina de escrever, Cascata, Do '
        'centro...) com um visualizador que mostra o escalonamento. '
        'Voce pensa "cada palavra 40 ms depois da anterior" e o app '
        'monta o rig sozinho.',
  ),
  NewsItem(
    CupertinoIcons.square_on_circle,
    'Formas com Tamanho de verdade',
    'Tamanho agora e parametro da geometria: crescer a forma nao '
        'engorda o traco. Pontas fracionarias animam triangulo virando '
        'quadrado, e o arredondamento vai de -100 a 200 para virar '
        'flor. Tudo animavel.',
  ),
  NewsItem(
    CupertinoIcons.cube,
    'Elementos 3D',
    'Nove solidos que giram de verdade no espaco — cubo, piramide, '
        'cone, esfera, cilindro, prisma, diamante, anel e estrela — e '
        'seguem um nulo 3D como qualquer camada.',
  ),
  NewsItem(
    CupertinoIcons.captions_bubble,
    'Legendas com configuracao',
    'Escolha entre frases, blocos curtos ou palavra por palavra antes '
        'de transcrever, e corrija o texto errado direto na lista de '
        'legendas — o cue corrigido nao e sobrescrito numa nova '
        'transcricao. Importar audio tambem chegou.',
  ),
  NewsItem(
    CupertinoIcons.sparkles,
    'Estilos de camada e paleta',
    'Sombra projetada, sombra interna, brilho externo, contorno e '
        'sobreposicao de cor ou gradiente. Mais paleta do projeto: '
        'trocar uma cor nomeada muda todas as camadas ligadas a ela.',
  ),
  NewsItem(
    CupertinoIcons.rectangle_expand_vertical,
    'Layout responsivo',
    'Uma forma pode abracar um texto e se redimensionar sozinha quando '
        'o texto muda, com ancora escolhendo qual lado fica parado. '
        'Grupos empilham os filhos automaticamente.',
  ),
  NewsItem(
    CupertinoIcons.share,
    'Exportar Lottie e SVG',
    'Exporte a cena como Lottie (.json) para apps e sites, com um '
        'validador que avisa antes quais camadas nao sobrevivem e por '
        'que. Tambem exporta SVG animado.',
  ),
  NewsItem(
    CupertinoIcons.speedometer,
    'Preview mais liso',
    'A composicao so recalcula quando algo muda de verdade, o corte '
        'agora decupa certo, e a travada periodica do audio acabou: o '
        'relogio segue a midia continuamente em vez de corrigir em '
        'bloco. A engrenagem mostra a marcha e os numeros.',
  ),
  NewsItem(
    CupertinoIcons.tag,
    'Organizacao',
    'Rotulos coloridos, solo, camadas timidas, bloqueio, busca por '
        'nome/tipo/"tem keyframe" e renomear em lote.',
  ),
];

/// Cartao "O que ha de novo" na tela inicial.
class WhatsNewCard extends StatelessWidget {
  const WhatsNewCard({super.key});

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: () => showWhatsNewSheet(context),
      child: Container(
        padding: const EdgeInsets.fromLTRB(16, 14, 14, 14),
        decoration: BoxDecoration(
          borderRadius: BorderRadius.circular(18),
          gradient: const LinearGradient(
            colors: [Color(0xFF1B2130), Color(0xFF16281B)],
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
          ),
          border: Border.all(color: AmColors.hairline),
        ),
        child: Row(
          children: [
            Container(
              width: 40,
              height: 40,
              decoration: BoxDecoration(
                color: AmColors.accent.withValues(alpha: 0.16),
                borderRadius: BorderRadius.circular(12),
              ),
              child: const Icon(
                CupertinoIcons.sparkles,
                color: AmColors.accent,
                size: 22,
              ),
            ),
            const SizedBox(width: 12),
            const Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'O que ha de novo',
                    style: TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.w700,
                      color: AmColors.text,
                    ),
                  ),
                  SizedBox(height: 2),
                  Text(
                    'Exportar MP4, animacao de texto refeita, Cena 3D, 12 '
                    'efeitos novos e seletor de qualquer cor.',
                    style: TextStyle(fontSize: 12, color: AmColors.muted),
                  ),
                ],
              ),
            ),
            const Icon(
              CupertinoIcons.chevron_right,
              size: 18,
              color: AmColors.muted,
            ),
          ],
        ),
      ),
    );
  }
}

Future<void> showWhatsNewSheet(BuildContext context) {
  return showModalBottomSheet<void>(
    context: context,
    backgroundColor: AmColors.panel,
    isScrollControlled: true,
    constraints: BoxConstraints(
      maxHeight: MediaQuery.of(context).size.height * 0.85,
    ),
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
    ),
    builder: (sheetContext) => SafeArea(
      child: ListView(
        padding: const EdgeInsets.fromLTRB(20, 16, 20, 24),
        children: [
          Center(
            child: Container(
              width: 34,
              height: 4,
              decoration: BoxDecoration(
                color: Colors.white24,
                borderRadius: BorderRadius.circular(2),
              ),
            ),
          ),
          const SizedBox(height: 16),
          const Text(
            'O que ha de novo',
            style: TextStyle(
              fontSize: 24,
              fontWeight: FontWeight.w700,
              color: AmColors.text,
            ),
          ),
          const SizedBox(height: 4),
          Text(
            '${aureaNews.length} novidades nesta versao',
            style: const TextStyle(fontSize: 13, color: AmColors.muted),
          ),
          const SizedBox(height: 18),
          for (final item in aureaNews)
            Padding(
              padding: const EdgeInsets.only(bottom: 18),
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Icon(item.icon, size: 20, color: AmColors.accent),
                  const SizedBox(width: 12),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          item.title,
                          style: const TextStyle(
                            fontSize: 15,
                            fontWeight: FontWeight.w600,
                            color: AmColors.text,
                          ),
                        ),
                        const SizedBox(height: 3),
                        Text(
                          item.body,
                          style: const TextStyle(
                            fontSize: 12.5,
                            height: 1.45,
                            color: AmColors.muted,
                          ),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
        ],
      ),
    ),
  );
}
