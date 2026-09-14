import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../../../../core/utils/time_format.dart';
import '../../application/playback_controller.dart';
import '../am/am_colors.dart';

/// A BARRA DE TEMPO DA TELA CHEIA.
///
/// Na tela cheia a timeline some, e sem ela nao havia como andar pelo
/// video: so o play e os saltos de keyframe. Esta barra e o scrub da tela
/// cheia — tocar pula para o ponto, arrastar percorre, e se o video
/// estava tocando ele volta a tocar ao soltar.
class BarraDeTempoTelaCheia extends StatefulWidget {
  const BarraDeTempoTelaCheia({super.key, required this.playback});

  static const double altura = 44;

  final PlaybackController playback;

  @override
  State<BarraDeTempoTelaCheia> createState() => _BarraDeTempoTelaCheiaState();
}

class _BarraDeTempoTelaCheiaState extends State<BarraDeTempoTelaCheia> {
  bool _tocavaAntes = false;
  bool _arrastando = false;

  void _irPara(double x, double largura) {
    final total = widget.playback.durationOf();
    if (largura <= 0 || total <= Duration.zero) return;
    final f = (x / largura).clamp(0.0, 1.0);
    widget.playback.seek(
      Duration(microseconds: (total.inMicroseconds * f).round()),
    );
  }

  @override
  Widget build(BuildContext context) {
    final p = widget.playback;
    return SizedBox(
      key: const ValueKey('tela-cheia-tempo'),
      height: BarraDeTempoTelaCheia.altura,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 14),
        child: ValueListenableBuilder<Duration>(
          valueListenable: p.time,
          builder: (context, agora, _) {
            final total = p.durationOf();
            final f = total.inMicroseconds <= 0
                ? 0.0
                : (agora.inMicroseconds / total.inMicroseconds).clamp(0.0, 1.0);
            const estilo = TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w600,
              color: AmColors.text,
              fontFeatures: [FontFeature.tabularFigures()],
            );
            return Row(
              children: [
                Text(formatTime(agora), style: estilo),
                const SizedBox(width: 12),
                Expanded(
                  child: LayoutBuilder(
                    builder: (context, c) => GestureDetector(
                      key: const ValueKey('tela-cheia-trilho'),
                      behavior: HitTestBehavior.opaque,
                      onTapDown: (d) => _irPara(d.localPosition.dx, c.maxWidth),
                      onHorizontalDragStart: (d) {
                        _tocavaAntes = p.playing.value;
                        if (_tocavaAntes) p.pause();
                        setState(() => _arrastando = true);
                        HapticFeedback.selectionClick();
                        _irPara(d.localPosition.dx, c.maxWidth);
                      },
                      onHorizontalDragUpdate: (d) =>
                          _irPara(d.localPosition.dx, c.maxWidth),
                      onHorizontalDragEnd: (_) {
                        setState(() => _arrastando = false);
                        if (_tocavaAntes) p.play();
                      },
                      child: SizedBox(
                        height: BarraDeTempoTelaCheia.altura,
                        child: CustomPaint(
                          painter: _TrilhoPainter(
                            fracao: f,
                            arrastando: _arrastando,
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
                const SizedBox(width: 12),
                Text(formatTime(total), style: estilo.copyWith(color: AmColors.muted)),
              ],
            );
          },
        ),
      ),
    );
  }
}

class _TrilhoPainter extends CustomPainter {
  const _TrilhoPainter({required this.fracao, required this.arrastando});

  final double fracao;
  final bool arrastando;

  @override
  void paint(Canvas canvas, Size size) {
    final y = size.height / 2;
    final espessura = arrastando ? 5.0 : 3.0;
    final fundo = Paint()
      ..color = Colors.white.withValues(alpha: .22)
      ..strokeWidth = espessura
      ..strokeCap = StrokeCap.round;
    final feito = Paint()
      ..color = AmColors.accent
      ..strokeWidth = espessura
      ..strokeCap = StrokeCap.round;
    final x = size.width * fracao;
    canvas.drawLine(Offset(0, y), Offset(size.width, y), fundo);
    canvas.drawLine(Offset(0, y), Offset(x, y), feito);
    canvas.drawCircle(
      Offset(x, y),
      arrastando ? 9 : 7,
      Paint()..color = Colors.white,
    );
  }

  @override
  bool shouldRepaint(_TrilhoPainter old) =>
      old.fracao != fracao || old.arrastando != arrastando;
}
