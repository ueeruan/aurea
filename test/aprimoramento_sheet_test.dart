import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/aprimoramento_ia.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/aprimoramento_sheet.dart';
import 'package:aurea/src/features/export/application/comparacao_aprimoramento.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// A FOLHA "APRIMORAR COM IA": estado verdadeiro do motor, liga/desliga,
/// intensidade e o antes/depois que descarta resultado velho.
void main() {
  late VideoLayer clipe;

  Future<(ProviderContainer, VideoLayer Function())> abrir(
    WidgetTester tester, {
    required bool motor,
    Future<ComparacaoDoAprimoramento> Function(PedidoDeComparacao)? comparador,
    bool ligado = false,
  }) async {
    tester.view.physicalSize = const Size(600, 1600);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    clipe = VideoLayer(
      name: 'v',
      startTime: Duration.zero,
      duration: const Duration(seconds: 4),
      sourcePath: '/x.mp4',
      aprimorar: ligado,
      position: AnimatedOffset(const Offset(960, 540)),
    );
    final container = ProviderContainer(
      overrides: [
        motorDeAprimoramentoProvider.overrideWithValue(motor),
        if (comparador != null)
          comparadorDeAprimoramentoProvider.overrideWithValue(comparador),
      ],
    );
    addTearDown(container.dispose);
    final editor = container.read(editorControllerProvider.notifier);
    editor.openProject(
      VideoProject(name: 'p', createdAt: DateTime(2026, 9, 14), layers: [clipe]),
    );
    final playback = PlaybackController(
      vsync: tester,
      durationOf: () => const Duration(seconds: 4),
    );
    addTearDown(playback.dispose);
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: Scaffold(
            body: Consumer(
              builder: (context, ref, _) => TextButton(
                onPressed: () =>
                    showAprimoramentoSheet(context, ref, clipe.id, playback),
                child: const Text('abrir'),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('abrir'));
    await tester.pumpAndSettle();
    VideoLayer video() =>
        container.read(editorControllerProvider).layerById(clipe.id)! as VideoLayer;
    return (container, video);
  }

  CupertinoSwitch interruptor(WidgetTester tester) => tester.widget<CupertinoSwitch>(
    find.byKey(const ValueKey('aprimorar-ia-ligar')),
  );

  testWidgets('sem motor: diz que nao existe e nao deixa ligar', (tester) async {
    final (_, video) = await abrir(tester, motor: false);
    expect(find.byKey(const ValueKey('aprimorar-ia-indisponivel')), findsOneWidget);
    expect(interruptor(tester).onChanged, isNull, reason: 'nunca "IA ativada" sem motor');
    expect(video().aprimorar, isFalse);
    expect(find.byKey(const ValueKey('aprimorar-ia-comparar')), findsNothing);
  });

  testWidgets('sem motor, um clipe que veio ligado ainda pode ser desligado', (tester) async {
    final (_, video) = await abrir(tester, motor: false, ligado: true);
    expect(interruptor(tester).onChanged, isNotNull);
    await tester.tap(find.byKey(const ValueKey('aprimorar-ia-ligar')));
    await tester.pumpAndSettle();
    expect(video().aprimorar, isFalse);
  });

  testWidgets('com motor: liga, escolhe intensidade e compara o quadro', (tester) async {
    final pedidos = <PedidoDeComparacao>[];
    final (_, video) = await abrir(
      tester,
      motor: true,
      comparador: (p) async {
        pedidos.add(p);
        expect(p.fonte, '/x.mp4');
        expect((p.largura, p.altura), (1920, 1080));
        return ComparacaoDoAprimoramento(
          plano: planoDeAprimoramento(
            ligado: true,
            motorDisponivel: true,
            larguraDaFonte: 640,
            alturaDaFonte: 360,
            larguraDaComposicao: p.largura,
            alturaDaComposicao: p.altura,
          ),
          antes: '/nao/existe/antes.png',
          depois: '/nao/existe/depois.png',
        );
      },
    );
    expect(find.byKey(const ValueKey('aprimorar-ia-indisponivel')), findsNothing);
    await tester.tap(find.byKey(const ValueKey('aprimorar-ia-ligar')));
    await tester.pumpAndSettle();
    expect(video().aprimorar, isTrue);
    // Nasce no perfil de video real, com a reducao de ruido padrao.
    expect(video().perfilDoAprimoramento, PerfilDoAprimoramento.videoReal);
    expect(find.byKey(const ValueKey('aprimorar-ia-ruido')), findsOneWidget);
    expect(
      tester.widget<Text>(find.byKey(const ValueKey('aprimorar-ia-ruido-valor'))).data,
      '50%',
    );

    // Animacao nao tem reducao de ruido (o modelo e um so).
    await tester.tap(find.byKey(const ValueKey('aprimorar-ia-perfil-animacao')));
    await tester.pumpAndSettle();
    expect(video().perfilDoAprimoramento, PerfilDoAprimoramento.animacao);
    expect(find.byKey(const ValueKey('aprimorar-ia-ruido')), findsNothing);
    await tester.tap(find.byKey(const ValueKey('aprimorar-ia-perfil-videoReal')));
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const ValueKey('aprimorar-ia-0.35')));
    await tester.pumpAndSettle();
    expect(video().forcaDoAprimoramento, .35);
    expect(find.text('35%'), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('aprimorar-ia-comparar')));
    await tester.pump();
    await tester.pump();
    expect(pedidos, hasLength(1));
    expect(pedidos.single.forca, .35, reason: 'a comparacao usa a forca escolhida');
    expect(pedidos.single.perfil, PerfilDoAprimoramento.videoReal);
    expect(pedidos.single.reducaoDeRuido, reducaoDeRuidoPadrao);
    expect(find.text('IA 640x360 -> 1920x1080 (x4)'), findsOneWidget);
    expect(find.byKey(const ValueKey('aprimorar-ia-antes-depois')), findsOneWidget);

    // Mudar a intensidade aposenta a comparacao feita com a anterior.
    await tester.tap(find.byKey(const ValueKey('aprimorar-ia-1.0')));
    await tester.pumpAndSettle();
    expect(video().forcaDoAprimoramento, 1.0);
    expect(find.byKey(const ValueKey('aprimorar-ia-antes-depois')), findsNothing);
  });

  testWidgets('falha na comparacao aparece com o motivo', (tester) async {
    await abrir(
      tester,
      motor: true,
      ligado: true,
      comparador: (_) async =>
          throw StateError('não consegui ler este quadro do vídeo'),
    );
    await tester.tap(find.byKey(const ValueKey('aprimorar-ia-comparar')));
    await tester.pump();
    await tester.pump();
    expect(find.text('não consegui ler este quadro do vídeo'), findsOneWidget);
  });

  testWidgets('video que ja tem a resolucao: a comparacao explica e nao mostra imagem', (tester) async {
    await abrir(
      tester,
      motor: true,
      ligado: true,
      comparador: (p) async => ComparacaoDoAprimoramento(
        plano: planoDeAprimoramento(
          ligado: true,
          motorDisponivel: true,
          larguraDaFonte: 1920,
          alturaDaFonte: 1080,
          larguraDaComposicao: p.largura,
          alturaDaComposicao: p.altura,
        ),
      ),
    );
    await tester.tap(find.byKey(const ValueKey('aprimorar-ia-comparar')));
    await tester.pump();
    await tester.pump();
    expect(find.byKey(const ValueKey('aprimorar-ia-motivo')), findsOneWidget);
    expect(find.text('o vídeo já tem a resolução da composição'), findsOneWidget);
    expect(find.byKey(const ValueKey('aprimorar-ia-antes-depois')), findsNothing);
  });
}
