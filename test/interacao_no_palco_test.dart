// O RASCUNHO LIGA COM O DEDO NO COMANDO E DESLIGA AO SOLTAR.
//
// Interagir era mais caro do que tocar: um slider arrastado com o relogio
// parado rodava em qualidade cheia a cada passo. Agora toda mutacao do
// projeto com o relogio parado (e todo seek parado) marca a interacao; o
// sinal cai sozinho depois da folga e sai UM quadro final inteiro.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/interacao.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  setUp(() => Interacao.ligada = true);
  tearDown(() {
    Interacao.zerar();
    Interacao.ligada = false;
  });

  testWidgets('mutacao com o relogio parado liga o rascunho; a folga desliga', (
    tester,
  ) async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final videos = VideoLayerManager();
    addTearDown(videos.dispose);
    final controller = container.read(editorControllerProvider.notifier);
    controller.openProject(VideoProject.empty('interacao'));

    late PlaybackController playback;
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: _Host(
            builder: (p) {
              playback = p;
              return PreviewStage(playback: p, videos: videos);
            },
          ),
        ),
      ),
    );
    await tester.pump();
    expect(Interacao.agora.value, isFalse, reason: 'em repouso nao ha gesto');

    // UMA MUTACAO QUALQUER DO PROJETO (o que um slider faz a cada passo).
    controller.addShapeLayer(Duration.zero);
    await tester.pump();
    expect(Interacao.agora.value, isTrue, reason: 'o dedo esta no comando');

    // A FOLGA PASSA SEM OUTRA MUTACAO: o sinal cai e o quadro final sai.
    await tester.pump(const Duration(milliseconds: 300));
    expect(Interacao.agora.value, isFalse);

    // TOCANDO, A MUTACAO NAO MARCA: o rascunho ja vale pelo play.
    playback.play();
    await tester.pump();
    controller.addShapeLayer(Duration.zero);
    await tester.pump();
    expect(Interacao.agora.value, isFalse);
    playback.pause();
    await tester.pump();

    // ESFREGAR A TIMELINE PARADA E UMA INTERACAO.
    playback.seek(const Duration(seconds: 1));
    expect(Interacao.agora.value, isTrue);
    await tester.pump(const Duration(milliseconds: 300));
    expect(Interacao.agora.value, isFalse);
  });

  test('soltar derruba o sinal sem esperar a folga; desligada nao marca', () {
    Interacao.marcar();
    expect(Interacao.agora.value, isTrue);
    Interacao.soltar();
    expect(Interacao.agora.value, isFalse);

    Interacao.ligada = false;
    Interacao.marcar();
    expect(Interacao.agora.value, isFalse, reason: 'nos testes nasce desligada');
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
  Widget build(BuildContext context) => Scaffold(
    body: SizedBox(width: 400, height: 700, child: widget.builder(playback)),
  );
}
