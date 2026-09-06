import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/editor/presentation/widgets/scene3d_painter.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// RASCUNHO ENQUANTO TOCA.
///
/// Um quadro cheio da cena 3D — reflexo no chao, sombra de contato,
/// profundidade de campo — passa de 300 ms num celular. Trinta e tres
/// milissegundos e o que existe entre dois quadros a 30 fps, entao
/// tocar em qualidade cheia nao e "um pouco lento": e o app parado.
///
/// A regra que estes testes fixam: com o play apertado, o preview
/// desenha em rascunho; parado ou exportando, em qualidade cheia.
void main() {
  testWidgets('o preview desenha a cena 3D em rascunho enquanto toca', (
    tester,
  ) async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    container.read(editorControllerProvider.notifier).addScene3DLayer(
      Duration.zero,
    );

    late PlaybackController playback;
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: _Host(
            builder: (p) {
              playback = p;
              return PreviewStage(playback: p, videos: VideoLayerManager());
            },
          ),
        ),
      ),
    );
    await tester.pump();

    bool desenhandoEmRascunho() {
      final pintores = tester
          .widgetList<CustomPaint>(find.byType(CustomPaint))
          .map((w) => w.painter)
          .whereType<Scene3DPainter>()
          .toList();
      expect(pintores, isNotEmpty, reason: 'a cena 3D nao esta no preview');
      return pintores.every((p) => p.scene.draftMode);
    }

    expect(
      desenhandoEmRascunho(),
      isFalse,
      reason: 'parado, o preview mostra a cena inteira',
    );

    playback.play();
    await tester.pump();
    expect(
      desenhandoEmRascunho(),
      isTrue,
      reason: 'tocando, o quadro cheio nao cabe no intervalo entre quadros',
    );

    playback.pause();
    await tester.pump();
    expect(
      desenhandoEmRascunho(),
      isFalse,
      reason: 'na pausa a qualidade cheia volta — e onde ela e olhada',
    );
  });
}

class _Host extends StatefulWidget {
  const _Host({required this.builder});

  final Widget Function(PlaybackController) builder;

  @override
  State<_Host> createState() => _HostState();
}

class _HostState extends State<_Host> with SingleTickerProviderStateMixin {
  late final PlaybackController playback = PlaybackController(
    vsync: this,
    durationOf: () => const Duration(seconds: 10),
  );

  @override
  void dispose() {
    playback.pause();
    playback.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) =>
      Scaffold(body: SizedBox(width: 400, height: 700, child: widget.builder(playback)));
}
