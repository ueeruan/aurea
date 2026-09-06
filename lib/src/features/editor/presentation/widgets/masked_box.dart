import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

import '../../domain/mask.dart';

/// Uma mascara ja avaliada no tempo local (caminho construido).
/// Coordenadas do caminho: origem no CENTRO do conteudo da camada.
class MaskSpec {
  const MaskSpec({
    required this.path,
    required this.closed,
    required this.mode,
    required this.inverted,
    required this.opacity,
    required this.feather,
    required this.expansion,
    this.featherY,
  });

  final Path path;

  /// Caminho aberto serve de entrada para efeitos/edicao, mas nao cria
  /// uma area de recorte. O Canvas preencheria fechando as pontas em
  /// silencio, por isso o estado precisa chegar explicitamente ao render.
  final bool closed;
  final MaskMode mode;
  final bool inverted;
  final double opacity;
  final double feather;
  final double expansion;

  /// Suavidade vertical; nulo = igual a horizontal.
  final double? featherY;

  double get featherVertical => featherY ?? feather;
}

/// Aplica a pilha de mascaras ao alfa do filho (PR-M2):
/// - o conteudo e pintado num saveLayer;
/// - a COBERTURA das mascaras e composta numa camada dstIn: cada mascara
///   pinta sua geometria (branca) numa subcamada propria e composita com
///   o blend do seu modo — a primeira contra o alfa da camada, as
///   seguintes contra as de cima;
/// - feather = blur gaussiano montado sobre a borda; expansao = stroke
///   que alarga (positiva) ou apaga a borda (negativa), sem mexer nos
///   vertices; inverted troca dentro/fora daquela mascara.
class MaskedBox extends SingleChildRenderObjectWidget {
  const MaskedBox({super.key, required this.specs, super.child});

  final List<MaskSpec> specs;

  @override
  RenderObject createRenderObject(BuildContext context) => _RenderMaskedBox(
    specs,
    MediaQuery.maybeDevicePixelRatioOf(context) ?? 1.0,
  );

  @override
  void updateRenderObject(BuildContext context, RenderObject renderObject) {
    (renderObject as _RenderMaskedBox)
      ..specs = specs
      ..pixelRatio = MediaQuery.maybeDevicePixelRatioOf(context) ?? 1.0;
  }
}

class _RenderMaskedBox extends RenderProxyBox {
  _RenderMaskedBox(this._specs, this._pixelRatio);

  /// A pilha e composta como um campo grayscale OPACO. So no restore
  /// final o cinza vira alfa; assim Lighten/Darken operam em max/min do
  /// valor da mascara, em vez de aplicarem as regras de alfa do srcOver.
  static const _grayToAlpha = ColorFilter.matrix(<double>[
    0,
    0,
    0,
    0,
    255,
    0,
    0,
    0,
    0,
    255,
    0,
    0,
    0,
    0,
    255,
    1,
    0,
    0,
    0,
    0,
  ]);

  List<MaskSpec> _specs;
  set specs(List<MaskSpec> v) {
    _specs = v;
    markNeedsPaint();
  }

  double _pixelRatio;
  set pixelRatio(double v) {
    if (v == _pixelRatio) return;
    _pixelRatio = v;
    markNeedsPaint();
  }

  static bool _temTextura(RenderObject r) {
    if (r is TextureBox) return true;
    var achou = false;
    r.visitChildren((c) {
      if (!achou && _temTextura(c)) achou = true;
    });
    return achou;
  }

  /// PINTA O FILHO DENTRO DO saveLayer — de verdade.
  ///
  /// Um filho com camadas proprias do motor (video, Opacity, efeito com
  /// ImageFiltered, grupo) escapa do saveLayer: o conteudo dele sai
  /// numa camada separada, pintada DEPOIS do restore, e a mascara nao
  /// o alcanca. Era "a mascara nao funciona em toda camada". Esses
  /// filhos sao fotografados (com folga para halo de efeito) e a foto
  /// entra no saveLayer, onde a cobertura da mascara os corta.
  void _pintarFilho(PaintingContext context, Offset offset, double folga) {
    final filho = child!;
    if (!filho.needsCompositing || _temTextura(filho)) {
      context.paintChild(filho, offset);
      return;
    }
    final limites = Rect.fromLTWH(
      -folga,
      -folga,
      size.width + 2 * folga,
      size.height + 2 * folga,
    );
    final camada = OffsetLayer();
    final ctx = PaintingContext(camada, limites);
    ctx.paintChild(filho, Offset.zero);
    // ignore: invalid_use_of_protected_member
    ctx.stopRecordingIfNeeded();
    final foto = camada.toImageSync(limites, pixelRatio: _pixelRatio);
    camada.dispose();
    final canvas = context.canvas;
    canvas.save();
    canvas.translate(offset.dx - folga, offset.dy - folga);
    canvas.scale(1 / _pixelRatio);
    canvas.drawImage(
      foto,
      Offset.zero,
      Paint()..filterQuality = FilterQuality.low,
    );
    canvas.restore();
    foto.dispose();
  }

  @override
  void paint(PaintingContext context, Offset offset) {
    if (child == null) return;
    final active = [
      for (final s in _specs)
        if (s.mode != MaskMode.none && s.closed) s,
    ];
    if (active.isEmpty) {
      context.paintChild(child!, offset);
      return;
    }

    final canvas = context.canvas;
    // A cobertura termina EXATAMENTE nos limites da camada. O retangulo
    // de trabalho continua com folga para fotografar filhos compostos,
    // mas o matte e recortado em [layerRect]: feather nunca nasce fora
    // do conteudo e um caminho encostado na borda termina seco, como a UI
    // avisa.
    final layerRect = offset & size;
    final rect = layerRect.inflate(1400);
    canvas.saveLayer(rect, Paint());
    // Folga da foto: o halo de um glow e a expansao da mascara cabem.
    var folga = 160.0;
    for (final s in active) {
      final alcance = s.feather * 3 + s.featherVertical * 3 + s.expansion.abs();
      if (alcance + 40 > folga) folga = alcance + 40;
    }
    _pintarFilho(context, offset, folga.clamp(160.0, 720.0));

    // Cobertura das mascaras multiplica o alfa do conteudo. Durante a
    // acumulacao ela fica em RGB grayscale com alfa 1; no restore este
    // filtro copia R para alfa e deixa RGB branco para o dstIn final.
    canvas.saveLayer(
      rect,
      Paint()
        ..blendMode = BlendMode.dstIn
        ..colorFilter = _grayToAlpha,
    );
    canvas.save();
    canvas.clipRect(layerRect);

    // A primeira Add/Lighten nasce do vazio. Os modos que medem, cortam
    // ou diferenciam a entrada nascem cheios, para agirem contra o alfa
    // original da camada quando o campo for aplicado por dstIn.
    final firstMode = active.first.mode;
    final startsFull =
        firstMode == MaskMode.subtract ||
        firstMode == MaskMode.intersect ||
        firstMode == MaskMode.darken ||
        firstMode == MaskMode.difference;
    canvas.drawRect(
      layerRect,
      Paint()
        ..color = startsFull
            ? const Color(0xFFFFFFFF)
            : const Color(0xFF000000),
    );

    final center = offset + Offset(size.width / 2, size.height / 2);
    for (final s in active) {
      final subtract = s.mode == MaskMode.subtract;
      final opacity = s.opacity.clamp(0.0, 1.0);

      // P e o campo geometrico branco (0..1). A mascara efetiva S e
      // opacity*P, ou opacity*(1-P) quando invertida. Subtract precisa
      // do operando (1-S); os outros recebem S diretamente.
      final double factor;
      final double bias;
      if (subtract) {
        factor = s.inverted ? opacity : -opacity;
        bias = s.inverted ? 255 * (1 - opacity) : 255;
      } else {
        factor = s.inverted ? -opacity : opacity;
        bias = s.inverted ? 255 * opacity : 0;
      }
      canvas.saveLayer(
        rect,
        Paint()
          ..blendMode = _blendFor(s.mode)
          ..colorFilter = ColorFilter.matrix(<double>[
            factor,
            0,
            0,
            0,
            bias,
            0,
            factor,
            0,
            0,
            bias,
            0,
            0,
            factor,
            0,
            bias,
            0,
            0,
            0,
            1,
            0,
          ]),
      );

      // Campo P opaco: preto fora, branco dentro. Inversao e opacidade
      // entram no filtro acima, sem borrar a borda externa da camada.
      canvas.drawRect(layerRect, Paint()..color = const Color(0xFF000000));
      final g = s.path.shift(center);

      // Feather por EIXO: o blur vai numa camada propria porque
      // MaskFilter e redondo por definicao — nao ha como pedir 40 px em
      // cima e 0 dos lados com ele. ImageFilter aceita os dois sigmas, e
      // e o que permite o degrade de horizonte.
      final borrar = s.feather > 0.5 || s.featherVertical > 0.5;
      canvas.saveLayer(
        rect,
        Paint()
          ..imageFilter = borrar
              ? ui.ImageFilter.blur(
                  // 25 ~ 12,5 px por lado, como antes.
                  sigmaX: s.feather > 0.5 ? s.feather / 4 : 0.0001,
                  sigmaY: s.featherVertical > 0.5
                      ? s.featherVertical / 4
                      : 0.0001,
                )
              : null,
      );

      canvas.drawPath(g, Paint()..color = const Color(0xFFFFFFFF));

      // Expansao: alarga ou contrai o alcance sem alterar o caminho.
      if (s.expansion.abs() > 0.5) {
        final stroke = Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = s.expansion.abs() * 2
          ..strokeJoin = StrokeJoin.round
          ..color = const Color(0xFFFFFFFF);
        if (s.expansion > 0) {
          canvas.drawPath(g, stroke);
        } else {
          stroke.blendMode = BlendMode.clear;
          canvas.drawPath(g, stroke);
        }
      }

      canvas.restore();
      canvas.restore();
    }

    canvas.restore();
    canvas.restore();
    canvas.restore();
  }

  /// Operacoes sobre campos escalares opacos D (acumulado) e S (mascara):
  /// Add = screen, Subtract = D*(1-S), Intersect = D*S,
  /// Lighten = max, Darken = min e Difference = abs(D-S).
  BlendMode _blendFor(MaskMode mode) => switch (mode) {
    MaskMode.add => BlendMode.screen,
    MaskMode.subtract => BlendMode.multiply,
    MaskMode.intersect => BlendMode.multiply,
    MaskMode.lighten => BlendMode.lighten,
    MaskMode.darken => BlendMode.darken,
    MaskMode.difference => BlendMode.difference,
    MaskMode.none => BlendMode.screen,
  };
}
