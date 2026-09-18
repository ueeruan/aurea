import 'dart:math' as math;
import 'dart:ui';

import 'package:flutter/widgets.dart'
    show FontWeight, TextDirection, TextPainter, TextSpan, TextStyle;

import 'ajuste_da_midia.dart';
import 'layer.dart';
import 'shape.dart';

/// Caixa RENDERIZADA de uma camada, em px logicos. E a medida que
/// alinhamento, distribuicao, forma-conteiner e empilhamento usam — por
/// isso vive num lugar so, e nao copiada em cada um.
Size measureLayerBox(
  Layer layer,
  Duration local, {
  double fallbackWidth = 1080,
  double? compHeight,
  bool scaled = true,
  /// Familia JA RESOLVIDA para a camada de texto. Sem ela a medida usa a
  /// fonte do aplicativo enquanto o palco desenha a importada.
  String? Function(String?)? resolveFamily,
}) {
  final sx = layer.scaleX.valueAt(local).abs();
  final sy = layer.scaleY.valueAt(local).abs();
  final base = switch (layer) {
    ShapeLayer l => shapeBounds(evaluateShape(l.contents, local)).size,
    Element3DLayer l => Size(l.size * 2, l.size * 2),
    ParticlesLayer _ => const Size(420, 420),
    TextLayer l => measureText(
      l.text,
      l.fontSize,
      l.bold,
      fontFamily: resolveFamily?.call(l.fontFamily),
    ),
    CameraLayer _ => Size.zero,
    NullLayer _ => Size.zero,
    AudioLayer _ => Size.zero,
    AdjustmentLayer _ => const Size(220, 220),
    // A legenda NAO tem familia propria (CaptionStyle so guarda corpo,
    // cor e negrito): ela sempre sai na fonte do aplicativo, e medir com
    // ela e o certo.
    CaptionLayer l => measureText(
      l.cueAt(local)?.text ?? '',
      l.style.fontSize,
      l.style.bold,
    ),
    // O GRUPO mede o que ele mostra: a uniao dos filhos. Antes caia no
    // retangulo generico 16:9, e em projeto vertical as alcas e o toque
    // nao tinham nada a ver com o conteudo.
    GroupLayer g =>
      groupContentRect(
        g,
        local,
        compWidth: fallbackWidth,
        compHeight: compHeight ?? fallbackWidth * 9 / 16,
      )?.size ??
      Size(fallbackWidth, compHeight ?? fallbackWidth * 9 / 16),
    // FOTO E VIDEO medem a caixa em que o palco os desenha: pelo ajuste
    // (cobrir, conter ou pela largura) e pela proporcao do arquivo. Antes
    // caiam no 16:9 inventado, e em projeto vertical a alca de escala e o
    // toque nao tinham nada a ver com a midia.
    ImageLayer l => caixaDaMidia(
      Size(fallbackWidth, compHeight ?? fallbackWidth * 9 / 16),
      l.proporcaoDaFonte,
      l.ajuste,
    ),
    VideoLayer l => caixaDaMidia(
      Size(fallbackWidth, compHeight ?? fallbackWidth * 9 / 16),
      l.proporcaoDaFonte,
      l.ajuste,
    ),
    _ => Size(fallbackWidth, fallbackWidth * 9 / 16),
  };
  return scaled ? Size(base.width * sx, base.height * sy) : base;
}

/// A CAIXA DO CONTEUDO de um grupo no espaco da CAMADA (a origem e a
/// posicao do grupo), sem a transformacao do proprio grupo: a uniao das
/// caixas dos filhos ativos no instante, cada uma girada e escalada pelo
/// filho. O conteudo e desenhado numa caixa do tamanho da composicao
/// centrada na posicao do grupo, entao o filho em q (coordenada da
/// composicao) mora em q - centro. Nulo se nenhum filho tem area.
Rect? groupContentRect(
  GroupLayer g,
  Duration local, {
  required double compWidth,
  required double compHeight,
}) {
  final tempo = g.contentTimeAt(local);
  final centro = Offset(compWidth / 2, compHeight / 2);
  Rect? uniao;
  for (final c in g.children) {
    if (!c.activeAt(tempo)) continue;
    final lc = c.localTime(tempo);
    final Rect caixa;
    if (c is GroupLayer) {
      final r = groupContentRect(c, lc, compWidth: compWidth, compHeight: compHeight);
      if (r == null) continue;
      caixa = r;
    } else {
      final s = measureLayerBox(
        c,
        lc,
        fallbackWidth: compWidth,
        compHeight: compHeight,
        scaled: false,
      );
      if (s.isEmpty) continue;
      caixa = Rect.fromCenter(center: Offset.zero, width: s.width, height: s.height);
    }
    final pos = c.position.valueAt(lc);
    final pivo = c.pivot.valueAt(lc);
    final ang = c.rotation.valueAt(lc) * math.pi / 180;
    final sx = c.scaleX.valueAt(lc), sy = c.scaleY.valueAt(lc);
    final cosA = math.cos(ang), senA = math.sin(ang);
    for (final canto in [caixa.topLeft, caixa.topRight, caixa.bottomLeft, caixa.bottomRight]) {
      // A mesma ordem do palco: posicao, pivo, giro, escala, pivo de volta.
      final u = Offset((canto.dx - pivo.dx) * sx, (canto.dy - pivo.dy) * sy);
      final q = pos + pivo + Offset(u.dx * cosA - u.dy * senA, u.dx * senA + u.dy * cosA);
      final p = q - centro;
      final ponto = Rect.fromLTWH(p.dx, p.dy, 0, 0);
      uniao = uniao == null ? ponto : uniao.expandToInclude(ponto);
    }
  }
  if (uniao == null || (uniao.width <= 0 && uniao.height <= 0)) return null;
  return uniao;
}

/// OS MESMOS NUMEROS COM QUE O PALCO DESENHA O TEXTO.
///
/// Estes tres valores viviam escritos a mao aqui e DIFERENTES do desenho
/// (`AnimatedTextView.styleFor`, em `animated_text.dart`): a medida usava
/// altura de linha 1,2 e espacamento zero, e o palco usa 1,1 e
/// -2% do corpo. A caixa de selecao, as alcas de arrasto e a area de
/// toque de uma camada de texto saiam sistematicamente MAIORES que o texto
/// desenhado — e com fonte importada o erro era outro, porque a medida
/// nem pedia a familia.
///
/// Agora o numero mora num lugar so e os dois lados leem daqui.
const double kTextHeightFactor = 1.1;
const double kTextTrackingFactor = -0.02;

/// Caixa do texto medida de verdade (nao estimada).
///
/// [fontFamily] deve vir ja resolvido (ver `resolveFontFamily`): fonte
/// importada que sumiu cai na do aplicativo, e medir com um nome que nao
/// existe da a metrica errada.
Size measureText(
  String text,
  double fontSize,
  bool bold, {
  String? fontFamily,
  double height = kTextHeightFactor,
}) {
  if (text.isEmpty) return Size(0, fontSize * height);
  final painter = TextPainter(
    text: TextSpan(
      text: text,
      style: TextStyle(
        fontSize: fontSize,
        fontFamily: fontFamily,
        fontWeight: bold ? FontWeight.w700 : FontWeight.w400,
        letterSpacing: fontSize * kTextTrackingFactor,
        height: height,
      ),
    ),
    textDirection: TextDirection.ltr,
  )..layout();
  return painter.size;
}
