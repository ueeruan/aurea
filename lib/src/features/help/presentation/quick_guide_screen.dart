import 'package:flutter/material.dart';

import '../../editor/domain/effect.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

/// Offline, shipped in the executable: no network or external PDF viewer.
class QuickGuideScreen extends StatefulWidget {
  const QuickGuideScreen({super.key, this.initialQuery = ''});
  final String initialQuery;

  @override
  State<QuickGuideScreen> createState() => _QuickGuideScreenState();
}

class _QuickGuideScreenState extends State<QuickGuideScreen> {
  late final TextEditingController _search;
  @override
  void initState() {
    super.initState();
    _search = TextEditingController(text: widget.initialQuery);
  }

  @override
  void dispose() {
    _search.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final query = _search.text.trim().toLowerCase();
    final entries = effectSpecs.entries
        .where(
          (entry) =>
              !efeitosInternos.contains(entry.key) &&
              '${entry.value.name} ${entry.value.category} ${entry.value.synonyms.join(' ')} ${effectHelp(entry.key)}'
                  .toLowerCase()
                  .contains(query),
        )
        .toList();
    return Scaffold(
      appBar: AppBar(title: const AppText('Como usar o AUREA')),
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.fromLTRB(20, 12, 20, 40),
          children: [
            AppText(
              'Do zero ao primeiro motion',
              style: Theme.of(context).textTheme.titleLarge,
            ),
            const SizedBox(height: 8),
            const AppText('Guia rápido • disponível sem internet'),
            const SizedBox(height: 16),
            for (final step in quickStartSteps)
              Padding(
                padding: const EdgeInsets.only(bottom: 16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    AppText(
                      step.$1,
                      style: Theme.of(context).textTheme.titleMedium,
                    ),
                    const SizedBox(height: 4),
                    AppText(step.$2),
                  ],
                ),
              ),
            const ExpansionTile(
              tilePadding: EdgeInsets.zero,
              title: AppText('Receita: texto com brilho e entrada suave'),
              children: [
                Padding(
                  padding: EdgeInsets.only(bottom: 16),
                  child: AppText(
                    'Adicione um texto. Em Mover e transformar, crie uma posição inicial fora da tela e outra no centro após 1 segundo. Escolha uma curva de desaceleração. Em Efeitos, adicione Glow; comece com intensidade 80, raio 20 e limite 60. Ajuste a cor do brilho. Use o interruptor do efeito para comparar antes/depois e exporte um trecho curto.',
                  ),
                ),
              ],
            ),
            const ExpansionTile(
              tilePadding: EdgeInsets.zero,
              title: AppText('Desempenho e limites'),
              children: [
                Padding(
                  padding: EdgeInsets.only(bottom: 16),
                  child: AppText(
                    'Muitos ecos, desfoques grandes, Pixel Sort e cenas 3D custam mais. Reduza amostras, raio ou qualidade enquanto edita. Exporte um trecho de teste antes do vídeo inteiro. O motor FX V2 trabalha em SDR; não reproduz o pipeline HDR/32 bits do After Effects. Nomes semelhantes não significam algoritmos, plugins ou resultados idênticos. Projetos antigos continuam abrindo, mas efeitos reescritos podem mudar o visual. A prévia em aparelhos sem o backend gráfico nativo pode ter limitações com vídeo ao vivo.',
                  ),
                ),
              ],
            ),
            const SizedBox(height: 24),
            AppText(
              'Guia dos ${efeitosDoCatalogo.length} efeitos',
              style: Theme.of(context).textTheme.titleLarge,
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _search,
              onChanged: (_) => setState(() {}),
              decoration: InputDecoration(
                labelText: translate(context, 'Buscar efeito ou finalidade'),
                prefixIcon: const Icon(Icons.search),
                suffixIcon: IconButton(
                  tooltip: 'Limpar busca',
                  icon: const Icon(Icons.clear),
                  onPressed: () => setState(_search.clear),
                ),
                border: const OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 8),
            if (entries.isEmpty)
              const Padding(
                padding: EdgeInsets.symmetric(vertical: 24),
                child: AppText(
                  'Nenhum efeito encontrado. Tente brilho, cor ou distorção.',
                ),
              ),
            for (final entry in entries)
              ExpansionTile(
                key: ValueKey(entry.key),
                tilePadding: EdgeInsets.zero,
                title: AppText(entry.value.name),
                subtitle: AppText(entry.value.category),
                children: [
                  Align(
                    alignment: Alignment.centerLeft,
                    child: Padding(
                      padding: const EdgeInsets.only(bottom: 16),
                      child: AppText(effectHelp(entry.key)),
                    ),
                  ),
                ],
              ),
          ],
        ),
      ),
    );
  }
}

const quickStartSteps = <(String, String)>[
  (
    '1 · Crie e adicione',
    'Em Projetos, crie um projeto e escolha resolução e FPS. No editor, toque em + para inserir vídeo, imagem, texto ou forma. Em Modelos, você também pode abrir um exemplo pronto.',
  ),
  (
    '2 · Selecione a camada',
    'Toque na camada da timeline. As ferramentas aparecem abaixo: mover, cor, efeitos e outras opções da camada. Voltar fecha primeiro o painel; não sai imediatamente do projeto.',
  ),
  (
    '3 · Anime com dois keyframes',
    'Posicione o cursor no início, abra Mover e transformar e toque no diamante. Avance no tempo e mude a posição. Ajuste a curva para suavizar a transição. O mesmo fluxo anima parâmetros de efeitos.',
  ),
  (
    '4 · Combine efeitos',
    'Abra Efeitos → Adicionar efeito. Toque no nome do efeito para abrir seus parâmetros; arraste a régua de cada um ou toque na caixa do valor para digitar. O diamante à esquerda cria um keyframe do efeito inteiro. Segure o nome e arraste para reordenar: o cálculo vai de cima para baixo. No ••• ficam desativar para comparar, duplicar, resetar e salvar como preset.',
  ),
  (
    '5 · Salve e retome',
    'As alterações do projeto são salvas automaticamente. Volte à lista de Projetos para abrir novamente. O modelo ABISMO permite explorar uma cena 3D com animação e trocas de câmera; os modelos de exemplo não substituem seus projetos.',
  ),
  (
    '6 · Exporte',
    'Use a exportação do editor, ajuste resolução e FPS e aguarde a conclusão mantendo o app aberto. Comece com um trecho curto em 720p para conferir som, timing e efeitos antes da versão final.',
  ),
];

String effectHelp(EffectType type) => switch (type) {
  EffectType.timeSlice => 'Divide o quadro em faixas e cada faixa mostra a camada num instante diferente. Escada imita o S_TimeSlice; linear com ease out vira a transição de faixas dos edits. Numa camada de ajuste, fatia tudo que está embaixo.',
  EffectType.posterizeTime => 'Faz a camada inteira andar em degraus de N quadros por segundo: 12 para o visual de anime, 8 para stop motion, 4 para travado. Numa camada de ajuste, quantiza tudo que está embaixo.',
  EffectType.flash => 'Clarão de um ou poucos quadros na batida: branco, somar, tela, exposição ou negativo. Segura N quadros e cai em M; o gatilho repete a cada N quadros ou dispara ao acaso. Evite flashes rápidos em conteúdo sensível.',
  EffectType.strobe => 'Pisca por quadros: a camada some, vira cor, negativo, estoura ou apaga. Periódico acende a cada período; aleatório sorteia blocos. Ótimo para texto piscando. Evite flashes rápidos em conteúdo sensível.',
  EffectType.zoomPunch => 'Zoom que entra seco na batida e volta ao tamanho normal: suave, exponencial ou com quique. Ataque, hold e soltura são contados em quadros; o rastro de zoom borra a entrada.',
  EffectType.sliceGlitch => 'Fatias horizontais sorteadas deslizam com separação RGB, com ruído de blocos, dessaturação e posterização opcionais. Velocidade troca o padrão por segundo; zero congela. Precisa da GPU.',
  EffectType.twitch => 'Valores aleatórios em instantes aleatórios: deslizar com RGB, escala, desfoque, luz e cor, cada um com pulso próprio. Quietude segura a camada parada entre rajadas; subida e descida suavizam o pulso.',
  EffectType.colorBalance => 'Empurra sombras, meios-tons e altas luzes em direção a vermelho, verde ou azul. Valores positivos puxam a faixa para o canal escolhido; preservar luminosidade mantém o brilho original.',
  EffectType.selectiveColor => 'Ajusta ciano, magenta, amarelo e preto só na faixa escolhida (vermelhos, azuis, brancos, neutros...). Para mexer em mais de uma faixa, empilhe outra instância. Precisa da GPU.',
  EffectType.channelMixer => 'Recria cada canal de saída como uma soma dos canais de entrada. Troque canais para cores cruzadas ou ligue Monochrome para um preto e branco com pesos próprios.',
  EffectType.photoFilter => 'Aplica um filtro de cor, como na lente, ou ajusta a temperatura em Kelvin. Kelvin maior esfria e menor esquenta; densidade controla a força e a luminosidade pode ser preservada.',
  EffectType.gradientMap => 'Mapeia a luminância para um gradiente de sombras, meios-tons e luzes. Em Soft Light com opacidade baixa é o coloring clássico; sem meios-tons vira duotone.',
  EffectType.brightnessContrast => 'Brilho e contraste do After Effects. No modo normal os dois são proporcionais: o preto continua preto e o branco continua branco, só os meios-tons andam. Modo legado soma o brilho em todos os pixels e escala o contraste em torno do cinza médio — estoura, como no AE antigo.',
  EffectType.colorTune => 'Rodas de lift, gamma, gain e offset, cada uma com matiz, saturação e luminância. Lift mexe nas sombras, gamma nos meios-tons, gain nas luzes e offset em tudo.',
  EffectType.hueSaturation => 'Matiz gira as cores, Saturação afasta ou aproxima do cinza (−100 é preto e branco) e Luminosidade mistura com o branco ou o preto, com a conta do Photoshop. Ligue Colorir para tingir tudo com uma matiz só, como sépia ou duotom. Tudo em zero deixa a imagem intacta.',
  EffectType.sFlicker => 'Pisca as cores da camada como filme antigo ou lâmpada ruim: um aleatório suave no brilho, outro por canal de cor e uma onda com fase por canal. Amplitude escala tudo e Brilho escala o resultado; a semente repete o mesmo piscar na exportação. Evite flashes intensos em conteúdo sensível.',
  EffectType.mathOps => 'Combina a camada (A) com uma fonte B numa operação de pixel: somar, subtrair, multiplicar, tela, média, sobrepor, mínimo, máximo ou diferença. Luzes escala, sombras desloca os escuros e saturação ajusta A, B e o resultado. A máscara de luma limita onde o resultado aparece. Precisa da GPU.',
  EffectType.sSharpen => 'Afia o detalhe em duas escalas sem estourar as bordas que já são fortes. Limiar alto afia menos; Nitidez da cor afia também as bordas coloridas.',
  EffectType.looks => 'Um look de cinema pronto sobre a camada, com a força que você quiser. Use numa camada de ajuste para colorir o edit inteiro.',
  EffectType.lightSweep => 'Uma faixa de luz atravessa a camada. Anime a Posição com dois keyframes para o brilho passar uma vez; ângulo e largura moldam a faixa. Clássico para logos e texto.',
  EffectType.saber => 'Núcleo branco com aura colorida a partir da própria silhueta da camada — texto e formas viram lâmina de energia. Matiz escolhe a cor; raio espalha; núcleo controla o miolo branco.',
  EffectType.lensBlur => 'Desfoque de lente: além de borrar, estoura os pontos claros como bokeh. Limiar decide o que conta como realce; brilho controla o estouro.',
  EffectType.smear =>
    'A camada escorre numa direcao: copias esticadas e cada vez mais '
        'transparentes atras da original. Anime o angulo ou o comprimento '
        'para o rastro seguir o movimento.',
  EffectType.bubbleBlur =>
    'Bolhas de vidro fosco sobre a camada: dentro de cada uma a imagem '
        'aparece ampliada e desfocada. A fase anda as bolhas — dois '
        'keyframes e elas derivam.',
  EffectType.dissolver =>
    'A camada some em chuvisco: cada pixel tem um sorteio fixo e a '
        'quantidade e o corte. O grao decide o tamanho do pontinho, de '
        'pixel a areia grossa, e a semente mantem o mesmo chuvisco ao '
        'voltar na timeline e ao exportar.',
  EffectType.pena =>
    'A borda da camada deixa de terminar em faca: o alfa cai suave para '
        'fora, na largura que voce escolher. Serve para encaixar uma '
        'camada em cima de outra sem recorte aparente.',
  EffectType.cortina =>
    'A camada e revelada (ou escondida) por uma linha reta no angulo que '
        'voce escolher. Dois keyframes no avanco e a cortina passa; a '
        'suavidade decide se a beirada e faca ou degrade.',
  EffectType.cortinaRadial =>
    'A mesma cortina dando a volta, como ponteiro de relogio: serve de '
        'contagem, de carregamento e de revelar em leque. O centro pode '
        'sair do meio da camada.',
  EffectType.apertarRecorte =>
    'Come ou devolve a borda do recorte: positivo tira a franja verde que '
        'sobra do chroma, negativo devolve o que o recorte comeu demais. '
        'A suavidade evita a borda serrilhada.',
  EffectType.meioTom =>
    'A imagem vira bolinhas de impressao: onde tem luz, a bolota e grande; '
        'onde tem sombra, e um ponto. Girar a trama evita o padrao de '
        'moire, e a mistura deixa o efeito a meia forca.',
  EffectType.contorno =>
    'Uma linha na sua cor em volta da silhueta da camada — o contorno do '
        'adesivo, sem precisar de estilo de camada. Com "so o contorno", '
        'o miolo some e fica a linha.',
  EffectType.brilhoPorDentro =>
    'A luz nasce na borda e cai para dentro, sem vazar para fora da '
        'silhueta. E o que da o neon aceso num texto ou numa forma.',
  EffectType.bordasAsperas =>
    'A beirada deixa de ser reta: o ruido come e devolve a borda, como '
        'carimbo gasto ou papel rasgado. A evolucao faz a franja tremer '
        'no tempo.',
  EffectType.nuvens =>
    'Nuvem de verdade, feita na hora: ruido em varias escalas entre as '
        'duas cores. A evolucao faz a nuvem andar sozinha, e recortar '
        'transforma a nuvem no recorte da camada.',
  EffectType.xadrez =>
    'Tabuleiro nas duas cores, com quantos quadros voce quiser, esticado '
        'e girado. Com recortar, o xadrez vira buraco na camada em vez '
        'de pintura por cima.',
  EffectType.listras =>
    'Listras nas duas cores: quantidade, angulo, proporcao entre clara e '
        'escura e suavidade, que vai da barra dura ao degrade. O '
        'deslocamento anima com dois keyframes.',
  EffectType.pontos =>
    'Trama de pontos, como meio-tom de revista: quantos cabem, o tamanho '
        'de cada um e a borda dura ou macia. Girar a trama evita o '
        'padrao de moire.',
  EffectType.estrelas =>
    'Um ceu de estrelas sorteadas com tamanho e brilho proprios. O '
        'cintilar pisca cada uma no seu tempo, a velocidade arrasta o '
        'ceu, e a semente guarda o mesmo ceu.',
  EffectType.raios =>
    'Raios saindo de um ponto, como sol atras da nuvem ou explosao de '
        'quadrinho. Largura, suavidade e alcance decidem se e um leque '
        'discreto ou uma explosao inteira.',
  EffectType.repetirEmLinha =>
    'A camada vira uma fileira de copias: o passo diz o quanto cada uma '
        'anda, e giro, escala e opacidade por copia constroem o rastro. '
        'Uma camada so — nada de duplicar na linha do tempo.',
  EffectType.repetirEmGrade =>
    'Copias em colunas e linhas, como uma folha de contato. O passo e em '
        'por cento do tamanho da camada: 100% encosta uma na outra, mais '
        'que isso abre espaco entre elas.',
  EffectType.repetirEmCirculo =>
    'As copias dao a volta: raio, abertura e comeco desenham de uma '
        'mandala fechada a um leque. Cada copia se inclina junto com o '
        'circulo.',
  EffectType.espalharCopias =>
    'Copias sorteadas dentro de um raio, com giro e tamanho variados. A '
        'semente guarda o sorteio: o mesmo espalhamento ao voltar na '
        'timeline e ao exportar.',
  EffectType.aparecerSumir =>
    'Entrar e sair sem keyframe nenhum: a camada aparece nos primeiros '
        'segundos e some nos ultimos, no tempo que voce der para cada '
        'ponta. Camada curta demais divide o que tem entre as duas.',
  EffectType.bit8 => 'Pixel grande, poucas cores e saturação de fliperama num efeito só. Pixel e Cores definem a época: 4 cores para NES, 8 para 16-bit.',
  EffectType.opticalFlow => 'Estima o movimento entre quadros para suavizar câmera lenta. A prévia é preparada em segundo plano; a exportação calcula os quadros a partir do original. Cortes de cena não são misturados.',
  EffectType.twirl => 'Torce a imagem ao redor de um centro. Angulo controla o giro e raio delimita a area afetada. As bordas sao espelhadas.',
  EffectType.fisheye => 'Deforma a imagem como uma lente grande angular. Valores positivos ampliam o centro; negativos comprimem.',
  EffectType.kaleidoscope => 'Espelha setores ao redor do centro. Ajuste segmentos, rotacao e mistura; todos aceitam keyframes.',
  EffectType.venetianBlinds => 'Revela ou recorta a camada em faixas. Conclusao vai de imagem completa a transparente; direcao, faixas e suavidade controlam o recorte.',
  EffectType.blockDissolve => 'Dissolve a camada em blocos. A semente fixa garante o mesmo resultado ao voltar na timeline e ao exportar.',
  EffectType.offset => 'Desloca a imagem com repeticao continua nas bordas. Centro X e Y em 0,5 preservam o enquadramento original.',
  EffectType.invert => 'Inverte os canais RGB preservando a transparencia. Mistura controla a intensidade.',
  EffectType.waveWarp => 'Ondula a imagem. Anime a fase para movimento continuo; amplitude, frequencia e eixo Y controlam a forma.',

  EffectType.oscillate => 'Oscila a camada numa direcao. Amplitude define a distancia; frequencia define ciclos por segundo. Ajuste fase e forma de onda para sincronizar o movimento. Os parametros aceitam keyframes.',
  EffectType.chromaKey => 'Tira o fundo verde (ou azul) e deixa transparente. Toque na cor para pegar a do seu fundo. Tolerância decide quanto some; suavidade amacia a borda; supressão tira o verde que ficou no cabelo e nos ombros.',
  EffectType.lumaKey => 'Tira o fundo pelo brilho: preto para fumaça, fogo e luz; branco para tinta e papel. Escolha qual dos dois some em Remover.',
  EffectType.colorKey => 'Tira uma cor chapada. Mais previsível que o Chroma Key quando o fundo é sólido — um estúdio, uma cor de marca.',
  EffectType.findEdges => 'Desenha só os contornos. Traço escolhe claro-no-escuro ou o inverso; misturar traz a imagem de volta por baixo.',
  EffectType.gaussianBlur => 'Suaviza detalhes. Aumente o raio aos poucos; use o modo de borda para controlar as margens.',
  EffectType.lightGlow => 'Cria um halo nas áreas claras. Limite escolhe o que brilha; raio espalha; intensidade controla a luz. Experimente depois de Levels.',
  EffectType.glowVol => 'Bloom em várias escalas. Ajuste threshold e softness para isolar os realces, radius para espalhar e exposure para força. Glow Only mostra apenas o halo. Tonemapping modela a resposta tonal e Lens Dirt acrescenta modulação procedural; não carrega uma fotografia de lente.',
  EffectType.tint => 'Tinge a imagem com a cor escolhida. Força controla a mistura com o original, preservando o alfa.',
  EffectType.tremor => 'Movimento procedural de câmera/camada. Ajuste amplitude e frequência por eixo; mantenha a semente para repetir o mesmo movimento na exportação.',
  EffectType.glitch => 'Combina saltos, escala, cor, luz, blur e separação RGB. Quantidade é o controle mestre; velocidade e intervalo definem os pulsos.',
  EffectType.rgbSplit => 'Separa um par de canais de cor. Use pouco deslocamento para franja de lente, ou mais para um visual digital. Ângulo define a direção.',
  EffectType.echo => 'Reamostra a camada em instantes anteriores. Ecos define as cópias; intervalo separa os instantes; decaimento enfraquece o rastro. O comportamento temporal não é uma equivalência exata ao Echo da Adobe.',
  EffectType.spatialEcho => 'Repete a imagem no espaço, não no passado. Ajuste deslocamento, escala, rotação e decaimento por cópia.',
  EffectType.radialAberration => 'Franja cromática que cresce em direção às bordas. Quantidades pequenas simulam uma lente; valores altos estilizam.',
  EffectType.levels => 'Os níveis do After Effects, de 0 a 255. Entrada preto e Entrada branco escolhem o que vira preto e branco puros; Gama acima de 1 clareia os meios-tons; Saída preto e Saída branco lavam o preto ou apagam o branco. Entrada invertida inverte a imagem.',
  EffectType.vibrance => 'Reforça mais as cores pouco saturadas. Saturação age globalmente; proteção de pele reduz mudanças em tons quentes, de forma aproximada.',
  EffectType.whiteBalance => 'Temperatura equilibra azul/vermelho; matiz equilibra verde/magenta. Corrija com sutileza antes de aplicar um look.',
  EffectType.colorWheels => 'Ajusta R, G e B separadamente nas sombras e altas luzes. Use para separar tons frios e quentes sem uma tinta uniforme.',
  EffectType.unmult => 'Transforma um fundo preto em transparência e desmultiplica a cor. Limiar e suavidade ajustam o recorte; não substitui uma máscara para fundos complexos.',
  EffectType.vignette => 'CC Vignette: escurece (ou clareia, com quantidade negativa) as bordas como uma lente de verdade, pela lei do cosseno à quarta. Ângulo de visão maior escurece mais perto do centro; Proteger luzes segura as áreas claras.',
  EffectType.threshold => 'CC Threshold: vira preto e branco puros. Pixels com luminância (ou o canal escolhido) acima do limiar ficam brancos, abaixo ficam pretos. Inverter troca os dois; Misturar com original devolve parte da imagem.',
  EffectType.thresholdRgb => 'CC Threshold RGB: o mesmo corte seco, mas separado em vermelho, verde e azul, o que gera até oito cores chapadas. Cada canal tem limiar e inversão próprios.',
  EffectType.blockLoad => 'CC Block Load: a imagem carrega em blocos que vão ficando menores, como uma foto baixando devagar. Anime Conclusão de 0 a 100%; Varreduras define quantas passadas; Bilinear suaviza os blocos.',
  EffectType.scanLines => 'S_ScanLines: linhas de monitor de TV. Frequência é o número de linhas, Nitidez endurece a borda delas e Gama ajusta o brilho médio. Deslocar vermelho e azul em -0,33 e 0,33 dá o visual de TV desalinhada.',
  EffectType.halfTone => 'S_HalfTone: retícula de pontos como em jornal e quadrinho. Frequência é a quantidade de pontos, Ângulo gira a grade e Nitidez define a borda dos pontos. Cor 1 e Cor 0 trocam o papel e a tinta.',
  EffectType.edgeColorize => 'S_EdgeColorize: pinta as bordas da imagem conforme a direção delas — topo, direita, base e esquerda com cores próprias — sobre um fundo. Suavizar bordas engrossa o traço e Girar cores muda qual cor cai em cada lado.',
  EffectType.jpegDamage => 'S_JpegDamage: compressão JPEG de verdade, com blocos 8x8 e as tabelas do padrão. Qualidade baixa estraga mais; Fator de resolução aumenta os blocos; as escalas de frequência deformam a compressão; Taxa de erros injeta falhas de decodificação por bloco.',
  EffectType.autoPaint => 'S_AutoPaint: transforma a imagem em pinceladas. Van Gogh segue as bordas, Hairy Paint cruza as bordas e Pointalize faz manchas sem direção. Frequência é a densidade das pinceladas e Comprimento o tamanho do traço.',
  EffectType.tvDamage => 'S_TVDamage: TV com problemas de recepção e de aparelho — estática, interferência, fantasmas, perda de sincronismo horizontal e vertical, barras, listras de cor, dropouts, vinheta e desligar. Recepção geral controla tudo de uma vez.',
  EffectType.vhsDamage => 'S_VHSDamage: fita VHS gasta — cor lavada, croma borrada e deslocada, luma suave, ruído de fita ondulado, avanço rápido, linhas de varredura e riscos brancos e pretos de dropout.',
  EffectType.directionalBlur => 'Borra ao longo de uma direção. Comprimento controla a distância; ângulo 0 é vertical. Não usa quadros anteriores.',
  EffectType.radialBlur => 'Desfoque de zoom ou giro. Quantidade controla a extensão; mais amostras suavizam o resultado e custam desempenho.',
  EffectType.lightRays => 'Espalha luz em direção a um centro. Posicione Centro X/Y na origem desejada, ajuste comprimento e intensidade, depois a cor.',
  EffectType.mosaic => 'Reduz a imagem a blocos de cor. Mais blocos dão um mosaico fino; menos blocos deixam pixels grandes.',
  EffectType.filmGrain => 'Adiciona grão determinístico à imagem. Ajuste intensidade e tamanho; a semente troca o padrão. Use por último para manter textura.',
  EffectType.fractalNoise => 'Gera ruído contínuo em várias escalas. Complexidade adiciona detalhes, contraste separa tons e evolução anima o padrão. Anime evolução com keyframes.',
  EffectType.digitalDamage => 'Insere faixas digitais deslocadas. Blocos e altura controlam a cobertura; intervalo troca o padrão; cor reforça a corrupção.',
  EffectType.zoomWarp => 'Zoom com rastro amostrado. Quantidade define o zoom; rastro alonga a transição espacial; amostras suavizam.',
  EffectType.posterize => 'Limita cada canal a um número de tons. Use 3–6 para pôsteres; valores maiores preservam mais gradações. Não é apenas contraste.',
  EffectType.curves => 'Modelagem tonal simplificada: contraste em S, brilho, sombras e altas. Não é o editor de curva livre ponto a ponto do After Effects.',
  EffectType.timeRemap => 'Muda o instante de origem da camada. Use para acelerar, desacelerar ou deslocar o conteúdo; não altera sozinho a posição espacial.',
  EffectType.pixelSort => 'Ordena pixels selecionados por limiar. Escolha direção, comprimento e critério; Show ajuda a inspecionar a máscara. É um efeito pesado: teste em baixa resolução.',
  EffectType.blobTracker => 'Detecta regiões e desenha marcações. Em vídeo, use Analisar para obter dados reais antes de ajustar os elementos gráficos. Sem análise, a visualização não comprova rastreamento do conteúdo.',
  EffectType.turbulentDisplace => 'Distorce usando um campo de ruído por pixel. Quantidade desloca, tamanho define a escala; anime evolução para criar movimento orgânico.',
  EffectType.unsharpMask => 'Nitidez de verdade: compara cada pixel com uma versão desfocada e reforça a diferença. Quantidade é a força (50% é o padrão do AE), Raio é a largura da borda em pixels de 1080p (1 a 3 para detalhe, 20 ou mais para clareza) e Limiar protege pele e céu. Só luminância afia sem franjas coloridas.',
  EffectType.exposure => 'Exposição em stops, como na câmera: +1 é o dobro de luz, calculado em luz linear. Deslocamento abre ou fecha as sombras; Correção de gama ajusta os meios-tons. Ignorar luz linear faz a conta direto no valor do pixel.',
  EffectType.motionTile => 'Repete a camada. Tile Width/Height e Output Width/Height são percentuais da entrada. Mirror Edges espelha repetições; Phase desloca linhas ou colunas.',
  EffectType.bend => 'Curva a imagem em um eixo. Quantidade define o desvio; curvatura e âncora moldam a dobra. Anime quantidade para uma entrada flexível.',
  EffectType.ccScatterize => 'Fragmenta a imagem em grãos. Dispersão espalha, rotação gira e gravidade desloca os fragmentos. É uma implementação AUREA, não o plugin Cycore.',
  EffectType.ccSplit => 'Abre a imagem em duas partes. Divisão afasta as metades; ângulo e centro posicionam o corte; suavidade reduz a dureza da transição.',
  EffectType.vhs => 'Combina linhas de varredura, sangramento de cor, instabilidade e ruído. Intensidade mistura o conjunto; reduza desbotar para conservar a cor.',
  EffectType.filmDamage => 'Simula poeira, riscos, cintilação, grão, queimado e salto; fios, balanço, desfoque, vinheta, saturação e tom sépia completam a cópia gasta. Reduza os controles que não precisa para um filme antigo mais sutil.',
  EffectType.glitchify => 'Corrupção por blocos e linhas. Intensidade mistura, deslocamento afasta os blocos; velocidade muda o padrão com o tempo.',
  EffectType.forceMotionBlur => 'Reamostra a animação da camada em vários instantes. Mais amostras melhoram o rastro, mas aumentam o trabalho de renderização.',
  EffectType.flicker => 'Modula brilho ou opacidade. Escolha aleatório, strobe ou senoide; frequência controla a repetição. Evite flashes intensos ou rápidos em conteúdo para o público.',
  EffectType.gradient4 => 'Gradiente de quatro cantos. Escolha as quatro cores, opacidade, ângulo e modo de mistura. Pode colorir a imagem sem apagar sua forma.',
  EffectType.liquidGlass => 'Look de vidro com refração, luz e bordas. Ajuste diretamente os parâmetros de refração e iluminação; o resultado depende do conteúdo disponível atrás da camada.',
  EffectType.corrections => 'Correção básica em sequência: exposição, contraste, sombras/altas, temperatura, matiz, saturação e gama. Ajuste primeiro exposição e balanço de branco.',
};
