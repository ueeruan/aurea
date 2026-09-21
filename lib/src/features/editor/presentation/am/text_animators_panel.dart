/// A PREVIA DE UMA PILHA DE ANIMADORES — e so isso.
///
/// ==========================================================================
/// O QUE MORAVA AQUI E FOI EMBORA (20/09, pedido do dono)
/// ==========================================================================
///
/// Este arquivo era o "Animador Manual de Texto": um painel com posicoes
/// (entrada/enfase/saida), grade de trinta e seis miniaturas, seis
/// controles proprios, secao "Avancado (animadores do AE)" com seletores
/// montados a mao — e uma aba gigante dentro da edicao de texto para
/// chegar ate ele. O dono foi direto: "parece plugin externo / editor
/// desktop enfiado no celular", "praticamente impossivel de usar".
///
/// O animador agora e UM EFEITO COMUM: Selecionar Texto → Efeitos →
/// Texto → Animador de Texto. Ele aparece na pilha ao lado de Glow e
/// Blur, como cartao que abre, com as linhas de parametro e o losango de
/// keyframe da casa (`am/effects_panel.dart`, `domain/animador_de_texto.dart`).
///
/// SOBROU O DESENHO: [PreviaDeAnimador] pinta tres letras rodando uma
/// pilha de animadores em loop, e e o que a aba de presets usa para
/// escolher olhando em vez de ler o nome. Ele fica porque desenhar nao
/// era o problema — o problema era a ferramenta em volta.
library;

import 'dart:async';
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' show ImageFilter;

import 'package:aurea/src/core/theme/aurea_colors.dart';
import 'package:flutter/cupertino.dart';

import '../../domain/text_animator.dart';

/// O RELOGIO DAS PREVIAS DE TEXTO: UM para todas, a 12 quadros por
/// segundo, e so enquanto houver previa na tela.
///
/// ============================ POR QUE NAO UM TICKER POR PREVIA ========
///
/// Cada previa tinha um `AnimationController..repeat()`: um Ticker cada,
/// agendando um quadro a CADA vsync (60-120 por segundo). Sem raster
/// cache no Impeller, cada quadro agendado e a composicao INTEIRA
/// rasterizada de novo — e o palco fica montado atras do painel. A grade
/// aberta punha a GPU a toda com nada mudando no palco.
///
/// Um Timer so, na taxa da previa, acorda todas juntas: 12 quadros por
/// segundo em vez de 120, e um quadro por tique em vez de um por previa.
/// Nos testes ele nao corre (a previa fica no quadro zero).
class _RelogioDasPreviasDeTexto {
  _RelogioDasPreviasDeTexto._();

  static final ValueNotifier<Duration> tempo = ValueNotifier(Duration.zero);

  /// Nos testes a animacao nao corre: um relogio que nunca para impede o
  /// `pumpAndSettle` de terminar.
  static bool animar = !Platform.environment.containsKey('FLUTTER_TEST');

  static const Duration _passo = Duration(microseconds: 1000000 ~/ 12);
  static final Stopwatch _cronometro = Stopwatch();
  static Timer? _timer;
  static int _ouvintes = 0;

  static void entrou() {
    if (_ouvintes++ > 0 || !animar) return;
    _cronometro
      ..reset()
      ..start();
    _timer = Timer.periodic(_passo, (_) => tempo.value = _cronometro.elapsed);
  }

  static void saiu() {
    if (--_ouvintes > 0) return;
    _ouvintes = 0;
    _timer?.cancel();
    _timer = null;
    _cronometro.stop();
  }
}

/// PREVIA VIVA DE UMA PILHA DE ANIMADORES, em loop: tres letras rodando
/// o que a pilha faz.
class PreviaDeAnimador extends StatefulWidget {
  const PreviaDeAnimador({
    super.key,
    required this.animadores,
    this.ciclo = const Duration(milliseconds: 2000),
  });

  final List<TextAnimator> animadores;

  /// Uma volta completa da previa (a animacao mais um respiro).
  final Duration ciclo;

  @override
  State<PreviaDeAnimador> createState() => _PreviaDeAnimadorState();
}

class _PreviaDeAnimadorState extends State<PreviaDeAnimador> {
  @override
  void initState() {
    super.initState();
    _RelogioDasPreviasDeTexto.entrou();
  }

  @override
  void dispose() {
    _RelogioDasPreviasDeTexto.saiu();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<Duration>(
      valueListenable: _RelogioDasPreviasDeTexto.tempo,
      builder: (context, agora, _) => CustomPaint(
        size: Size.infinite,
        painter: _PreviewPainter(
          animators: widget.animadores,
          time: Duration(
            microseconds: agora.inMicroseconds % widget.ciclo.inMicroseconds,
          ),
        ),
      ),
    );
  }
}

class _PreviewPainter extends CustomPainter {
  _PreviewPainter({required this.animators, required this.time});

  /// A pilha, na ordem: o deslocamento de uma entra como base da
  /// seguinte, que e exatamente como o texto composto as soma.
  final List<TextAnimator> animators;
  final Duration time;

  static const _glyphs = ['A', 'b', 'c'];

  @override
  void paint(Canvas canvas, Size size) {
    const n = 3;
    const fontSize = 19.0;
    const advance = 15.0;
    final cx = size.width / 2;
    final cy = size.height / 2;

    for (var i = 0; i < n; i++) {
      var dx = 0.0, dy = 0.0, rot = 0.0, blur = 0.0, skew = 0.0, hue = 0.0;
      var sc = 100.0, op = 100.0, scx = 100.0, scy = 100.0;
      var sat = 100.0, bri = 100.0, track = 0.0;
      for (final animator in animators) {
        final c = animator.coverageAt(i, n, time);
        for (final p in animator.properties) {
          switch (p.type) {
            case TextAnimProp.positionX:
              dx = p.apply(dx, time, c);
            case TextAnimProp.positionY:
              dy = p.apply(dy, time, c);
            case TextAnimProp.rotation:
              rot = p.apply(rot, time, c);
            case TextAnimProp.tracking:
              track = p.apply(track, time, c);
            case TextAnimProp.scale:
              sc = p.apply(sc, time, c);
            case TextAnimProp.opacity:
              op = p.apply(op, time, c);
            case TextAnimProp.scaleX:
              scx = p.apply(scx, time, c);
            case TextAnimProp.scaleY:
              scy = p.apply(scy, time, c);
            case TextAnimProp.blur:
              blur = p.apply(blur, time, c);
            case TextAnimProp.skew:
              skew = p.apply(skew, time, c);
            case TextAnimProp.hue:
              hue = p.apply(hue, time, c);
            case TextAnimProp.saturation:
              sat = p.apply(sat, time, c);
            case TextAnimProp.brightness:
              bri = p.apply(bri, time, c);
            case TextAnimProp.rotationX:
            case TextAnimProp.rotationY:
            case TextAnimProp.positionZ:
              // A miniatura e 2D; o 3D aparece no preview.
              break;
          }
        }
      }

      final opacity = (op / 100).clamp(0.0, 1.0);
      if (opacity <= 0.01) continue;
      final sx = math.max(0.0, sc / 100 * scx / 100);
      final sy = math.max(0.0, sc / 100 * scy / 100);
      if (sx <= 0.01 || sy <= 0.01) continue;

      var color = AureaColors.text;
      if (hue != 0 || sat != 100 || bri != 100) {
        final hsl = HSLColor.fromColor(color);
        final h = (hsl.hue + hue) % 360;
        color = hsl
            .withHue(h < 0 ? h + 360 : h)
            .withSaturation((hsl.saturation * sat / 100).clamp(0.0, 1.0))
            .withLightness((hsl.lightness * bri / 100).clamp(0.0, 1.0))
            .toColor();
      }

      final tp = TextPainter(
        text: TextSpan(
          text: _glyphs[i],
          style: TextStyle(
            fontSize: fontSize,
            fontWeight: FontWeight.w700,
            color: color.withValues(alpha: opacity),
          ),
        ),
        textDirection: TextDirection.ltr,
      )..layout();

      // A previa e pequena: o deslocamento entra reduzido, senao a letra
      // sai da miniatura e a pessoa nao ve nada.
      const k = 0.16;
      final x = cx + (i - 1) * (advance + track * k) + dx * k;
      final y = cy + dy * k;

      canvas.save();
      final blurring = blur > 0.4;
      if (blurring) {
        canvas.saveLayer(
          Rect.fromCenter(
            center: Offset(x, y),
            width: size.width,
            height: size.height,
          ),
          Paint()
            ..imageFilter = ImageFilter.blur(
              sigmaX: blur * 0.22,
              sigmaY: blur * 0.22,
            ),
        );
      }
      canvas.translate(x, y);
      if (rot != 0) canvas.rotate(rot * math.pi / 180);
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
      canvas.scale(sx, sy);
      tp.paint(canvas, Offset(-tp.width / 2, -tp.height / 2));
      canvas.restore();
      if (blurring) canvas.restore();
    }
  }

  @override
  bool shouldRepaint(_PreviewPainter old) => true;
}
