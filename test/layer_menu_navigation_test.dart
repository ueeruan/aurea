import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/effect_preset_store.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/am_widgets.dart';
import 'package:aurea/src/features/editor/presentation/am/layer_menu.dart';
import 'package:aurea/src/features/editor/presentation/am/scene3d_studio.dart';
import 'package:aurea/src/features/editor/presentation/am/param_sheet_shell.dart'
    show ParamSheetShell;
import 'package:aurea/src/features/editor/presentation/widgets/add_layer_sheet.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  setUp(() {
    RecentSheets.instance.clear();
    EffectPresetStore.semArquivo = true;
  });
  tearDown(() {
    RecentSheets.instance.clear();
    EffectPresetStore.semArquivo = false;
  });

  Future<(_NavigationObserver, ProviderContainer)> openEditor(
    WidgetTester tester,
    void Function(EditorController) seed,
  ) async {
    tester.view.physicalSize = const Size(430, 932);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final container = ProviderContainer();
    addTearDown(container.dispose);
    seed(container.read(editorControllerProvider.notifier));
    final observer = _NavigationObserver();
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          theme: ThemeData(platform: TargetPlatform.iOS),
          navigatorObservers: [observer],
          home: Builder(
            builder: (context) => Scaffold(
              body: TextButton(
                onPressed: () => Navigator.of(context).push(
                  MaterialPageRoute<void>(builder: (_) => const _MenuHost()),
                ),
                child: const Text('inicio'),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('inicio'));
    await tester.pumpAndSettle();
    return (observer, container);
  }

  void scene(EditorController c) => c.addScene3DLayer(Duration.zero);
  void text(EditorController c) => c.addTextLayer(Duration.zero);
  void grid(EditorController c) {
    c.addNullLayer(Duration.zero);
  }

  void element(EditorController c) =>
      c.addElement3DLayer(Duration.zero, Element3DKind.cube);
  void audio(EditorController c) => c.addAudioLayer(
    Duration.zero,
    'audio.wav',
    'Som',
    const Duration(seconds: 5),
  );

  // A secao da cena 3D nao entra neste laco: ela abre o ESTUDIO (uma
  // rota), nao uma ficha de parametros. O teste dela vem logo abaixo.
  final cases = <(String, void Function(EditorController), String)>[
    ('cor da cena 3D', scene, 'Cor e\npreench.'),
    ('cameras', scene, 'Cameras'),
    ('Grid / Clonar', grid, 'Clonar'),
    ('Elemento 3D', element, 'Elemento\n3D'),
    ('cor do elemento 3D', element, 'Cor e\npreench.'),
    ('borda e sombra', text, 'Borda e\nsombra'),
    ('fonte', text, 'Fonte'),
    ('texto em caminho', text, 'Caminho'),
    ('presets', text, 'Presets'),
    ('volume', audio, 'Volume'),
    ('fade', audio, 'Fade'),
    ('particulas', (c) => c.addParticlesLayer(Duration.zero), 'Particulas'),
    (
      'legendas',
      (c) =>
          c.addCaptionLayerFromSrt('1\n00:00:00,000 --> 00:00:02,000\nTeste\n'),
      'Editar\nlegendas',
    ),
    (
      'tempo da precomp',
      (c) => c.openProject(
        VideoProject.empty('Teste').copyWith(
          layers: [
            GroupLayer(
              name: 'Grupo',
              startTime: Duration.zero,
              duration: const Duration(seconds: 5),
            ),
          ],
        ),
      ),
      'Tempo',
    ),
  ];
  for (final (name, seed, button) in cases) {
    testWidgets('$name abre e fecha pelo menu real sem retirar o editor', (
      tester,
    ) async {
      final (observer, container) = await openEditor(tester, seed);
      final project = container.read(editorControllerProvider);
      // Reabrir detecta tambem callbacks que continuam fechando o menu antigo.
      for (var repeat = 0; repeat < 2; repeat++) {
        await tester.tap(find.text('abrir menu'));
        await tester.pumpAndSettle();
        final target = find.text(button).evaluate().isNotEmpty
            ? find.text(button)
            : find.byTooltip(button);
        await tester.ensureVisible(target);
        await tester.tap(target);
        await tester.pumpAndSettle();
        expect(observer.pagesPopped, 0, reason: '$name retirou o editor');
        expect(find.text('editor aberto'), findsOneWidget);
        expect(find.byType(ParamSheetShell), findsOneWidget);
        expect(tester.takeException(), isNull);
        closeParamSheet(tester.element(find.byType(ParamSheetShell)));
        await tester.pumpAndSettle();
        expect(observer.pagesPopped, 0);
        expect(find.byType(ParamSheetShell), findsNothing);
      }
      expect(container.read(editorControllerProvider), same(project));
    });
  }

  testWidgets('toque duplo em Cena 3D fecha apenas o menu uma vez', (
    tester,
  ) async {
    final (observer, _) = await openEditor(tester, scene);
    await tester.tap(find.text('abrir menu'));
    await tester.pumpAndSettle();
    final oldTap = tester
        .widget<GestureDetector>(
          find
              .ancestor(
                of: find.text('Cena 3D'),
                matching: find.byType(GestureDetector),
              )
              .first,
        )
        .onTap!;
    await tester.tap(find.text('Cena 3D'));
    oldTap();
    await tester.pumpAndSettle();
    expect(observer.pagesPopped, 0);
    // Um toque duplo nao pode empilhar dois Estudios — nem retirar o
    // editor com um segundo pop.
    expect(find.byType(Scene3DStudio), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('Cena 3D abre o Estudio sem retirar o editor', (tester) async {
    final (observer, container) = await openEditor(tester, scene);
    final project = container.read(editorControllerProvider);
    // Duas voltas: abrir, voltar, abrir de novo — e o que pega um
    // callback que continua fechando o menu antigo.
    for (var volta = 0; volta < 2; volta++) {
      await tester.tap(find.text('abrir menu'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Cena 3D'));
      await tester.pumpAndSettle();
      expect(find.byType(Scene3DStudio), findsOneWidget);
      expect(
        observer.pagesPopped,
        volta,
        reason: 'abrir o Estudio nao pode retirar o editor',
      );
      expect(tester.takeException(), isNull);
      // Voltar do Estudio devolve o editor inteiro.
      await tester.binding.handlePopRoute();
      await tester.pumpAndSettle();
      expect(find.byType(Scene3DStudio), findsNothing);
      expect(find.text('editor aberto'), findsOneWidget);
    }
    expect(container.read(editorControllerProvider), same(project));
  });

  for (final name in ['Grid', 'Cena 3D']) {
    testWidgets('adicionar $name mantem a composicao aberta', (tester) async {
      final (observer, container) = await openEditor(tester, (_) {});
      await tester.tap(find.text('adicionar'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Objeto'));
      await tester.pumpAndSettle();
      await tester.tap(find.text(name));
      await tester.pumpAndSettle();
      expect(observer.pagesPopped, 0);
      final layers = container.read(editorControllerProvider).layers;
      expect(layers, hasLength(1));
      if (name == 'Grid') {
        expect(layers.single, isA<NullLayer>());
        expect((layers.single as NullLayer).grid, isNotNull);
      } else {
        expect(layers.single, isA<Scene3DLayer>());
      }
      expect(find.text('editor aberto'), findsOneWidget);
      expect(tester.takeException(), isNull);
    });
  }
}

class _NavigationObserver extends NavigatorObserver {
  int pagesPopped = 0;

  @override
  void didPop(Route<dynamic> route, Route<dynamic>? previousRoute) {
    if (route is PageRoute) pagesPopped++;
  }
}

class _MenuHost extends ConsumerStatefulWidget {
  const _MenuHost();

  @override
  ConsumerState<_MenuHost> createState() => _MenuHostState();
}

class _MenuHostState extends ConsumerState<_MenuHost>
    with SingleTickerProviderStateMixin {
  late final PlaybackController playback;

  @override
  void initState() {
    super.initState();
    playback = PlaybackController(
      vsync: this,
      durationOf: () => ref.read(editorControllerProvider).duration,
    );
  }

  @override
  void dispose() {
    playback.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    key: paramSheetHostKey,
    body: Column(
      children: [
        const Text('editor aberto'),
        TextButton(
          onPressed: () => showAddLayerSheet(context, ref, Duration.zero),
          child: const Text('adicionar'),
        ),
        TextButton(
          onPressed: () => showLayerMenu(
            context,
            ref,
            ref.read(editorControllerProvider).layers.first,
            playback,
          ),
          child: const Text('abrir menu'),
        ),
      ],
    ),
  );
}
