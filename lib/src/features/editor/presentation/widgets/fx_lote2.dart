import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import '../../domain/blob_track.dart';
import '../../domain/pixel_sort.dart';
import 'package:flutter/rendering.dart';

/// EFEITOS DO LOTE 2.
///
/// Vários deles precisam da camada COMO IMAGEM para funcionar de
/// verdade — deslocar turbulento, entortar, ordenar pixels e semear nao
/// sao filtros de cor, sao redistribuicao de pixels. O [SnapshotWidget]
/// rasteriza a subarvore e entrega uma `ui.Image`, e dai para frente e
/// malha e `drawVertices`, que a GPU faz de graca. A foto e refeita a
/// cada reconstrucao do efeito — ver [_FxSnapshotState.didUpdateWidget].

// ------------------------------------------------------------ ruido

/// Ruido de valor deterministico: funcao pura de (x, y, semente).
double fxNoise(double x, double y, int seed) {
  int h = seed * 374761393;
  h += x.floor() * 668265263;
  h ^= y.floor() * 2246822519;
  h = (h ^ (h >> 13)) * 1274126177;
  return ((h ^ (h >> 16)) & 0x7fffffff) / 0x7fffffff;
}

double _smooth(double t) => t * t * (3 - 2 * t);

/// Ruido interpolado (continuo no espaco).
double fxValueNoise(double x, double y, int seed) {
  final xi = x.floorToDouble(), yi = y.floorToDouble();
  final xf = x - xi, yf = y - yi;
  final a = fxNoise(xi, yi, seed);
  final b = fxNoise(xi + 1, yi, seed);
  final c = fxNoise(xi, yi + 1, seed);
  final d = fxNoise(xi + 1, yi + 1, seed);
  final u = _smooth(xf), v = _smooth(yf);
  return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}

/// Ruido fractal (varias oitavas) — a base do deslocar turbulento.
double fxFractal(double x, double y, int seed, int octaves) {
  var amp = 1.0, freq = 1.0, sum = 0.0, norm = 0.0;
  for (var i = 0; i < octaves; i++) {
    sum += fxValueNoise(x * freq, y * freq, seed + i * 101) * amp;
    norm += amp;
    amp *= 0.5;
    freq *= 2.0;
  }
  return norm <= 0 ? 0 : sum / norm;
}

// ------------------------------------------------- base de snapshot

/// Envelope que rasteriza o filho e deixa um pintor trabalhar em cima
/// da imagem.
class FxSnapshot extends StatefulWidget {
  const FxSnapshot({
    super.key,
    required this.painter,
    required this.child,
    this.mode = SnapshotMode.forced,
  });

  final SnapshotPainter painter;
  final Widget child;

  /// FORCADO — e tem de continuar forcado.
  ///
  /// O efeito PRECISA da imagem para existir: sem a foto, nao ha o que
  /// mandar para o shader. Tentei trocar por `permissive` achando que
  /// ele salvaria a textura de video, e o resultado foi a composicao
  /// INTEIRA sair preta — o modo permissivo nao entrega a imagem, e o
  /// pintor desenha em cima do nada.
  ///
  /// A textura de video continua nao sobrevivendo a foto (ela nao entra
  /// em `toImage`), e a solucao para isso e outra: quem embrulha a
  /// composicao toda — o dithering — simplesmente NAO ACONTECE quando ha
  /// video na cena.
  final SnapshotMode mode;

  @override
  State<FxSnapshot> createState() => _FxSnapshotState();
}

/// Controle com um jeito de dizer "essa foto venceu".
class _FxSnapshotController extends SnapshotController {
  _FxSnapshotController() : super(allowSnapshotting: true);

  /// Joga fora o raster guardado e manda pintar de novo.
  void invalidar() => notifyListeners();
}

class _FxSnapshotState extends State<FxSnapshot> {
  // Ligado o tempo todo: o efeito PRECISA da imagem para existir.
  final _FxSnapshotController _controller = _FxSnapshotController();

  @override
  void didUpdateWidget(FxSnapshot old) {
    super.didUpdateWidget(old);
    // A FOTO NAO SE INVALIDA SOZINHA — e esse era o defeito.
    //
    // O SnapshotWidget guarda o raster no objeto de render e so o
    // descarta em casos que nao incluem "o filho mudou": o filho e
    // pintado num layer separado, entao o markNeedsPaint dele nao
    // alcanca a foto. Na pratica a camada congelava no primeiro quadro
    // pintado e ficava assim para sempre — dar play e nada andar,
    // apagar e continuar na tela.
    //
    // Reconstruiu o efeito = o conteudo pode ter mudado = foto vencida.
    _controller.invalidar();
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => SnapshotWidget(
        controller: _controller,
        mode: widget.mode,
        painter: widget.painter,
        child: widget.child,
      );
}

/// Base comum: quando o snapshot nao esta disponivel, pinta o filho
/// normalmente em vez de sumir com ele.
abstract class _FxPainter extends SnapshotPainter {
  @override
  void paint(PaintingContext context, Offset offset, Size size,
      PaintingContextCallback painter) {
    painter(context, offset);
  }
}

Rect _dst(Offset offset, Size size) => offset & size;

/// Shader que mapeia a imagem no retangulo de destino.
ui.ImageShader _imageShader(ui.Image image, Offset offset, Size size) {
  final m = Matrix4.identity()
    ..translateByDouble(offset.dx, offset.dy, 0, 1)
    ..scaleByDouble(
        size.width / image.width, size.height / image.height, 1, 1);
  return ui.ImageShader(
      image, TileMode.clamp, TileMode.clamp, m.storage);
}

/// Desenha uma malha deformada: para cada no da grade, [displace]
/// devolve o deslocamento em pixels.
void _drawMesh(
  Canvas canvas,
  ui.Image image,
  Offset offset,
  Size size, {
  required int cols,
  required int rows,
  required Offset Function(double u, double v) displace,
  bool antiAlias = true,
}) {
  final nx = cols + 1, ny = rows + 1;
  final positions = <double>[];
  final texcoords = <double>[];
  final indices = <int>[];

  for (var j = 0; j < ny; j++) {
    for (var i = 0; i < nx; i++) {
      final u = i / cols, v = j / rows;
      final tx = u * size.width, ty = v * size.height;
      final d = displace(u, v);
      positions.add(offset.dx + tx + d.dx);
      positions.add(offset.dy + ty + d.dy);
      texcoords.add(offset.dx + tx);
      texcoords.add(offset.dy + ty);
    }
  }
  for (var j = 0; j < rows; j++) {
    for (var i = 0; i < cols; i++) {
      final a = j * nx + i, b = a + 1, c = a + nx, d = c + 1;
      indices..addAll([a, b, c])..addAll([b, d, c]);
    }
  }

  canvas.drawVertices(
    ui.Vertices.raw(
      ui.VertexMode.triangles,
      Float32List.fromList(positions),
      textureCoordinates: Float32List.fromList(texcoords),
      indices: Uint16List.fromList(indices),
    ),
    BlendMode.srcOver,
    Paint()
      ..isAntiAlias = antiAlias
      ..shader = _imageShader(image, offset, size)
      ..filterQuality = FilterQuality.low,
  );
}

// --------------------------------------------- deslocar turbulento

/// DESLOCAR TURBULENTO: cada ponto da malha anda conforme um ruido
/// fractal. Evoluir o ruido no tempo e o que faz o liquido "viver".
class TurbulentDisplacePainter extends _FxPainter {
  TurbulentDisplacePainter({
    required this.amount,
    required this.scale,
    required this.complexity,
    required this.evolution,
    required this.seed,
  });

  final double amount;
  final double scale;
  final double complexity;
  final double evolution;
  final int seed;

  @override
  void paintSnapshot(PaintingContext context, Offset offset, Size size,
      ui.Image image, Size sourceSize, double pixelRatio) {
    if (amount.abs() < 0.5 || size.isEmpty) {
      context.canvas.drawImageRect(
          image,
          Rect.fromLTWH(0, 0, image.width.toDouble(),
              image.height.toDouble()),
          _dst(offset, size),
          Paint()..filterQuality = FilterQuality.low);
      return;
    }
    final oct = complexity.round().clamp(1, 5);
    final s = math.max(4.0, scale);
    final ev = evolution / 60.0;
    final cols = (size.width / 18).clamp(8, 40).round();
    final rows = (size.height / 18).clamp(8, 40).round();

    _drawMesh(
      context.canvas,
      image,
      offset,
      size,
      cols: cols,
      rows: rows,
      displace: (u, v) {
        final x = u * size.width / s + ev;
        final y = v * size.height / s + ev;
        final dx = (fxFractal(x, y, seed, oct) - 0.5) * 2;
        final dy = (fxFractal(x + 37.7, y - 11.3, seed + 5, oct) - 0.5) * 2;
        return Offset(dx * amount, dy * amount);
      },
    );
  }

  @override
  bool shouldRepaint(covariant TurbulentDisplacePainter old) =>
      old.amount != amount ||
      old.scale != scale ||
      old.complexity != complexity ||
      old.evolution != evolution ||
      old.seed != seed;
}

// ------------------------------------------------------- entortar

/// ENTORTAR: a malha ganha um arco. Curvatura muda o quanto o arco e
/// concentrado no meio.
class BendPainter extends _FxPainter {
  BendPainter({
    required this.amount,
    required this.vertical,
    required this.curvature,
    required this.anchor,
  });

  final double amount;
  final bool vertical;
  final double curvature;
  final double anchor;

  @override
  void paintSnapshot(PaintingContext context, Offset offset, Size size,
      ui.Image image, Size sourceSize, double pixelRatio) {
    if (amount.abs() < 0.5 || size.isEmpty) {
      context.canvas.drawImageRect(
          image,
          Rect.fromLTWH(
              0, 0, image.width.toDouble(), image.height.toDouble()),
          _dst(offset, size),
          Paint()..filterQuality = FilterQuality.low);
      return;
    }
    final k = curvature.clamp(0.2, 4.0);
    _drawMesh(
      context.canvas,
      image,
      offset,
      size,
      cols: vertical ? 12 : 28,
      rows: vertical ? 28 : 12,
      displace: (u, v) {
        // Perfil de arco: 0 nas pontas, 1 na ancora.
        final t = vertical ? v : u;
        final d = (t - anchor).abs() / math.max(1e-6, math.max(anchor, 1 - anchor));
        final w = math.pow(1 - d.clamp(0.0, 1.0), k).toDouble();
        return vertical
            ? Offset(amount * w, 0)
            : Offset(0, amount * w);
      },
    );
  }

  @override
  bool shouldRepaint(covariant BendPainter old) =>
      old.amount != amount ||
      old.vertical != vertical ||
      old.curvature != curvature ||
      old.anchor != anchor;
}

// -------------------------------------------------- ordenar pixels

/// ORDENAR PIXELS: as faixas mais claras que o limiar sao esticadas na
/// direcao escolhida, que e o rastro que o pixel sorting produz.
/// PIXEL SORTER — arrasta os pixels ao longo de linhas.
///
/// O que esta implementado e o ARRASTO por faixa, em tres arrumacoes de
/// linha: Linear (em qualquer angulo), Radial (raios saindo do centro) e
/// Circular (aneis em volta do centro).
///
/// O que NAO esta: a ordenacao pixel a pixel de verdade. Ela e
/// sequencial, precisa dos bytes da imagem, e em Dart no fio da
/// interface derrubaria o preview. O caminho certo e o mesmo do Blob
/// Tracker — analisar sob comando e guardar o resultado — e esta
/// anotado como pendente, nao disfarcado.
/// PIXEL SORTER — de verdade, lendo os pixels.
///
/// O efeito classico ordena, em cada linha, os trechos onde o brilho
/// passa de um limiar. E isso que faz um rosto escorrer em faixas e o
/// cabelo de uma estatua derreter para baixo: o escorrido nasce ONDE A
/// IMAGEM E CLARA, nao de ruido sorteado. A versao anterior esticava
/// fatias por ruido — parecia glitch, nunca pixel sorting.
///
/// Ordenar exige ler pixel, e ler pixel na GPU nao existe em passe
/// unico. Entao o caminho e este:
///
///   1. no `paint`, a foto da camada e desenhada REDUZIDA num buffer
///      (e girada, se o angulo pede) — `toImageSync`, ainda na GPU;
///   2. o buffer e lido para a CPU (`toByteData`, assincrono) e a conta
///      pura de [pixelSort] roda num isolate;
///   3. o resultado volta como imagem e fica num CACHE por efeito, que
///      sobrevive a reconstrucao do pintor. Sem isso, cada rebuild
///      apagaria o resultado e a tela piscaria entre ordenado e cru.
///
/// Enquanto a conta roda, o pintor mostra o ULTIMO resultado (ou a
/// imagem crua, na primeira vez). Em 360 px de lado sao poucos
/// milissegundos por quadro.
class PixelSortPainter extends _FxPainter {
  PixelSortPainter({
    required this.cacheKey,
    required this.mode,
    required this.sortAngle,
    required this.threshold,
    required this.aboveThreshold,
    required this.reverse,
    required this.sortBy,
    required this.length,
    required this.randomRestart,
    required this.seed,
    required this.sortResolution,
    required this.downsample,
    required this.matteBlur,
    required this.blendWithOriginal,
    required this.show,
    required this.softEdges,
    required this.centerX,
    required this.centerY,
    required this.startAngle,
    required this.degreesSorted,
    required this.innerRadius,
    required this.radiusVariation,
    required this.startVariation,
    required this.thickness,
  }) : _cache = _SortCache.of(cacheKey);

  /// Identifica a INSTANCIA do efeito: e a chave do cache do resultado.
  final String cacheKey;

  /// 0 Linear, 1 Radial, 2 Circular.
  final int mode;
  final double sortAngle;
  final double threshold;
  final bool aboveThreshold;
  final bool reverse;

  /// 0 luminancia, 1 matiz, 2 saturacao.
  final int sortBy;

  /// Comprimento maximo do trecho, em fracao da linha.
  final double length;

  /// Reinicios aleatorios, 0..1000 na ficha.
  final double randomRestart;
  final int seed;

  /// Lado maior do buffer em que a ordenacao acontece.
  final double sortResolution;
  final double downsample;

  /// Desfoque 1D do matte, em pixels do buffer.
  final double matteBlur;
  final double blendWithOriginal;

  /// 0 Result, 1 Raw Values, 2 Threshold Matte, 3 Restart Noise.
  final int show;
  final bool softEdges;

  final double centerX;
  final double centerY;
  final double startAngle;
  final double degreesSorted;
  final double innerRadius;
  final double radiusVariation;
  final double startVariation;
  final double thickness;

  final _SortCache _cache;

  // O objeto de render escuta o PINTOR; o pintor nasce de novo a cada
  // rebuild. Repassar o ouvinte ao cache e o que faz o resultado que
  // chega depois repintar o pintor ATUAL, seja ele qual for.
  @override
  void addListener(VoidCallback listener) {
    super.addListener(listener);
    _cache.addListener(listener);
  }

  @override
  void removeListener(VoidCallback listener) {
    super.removeListener(listener);
    _cache.removeListener(listener);
  }

  PixelSortSpec get _spec => PixelSortSpec(
        mode: PixelSortMode.values[mode.clamp(0, 2)],
        threshold: threshold,
        above: aboveThreshold,
        reverse: reverse,
        key: PixelSortKey.values[sortBy.clamp(0, 2)],
        maxRun: length,
        // 0..1000 na ficha -> 0..100 na conta (probabilidade / 1000).
        restart: randomRestart / 10.0,
        seed: seed,
        matteBlur: matteBlur.round().clamp(0, 20),
        centerX: centerX,
        centerY: centerY,
        startAngle: startAngle,
        degreesSorted: degreesSorted,
        innerRadius: innerRadius,
        radiusVariation: radiusVariation,
        startVariation: startVariation,
        thickness: thickness,
      );

  String get _assinatura => [
        mode, sortAngle, threshold, aboveThreshold, reverse, sortBy, length,
        randomRestart, seed, sortResolution, downsample, matteBlur, centerX,
        centerY, startAngle, degreesSorted, innerRadius, radiusVariation,
        startVariation, thickness,
      ].join('|');

  bool get _linearComAngulo => mode == 0 && sortAngle.abs() % 360 > 0.01;

  @override
  void paintSnapshot(PaintingContext context, Offset offset, Size size,
      ui.Image image, Size sourceSize, double pixelRatio) {
    final canvas = context.canvas;
    if (size.isEmpty) return;
    final src = Rect.fromLTWH(
        0, 0, image.width.toDouble(), image.height.toDouble());
    final dst = _dst(offset, size);

    // DIAGNOSTICO: os modos de "Show" sao como se descobre por que o
    // efeito nao pegou onde devia.
    if (show == 3) {
      _mostrarRuido(canvas, dst, size);
      return;
    }
    if (show == 2) {
      _mostrarMatte(canvas, image, src, dst);
      return;
    }
    if (show == 1) {
      _mostrarChave(canvas, image, src, dst);
      return;
    }

    // Poe a conta para rodar (ou reaproveita o que ja esta pronto).
    _agendar(image);

    final pronto = _cache.resultado;
    // A ORDENACAO ACONTECE MENOR e o resultado e AMPLIADO de volta.
    // Ampliar sem filtro transforma cada pixel ordenado num quadradinho,
    // e o efeito, que deveria escorrer, sai picotado — parece dither, e
    // foi exatamente o que apareceu na primeira prova no aparelho.
    // "Soft edges" escolhe o quanto suavizar, nunca se suaviza.
    final pintura = Paint()
      ..filterQuality =
          softEdges ? FilterQuality.medium : FilterQuality.low;

    if (pronto == null) {
      canvas.drawImageRect(image, src, dst, pintura);
    } else {
      canvas.save();
      canvas.clipRect(dst);
      final bufRect = Rect.fromLTWH(
          0, 0, pronto.width.toDouble(), pronto.height.toDouble());
      if (_cache.anguloRad.abs() > 1e-6) {
        // O buffer foi girado por -angulo antes de ordenar; desgira.
        final e = _cache.escala;
        canvas.translate(dst.center.dx, dst.center.dy);
        canvas.rotate(_cache.anguloRad);
        canvas.drawImageRect(
          pronto,
          bufRect,
          Rect.fromCenter(
              center: Offset.zero,
              width: pronto.width / e * (size.width / image.width),
              height: pronto.height / e * (size.height / image.height)),
          pintura,
        );
      } else {
        canvas.drawImageRect(pronto, bufRect, dst, pintura);
      }
      canvas.restore();
    }

    // BLEND WITH ORIGINAL em 1 tem de devolver a imagem INTACTA — e o
    // valor "desligado" da ficha, e a prova de neutralidade.
    if (blendWithOriginal > 0.001) {
      canvas.drawImageRect(
        image,
        src,
        dst,
        Paint()
          ..filterQuality = FilterQuality.low
          ..color = Colors.white
              .withValues(alpha: blendWithOriginal.clamp(0.0, 1.0)),
      );
    }
  }

  /// Reduz (e gira) a foto para o buffer de ordenacao, na GPU, e entrega
  /// a conta. Se ja ha conta rodando, guarda o pedido mais novo e joga
  /// fora o anterior: so o quadro mais recente interessa.
  void _agendar(ui.Image image) {
    final assinatura = _assinatura;
    final lado = (sortResolution / downsample.clamp(1.0, 4.0))
        .clamp(64.0, 1080.0);
    final escala = lado / math.max(image.width, image.height);
    final w = math.max(2, (image.width * escala).round());
    final h = math.max(2, (image.height * escala).round());
    final girar = _linearComAngulo;
    final ang = girar ? sortAngle * math.pi / 180 : 0.0;
    // Girado, o buffer e um quadrado que contem a imagem em qualquer
    // angulo; o que sobra fica transparente e nunca entra num trecho.
    final lado2 = girar ? math.sqrt(w * w + h * h).ceil() : 0;
    final bufW = girar ? lado2 : w;
    final bufH = girar ? lado2 : h;

    final rec = ui.PictureRecorder();
    final c = Canvas(rec);
    c.translate(bufW / 2, bufH / 2);
    if (girar) c.rotate(-ang);
    c.scale(escala, escala);
    c.drawImage(image, Offset(-image.width / 2, -image.height / 2),
        Paint()..filterQuality = FilterQuality.low);
    final pequena = rec.endRecording().toImageSync(bufW, bufH);

    final pedido = _Pedido(
      imagem: pequena,
      w: bufW,
      h: bufH,
      escala: escala,
      anguloRad: ang,
      assinatura: assinatura,
      spec: _spec,
    );
    if (_cache.ocupado) {
      _cache.pendente?.imagem.dispose();
      _cache.pendente = pedido;
      return;
    }
    _cache.rodar(pedido);
  }

  /// RAW VALUES: a chave em cinza — o que o limiar esta lendo.
  void _mostrarChave(Canvas canvas, ui.Image image, Rect src, Rect dst) {
    canvas.drawImageRect(
      image,
      src,
      dst,
      Paint()
        ..filterQuality = FilterQuality.low
        ..colorFilter = const ColorFilter.matrix(<double>[
          0.2126, 0.7152, 0.0722, 0, 0, //
          0.2126, 0.7152, 0.0722, 0, 0,
          0.2126, 0.7152, 0.0722, 0, 0,
          0, 0, 0, 1, 0,
        ]),
    );
  }

  /// O RUIDO DE REINICIO, desenhado: onde os trechos tendem a quebrar.
  void _mostrarRuido(Canvas canvas, Rect dst, Size size) {
    canvas.drawRect(dst, Paint()..color = const Color(0xFF000000));
    const linhas = 96;
    final passo = size.height / linhas;
    for (var i = 0; i < linhas; i++) {
      final v = fxNoise(i.toDouble(), 0, seed);
      canvas.drawRect(
        Rect.fromLTWH(dst.left, dst.top + i * passo, dst.width, passo),
        Paint()
          ..color = Color.fromRGBO(
              (v * 255).round(), (v * 255).round(), (v * 255).round(), 1),
      );
    }
  }

  /// O MATTE DO LIMIAR: branco onde o efeito age, preto onde nao age.
  void _mostrarMatte(Canvas canvas, ui.Image image, Rect src, Rect dst) {
    canvas.saveLayer(dst, Paint());
    canvas.drawImageRect(
        image, src, dst, Paint()..filterQuality = FilterQuality.low);
    // Luminancia -> preto e branco, cortada no limiar.
    final corte = (threshold * 255).round().clamp(0, 255).toDouble();
    canvas.saveLayer(
      dst,
      Paint()
        ..colorFilter = ColorFilter.matrix(<double>[
          0.2126 * 255, 0.7152 * 255, 0.0722 * 255, 0, -corte * 255, //
          0.2126 * 255, 0.7152 * 255, 0.0722 * 255, 0, -corte * 255,
          0.2126 * 255, 0.7152 * 255, 0.0722 * 255, 0, -corte * 255,
          0, 0, 0, 1, 0,
        ]),
    );
    canvas.drawImageRect(
        image, src, dst, Paint()..filterQuality = FilterQuality.low);
    canvas.restore();
    canvas.restore();
  }

  @override
  bool shouldRepaint(covariant PixelSortPainter old) =>
      old.cacheKey != cacheKey ||
      old.mode != mode ||
      old.sortAngle != sortAngle ||
      old.threshold != threshold ||
      old.aboveThreshold != aboveThreshold ||
      old.reverse != reverse ||
      old.sortBy != sortBy ||
      old.length != length ||
      old.randomRestart != randomRestart ||
      old.seed != seed ||
      old.sortResolution != sortResolution ||
      old.downsample != downsample ||
      old.matteBlur != matteBlur ||
      old.blendWithOriginal != blendWithOriginal ||
      old.show != show ||
      old.softEdges != softEdges ||
      old.centerX != centerX ||
      old.centerY != centerY ||
      old.startAngle != startAngle ||
      old.degreesSorted != degreesSorted ||
      old.innerRadius != innerRadius ||
      old.radiusVariation != radiusVariation ||
      old.startVariation != startVariation ||
      old.thickness != thickness;
}

/// Um pedido de ordenacao: o buffer ja reduzido e os parametros.
class _Pedido {
  const _Pedido({
    required this.imagem,
    required this.w,
    required this.h,
    required this.escala,
    required this.anguloRad,
    required this.assinatura,
    required this.spec,
  });

  final ui.Image imagem;
  final int w;
  final int h;
  final double escala;
  final double anguloRad;
  final String assinatura;
  final PixelSortSpec spec;
}

/// O RESULTADO ORDENADO de um efeito, vivo entre reconstrucoes.
///
/// E um ChangeNotifier porque o objeto de render escuta o pintor, e o
/// pintor nasce de novo a cada rebuild: o pintor atual repassa o seu
/// ouvinte para ca, e quando a conta termina e AQUI que se avisa.
class _SortCache extends ChangeNotifier {
  _SortCache._();

  static final Map<String, _SortCache> _todos = {};
  static const _maximo = 8;

  /// Um cache por efeito, com teto: efeito removido ha muito tempo nao
  /// pode segurar imagem para sempre.
  static _SortCache of(String chave) {
    final existente = _todos[chave];
    if (existente != null) return existente;
    if (_todos.length >= _maximo) {
      final velha = _todos.keys.first;
      _todos.remove(velha)?._descartar();
    }
    return _todos[chave] = _SortCache._();
  }

  ui.Image? resultado;
  double escala = 1;
  double anguloRad = 0;
  String assinatura = '';
  int hashFonte = 0;
  bool ocupado = false;
  _Pedido? pendente;

  void _descartar() {
    resultado?.dispose();
    resultado = null;
    pendente?.imagem.dispose();
    pendente = null;
  }

  Future<void> rodar(_Pedido pedido) async {
    ocupado = true;
    try {
      final dados =
          await pedido.imagem.toByteData(format: ui.ImageByteFormat.rawRgba);
      pedido.imagem.dispose();
      if (dados == null) return;
      final bytes = dados.buffer.asUint8List();

      // Mesma fonte, mesmos parametros: nada a fazer. Poupa a conta E a
      // troca de imagem, que e o que faria a tela piscar.
      var hash = pedido.w * 73856093 ^ pedido.h * 19349663;
      for (var i = 0; i < bytes.length; i += 251) {
        hash = (hash * 31 + bytes[i]) & 0x7fffffff;
      }
      if (hash == hashFonte &&
          pedido.assinatura == assinatura &&
          resultado != null) {
        return;
      }

      final s = pedido.spec;
      final saida = await compute(_ordenarEmIsolate, <String, Object>{
        'bytes': bytes,
        'w': pedido.w,
        'h': pedido.h,
        'mode': s.mode.index,
        'threshold': s.threshold,
        'above': s.above,
        'reverse': s.reverse,
        'key': s.key.index,
        'maxRun': s.maxRun,
        'restart': s.restart,
        'seed': s.seed,
        'matteBlur': s.matteBlur,
        'centerX': s.centerX,
        'centerY': s.centerY,
        'startAngle': s.startAngle,
        'degreesSorted': s.degreesSorted,
        'innerRadius': s.innerRadius,
        'radiusVariation': s.radiusVariation,
        'startVariation': s.startVariation,
        'thickness': s.thickness,
      });

      final done = Completer<ui.Image>();
      ui.decodeImageFromPixels(
          saida, pedido.w, pedido.h, ui.PixelFormat.rgba8888, done.complete);
      final nova = await done.future;

      resultado?.dispose();
      resultado = nova;
      escala = pedido.escala;
      anguloRad = pedido.anguloRad;
      assinatura = pedido.assinatura;
      hashFonte = hash;
      notifyListeners();
    } catch (_) {
      // Leitura da GPU pode falhar num aparelho sem suporte: fica a
      // imagem crua, sem derrubar o preview.
    } finally {
      ocupado = false;
      final proximo = pendente;
      pendente = null;
      if (proximo != null) {
        // O quadro que chegou enquanto a conta rodava.
        unawaited(rodar(proximo));
      }
    }
  }
}

/// A conta, num isolate: recebe primitivos e devolve bytes.
Uint8List _ordenarEmIsolate(Map<String, Object> m) {
  final spec = PixelSortSpec(
    mode: PixelSortMode.values[m['mode'] as int],
    threshold: m['threshold'] as double,
    above: m['above'] as bool,
    reverse: m['reverse'] as bool,
    key: PixelSortKey.values[m['key'] as int],
    maxRun: m['maxRun'] as double,
    restart: m['restart'] as double,
    seed: m['seed'] as int,
    matteBlur: m['matteBlur'] as int,
    centerX: m['centerX'] as double,
    centerY: m['centerY'] as double,
    startAngle: m['startAngle'] as double,
    degreesSorted: m['degreesSorted'] as double,
    innerRadius: m['innerRadius'] as double,
    radiusVariation: m['radiusVariation'] as double,
    startVariation: m['startVariation'] as double,
    thickness: m['thickness'] as double,
  );
  return pixelSort(
      m['bytes'] as Uint8List, m['w'] as int, m['h'] as int, spec);
}

// -------------------------------------------------------- CC Semear

/// CC SEMEAR: a imagem vira graos e os graos se espalham. Transferencia
/// controla quanto do original ainda aparece por baixo.
class ScatterizePainter extends _FxPainter {
  ScatterizePainter({
    required this.spread,
    required this.grain,
    required this.rotation,
    required this.transfer,
    required this.gravity,
    required this.seed,
  });

  final double spread;
  final double grain;
  final double rotation;
  final double transfer;
  final double gravity;
  final int seed;

  @override
  void paintSnapshot(PaintingContext context, Offset offset, Size size,
      ui.Image image, Size sourceSize, double pixelRatio) {
    final canvas = context.canvas;
    final src = Rect.fromLTWH(
        0, 0, image.width.toDouble(), image.height.toDouble());
    if (transfer < 0.999) {
      canvas.drawImageRect(
        image,
        src,
        _dst(offset, size),
        Paint()
          ..filterQuality = FilterQuality.low
          ..color = Colors.white
              .withValues(alpha: (1 - transfer).clamp(0.0, 1.0)),
      );
    }
    if (spread < 0.5 || size.isEmpty) {
      if (transfer >= 0.999) {
        canvas.drawImageRect(image, src, _dst(offset, size),
            Paint()..filterQuality = FilterQuality.low);
      }
      return;
    }

    final g = grain.clamp(4.0, 200.0);
    final cols = (size.width / g).ceil().clamp(1, 90);
    final rows = (size.height / g).ceil().clamp(1, 90);
    final sx = image.width / size.width;
    final sy = image.height / size.height;
    final paint = Paint()
      ..filterQuality = FilterQuality.low
      ..color = Colors.white.withValues(alpha: transfer.clamp(0.0, 1.0));

    canvas.save();
    canvas.clipRect(_dst(offset, size));
    for (var j = 0; j < rows; j++) {
      for (var i = 0; i < cols; i++) {
        final n1 = fxNoise(i.toDouble(), j.toDouble(), seed);
        final n2 = fxNoise(i.toDouble(), j.toDouble(), seed + 77);
        final dx = (n1 - 0.5) * 2 * spread;
        final dy = (n2 - 0.5) * 2 * spread + gravity * spread * n1;
        final rot = (n1 - 0.5) * 2 * rotation * math.pi / 180;

        final cellW = size.width / cols;
        final cellH = size.height / rows;
        final s = Rect.fromLTWH(
            i * cellW * sx, j * cellH * sy, cellW * sx, cellH * sy);
        final cx = offset.dx + i * cellW + cellW / 2 + dx;
        final cy = offset.dy + j * cellH + cellH / 2 + dy;

        canvas.save();
        canvas.translate(cx, cy);
        if (rot != 0) canvas.rotate(rot);
        canvas.drawImageRect(
          image,
          s,
          Rect.fromCenter(
              center: Offset.zero, width: cellW, height: cellH),
          paint,
        );
        canvas.restore();
      }
    }
    canvas.restore();
  }

  @override
  bool shouldRepaint(covariant ScatterizePainter old) =>
      old.spread != spread ||
      old.grain != grain ||
      old.rotation != rotation ||
      old.transfer != transfer ||
      old.gravity != gravity ||
      old.seed != seed;
}

// ----------------------------------------------------- motion tile

/// MOSAICO DE MOVIMENTO: repete a camada num tabuleiro, com espelho
/// opcional nas bordas — o jeito de encher a tela com um elemento so.
/// MOTION TILE — replica a imagem de origem atraves da imagem de saida.
///
/// A semantica e a do After Effects, e a parte que costuma sair errada e
/// a unidade: largura e altura do ladrilho sao **% das dimensoes da
/// camada de entrada**, nao pixels. A saida tambem.
///
/// `Phase` desloca as FILEIRAS alternadas — nao a grade inteira. E o que
/// da o padrao de tijolo, e o que faz um fundo rolar quando animado.
class MotionTilePainter extends _FxPainter {
  MotionTilePainter({
    required this.tileW,
    required this.tileH,
    required this.outW,
    required this.outH,
    required this.centerX,
    required this.centerY,
    required this.mirror,
    required this.phase,
    required this.horizontalPhase,
  });

  final double tileW;
  final double tileH;
  final double outW;
  final double outH;

  /// Centro do ladrilho principal, em 0..1 da camada.
  final double centerX;
  final double centerY;

  final bool mirror;

  /// Em graus: 360 desloca uma fileira inteira.
  final double phase;

  /// Desloca na horizontal em vez de na vertical.
  final bool horizontalPhase;

  @override
  void paintSnapshot(PaintingContext context, Offset offset, Size size,
      ui.Image image, Size sourceSize, double pixelRatio) {
    final canvas = context.canvas;
    if (size.isEmpty) return;
    final src = Rect.fromLTWH(
        0, 0, image.width.toDouble(), image.height.toDouble());

    final tw = size.width * (tileW / 100).clamp(0.05, 3.0);
    final th = size.height * (tileH / 100).clamp(0.05, 3.0);
    final ow = size.width * (outW / 100).clamp(1.0, 6.0);
    final oh = size.height * (outH / 100).clamp(1.0, 6.0);

    final cx = offset.dx + size.width * centerX;
    final cy = offset.dy + size.height * centerY;
    final area = Rect.fromCenter(
        center: Offset(cx, cy), width: ow, height: oh);

    final nx = (ow / tw).ceil() + 2;
    final ny = (oh / th).ceil() + 2;

    // A fase desloca FILEIRAS ALTERNADAS — e o padrao de tijolo. Uma
    // volta inteira (360) desloca a fileira em um ladrilho.
    final desloc = phase / 360.0;

    // O RECORTE E A AREA DE SAIDA, nao a caixa da camada. Output
    // Width/Height acima de 100% e justamente a camada crescendo para
    // fora da propria caixa (o After Effects faz assim); recortar na
    // caixa jogava fora tudo que o efeito produzia alem dela — e o
    // Motion Tile parecia nao fazer nada.
    canvas.save();
    canvas.clipRect(area);
    for (var j = -ny ~/ 2; j <= ny ~/ 2; j++) {
      for (var i = -nx ~/ 2; i <= nx ~/ 2; i++) {
        final shift = horizontalPhase
            ? (j.isOdd ? desloc * tw : 0.0)
            : (i.isOdd ? desloc * th : 0.0);
        final x = cx - tw / 2 + i * tw + (horizontalPhase ? shift : 0);
        final y = cy - th / 2 + j * th + (horizontalPhase ? 0 : shift);
        final flipX = mirror && i.isOdd;
        final flipY = mirror && j.isOdd;

        // Fora da area de saida o ladrilho nem e desenhado: a saida e
        // uma janela, e desenhar por tras dela e trabalho jogado fora.
        final quadro = Rect.fromCenter(
            center: Offset(x + tw / 2, y + th / 2), width: tw, height: th);
        if (!quadro.overlaps(area)) continue;

        canvas.save();
        canvas.translate(x + tw / 2, y + th / 2);
        canvas.scale(flipX ? -1.0 : 1.0, flipY ? -1.0 : 1.0);
        canvas.drawImageRect(
          image,
          src,
          Rect.fromCenter(center: Offset.zero, width: tw, height: th),
          Paint()..filterQuality = FilterQuality.low,
        );
        canvas.restore();
      }
    }
    canvas.restore();
  }

  @override
  bool shouldRepaint(covariant MotionTilePainter old) =>
      old.tileW != tileW ||
      old.tileH != tileH ||
      old.outW != outW ||
      old.outH != outH ||
      old.centerX != centerX ||
      old.centerY != centerY ||
      old.mirror != mirror ||
      old.phase != phase ||
      old.horizontalPhase != horizontalPhase;
}

// -------------------------------------------------------- CC Split

/// CC SPLIT: a imagem se rasga em duas metades que se afastam.
class SplitPainter extends _FxPainter {
  SplitPainter({
    required this.split,
    required this.angleDeg,
    required this.center,
    required this.softness,
  });

  final double split;
  final double angleDeg;
  final double center;
  final double softness;

  @override
  void paintSnapshot(PaintingContext context, Offset offset, Size size,
      ui.Image image, Size sourceSize, double pixelRatio) {
    final canvas = context.canvas;
    final src = Rect.fromLTWH(
        0, 0, image.width.toDouble(), image.height.toDouble());
    if (split.abs() < 0.5 || size.isEmpty) {
      canvas.drawImageRect(image, src, _dst(offset, size),
          Paint()..filterQuality = FilterQuality.low);
      return;
    }

    final rad = angleDeg * math.pi / 180;
    final dir = Offset(math.cos(rad), math.sin(rad));
    // Normal da linha de corte: e por ela que as metades se afastam.
    final nrm = Offset(-dir.dy, dir.dx);
    final cx = offset.dx + size.width / 2;
    final cy = offset.dy + size.height / 2;
    final cut = Offset(
      cx + nrm.dx * (center - 0.5) * size.height,
      cy + nrm.dy * (center - 0.5) * size.height,
    );

    final big = size.longestSide * 2;
    final paint = Paint()
      ..filterQuality = FilterQuality.low
      ..isAntiAlias = true;

    for (final side in [1.0, -1.0]) {
      canvas.save();
      // Meio plano: retangulo enorme girado sobre a linha de corte.
      final path = Path()
        ..moveTo(cut.dx - dir.dx * big, cut.dy - dir.dy * big)
        ..lineTo(cut.dx + dir.dx * big, cut.dy + dir.dy * big)
        ..lineTo(cut.dx + dir.dx * big + nrm.dx * big * side,
            cut.dy + dir.dy * big + nrm.dy * big * side)
        ..lineTo(cut.dx - dir.dx * big + nrm.dx * big * side,
            cut.dy - dir.dy * big + nrm.dy * big * side)
        ..close();
      canvas.clipPath(path);
      canvas.translate(nrm.dx * split * side, nrm.dy * split * side);
      if (softness > 0.01) {
        paint.imageFilter = ui.ImageFilter.blur(
            sigmaX: softness * 8, sigmaY: softness * 8,
            tileMode: TileMode.decal);
      }
      canvas.drawImageRect(image, src, _dst(offset, size), paint);
      canvas.restore();
    }
  }

  @override
  bool shouldRepaint(covariant SplitPainter old) =>
      old.split != split ||
      old.angleDeg != angleDeg ||
      old.center != center ||
      old.softness != softness;
}

// -------------------------------------------------- nitidez (USM)

/// MASCARA DE NITIDEZ: resultado = (1+q)*original − q*borrado. E a
/// definicao classica, feita com duas passadas na mesma imagem.
class UnsharpMaskPainter extends _FxPainter {
  UnsharpMaskPainter({
    required this.amount,
    required this.radius,
    required this.threshold,
  });

  final double amount;
  final double radius;
  final double threshold;

  @override
  void paintSnapshot(PaintingContext context, Offset offset, Size size,
      ui.Image image, Size sourceSize, double pixelRatio) {
    final canvas = context.canvas;
    final src = Rect.fromLTWH(
        0, 0, image.width.toDouble(), image.height.toDouble());
    final dst = _dst(offset, size);
    final a = amount.clamp(0.0, 3.0);
    if (a < 0.01) {
      canvas.drawImageRect(image, src, dst,
          Paint()..filterQuality = FilterQuality.low);
      return;
    }

    canvas.saveLayer(dst, Paint());
    // (1+q) * original
    canvas.drawImageRect(
      image,
      src,
      dst,
      Paint()
        ..filterQuality = FilterQuality.low
        ..colorFilter = ColorFilter.matrix(_gain(1 + a)),
    );
    // menos q * borrado
    canvas.drawImageRect(
      image,
      src,
      dst,
      Paint()
        ..filterQuality = FilterQuality.low
        ..imageFilter = ui.ImageFilter.blur(
            sigmaX: radius, sigmaY: radius, tileMode: TileMode.decal)
        ..colorFilter = ColorFilter.matrix(_gain(a * (1 - threshold)))
        ..blendMode = BlendMode.difference,
    );
    canvas.restore();
  }

  static List<double> _gain(double g) => <double>[
        g, 0, 0, 0, 0, //
        0, g, 0, 0, 0, //
        0, 0, g, 0, 0, //
        0, 0, 0, 1, 0,
      ];

  @override
  bool shouldRepaint(covariant UnsharpMaskPainter old) =>
      old.amount != amount ||
      old.radius != radius ||
      old.threshold != threshold;
}

// ----------------------------------------------------- sobreposicoes

/// VHS: linhas de varredura, sangramento de cor, tremor horizontal e
/// ruido de fita.
class VhsPainter extends CustomPainter {
  const VhsPainter({
    required this.intensity,
    required this.lines,
    required this.noise,
    required this.time,
    required this.seed,
  });

  final double intensity;
  final double lines;
  final double noise;
  final Duration time;
  final int seed;

  @override
  void paint(Canvas canvas, Size size) {
    if (size.isEmpty) return;
    final t = time.inMilliseconds / 1000.0;

    // Linhas de varredura.
    if (lines > 0.01) {
      final paint = Paint()
        ..color = Colors.black.withValues(alpha: 0.10 * lines * intensity);
      for (var y = 0.0; y < size.height; y += 3) {
        canvas.drawRect(Rect.fromLTWH(0, y, size.width, 1.4), paint);
      }
    }

    // Faixa de cabecote descendo: a marca registrada da fita.
    final bandY =
        ((t * 0.22) % 1.0) * (size.height + 160) - 80;
    canvas.drawRect(
      Rect.fromLTWH(0, bandY, size.width, 46),
      Paint()
        ..shader = ui.Gradient.linear(
          Offset(0, bandY),
          Offset(0, bandY + 46),
          [
            Colors.white.withValues(alpha: 0),
            Colors.white.withValues(alpha: 0.16 * intensity),
            Colors.white.withValues(alpha: 0),
          ],
          [0, 0.5, 1],
        ),
    );

    // Ruido de fita em riscos curtos.
    if (noise > 0.01) {
      final n = (noise * 90).round();
      final frame = (t * 24).floor();
      final paint = Paint();
      for (var i = 0; i < n; i++) {
        final r1 = fxNoise(i.toDouble(), frame.toDouble(), seed);
        final r2 = fxNoise(i.toDouble(), frame.toDouble() + 1, seed + 9);
        final r3 = fxNoise(i.toDouble(), frame.toDouble() + 2, seed + 19);
        paint.color =
            Colors.white.withValues(alpha: 0.05 + r3 * 0.30 * noise);
        canvas.drawRect(
          Rect.fromLTWH(r1 * size.width, r2 * size.height,
              4 + r3 * 60, 1 + r3 * 2),
          paint,
        );
      }
    }
  }

  @override
  bool shouldRepaint(VhsPainter old) =>
      old.intensity != intensity ||
      old.lines != lines ||
      old.noise != noise ||
      old.time != time ||
      old.seed != seed;
}

/// FILME DANIFICADO: poeira, riscos verticais, queimado nas bordas.
class FilmDamagePainter extends CustomPainter {
  const FilmDamagePainter({
    required this.dust,
    required this.scratches,
    required this.burn,
    required this.time,
    required this.seed,
  });

  final double dust;
  final double scratches;
  final double burn;
  final Duration time;
  final int seed;

  @override
  void paint(Canvas canvas, Size size) {
    if (size.isEmpty) return;
    // O quadro do filme troca a 16 fps: e o que da o ar de projetor.
    final frame = (time.inMilliseconds / 1000.0 * 16).floor();

    if (dust > 0.01) {
      final n = (dust * 60).round();
      final paint = Paint();
      for (var i = 0; i < n; i++) {
        final r1 = fxNoise(i.toDouble(), frame.toDouble(), seed);
        final r2 = fxNoise(i.toDouble(), frame.toDouble(), seed + 41);
        final r3 = fxNoise(i.toDouble(), frame.toDouble(), seed + 83);
        paint.color = (r3 > 0.5 ? Colors.black : Colors.white)
            .withValues(alpha: 0.20 + r3 * 0.45);
        canvas.drawCircle(
            Offset(r1 * size.width, r2 * size.height), 0.6 + r3 * 2.2,
            paint);
      }
    }

    if (scratches > 0.01) {
      final n = (scratches * 6).round() + 1;
      for (var i = 0; i < n; i++) {
        final r1 = fxNoise(i.toDouble(), (frame ~/ 3).toDouble(), seed + 7);
        final r2 = fxNoise(i.toDouble(), (frame ~/ 3).toDouble(), seed + 17);
        if (r2 < 0.45) continue;
        final x = r1 * size.width;
        canvas.drawRect(
          Rect.fromLTWH(x, 0, 0.8 + r2 * 1.6, size.height),
          Paint()
            ..color = Colors.white
                .withValues(alpha: 0.10 + r2 * 0.22 * scratches),
        );
      }
    }

    if (burn > 0.01) {
      canvas.drawRect(
        Offset.zero & size,
        Paint()
          ..shader = ui.Gradient.radial(
            Offset(size.width / 2, size.height / 2),
            size.longestSide * 0.62,
            [
              Colors.transparent,
              Colors.black.withValues(alpha: 0.55 * burn),
            ],
            [0.55, 1.0],
          ),
      );
    }
  }

  @override
  bool shouldRepaint(FilmDamagePainter old) =>
      old.dust != dust ||
      old.scratches != scratches ||
      old.burn != burn ||
      old.time != time ||
      old.seed != seed;
}

/// GLITCHIFY: blocos deslocados na horizontal, com linhas de erro.
class GlitchifyPainter extends _FxPainter {
  GlitchifyPainter({
    required this.intensity,
    required this.blocks,
    required this.shift,
    required this.colorSplit,
    required this.lineNoise,
    required this.speed,
    required this.time,
    required this.seed,
  });

  final double intensity;
  final double blocks;
  final double shift;
  final double colorSplit;
  final double lineNoise;
  final double speed;
  final Duration time;
  final int seed;

  @override
  void paintSnapshot(PaintingContext context, Offset offset, Size size,
      ui.Image image, Size sourceSize, double pixelRatio) {
    final canvas = context.canvas;
    final src = Rect.fromLTWH(
        0, 0, image.width.toDouble(), image.height.toDouble());
    final dst = _dst(offset, size);
    canvas.drawImageRect(image, src, dst,
        Paint()..filterQuality = FilterQuality.low);
    if (intensity < 0.01 || size.isEmpty) return;

    // O tique: o glitch nao e continuo, ele ACONTECE em instantes.
    final tick = (time.inMilliseconds / 1000.0 * speed).floor();
    final n = blocks.round().clamp(1, 40);
    final sy = image.height / size.height;

    canvas.save();
    canvas.clipRect(dst);
    for (var i = 0; i < n; i++) {
      final r = fxNoise(i.toDouble(), tick.toDouble(), seed);
      if (r > intensity) continue;
      final r2 = fxNoise(i.toDouble(), tick.toDouble(), seed + 53);
      final r3 = fxNoise(i.toDouble(), tick.toDouble(), seed + 97);

      final y = r2 * size.height;
      final h = (2 + r3 * size.height * 0.12).clamp(2.0, size.height);
      final dx = (r3 - 0.5) * 2 * shift;

      final sRect = Rect.fromLTWH(0, y * sy, image.width.toDouble(), h * sy);
      final dRect =
          Rect.fromLTWH(offset.dx + dx, offset.dy + y, size.width, h);

      if (colorSplit > 0.02) {
        // Cada bloco puxa um canal para um lado: o corte de cor.
        for (final ch in [0, 2]) {
          canvas.drawImageRect(
            image,
            sRect,
            dRect.translate(ch == 0 ? -colorSplit * 12 : colorSplit * 12, 0),
            Paint()
              ..filterQuality = FilterQuality.none
              ..blendMode = BlendMode.plus
              ..colorFilter = ColorFilter.matrix(_channel(ch)),
          );
        }
      }
      canvas.drawImageRect(image, sRect, dRect,
          Paint()..filterQuality = FilterQuality.none);
    }

    if (lineNoise > 0.01) {
      final paint = Paint();
      final lines = (lineNoise * 40).round();
      for (var i = 0; i < lines; i++) {
        final r = fxNoise(i.toDouble(), tick.toDouble(), seed + 211);
        paint.color = Colors.white.withValues(alpha: 0.06 + r * 0.22);
        canvas.drawRect(
          Rect.fromLTWH(offset.dx, offset.dy + r * size.height,
              size.width, 1 + r * 2),
          paint,
        );
      }
    }
    canvas.restore();
  }

  static List<double> _channel(int ch) => <double>[
        ch == 0 ? 1 : 0, 0, 0, 0, 0, //
        0, ch == 1 ? 1 : 0, 0, 0, 0, //
        0, 0, ch == 2 ? 1 : 0, 0, 0, //
        0, 0, 0, 1, 0,
      ];

  @override
  bool shouldRepaint(covariant GlitchifyPainter old) =>
      old.intensity != intensity ||
      old.blocks != blocks ||
      old.shift != shift ||
      old.colorSplit != colorSplit ||
      old.lineNoise != lineNoise ||
      old.speed != speed ||
      old.time != time ||
      old.seed != seed;
}

/// RASTREADOR DE BLOBS: os alvos de rastreio como elemento grafico —
/// caixas com cantos, mira e rotulo, andando devagar pelo quadro.
/// BLOB TRACKER — as sobreposicoes de rastreio.
///
/// O pintor NAO detecta nada: ele desenha o que a analise ja gravou.
/// Rastreio depende do quadro anterior, e detectar aqui quebraria o seek
/// instantaneo — pular para o segundo 40 exigiria processar os 1200
/// quadros anteriores toda vez.
///
/// Sem analise, ele desenha um rastreio SIMULADO e deterministico, para
/// a pessoa ver a aparencia e ajustar antes de gastar o processamento.
class BlobTrackerPainter extends CustomPainter {
  const BlobTrackerPainter({
    required this.track,
    required this.time,
    required this.color,
    required this.style,
    required this.showCenter,
    required this.showLines,
    required this.lineType,
    required this.lineStyle,
    required this.palette,
    required this.thickness,
    required this.opacity,
    required this.fill,
    required this.cornerLength,
    required this.showCaption,
    required this.captionContent,
    required this.captionPosition,
    required this.fontSize,
    required this.seed,
    this.simulatedCount = 4,
  });

  /// As caixas ja analisadas. Nulo = ainda nao analisou.
  final BlobTrackData? track;
  final Duration time;
  final Color color;

  /// 0 Full Box, 1 Corner Box, 2 Circle, 3 Crosshair, 4 None.
  final int style;
  final bool showCenter;
  final bool showLines;

  /// 0 Nearest, 1 All Pairs, 2 To Centroid.
  final int lineType;

  /// 0 Solid, 1 Dashed, 2 Dotted.
  final int lineStyle;

  /// 0 Single, 1 Per-ID, 2 Random.
  final int palette;
  final double thickness;
  final double opacity;
  final double fill;
  final double cornerLength;
  final bool showCaption;

  /// 0 ID, 1 ID + Size, 2 ID + Coordinates.
  final int captionContent;

  /// 0 Top Left, 1 Top Right, 2 Bottom, 3 Inside.
  final int captionPosition;
  final double fontSize;
  final int seed;
  final int simulatedCount;

  /// As caixas deste instante, ja na escala da tela.
  List<Blob> _caixas(Size size) {
    final t = track;
    if (t != null && !t.isEmpty && t.width > 0 && t.height > 0) {
      final ex = size.width / t.width;
      final ey = size.height / t.height;
      return [
        for (final b in t.at(time))
          Blob(
            id: b.id,
            area: b.area,
            rect: Rect.fromLTRB(b.rect.left * ex, b.rect.top * ey,
                b.rect.right * ex, b.rect.bottom * ey),
          ),
      ];
    }

    // SIMULADO: puro em (semente, indice, tempo) — o mesmo instante da
    // sempre a mesma caixa, entao o preview nao pisca ao dar scrub.
    final segundos = time.inMicroseconds / 1000000.0;
    final lado = math.min(size.width, size.height) * 0.22;
    return [
      for (var i = 0; i < simulatedCount; i++)
        () {
          final fx = fxNoise(i.toDouble(), 0, seed);
          final fy = fxNoise(i.toDouble(), 1, seed + 7);
          final x = (0.5 + math.sin(segundos * 0.7 + fx * 6.28) * 0.28) *
              (size.width - lado);
          final y = (0.5 + math.cos(segundos * 0.5 + fy * 6.28) * 0.28) *
              (size.height - lado);
          return Blob(
            id: i + 1,
            rect: Rect.fromLTWH(x, y, lado, lado * 0.8),
            area: (lado * lado * 0.8).round(),
          );
        }(),
    ];
  }

  Color _corDe(int id) {
    final a = (opacity.clamp(0.0, 100.0) / 100);
    switch (palette) {
      case 1:
        // Per-ID: a cor identifica o objeto. Trocar de cor a cada quadro
        // faria a cor deixar de significar alguma coisa.
        final h = (id * 47) % 360;
        return HSVColor.fromAHSV(a, h.toDouble(), 0.85, 1).toColor();
      case 2:
        final r = fxNoise(id.toDouble(), 3, seed);
        return HSVColor.fromAHSV(a, r * 360, 0.8, 1).toColor();
      default:
        return color.withValues(alpha: a);
    }
  }

  void _linha(Canvas canvas, Offset a, Offset b, Paint p) {
    if (lineStyle == 0) {
      canvas.drawLine(a, b, p);
      return;
    }
    // Tracejada e pontilhada: pedacos ao longo da reta.
    final d = (b - a).distance;
    final passo = lineStyle == 1 ? 10.0 : 4.0;
    final cheio = lineStyle == 1 ? 6.0 : 1.5;
    if (d < 1) return;
    final dir = (b - a) / d;
    for (var x = 0.0; x < d; x += passo) {
      final fim = math.min(x + cheio, d);
      canvas.drawLine(a + dir * x, a + dir * fim, p);
    }
  }

  @override
  void paint(Canvas canvas, Size size) {
    if (size.isEmpty) return;
    final caixas = _caixas(size);
    if (caixas.isEmpty) return;

    final traco = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = thickness;

    // LINHAS DE CONEXAO, por tras das caixas.
    if (showLines && caixas.length > 1) {
      final p = Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = math.max(0.5, thickness * 0.6)
        ..color = _corDe(caixas.first.id).withValues(alpha: 0.45);
      switch (lineType) {
        case 1:
          for (var i = 0; i < caixas.length; i++) {
            for (var j = i + 1; j < caixas.length; j++) {
              _linha(canvas, caixas[i].center, caixas[j].center, p);
            }
          }
        case 2:
          var cx = 0.0, cy = 0.0;
          for (final b in caixas) {
            cx += b.center.dx;
            cy += b.center.dy;
          }
          final centro = Offset(cx / caixas.length, cy / caixas.length);
          for (final b in caixas) {
            _linha(canvas, b.center, centro, p);
          }
        default:
          for (var i = 0; i < caixas.length; i++) {
            var melhor = -1;
            var melhorD = double.infinity;
            for (var j = 0; j < caixas.length; j++) {
              if (i == j) continue;
              final d = (caixas[i].center - caixas[j].center).distance;
              if (d < melhorD) {
                melhorD = d;
                melhor = j;
              }
            }
            if (melhor >= 0) {
              _linha(canvas, caixas[i].center, caixas[melhor].center, p);
            }
          }
      }
    }

    for (final b in caixas) {
      final cor = _corDe(b.id);
      traco.color = cor;
      final r = b.rect;

      if (fill > 0.01) {
        canvas.drawRect(
            r,
            Paint()
              ..color = cor.withValues(
                  alpha: (fill / 100).clamp(0.0, 1.0) *
                      (opacity / 100).clamp(0.0, 1.0)));
      }

      switch (style) {
        case 1:
          // CANTOS: o comprimento e % da caixa, entao caixa pequena tem
          // canto pequeno — em pixel fixo, o canto engoliria a caixa.
          final cl = math.min(r.width, r.height) *
              (cornerLength.clamp(1.0, 50.0) / 100);
          for (final (px, py, sx, sy) in [
            (r.left, r.top, 1.0, 1.0),
            (r.right, r.top, -1.0, 1.0),
            (r.left, r.bottom, 1.0, -1.0),
            (r.right, r.bottom, -1.0, -1.0),
          ]) {
            canvas.drawLine(
                Offset(px, py), Offset(px + cl * sx, py), traco);
            canvas.drawLine(
                Offset(px, py), Offset(px, py + cl * sy), traco);
          }
        case 2:
          canvas.drawCircle(
              r.center, math.min(r.width, r.height) / 2, traco);
        case 3:
          final c = r.center;
          final l = math.min(r.width, r.height) / 2;
          canvas.drawLine(
              Offset(c.dx - l, c.dy), Offset(c.dx + l, c.dy), traco);
          canvas.drawLine(
              Offset(c.dx, c.dy - l), Offset(c.dx, c.dy + l), traco);
        case 4:
          break;
        default:
          canvas.drawRect(r, traco);
      }

      if (showCenter) {
        canvas.drawCircle(
            r.center, math.max(1.5, thickness), Paint()..color = cor);
      }

      if (showCaption) {
        final texto = switch (captionContent) {
          1 => 'ID ${b.id} · ${b.area}px',
          2 =>
            'ID ${b.id} · ${r.left.round()},${r.top.round()}',
          _ => 'ID ${b.id}',
        };
        final tp = TextPainter(
          text: TextSpan(
            text: texto,
            style: TextStyle(
              fontSize: fontSize,
              color: cor,
              fontWeight: FontWeight.w600,
            ),
          ),
          textDirection: TextDirection.ltr,
        )..layout();
        final pos = switch (captionPosition) {
          1 => Offset(r.right - tp.width, r.top - tp.height - 3),
          2 => Offset(r.left, r.bottom + 3),
          3 => Offset(r.left + 4, r.top + 4),
          _ => Offset(r.left, r.top - tp.height - 3),
        };
        tp.paint(canvas, pos);
      }
    }
  }

  @override
  bool shouldRepaint(BlobTrackerPainter old) =>
      old.track != track ||
      old.time != time ||
      old.color != color ||
      old.style != style ||
      old.showCenter != showCenter ||
      old.showLines != showLines ||
      old.lineType != lineType ||
      old.lineStyle != lineStyle ||
      old.palette != palette ||
      old.thickness != thickness ||
      old.opacity != opacity ||
      old.fill != fill ||
      old.cornerLength != cornerLength ||
      old.showCaption != showCaption ||
      old.captionContent != captionContent ||
      old.captionPosition != captionPosition ||
      old.fontSize != fontSize ||
      old.seed != seed;
}
