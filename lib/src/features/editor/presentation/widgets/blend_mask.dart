import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

/// Aplica um BlendMode ao filho contra o que ja foi pintado por baixo.
///
/// DOIS CAMINHOS, e a diferenca entre eles e a diferenca entre o modo
/// funcionar e ser ignorado em silencio:
///
///  * Filho SEM camadas proprias (formas, texto, imagens): um saveLayer
///    no canvas com `Paint.blendMode`. Barato e exato.
///
///  * Filho COM camadas do motor (Opacity, ImageFiltered, ColorFiltered,
///    Transform composto...): o saveLayer do canvas NAO os alcanca — o
///    conteudo deles sai numa camada separada, desenhada DEPOIS do
///    restore, em modo normal. Era o que acontecia com todo glow: os
///    niveis "somados" cobriam a fonte em vez de somar, e o miolo ficava
///    escuro. Aqui o filho e FOTOGRAFADO (com [margem] para o que
///    escapa da caixa, como o halo de um desfoque) e a foto e desenhada
///    com o modo — um drawImage, que o modo alcanca sempre.
///
/// Textura de video nao entra em foto; para ela [permitirFoto] deve ser
/// falso e o caminho do canvas e usado mesmo sem alcancar tudo.
class BlendMask extends SingleChildRenderObjectWidget {
  const BlendMask({
    super.key,
    required this.blendMode,
    this.margem = 0,
    this.permitirFoto = true,
    this.isolate = false,
    super.child,
  });

  final BlendMode blendMode;

  /// Quanto o filho pinta FORA da propria caixa (em pixels logicos):
  /// o alcance do desfoque, o deslocamento de um canal. Entra na foto.
  final double margem;

  final bool permitirFoto;

  /// Mesmo em [BlendMode.srcOver], pinta o filho num grupo isolado.
  /// Necessario quando um descendente usa dstIn/dstOut: sem este grupo,
  /// ele tambem altera tudo o que ja estava no canvas por baixo.
  final bool isolate;

  @override
  RenderObject createRenderObject(BuildContext context) => _RenderBlendMask(
    blendMode,
    margem,
    permitirFoto,
    isolate,
    MediaQuery.maybeDevicePixelRatioOf(context) ?? 1.0,
  );

  @override
  void updateRenderObject(
    BuildContext context,
    covariant RenderObject renderObject,
  ) {
    (renderObject as _RenderBlendMask)
      ..blendMode = blendMode
      ..margem = margem
      ..permitirFoto = permitirFoto
      ..isolate = isolate
      ..pixelRatio = MediaQuery.maybeDevicePixelRatioOf(context) ?? 1.0;
  }
}

class _RenderBlendMask extends RenderProxyBox {
  _RenderBlendMask(
    this._blendMode,
    this._margem,
    this._permitirFoto,
    this._isolate,
    this._pixelRatio,
  );

  BlendMode _blendMode;
  BlendMode get blendMode => _blendMode;
  set blendMode(BlendMode value) {
    if (_blendMode == value) return;
    _blendMode = value;
    markNeedsPaint();
  }

  double _margem;
  double get margem => _margem;
  set margem(double value) {
    if (_margem == value) return;
    _margem = value;
    markNeedsPaint();
  }

  bool _permitirFoto;
  bool get permitirFoto => _permitirFoto;
  set permitirFoto(bool value) {
    if (_permitirFoto == value) return;
    _permitirFoto = value;
    markNeedsPaint();
  }

  bool _isolate;
  bool get isolate => _isolate;
  set isolate(bool value) {
    if (_isolate == value) return;
    _isolate = value;
    markNeedsPaint();
  }

  double _pixelRatio;
  double get pixelRatio => _pixelRatio;
  set pixelRatio(double value) {
    if (_pixelRatio == value) return;
    _pixelRatio = value;
    markNeedsPaint();
  }

  /// A foto do filho, com a margem, na razao de pixels do preview.
  ui.Image _fotografar(RenderBox filho, Rect limites, double razao) {
    final camada = OffsetLayer();
    final ctx = PaintingContext(camada, limites);
    ctx.paintChild(filho, Offset.zero);
    // ignore: invalid_use_of_protected_member
    ctx.stopRecordingIfNeeded();
    final img = camada.toImageSync(limites, pixelRatio: razao);
    camada.dispose();
    return img;
  }

  /// Ha textura de video (ou outra superficie externa) no filho: ela
  /// nao entra em foto, entao a foto sairia com um buraco preto.
  static bool _temTextura(RenderObject r) {
    if (r is TextureBox) return true;
    var achou = false;
    r.visitChildren((c) {
      if (!achou && _temTextura(c)) achou = true;
    });
    return achou;
  }

  @override
  void paint(PaintingContext context, Offset offset) {
    final filho = child;
    if (filho == null) return;
    final canvas = context.canvas;

    // Modo normal nao precisa de camada nenhuma.
    if (_blendMode == BlendMode.srcOver && !_isolate) {
      super.paint(context, offset);
      return;
    }

    if (!filho.needsCompositing || !_permitirFoto || _temTextura(filho)) {
      // Sem limite: o filho pode pintar fora da caixa (halo de desfoque)
      // e o limite cortava o que ficava acima e a esquerda.
      canvas.saveLayer(null, Paint()..blendMode = _blendMode);
      super.paint(context, offset);
      canvas.restore();
      return;
    }

    final cabe = fotoQueCabe(size, _margem, _pixelRatio);
    final m = cabe.margem;
    final limites = Rect.fromLTWH(
      -m,
      -m,
      size.width + 2 * m,
      size.height + 2 * m,
    );
    if (limites.isEmpty) return;
    final razao = cabe.razao;
    final foto = _fotografar(filho, limites, razao);

    canvas.save();
    canvas.translate(offset.dx - m, offset.dy - m);
    canvas.scale(1 / razao);
    canvas.drawImage(
      foto,
      Offset.zero,
      Paint()
        ..blendMode = _blendMode
        ..filterQuality = FilterQuality.low,
    );
    canvas.restore();
    // O drawImage ja copiou a referencia para o quadro: a foto pode ser
    // liberada agora (mesmo ciclo da mescla customizada).
    foto.dispose();
  }
}

/// TETO DA FOTO DA MESCLA, em megapixels.
///
/// `toImageSync` aloca uma textura de verdade: largura x altura x razao
/// de pixels, quatro bytes cada. Um Deep Glow de raio grande pedia dois
/// mil pixels de margem numa composicao 1080x1920 a 3x — 284
/// megapixels, 1,1 GB — e o app fechava antes de desenhar o quadro.
/// Corrigir os efeitos um a um nao basta: um efeito novo pode pedir o
/// mesmo amanha, e este e o unico ponto por onde todos passam.
const double kFotoTetoMegapixels = 12;

/// Margem maxima da foto, em pixels logicos. Acima disto o halo ja saiu
/// da composicao inteira: o que se ganha e nada, e o que se paga e a
/// resolucao de tudo o que sobrou dentro da foto.
const double kFotoTetoDaMargem = 2048;

/// A MARGEM E A RESOLUCAO que cabem no teto, para um filho de
/// [tamanho] com [margem] pedida e razao de pixels [pixelRatio].
///
/// Quando o pedido passa do teto a foto sai em RESOLUCAO MENOR, nunca
/// cortada: um halo desfocado em meia resolucao continua sendo o mesmo
/// halo; um halo cortado e um retangulo no meio da tela.
({double margem, double razao}) fotoQueCabe(
  Size tamanho,
  double margem,
  double pixelRatio,
) {
  final m = margem.isFinite ? margem.clamp(0.0, kFotoTetoDaMargem) : 0.0;
  final area = (tamanho.width + 2 * m) * (tamanho.height + 2 * m);
  final pr = pixelRatio.isFinite && pixelRatio > 0 ? pixelRatio : 1.0;
  if (area <= 0) return (margem: m, razao: pr);
  final maxima = math.sqrt(kFotoTetoMegapixels * 1e6 / area);
  return (margem: m, razao: math.min(pr, math.max(0.05, maxima)));
}
