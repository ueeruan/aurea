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
            AppText('Do zero ao primeiro motion',
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
                  child: AppText('Adicione um texto. Em Mover e transformar, crie uma posição inicial fora da tela e outra no centro após 1 segundo. Escolha uma curva de desaceleração. Em Efeitos, adicione Glow; comece com intensidade 80, raio 20 e limite 60. Ajuste a cor do brilho. Use o interruptor do efeito para comparar antes/depois e exporte um trecho curto.',
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
                  child: AppText('Muitos ecos, desfoques grandes, Pixel Sort e cenas 3D custam mais. Reduza amostras, raio ou qualidade enquanto edita. Exporte um trecho de teste antes do vídeo inteiro. O motor FX V2 trabalha em SDR; não reproduz o pipeline HDR/32 bits do After Effects. Nomes semelhantes não significam algoritmos, plugins ou resultados idênticos. Projetos antigos continuam abrindo, mas efeitos reescritos podem mudar o visual. A prévia em aparelhos sem o backend gráfico nativo pode ter limitações com vídeo ao vivo.',
                  ),
                ),
              ],
            ),
            const SizedBox(height: 24),
            AppText(
              'Guia dos ${effectSpecs.length} efeitos',
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
                child: AppText('Nenhum efeito encontrado. Tente brilho, cor ou distorção.',
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
    'Abra Efeitos → Adicionar efeito. Toque no nome do efeito para abrir seus parâmetros e cores. Arraste as réguas para ajustar os valores e toque no diamante para criar um keyframe. Role para acessar os demais controles. Arraste a alça para reordenar: o cálculo vai de cima para baixo. Use Resetar para restaurar os valores ou o olho para comparar com o efeito desativado.',
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
  EffectType.brightnessContrast => 'Brilho empurra a imagem para o branco ou para o preto sem estourar; contraste afasta ou aproxima os tons do cinza médio. Os dois aceitam keyframes.',
  EffectType.colorTune => 'Rodas de lift, gamma, gain e offset, cada uma com matiz, saturação e luminância. Lift mexe nas sombras, gamma nos meios-tons, gain nas luzes e offset em tudo.',
  EffectType.opticalFlow => 'Estima o movimento entre quadros para suavizar câmera lenta e curvas de Time Remap. A prévia é preparada em segundo plano; a exportação calcula os quadros a partir do original. Cortes de cena não são misturados.',
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
  EffectType.levels => 'Remapeia preto, branco e gama. Primeiro ajuste os pontos de entrada; depois a gama dos meios-tons. Saída min/max limita o contraste final; canal isola R, G ou B.',
  EffectType.vibrance => 'Reforça mais as cores pouco saturadas. Saturação age globalmente; proteção de pele reduz mudanças em tons quentes, de forma aproximada.',
  EffectType.whiteBalance => 'Temperatura equilibra azul/vermelho; matiz equilibra verde/magenta. Corrija com sutileza antes de aplicar um look.',
  EffectType.colorWheels => 'Ajusta R, G e B separadamente nas sombras e altas luzes. Use para separar tons frios e quentes sem uma tinta uniforme.',
  EffectType.unmult => 'Transforma um fundo preto em transparência e desmultiplica a cor. Limiar e suavidade ajustam o recorte; não substitui uma máscara para fundos complexos.',
  EffectType.vignette => 'Tinge as bordas; escolha preto para escurecer. Raio, suavidade, forma e centro definem a área preservada. A transparência da camada é mantida.',
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
  EffectType.unsharpMask => 'Aumenta o contraste de detalhes. Quantidade controla a força, raio a escala dos detalhes e limiar protege áreas uniformes. Valores altos geram halos.',
  EffectType.motionTile => 'Repete a camada. Tile Width/Height e Output Width/Height são percentuais da entrada. Mirror Edges espelha repetições; Phase desloca linhas ou colunas.',
  EffectType.bend => 'Curva a imagem em um eixo. Quantidade define o desvio; curvatura e âncora moldam a dobra. Anime quantidade para uma entrada flexível.',
  EffectType.ccScatterize => 'Fragmenta a imagem em grãos. Dispersão espalha, rotação gira e gravidade desloca os fragmentos. É uma implementação AUREA, não o plugin Cycore.',
  EffectType.ccSplit => 'Abre a imagem em duas partes. Divisão afasta as metades; ângulo e centro posicionam o corte; suavidade reduz a dureza da transição.',
  EffectType.vhs => 'Combina linhas de varredura, sangramento de cor, instabilidade e ruído. Intensidade mistura o conjunto; reduza desbotar para conservar a cor.',
  EffectType.filmDamage => 'Simula poeira, riscos, cintilação, grão, queimado e salto. Reduza os controles que não precisa para um filme antigo mais sutil.',
  EffectType.glitchify => 'Corrupção por blocos e linhas. Intensidade mistura, deslocamento afasta os blocos; velocidade muda o padrão com o tempo.',
  EffectType.forceMotionBlur => 'Reamostra a animação da camada em vários instantes. Mais amostras melhoram o rastro, mas aumentam o trabalho de renderização.',
  EffectType.flicker => 'Modula brilho ou opacidade. Escolha aleatório, strobe ou senoide; frequência controla a repetição. Evite flashes intensos ou rápidos em conteúdo para o público.',
  EffectType.gradient4 => 'Gradiente de quatro cantos. Escolha as quatro cores, opacidade, ângulo e modo de mistura. Pode colorir a imagem sem apagar sua forma.',
  EffectType.liquidGlass => 'Look de vidro com refração, luz e bordas. Ajuste diretamente os parâmetros de refração e iluminação; o resultado depende do conteúdo disponível atrás da camada.',
  EffectType.corrections => 'Correção básica em sequência: exposição, contraste, sombras/altas, temperatura, matiz, saturação e gama. Ajuste primeiro exposição e balanço de branco.',
};
