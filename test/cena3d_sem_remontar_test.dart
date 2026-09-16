import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/scene3d_gpu.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/editor/presentation/widgets/scene3d_painter.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// A CENA 3D NAO E RECRIADA POR EVENTO COMUM DA INTERFACE.
///
/// Recriar o conteudo de uma camada Cena 3D e, no aparelho, jogar fora o
/// motor inteiro: cena nova, geometria e texturas subindo de novo, alvos
/// de desenho novos — e a memoria antiga so voltando quando o coletor
/// passar. Era o engasgo de "poucos elementos": selecionar a camada
/// embrulhava o conteudo num Stack (a moldura), e uma camada entrando no
/// tempo deslocava as vizinhas sem chave. Aqui a prova e o ELEMENTO da
/// cena: o mesmo objeto antes e depois.
void main() {
  Element elementoDaCena(WidgetTester tester) {
    final achados = tester
        .elementList(find.byType(CustomPaint))
        .where((e) => (e.widget as CustomPaint).painter is Scene3DPainter)
        .toList();
    expect(achados, hasLength(1), reason: 'uma cena 3D no palco');
    return achados.single;
  }

  Future<(ProviderContainer, PlaybackController)> montar(
    WidgetTester tester,
  ) async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    // A pergunta "ha GPU?" responde uma vez por sessao e troca o caminho
    // da cena (motor em GPU -> pintor em CPU) uma unica vez. Responder
    // antes, para medir so o que a interface faz.
    await tester.runAsync(Scene3DGpu.preparar);
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
    return (container, playback);
  }

  testWidgets('selecionar e tirar a selecao nao recria a cena', (tester) async {
    final (c, _) = await montar(tester);
    c.read(editorControllerProvider.notifier).addScene3DLayer(Duration.zero);
    await tester.pump();
    final cena = c
        .read(editorControllerProvider)
        .layers
        .whereType<Scene3DLayer>()
        .single;
    final antes = elementoDaCena(tester);

    c.read(selectedLayerProvider.notifier).state = cena.id;
    await tester.pump();
    expect(
      identical(elementoDaCena(tester), antes),
      isTrue,
      reason: 'selecionar recriou a cena',
    );

    c.read(selectedLayerProvider.notifier).state = null;
    await tester.pump();
    expect(
      identical(elementoDaCena(tester), antes),
      isTrue,
      reason: 'tirar a selecao recriou a cena',
    );
  });

  testWidgets('outra camada entrando no tempo nao recria a cena', (
    tester,
  ) async {
    final (c, playback) = await montar(tester);
    final editor = c.read(editorControllerProvider.notifier);
    // Um texto que so aparece em 2 s, por BAIXO da cena na pilha: quando
    // ele entra, a cena muda de posicao na lista de filhos do palco.
    editor.addTextLayer(const Duration(seconds: 2), text: 'Depois');
    editor.addScene3DLayer(Duration.zero);
    await tester.pump();
    final antes = elementoDaCena(tester);

    playback.seek(const Duration(seconds: 3));
    await tester.pump();
    expect(
      find.text('Depois'),
      findsNothing,
      reason: 'o texto do palco nao e um Text cru',
    );
    expect(
      identical(elementoDaCena(tester), antes),
      isTrue,
      reason: 'a camada que entrou no tempo recriou a cena',
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
