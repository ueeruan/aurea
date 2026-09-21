import 'package:flutter/foundation.dart' show ValueListenable;
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../shell/layer_actions.dart' show menuDasMarcas;
import 'estado_da_timeline.dart';
import 'regua.dart' show textoDoTempo;

/// O CABECOTE: linha de 1,5 FIXA NO CENTRO da timeline, com o selo do
/// tempo no alto da regua. Nao se arrasta — quem anda e o tempo, por baixo
/// dele. A zona de toque (100 x 38) no alto: tocar poe (ou tira) uma marca
/// no instante do cabecote; segurar abre as marcas. Arrastar a partir dela
/// continua sendo scrub (a zona so disputa toque e toque longo).
///
/// O UNICO pedaco que o tique do relogio repinta por conta propria e o
/// selo — numa fronteira de repintura propria.
class CabecoteDaTimeline extends ConsumerWidget {
  const CabecoteDaTimeline({
    super.key,
    required this.estado,
    required this.playback,
  });

  final EstadoDaTimeline estado;
  final PlaybackController playback;

  static const double _alturaDoSelo = 18;
  static const double _larguraDoSelo = 70;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final fps = ref.watch(projetoVisivelProvider.select((p) => p.fps));
    final centro = estado.centro;
    final base = DefaultTextStyle.of(context).style;
    return Stack(
      children: [
        Positioned(
          left: centro - AureaDims.cabecote / 2,
          top: 2 + _alturaDoSelo,
          bottom: 0,
          width: AureaDims.cabecote,
          child: IgnorePointer(
            child: ColoredBox(
              key: const ValueKey('timeline-cabecote'),
              color: AureaCores.cabecote,
            ),
          ),
        ),
        Positioned(
          left: centro - _larguraDoSelo / 2,
          top: 2,
          width: _larguraDoSelo,
          height: _alturaDoSelo,
          child: IgnorePointer(
            child: RepaintBoundary(
              child: CustomPaint(
                key: const ValueKey('timeline-selo-do-tempo'),
                painter: _PintorDoSelo(
                  tempo: playback.time,
                  fps: fps <= 0 ? 30 : fps,
                  fundo: AureaCores.cromo,
                  estilo: base.copyWith(
                    fontSize: AureaDims.textoDeInfo,
                    fontWeight: FontWeight.w600,
                    color: AureaCores.texto,
                    decoration: TextDecoration.none,
                    fontFeatures: const [FontFeature.tabularFigures()],
                  ),
                ),
              ),
            ),
          ),
        ),
        Positioned(
          left: centro - AureaDims.toqueDoCabecote.width / 2,
          top: 5,
          width: AureaDims.toqueDoCabecote.width,
          height: AureaDims.toqueDoCabecote.height,
          child: GestureDetector(
            key: const ValueKey('timeline-toque-do-cabecote'),
            behavior: HitTestBehavior.translucent,
            onTap: () {
              HapticFeedback.selectionClick();
              ref
                  .read(editorControllerProvider.notifier)
                  .toggleMarker(playback.timeForInput());
            },
            onLongPress: () {
              HapticFeedback.mediumImpact();
              menuDasMarcas(context, ref, playback);
            },
          ),
        ),
      ],
    );
  }
}

class _PintorDoSelo extends CustomPainter {
  _PintorDoSelo({
    required this.tempo,
    required this.fps,
    required this.fundo,
    required this.estilo,
  }) : super(repaint: tempo);

  final ValueListenable<Duration> tempo;
  final int fps;
  final Color fundo;
  final TextStyle estilo;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRRect(
      RRect.fromRectAndRadius(
        Offset.zero & size,
        const Radius.circular(AureaDims.raioSm),
      ),
      Paint()..color = fundo,
    );
    final tp = TextPainter(
      text: TextSpan(text: textoDoTempo(tempo.value, fps), style: estilo),
      textDirection: TextDirection.ltr,
    )..layout();
    tp.paint(
      canvas,
      Offset((size.width - tp.width) / 2, (size.height - tp.height) / 2),
    );
    tp.dispose();
  }

  @override
  bool shouldRepaint(_PintorDoSelo old) =>
      old.tempo != tempo ||
      old.fps != fps ||
      old.fundo != fundo ||
      old.estilo != estilo;
}
