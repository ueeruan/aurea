import '../../application/font_service.dart';

import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import '../../domain/layer.dart';
import '../../domain/cor_da_unidade.dart';
import '../../domain/direcao_do_texto.dart';
import '../../domain/text_animator.dart';
import '../../domain/text_path.dart';
import 'texto_no_atlas.dart';

/// Render por unidade (PR-T2): segmenta em grapheme clusters, mede o
/// avanco pela LINHA INTEIRA (getBoxesForRange no layout completo) e pinta
/// cada unidade com o transform acumulado dos animadores.
///
/// Caminho rapido: sem animador ativo, o chamador usa o Text normal.
class AnimatedTextView extends StatelessWidget {
  const AnimatedTextView({
    super.key,
    required this.layer,
    required this.localTime,
    this.pathOverride,
  });

  final TextLayer layer;
  final Duration localTime;

  /// Caminho de outra camada de forma, quando o texto segue ela. O
  /// widget nao sabe resolver id de camada — quem monta a composicao
  /// sabe, e passa pronto.
  final Path? pathOverride;

  static TextStyle styleFor(TextLayer l, {bool animated = false}) => TextStyle(
    color: l.color,
    fontSize: l.fontSize,
    // Fonte importada. Se ela sumiu (projeto trazido de outro
    // aparelho), cai na do aplicativo em vez de nao desenhar nada.
    fontFamily: resolveFontFamily(l.fontFamily),
    fontWeight: l.bold ? FontWeight.w700 : FontWeight.w400,
    letterSpacing: -l.fontSize * 0.02,
    height: 1.1,
    // Com animacao por caractere, ligaduras partiriam glifos (§3.2).
    fontFeatures: animated
        ? const [
            FontFeature.disable('liga'),
            FontFeature.disable('clig'),
            FontFeature.disable('dlig'),
          ]
        : null,
  );

  @override
  Widget build(BuildContext context) {
    final style = styleFor(layer, animated: true);
    // O LAYOUT DA LINHA nao muda com o tempo: vem do cache, e nao de um
    // TextPainter novo, uma segmentacao nova e uma consulta de caixa por
    // letra a cada quadro.
    final diagrama = _Diagrama.de(layer.text, style, layer.alinhamento);
    final full = diagrama.cheio;
    final units = diagrama.unidades;

    // TEXTO EM CAMINHO: o caminho decide o tamanho da area, nao a linha
    // de texto — um selo circular ocupa um quadrado, nao uma tira.
    final spec = layer.textPath;
    final path = spec.active ? (pathOverride ?? buildTextPath(spec)) : null;
    final size = path == null || path.getBounds().isEmpty
        ? full.size
        : path.getBounds().inflate(layer.fontSize).size;

    return CustomPaint(
      size: size,
      painter: _AnimatedTextPainter(
        layer: layer,
        style: style,
        full: full,
        units: units,
        diagrama: diagrama,
        localTime: localTime,
        path: path,
      ),
    );
  }
}

/// O PAINTER DO TEXTO ANIMADO, PARA O TESTE DESENHAR NUM CANVAS PROPRIO.
///
/// O caminho de desenho nao e observavel de fora — passa por dentro de um
/// `CustomPaint`. Quem precisa dele (o teste que prova que a letra e
/// desenhada DENTRO DO RECORTE DA PALAVRA, em vez de um paragrafo por
/// letra) pega por aqui e desenha num canvas que grava as chamadas.
@visibleForTesting
CustomPainter painterDoTextoAnimado(
  TextLayer layer,
  Duration localTime, {
  Path? path,
}) {
  final estilo = AnimatedTextView.styleFor(layer, animated: true);
  final diagrama = _Diagrama.de(layer.text, estilo, layer.alinhamento);
  final spec = layer.textPath;
  final caminho = spec.active ? (path ?? buildTextPath(spec)) : null;
  final size = caminho == null || caminho.getBounds().isEmpty
      ? diagrama.cheio.size
      : caminho.getBounds().inflate(layer.fontSize).size;
  return _AnimatedTextPainter(
    layer: layer,
    style: estilo,
    full: diagrama.cheio,
    units: diagrama.unidades,
    diagrama: diagrama,
    localTime: localTime,
    path: caminho,
  ).._tamanhoDoTeste = size;
}

/// A linha medida uma vez por (texto, estilo, alinhamento): o painter da
/// linha, as unidades e a caixa de cada unidade. Cache pequeno, o mais
/// antigo sai.
class _Diagrama {
  _Diagrama._(this.texto, this.estilo, this.alinhamento)
    : cheio = TextPainter(
        text: TextSpan(text: texto, style: estilo),
        textDirection: direcaoDoTexto(texto),
        textAlign: alinhamento,
      )..layout(),
      unidades = TextUnits.of(texto),
      direcao = direcaoDoTexto(texto);

  final String texto;
  final TextStyle estilo;
  final TextAlign alinhamento;

  /// A DIRECAO BASE, tirada do proprio texto. Ver [direcaoDoTexto].
  final TextDirection direcao;
  final TextPainter cheio;
  final TextUnits unidades;
  final Map<int, Rect?> _caixas = {};
  final Map<int, Rect?> _recortes = {};

  static final Map<Object, _Diagrama> _cache = {};

  static _Diagrama de(String texto, TextStyle estilo, TextAlign alinhamento) {
    final chave = Object.hash(texto, estilo, alinhamento);
    final achado = _cache.remove(chave);
    if (achado != null &&
        achado.texto == texto &&
        achado.estilo == estilo &&
        achado.alinhamento == alinhamento) {
      _cache[chave] = achado;
      return achado;
    }
    if (_cache.length >= 32) _cache.remove(_cache.keys.first);
    return _cache[chave] = _Diagrama._(texto, estilo, alinhamento);
  }

  /// A caixa da unidade [i] na linha inteira (nulo quando nao ha).
  Rect? caixa(int i) => _caixas.putIfAbsent(i, () {
    final boxes = cheio.getBoxesForSelection(
      TextSelection(
        baseOffset: unidades.codeUnitStart[i],
        extentOffset: unidades.codeUnitEnd[i],
      ),
      boxHeightStyle: BoxHeightStyle.tight,
    );
    if (boxes.isEmpty) return null;
    var rect = boxes.first.toRect();
    for (final b in boxes.skip(1)) {
      rect = rect.expandToInclude(b.toRect());
    }
    return rect;
  });

  /// O PEDACO DA LINHA QUE PERTENCE A UNIDADE [i].
  ///
  /// A CAIXA DA UNIDADE NAO SERVE COMO RECORTE quando a escrita e cursiva:
  /// a caixa e a TINTA da letra, e em arabe a tinta de duas letras vizinhas
  /// se ENCOSTA — recortar pela tinta traria um pedaco da vizinha junto. A
  /// divisa vai no MEIO da distancia entre as duas, que e o unico lugar
  /// onde nao ha tinta de ninguem.
  ///
  /// Verticalmente o problema nao existe: a caixa ja e a faixa da linha, e
  /// crescer um pixel para cima e para baixo so pega antialias.
  Rect? recorte(int i) => _recortes.putIfAbsent(i, () {
    final r = caixa(i);
    if (r == null) return null;
    var esq = r.left;
    var dir = r.right;
    for (var j = 0; j < unidades.length; j++) {
      if (j == i) continue;
      final o = caixa(j);
      if (o == null || o.isEmpty) continue;
      if (o.bottom <= r.top || o.top >= r.bottom) continue;
      if (o.right <= r.left) esq = math.max(esq, (o.right + r.left) / 2);
      if (o.left >= r.right) dir = math.min(dir, (o.left + r.right) / 2);
    }
    return Rect.fromLTRB(esq, r.top - 1, dir, r.bottom + 1);
  });
}

/// A PALAVRA desenhada, por (texto, estilo, alinhamento, direcao).
///
/// ERA A LETRA, e essa era a causa do relato. Uma letra arabe construida
/// sozinha recebe a forma ISOLADA — o Shaper nao tem com quem ligar — e o
/// texto inteiro saia com as letras soltas depois de aplicar o animador.
/// A juncao vem do contexto dentro da palavra; a palavra e a menor unidade
/// que pode ser moldada sozinha.
///
/// O mais antigo sai SEM dispose: pode estar em uso no mesmo quadro, e o
/// paragrafo nativo e liberado pelo coletor.
final Map<Object, (String, TextStyle, TextoNoAtlas)> _palavras = {};

TextoNoAtlas _palavraNoAtlas(
  String texto,
  TextStyle estilo,
  TextAlign alinhamento,
  TextDirection direcao,
) {
  final chave = Object.hash(texto, estilo, alinhamento, direcao);
  final achada = _palavras.remove(chave);
  if (achada != null &&
      achada.$1 == texto &&
      achada.$2 == estilo &&
      achada.$3.cheio.textAlign == alinhamento &&
      achada.$3.cheio.textDirection == direcao) {
    _palavras[chave] = achada;
    return achada.$3;
  }
  if (_palavras.length >= 512) _palavras.remove(_palavras.keys.first);
  final nova = TextoNoAtlas(
    texto: texto,
    estilo: estilo,
    alinhamento: alinhamento,
    direcao: direcao,
  )..layout();
  _palavras[chave] = (texto, estilo, nova);
  return nova;
}

class _AnimatedTextPainter extends CustomPainter {
  _AnimatedTextPainter({
    required this.layer,
    required this.style,
    required this.full,
    required this.units,
    required this.diagrama,
    required this.localTime,
    this.path,
  });

  final _Diagrama diagrama;

  final TextLayer layer;
  final TextStyle style;
  final TextPainter full;
  final TextUnits units;
  final Duration localTime;
  final Path? path;

  /// Tamanho usado quando quem chama nao passa um (o caso do teste).
  Size _tamanhoDoTeste = Size.zero;
  Size get tamanhoDeTeste => _tamanhoDoTeste;

  @override
  void paint(Canvas canvas, Size size) {
    final t = localTime;
    final animators = [
      for (final a in layer.effectiveAnimators(units.length, units: units))
        if (a.enabled && a.properties.isNotEmpty) a,
    ];

    var runningTracking = 0.0;

    for (var i = 0; i < units.length; i++) {
      final trackingShift = runningTracking;

      // Acumula o transform desta unidade pelos animadores em pilha.
      var dx = 0.0, dy = 0.0, rotation = 0.0, tracking = 0.0;
      var blur = 0.0, skew = 0.0, hue = 0.0;
      var rotX = 0.0, rotY = 0.0, dz = 0.0;
      var scaleP = 100.0, opacityP = 100.0;
      var scaleXP = 100.0, scaleYP = 100.0;
      var satP = 100.0, brightP = 100.0;
      for (final a in animators) {
        final c = units.coverageFor(
          a.selectors,
          i,
          t,
          allowOvershoot: a.allowOvershoot,
        );
        for (final p in a.properties) {
          switch (p.type) {
            case TextAnimProp.positionX:
              dx = p.apply(dx, t, c);
            case TextAnimProp.positionY:
              dy = p.apply(dy, t, c);
            case TextAnimProp.rotation:
              rotation = p.apply(rotation, t, c);
            case TextAnimProp.tracking:
              tracking = p.apply(tracking, t, c);
            case TextAnimProp.scale:
              scaleP = p.apply(scaleP, t, c);
            case TextAnimProp.opacity:
              opacityP = p.apply(opacityP, t, c);
            case TextAnimProp.scaleX:
              scaleXP = p.apply(scaleXP, t, c);
            case TextAnimProp.scaleY:
              scaleYP = p.apply(scaleYP, t, c);
            case TextAnimProp.blur:
              blur = p.apply(blur, t, c);
            case TextAnimProp.skew:
              skew = p.apply(skew, t, c);
            case TextAnimProp.hue:
              hue = p.apply(hue, t, c);
            case TextAnimProp.saturation:
              satP = p.apply(satP, t, c);
            case TextAnimProp.brightness:
              brightP = p.apply(brightP, t, c);
            case TextAnimProp.rotationX:
              rotX = p.apply(rotX, t, c);
            case TextAnimProp.rotationY:
              rotY = p.apply(rotY, t, c);
            case TextAnimProp.positionZ:
              dz = p.apply(dz, t, c);
          }
        }
      }
      runningTracking += tracking;

      if (units.isWhitespace[i]) continue;

      // Avanco SEMPRE da medicao da linha completa (§3.4).
      final rect = diagrama.caixa(i);
      if (rect == null) continue;

      final opacity = (opacityP / 100).clamp(0.0, 1.0);
      if (opacity <= 0.001) continue;
      final sx = math.max(0.0, (scaleP / 100) * (scaleXP / 100));
      final sy = math.max(0.0, (scaleP / 100) * (scaleYP / 100));
      if (sx <= 0.001 || sy <= 0.001) continue;

      // A PALAVRA INTEIRA, e nao a letra — ver [_palavraNoAtlas]. O que
      // recorta esta unidade e o `clipRect` do desenho.
      final unidade = _palavraNoAtlas(
        layer.text,
        style,
        layer.alinhamento,
        diagrama.direcao,
      );
      // O RECORTE NAO PODE CORTAR A PROPRIA LETRA.
      //
      // A caixa da unidade e a TINTA da letra, e o recorte saia exatamente
      // dela. Tudo o que a animacao desenha FORA da tinta era decepado: a
      // letra ampliada, a letra girada, a letra que entra pela lateral, o
      // desfoque e o traco. Era esse o "texto cortado".
      //
      // O recorte agora cresce pelo que ESTA unidade ocupa de verdade —
      // escala, deslocamento, giro e desfoque — e sempre pela faixa da
      // linha, para acento, cedilha e descendente caberem. E conta, nao
      // chute: sai dos proprios numeros lidos acima.
      // Isolar a letra no espaco da fonte, DEPOIS do transform. Inflar
      // este recorte incluia letras vizinhas e duplicava a palavra; fazer
      // o clip antes do transform prendia a letra na caixa original.
      final recorte = diagrama.recorte(i) ?? rect;
      // A COR DA UNIDADE VIROU FILTRO, e nao estilo. Trocar a cor do
      // `TextStyle` obrigaria a um paragrafo por (palavra, cor) — e o
      // paragrafo e justamente quem carrega a juncao da escrita cursiva.
      final matriz = matrizDaUnidade(hue, satP, brightP, opacity);

      // Sobre o caminho, a posicao vem do AVANCO acumulado ao longo da
      // curva, nao da caixa da linha — e a diferenca entre letras
      // acompanhando a curva e letras enfileiradas em cima dela.
      Offset center;
      var pathAngle = 0.0;
      if (path != null) {
        final spec = layer.textPath;
        // O avanco natural da letra na linha vira a distancia
        // percorrida sobre a curva.
        final d = rect.center.dx + spec.offset + spec.spacing * i;
        final posto = placeOnPath(
          path!,
          d,
          spec: spec,
          glyphHeight: rect.height,
        );
        if (posto == null) continue;
        center = posto.position + Offset(size.width / 2, size.height / 2);
        pathAngle = posto.angleRad;
      } else {
        center = rect.center;
      }

      canvas.save();
      // O RECORTE VEM PRIMEIRO, no espaco da LINHA: depois dos giros ele
      // nao seria mais um retangulo alinhado, e recortar uma letra girada
      // com um retangulo torto nao e a mesma coisa.
      // DESFOQUE POR UNIDADE: e o que faz "aparecer em desfoque" existir.
      // Sem isto so da para borrar a camada inteira, que e outra coisa.
      final blurring = blur > 0.05;
      if (blurring) {
        canvas.saveLayer(
          null,
          Paint()
            ..imageFilter = ui.ImageFilter.blur(sigmaX: blur, sigmaY: blur),
        );
      }
      canvas.translate(center.dx + dx + trackingShift, center.dy + dy);
      if (pathAngle != 0) canvas.rotate(pathAngle);
      if (rotation != 0) canvas.rotate(rotation * math.pi / 180);
      // 3D DA UNIDADE: perspectiva com a focal do app (1200), a mesma
      // dos solidos e das particulas. Z positivo afasta (encolhe).
      if (rotX != 0 || rotY != 0 || dz != 0) {
        const focal = 1200.0;
        final m = Matrix4.identity()..setEntry(3, 2, -1 / focal);
        if (dz != 0) {
          final k = (focal / (focal + dz)).clamp(0.05, 8.0);
          m.scaleByDouble(k, k, 1, 1);
        }
        if (rotX != 0) m.rotateX(rotX * math.pi / 180);
        if (rotY != 0) m.rotateY(rotY * math.pi / 180);
        canvas.transform(m.storage);
      }
      if (skew != 0) {
        canvas.transform(
          Float64List.fromList(<double>[
            1, 0, 0, 0, //
            math.tan(-skew * math.pi / 180), 1, 0, 0, //
            0, 0, 1, 0, //
            0, 0, 0, 1,
          ]),
        );
      }
      if (sx != 1 || sy != 1) canvas.scale(sx, sy);
      // VOLTA PARA O ESPACO DA LINHA. Toda a transformacao acima age em
      // torno do centro DESTA unidade, entao desenhar a palavra no lugar
      // de sempre move esta letra e deixa as vizinhas paradas — que e o
      // ponto: movimento por letra, forma por palavra.
      canvas.translate(-rect.center.dx, -rect.center.dy);
      canvas.clipRect(recorte, doAntiAlias: false);
      unidade.paintDaPalavra(canvas, Offset.zero, matrizDeCor: matriz);
      canvas.restore();
      if (blurring) canvas.restore();
    }
  }

  @override
  bool shouldRepaint(_AnimatedTextPainter old) => true;
}
