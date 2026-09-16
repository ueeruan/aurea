import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';

/// O TEXTO DO PALCO NUNCA PODE PEDIR UM GLIFO GIGANTE AO IMPELLER.
///
/// O que o testador viu em 16/09 (icones virando quadrados, nomes sumindo,
/// texto fantasma na Inicio, e tudo continuando assim fora do editor) e o
/// atlas de glifos do Impeller corrompido. Lido no codigo do motor desta
/// versao do Flutter:
///
/// * o pre-passe (`FirstPassDispatcher::drawText`) poe TODO texto do quadro
///   no atlas, inclusive o que depois sera desenhado como contorno;
/// * o glifo vai para o atlas com `corpo x min(escala na tela, 48)`;
/// * o atlas tem 4096 px de largura e cresce so em altura. Quando os glifos
///   novos de um quadro nao cabem, a montagem falha no meio: os glifos
///   ficam registrados como "ja no atlas" com retangulo vazio, o empacotador
///   e trocado e a textura seguinte recebe a antiga por cima. Dali em diante
///   o app inteiro desenha texto errado ate ser fechado.
///
/// No palco a escala na tela chega facil a 48: escala da camada x
/// profundidade Z (ate 12x) x zoom da camera (ate 10x) x zoom do palco.
/// Uma camada de texto de corpo 120 pedia glifos de 5760 px.
///
/// A SAIDA: desenhar com o corpo [corpoDeDesenho] e ampliar pelo canvas.
/// O tamanho em pixels na tela e o mesmo (corpo x escala nao muda), entao:
///
/// * ate 250 px de corpo na tela o Impeller rasteriza o glifo no tamanho
///   exato de sempre — nitidez identica;
/// * acima disso ele ja desenhava contorno (`kMaxTextScale`), e contorno sai
///   do corpo canonico de 64 px sem hinting — forma identica;
/// * o atlas nunca recebe glifo com mais de 48 x [corpoDeDesenho] px, o
///   envelope para o qual o motor foi feito.
///
/// A unica conta a acertar e a LINHA DE BASE: o SkParagraph arredonda a
/// linha de base para pixel inteiro NO ESPACO DO PARAGRAFO, e no corpo
/// reduzido esse pixel vale `k` vezes mais. [correcaoDaLinhaDeBase] devolve o
/// arredondamento do corpo cheio.
const double corpoDeDesenho = 12;

/// Por quanto o texto de [corpo] e reduzido para ser desenhado (>= 1).
double reducaoDoCorpo(double corpo) =>
    corpo.isFinite && corpo > corpoDeDesenho ? corpo / corpoDeDesenho : 1;

/// Quanto deslocar em Y, NO CORPO REDUZIDO, para a linha de base desenhada
/// cair onde cairia no corpo cheio. As bases sao as do primeiro alinhamento
/// alfabetico de cada paragrafo (sem arredondar).
double correcaoDaLinhaDeBase(double baseCheia, double baseReduzida, double k) =>
    ((baseCheia + .5).floorToDouble() -
        k * (baseReduzida + .5).floorToDouble()) /
    k;

/// Se [estilo] pode ser desenhado reduzido. Pintura propria (foreground,
/// background) guarda medidas que nao da para ler de volta.
bool estiloReduzivel(TextStyle estilo) =>
    estilo.foreground == null && estilo.background == null;

/// [estilo] com toda medida absoluta dividida por [k]. `height` e
/// `decorationThickness` sao multiplicadores e ficam.
TextStyle estiloReduzido(TextStyle estilo, double k, {double? corpo}) =>
    estilo.copyWith(
      fontSize: (corpo ?? estilo.fontSize ?? 14) / k,
      letterSpacing: estilo.letterSpacing == null
          ? null
          : estilo.letterSpacing! / k,
      wordSpacing: estilo.wordSpacing == null ? null : estilo.wordSpacing! / k,
      shadows: estilo.shadows == null
          ? null
          : [
              for (final s in estilo.shadows!)
                Shadow(
                  color: s.color,
                  offset: s.offset / k,
                  blurRadius: s.blurRadius / k,
                ),
            ],
    );

/// UM TEXTO MEDIDO NO CORPO CHEIO E DESENHADO NO CORPO DE DESENHO.
///
/// [cheio] mede (tamanho, caixas, linha de base) exatamente como antes;
/// [paint] desenha pelo corpo reduzido quando o corpo passa do de desenho.
class TextoNoAtlas {
  TextoNoAtlas({
    required String texto,
    required TextStyle estilo,
    TextAlign alinhamento = TextAlign.start,
    TextDirection direcao = TextDirection.ltr,
    TextScaler escalaDoTexto = TextScaler.noScaling,
    int? maxLinhas,
    Locale? local,
    TextWidthBasis baseDaLargura = TextWidthBasis.parent,
    ui.TextHeightBehavior? alturaDoTexto,
  }) : _texto = texto,
       _direcao = direcao,
       _local = local,
       cheio = TextPainter(
         text: TextSpan(text: texto, style: estilo),
         textAlign: alinhamento,
         textDirection: direcao,
         textScaler: escalaDoTexto,
         maxLines: maxLinhas,
         locale: local,
         textWidthBasis: baseDaLargura,
         textHeightBehavior: alturaDoTexto,
       ) {
    final corpo = escalaDoTexto.scale(estilo.fontSize ?? 14);
    k = estiloReduzivel(estilo) ? reducaoDoCorpo(corpo) : 1;
    if (k > 1) _estiloDasLinhas = estiloReduzido(estilo, k, corpo: corpo);
  }

  final String _texto;
  final TextDirection _direcao;
  final Locale? _local;

  /// Quem mede — e quem desenha, quando nao ha reducao.
  final TextPainter cheio;

  /// A reducao do corpo; 1 quando o texto ja cabe no corpo de desenho.
  late final double k;

  TextStyle? _estiloDasLinhas;

  /// Cada PALAVRA do paragrafo cheio, construida sozinha no corpo reduzido,
  /// com a origem (no espaco reduzido) que poe a palavra onde o paragrafo
  /// cheio a desenharia.
  final List<(TextPainter, Offset)> _pedacos = [];
  bool _diagramado = false;

  /// Se os pedacos estao montados para [paint].
  bool get diagramado => _diagramado;

  Size get size => cheio.size;
  double get width => cheio.width;
  double get height => cheio.height;

  static final _palavra = RegExp(r'\S+');

  /// [soMedir] pula a montagem dos pedacos reduzidos (layout seco).
  void layout({
    double minWidth = 0,
    double maxWidth = double.infinity,
    bool soMedir = false,
  }) {
    cheio.layout(minWidth: minWidth, maxWidth: maxWidth);
    _diagramado = !soMedir;
    _soltarPedacos();
    final estilo = _estiloDasLinhas;
    if (estilo == null || soMedir) return;
    // PALAVRA POR PALAVRA, no lugar que o paragrafo cheio deu a ela. O
    // paragrafo inteiro reduzido nao serve: o SkParagraph arredonda a altura
    // de cada linha no espaco dele (no corpo 12 isso vale k vezes mais, e a
    // terceira linha descia pixels inteiros), e o avanco das letras no corpo
    // 12 nao e exatamente proporcional (uma linha longa escorregava ~1 px).
    // Quebra, alinhamento, kerning entre palavras e linha de base saem do
    // paragrafo cheio; do reduzido so sai o desenho. A palavra fica inteira
    // para nao partir ligadura nem a juncao das letras arabes.
    for (final m in cheio.computeLineMetrics()) {
      if (m.width <= 0) continue;
      final dentro = cheio.getPositionForOffset(
        Offset(m.left + m.width / 2, m.baseline),
      );
      final faixa = cheio.getLineBoundary(dentro);
      if (faixa.isCollapsed || faixa.start < 0 || faixa.end > _texto.length) {
        continue;
      }
      // Onde o paragrafo cheio desenha a linha de base desta linha: topo da
      // linha + ascendente arredondado (TextLine::paintText). O topo sai de
      // `baseline = topo + altura - descendente` (TextLine::getMetrics); a
      // altura da linha vem arredondada, entao `baseline - ascent` NAO e o
      // topo — errava 0,13 px, e o encaixe da linha de base no pixel do
      // aparelho transformava isso num pixel inteiro.
      final topo = m.baseline - m.height + m.descent;
      final baseCheia = topo + (m.ascent + .5).floorToDouble();
      final linha = _texto.substring(faixa.start, faixa.end);
      for (final w in _palavra.allMatches(linha)) {
        final caixas = cheio.getBoxesForSelection(
          TextSelection(
            baseOffset: faixa.start + w.start,
            extentOffset: faixa.start + w.end,
          ),
        );
        if (caixas.isEmpty) continue;
        final pedaco = TextPainter(
          text: TextSpan(text: w.group(0), style: estilo),
          textDirection: _direcao,
          locale: _local,
        )..layout();
        final proprias = pedaco.getBoxesForSelection(
          TextSelection(baseOffset: 0, extentOffset: w.end - w.start),
        );
        if (proprias.isEmpty) {
          pedaco.dispose();
          continue;
        }
        final esquerda = caixas
            .map((c) => c.left)
            .reduce((a, b) => a < b ? a : b);
        final esquerdaPropria = proprias
            .map((c) => c.left)
            .reduce((a, b) => a < b ? a : b);
        final baseReduzida =
            (pedaco.computeDistanceToActualBaseline(TextBaseline.alphabetic) +
                    .5)
                .floorToDouble();
        _pedacos.add((
          pedaco,
          Offset(esquerda / k - esquerdaPropria, baseCheia / k - baseReduzida),
        ));
      }
    }
  }

  void paint(Canvas canvas, Offset offset) {
    assert(_diagramado, 'layout antes de paint');
    if (_estiloDasLinhas == null) {
      cheio.paint(canvas, offset);
      return;
    }
    canvas
      ..save()
      ..translate(offset.dx, offset.dy)
      ..scale(k);
    for (final (pedaco, origem) in _pedacos) {
      pedaco.paint(canvas, origem);
    }
    canvas.restore();
  }

  void _soltarPedacos() {
    for (final (pedaco, _) in _pedacos) {
      pedaco.dispose();
    }
    _pedacos.clear();
  }

  void dispose() {
    cheio.dispose();
    _soltarPedacos();
  }
}

/// PARAGRAFO CRU (`dart:ui`) no corpo de desenho: [cheio] ja diagramado,
/// [reduzido] o mesmo texto construido com corpo / [k] e diagramado na
/// largura do cheio / [k].
void desenharParagrafoNoAtlas(
  Canvas canvas,
  ui.Paragraph cheio,
  ui.Paragraph? reduzido,
  double k,
  Offset offset,
) {
  if (reduzido == null || k <= 1) {
    canvas.drawParagraph(cheio, offset);
    return;
  }
  final correcao = correcaoDaLinhaDeBase(
    cheio.alphabeticBaseline,
    reduzido.alphabeticBaseline,
    k,
  );
  canvas
    ..save()
    ..translate(offset.dx, offset.dy)
    ..scale(k)
    ..translate(0, correcao);
  canvas.drawParagraph(reduzido, Offset.zero);
  canvas.restore();
}

/// O `Text` das camadas do palco, desenhado no corpo de desenho.
///
/// Resolve estilo, escala de acessibilidade, alinhamento e quebra como o
/// `Text` resolve — o mesmo texto de antes, com a mesma caixa.
class TextoDoPalco extends StatelessWidget {
  const TextoDoPalco(this.texto, {super.key, this.estilo, this.alinhamento});

  final String texto;
  final TextStyle? estilo;
  final TextAlign? alinhamento;

  @override
  Widget build(BuildContext context) {
    final padrao = DefaultTextStyle.of(context);
    var efetivo = padrao.style.merge(estilo);
    if (MediaQuery.boldTextOf(context)) {
      efetivo = efetivo.merge(const TextStyle(fontWeight: FontWeight.bold));
    }
    return _TextoDoPalco(
      texto: texto,
      estilo: efetivo,
      alinhamento: alinhamento ?? padrao.textAlign ?? TextAlign.start,
      direcao: Directionality.of(context),
      escala: MediaQuery.textScalerOf(context),
      quebra: padrao.softWrap,
      maxLinhas: padrao.maxLines,
      baseDaLargura: padrao.textWidthBasis,
      alturaDoTexto:
          padrao.textHeightBehavior ??
          DefaultTextHeightBehavior.maybeOf(context),
      local: Localizations.maybeLocaleOf(context),
    );
  }
}

class _TextoDoPalco extends LeafRenderObjectWidget {
  const _TextoDoPalco({
    required this.texto,
    required this.estilo,
    required this.alinhamento,
    required this.direcao,
    required this.escala,
    required this.quebra,
    required this.maxLinhas,
    required this.baseDaLargura,
    required this.alturaDoTexto,
    required this.local,
  });

  final String texto;
  final TextStyle estilo;
  final TextAlign alinhamento;
  final TextDirection direcao;
  final TextScaler escala;
  final bool quebra;
  final int? maxLinhas;
  final TextWidthBasis baseDaLargura;
  final ui.TextHeightBehavior? alturaDoTexto;
  final Locale? local;

  @override
  RenderTextoDoPalco createRenderObject(BuildContext context) =>
      RenderTextoDoPalco(_chave);

  @override
  void updateRenderObject(BuildContext context, RenderTextoDoPalco r) =>
      r.chave = _chave;

  ChaveDoTextoDoPalco get _chave => ChaveDoTextoDoPalco(
    texto,
    estilo,
    alinhamento,
    direcao,
    escala,
    quebra,
    maxLinhas,
    baseDaLargura,
    alturaDoTexto,
    local,
  );
}

@immutable
class ChaveDoTextoDoPalco {
  const ChaveDoTextoDoPalco(
    this.texto,
    this.estilo,
    this.alinhamento,
    this.direcao,
    this.escala,
    this.quebra,
    this.maxLinhas,
    this.baseDaLargura,
    this.alturaDoTexto,
    this.local,
  );

  final String texto;
  final TextStyle estilo;
  final TextAlign alinhamento;
  final TextDirection direcao;
  final TextScaler escala;
  final bool quebra;
  final int? maxLinhas;
  final TextWidthBasis baseDaLargura;
  final ui.TextHeightBehavior? alturaDoTexto;
  final Locale? local;

  TextoNoAtlas criar() => TextoNoAtlas(
    texto: texto,
    estilo: estilo,
    alinhamento: alinhamento,
    direcao: direcao,
    escalaDoTexto: escala,
    maxLinhas: maxLinhas,
    local: local,
    baseDaLargura: baseDaLargura,
    alturaDoTexto: alturaDoTexto,
  );

  @override
  bool operator ==(Object other) =>
      other is ChaveDoTextoDoPalco &&
      other.texto == texto &&
      other.estilo == estilo &&
      other.alinhamento == alinhamento &&
      other.direcao == direcao &&
      other.escala == escala &&
      other.quebra == quebra &&
      other.maxLinhas == maxLinhas &&
      other.baseDaLargura == baseDaLargura &&
      other.alturaDoTexto == alturaDoTexto &&
      other.local == local;

  @override
  int get hashCode => Object.hash(
    texto,
    estilo,
    alinhamento,
    direcao,
    escala,
    quebra,
    maxLinhas,
    baseDaLargura,
    alturaDoTexto,
    local,
  );
}

class RenderTextoDoPalco extends RenderBox {
  RenderTextoDoPalco(this._chave) : _texto = _chave.criar();

  ChaveDoTextoDoPalco _chave;
  TextoNoAtlas _texto;

  set chave(ChaveDoTextoDoPalco nova) {
    if (nova == _chave) return;
    _chave = nova;
    _texto.dispose();
    _texto = nova.criar();
    markNeedsLayout();
  }

  /// A reducao em uso — para os testes.
  double get reducao => _texto.k;

  bool _estourou = false;

  double _larguraMaxima(double max) => _chave.quebra ? max : double.infinity;

  @override
  double computeMinIntrinsicWidth(double height) {
    _texto.layout(soMedir: true);
    return _texto.cheio.minIntrinsicWidth;
  }

  @override
  double computeMaxIntrinsicWidth(double height) {
    _texto.layout(soMedir: true);
    return _texto.cheio.maxIntrinsicWidth;
  }

  @override
  double computeMinIntrinsicHeight(double width) {
    _texto.layout(maxWidth: _larguraMaxima(width), soMedir: true);
    return _texto.height;
  }

  @override
  double computeMaxIntrinsicHeight(double width) =>
      computeMinIntrinsicHeight(width);

  @override
  Size computeDryLayout(BoxConstraints constraints) {
    final medidor = _chave.criar()
      ..layout(
        minWidth: constraints.minWidth,
        maxWidth: _larguraMaxima(constraints.maxWidth),
        soMedir: true,
      );
    final s = constraints.constrain(medidor.size);
    medidor.dispose();
    return s;
  }

  @override
  double? computeDistanceToActualBaseline(TextBaseline baseline) {
    _texto.layout(
      minWidth: constraints.minWidth,
      maxWidth: _larguraMaxima(constraints.maxWidth),
    );
    return _texto.cheio.computeDistanceToActualBaseline(baseline);
  }

  @override
  void performLayout() {
    _texto.layout(
      minWidth: constraints.minWidth,
      maxWidth: _larguraMaxima(constraints.maxWidth),
    );
    size = constraints.constrain(_texto.size);
    _estourou =
        _texto.cheio.didExceedMaxLines ||
        _texto.width > size.width + .01 ||
        _texto.height > size.height + .01;
  }

  @override
  void paint(PaintingContext context, Offset offset) {
    // Uma medicao intrinseca depois do layout (so mede, sem os pedacos)
    // nao pode deixar o texto sem desenho: o RenderParagraph tambem refaz.
    if (!_texto.diagramado) {
      _texto.layout(
        minWidth: constraints.minWidth,
        maxWidth: _larguraMaxima(constraints.maxWidth),
      );
    }
    if (_estourou) {
      // O `Text` padrao corta o que passa da caixa; aqui tambem.
      context.canvas
        ..save()
        ..clipRect(offset & size);
      _texto.paint(context.canvas, offset);
      context.canvas.restore();
      return;
    }
    _texto.paint(context.canvas, offset);
  }

  @override
  void dispose() {
    _texto.dispose();
    super.dispose();
  }
}

/// Um icone de fonte no palco, desenhado no corpo de desenho.
class IconeDoPalco extends StatelessWidget {
  const IconeDoPalco(this.icone, {super.key, this.tamanho = 24, this.cor});

  final IconData icone;
  final double tamanho;
  final Color? cor;

  @override
  Widget build(BuildContext context) {
    final k = reducaoDoCorpo(tamanho);
    return SizedBox.square(
      dimension: tamanho,
      child: Center(
        child: Transform.scale(
          scale: k,
          child: Icon(
            icone,
            size: math.min(tamanho, corpoDeDesenho),
            color: cor,
          ),
        ),
      ),
    );
  }
}
