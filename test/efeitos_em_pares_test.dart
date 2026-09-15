// O AUDITOR DE PARES: todo efeito em cima de todo efeito, no palco de
// verdade, um quadro bombeado por par — e NENHUMA excecao aceita.
//
// O relato de campo (15/09): "alguns efeitos se por em cima um do outro
// buga TUDO". Um efeito que estoura no meio do pipeline derruba a
// arvore inteira do palco — e o quadro fica podre dali em diante. Este
// teste e a rede: percorre os pares ORDENADOS (a ordem da pilha muda o
// caminho do codigo), com o preset mais forte de cada um (default zero
// esconderia o trabalho), e falha nomeando exatamente quais pares
// quebraram.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Host extends StatefulWidget {
  const _Host({required this.builder});
  final Widget Function(PlaybackController) builder;

  @override
  State<_Host> createState() => _HostState();
}

class _HostState extends State<_Host>
    with SingleTickerProviderStateMixin {
  late final PlaybackController pb = PlaybackController(
    vsync: this,
    durationOf: () => const Duration(seconds: 5),
  );

  @override
  void dispose() {
    pb.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Scaffold(body: widget.builder(pb));
}

void main() {
  testWidgets('nenhum efeito quebra em cima de outro (pares ordenados)', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(320, 568);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    final container = ProviderContainer();
    addTearDown(container.dispose);
    final c = container.read(editorControllerProvider.notifier);
    c.addShapeLayer(Duration.zero);
    final base = c.state;
    final camada = base.layers.single;

    EffectInstance forte(EffectType t) {
      var fx = EffectInstance(type: t);
      final presets = effectSpecs[t]?.presets ?? const [];
      if (presets.isNotEmpty) fx = fx.withPreset(presets.last);
      return fx;
    }

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: _Host(
            builder: (pb) =>
                PreviewStage(playback: pb, videos: VideoLayerManager()),
          ),
        ),
      ),
    );
    await tester.pump();
    tester.takeException();

    final tipos = EffectType.values;
    final quebrados = <String>[];
    for (final a in tipos) {
      for (final b in tipos) {
        c.openProject(
          base.copyWith(
            layers: [
              camada.copyLayer(effects: [forte(a), forte(b)]),
            ],
          ),
        );
        await tester.pump(const Duration(milliseconds: 40));
        final erro = tester.takeException();
        if (erro != null) {
          quebrados.add('${a.name} + ${b.name}: $erro');
          // Uma arvore que estourou pode seguir estourando nos pumps
          // seguintes por causa do MESMO erro; recomeca limpa.
          await tester.pumpWidget(const SizedBox.shrink());
          tester.takeException();
          await tester.pumpWidget(
            UncontrolledProviderScope(
              container: container,
              child: MaterialApp(
                home: _Host(
                  builder: (pb) =>
                      PreviewStage(playback: pb, videos: VideoLayerManager()),
                ),
              ),
            ),
          );
          await tester.pump();
          tester.takeException();
        }
      }
      // Timers de efeitos com relogio proprio nao podem vazar entre
      // linhas da matriz.
      await tester.pump(const Duration(seconds: 1));
      tester.takeException();
    }

    expect(
      quebrados,
      isEmpty,
      reason:
          '${quebrados.length} pares quebraram. Primeiros:\n'
          '${quebrados.take(14).join('\n')}',
    );
  }, timeout: const Timeout(Duration(minutes: 25)));
}
