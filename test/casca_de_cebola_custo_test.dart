import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// O QUE A CASCA DE CEBOLA CUSTA, EM QUADROS INTEIROS.
///
/// Cada fantasma nao e um retangulo tingido: e a composicao INTEIRA —
/// todas as camadas, todos os efeitos — desenhada num instante vizinho.
/// Com a casca em 2 sao QUATRO composicoes a mais por quadro de video,
/// sobrepostas a principal. Em cinco quadros empilhados ninguem enxerga
/// o espacamento entre poses, entao durante o play isso nao e qualidade
/// a menos: e trabalho jogado fora, e o que a pessoa quer ver tocando e
/// o movimento, nao a pose.
///
/// A casca e uma ajuda de POSICAO: ela vale parada. Estes testes fixam
/// as duas contas — quantas composicoes e quantas camadas o fantasma
/// pede.
void main() {
  testWidgets('a casca some enquanto toca e volta na pausa', (tester) async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final videos = VideoLayerManager();
    addTearDown(videos.dispose);

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

    // Um instante no meio da composicao: em zero os fantasmas do passado
    // cairiam antes do comeco e nao existiriam (o que e correto, e tem
    // teste proprio abaixo).
    playback.time.value = const Duration(seconds: 4);
    await tester.pump();

    int composicoes() => tester.widgetList(find.byType(CompositionView)).length;

    expect(
      composicoes(),
      1,
      reason: 'casca desligada: so a composicao principal',
    );

    container.read(onionSkinProvider.notifier).state = 2;
    await tester.pump();
    expect(
      composicoes(),
      5,
      reason: 'casca em 2: a principal e quatro fantasmas (2 aneis x 2 lados)',
    );

    playback.play();
    await tester.pump();
    expect(
      composicoes(),
      1,
      reason: 'tocando, a casca nao desenha: quatro composicoes por quadro '
          'e trabalho que nao se aproveita',
    );

    playback.pause();
    await tester.pump();
    expect(
      composicoes(),
      5,
      reason: 'na pausa a casca volta inteira, no instante em que parou',
    );
  });

  testWidgets('o fantasma nao abre uma segunda camada de tinta', (
    tester,
  ) async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final videos = VideoLayerManager();
    addTearDown(videos.dispose);

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
    // A casca em 1 vem sem desenho livre e sem nenhuma camada de
    // transicao na arvore: o que a contagem pega e do fantasma.
    container.read(onionSkinProvider.notifier).state = 1;
    playback.time.value = const Duration(seconds: 4);
    await tester.pump();

    final umAnel = tester.widgetList(find.byType(Opacity)).length;
    expect(
      find.byType(CompositionView),
      findsNWidgets(3),
      reason: 'um anel sao dois fantasmas',
    );

    // Tinta e opacidade do fantasma cabem no MESMO `ColorFiltered`: o
    // alfa do `modulate` ja multiplica pelo alfa da camada. Enquanto
    // eram um `Opacity` por cima de um `ColorFiltered`, cada fantasma
    // custava dois `saveLayer` — dois alvos de render, duas texturas.
    container.read(onionSkinProvider.notifier).state = 2;
    await tester.pump();
    expect(
      find.byType(CompositionView),
      findsNWidgets(5),
      reason: 'dois aneis sao quatro fantasmas',
    );
    expect(
      tester.widgetList(find.byType(Opacity)).length,
      umAnel,
      reason: 'dobrar os fantasmas nao pode abrir nenhuma camada de '
          'opacidade nova',
    );
  });

  testWidgets('antes do primeiro quadro nao ha fantasma do passado', (
    tester,
  ) async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final videos = VideoLayerManager();
    addTearDown(videos.dispose);

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: _Host(
            builder: (p) => PreviewStage(playback: p, videos: videos),
          ),
        ),
      ),
    );
    await tester.pump();
    container.read(onionSkinProvider.notifier).state = 2;
    await tester.pump();

    expect(
      find.byType(CompositionView),
      findsNWidgets(3),
      reason: 'em zero, so os dois fantasmas do futuro existem',
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
  Widget build(BuildContext context) => Scaffold(
    body: SizedBox(width: 400, height: 700, child: widget.builder(playback)),
  );
}
